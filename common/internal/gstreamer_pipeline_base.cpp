// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "gstreamer_pipeline_base.hpp"
#include "../logging.hpp"
#include "webrtc_client.hpp"

#include <QCoreApplication>
#include <QThread>
#include <gst/app/gstappsink.h>
#include <gst/rtp/gstrtpbuffer.h>

#include <algorithm>
#include <cstring>
#include <thread>

namespace ros_camera_server_qml_common
{

namespace
{
// Must match the RTP header extension id that ros_camera_server writes the capture timestamp to.
constexpr guint8 RTP_EXTENSION_ID_TIMESTAMP = 1;

/**
 * Reads the capture timestamp (nanoseconds since epoch) that ros_camera_server stores as a
 * RFC 5285 one-byte RTP header extension.
 * @return TRUE if the buffer contains the extension and it was read, FALSE otherwise.
 */
gboolean rtp_buffer_get_timestamp_extension( GstBuffer *buffer, GstClockTime &timestamp )
{
  GstRTPBuffer rtp_buffer = GST_RTP_BUFFER_INIT;
  gboolean success = FALSE;

  if ( !buffer || !GST_IS_BUFFER( buffer ) ) {
    g_warning( "Invalid parameters provided to rtp_buffer_get_timestamp_extension" );
    return FALSE;
  }

  if ( !gst_rtp_buffer_map( buffer, GST_MAP_READ, &rtp_buffer ) ) {
    g_warning( "Failed to map RTP buffer for reading" );
    return FALSE;
  }

  if ( !gst_rtp_buffer_get_extension( &rtp_buffer ) ) {
    // No extension header present - this is normal for some packets
    gst_rtp_buffer_unmap( &rtp_buffer );
    return FALSE;
  }

  gpointer extension_data = nullptr;
  guint extension_size = 0;
  success = gst_rtp_buffer_get_extension_onebyte_header( &rtp_buffer, RTP_EXTENSION_ID_TIMESTAMP, 0,
                                                         &extension_data, &extension_size );

  if ( success ) {
    if ( extension_size != sizeof( guint64 ) ) {
      g_warning( "Timestamp extension has unexpected size: %u bytes (expected %zu)", extension_size,
                 sizeof( guint64 ) );
      success = FALSE;
    } else {
      guint64 timestamp_le;
      memcpy( &timestamp_le, extension_data, sizeof( guint64 ) );
      timestamp = GUINT64_FROM_LE( timestamp_le );
    }
  }

  gst_rtp_buffer_unmap( &rtp_buffer );
  return success;
}
} // namespace

GStreamerPipelineBase::GStreamerPipelineBase( const CameraStreamInfo &stream,
                                              const CameraInfo & /*camera_info*/,
                                              RosTimeFunc ros_time_func )
    : stream_( stream ), ros_time_func_( std::move( ros_time_func ) )
{
  if ( !gst_is_initialized() ) {
    gst_init( nullptr, nullptr );
  }
}

void GStreamerPipelineBase::start()
{
  // The watchdog QTimer below takes the affinity of this thread so it must be on the main thread.
  Q_ASSERT( QThread::currentThread() == QCoreApplication::instance()->thread() );

  buildPipeline();

  // Health watchdog: nothing else monitors the media, so a failure that stops frames without
  // dropping the WebSocket (decoder error, wedged jitterbuffer, ICE drop the server has not yet
  // closed) would otherwise freeze the video permanently. The watchdog recovers by rebuilding.
  watchdog_timer_.setInterval( WATCHDOG_INTERVAL_MS );
  QObject::connect( &watchdog_timer_, &QTimer::timeout, [this]() { onWatchdogTick(); } );
  watchdog_timer_.start();
}

GStreamerPipelineBase::~GStreamerPipelineBase()
{
  watchdog_timer_.stop();
  teardownPipeline();
}

void GStreamerPipelineBase::buildPipeline()
{
  unsupported_config_ = false;
  QString pipeline_str;
  if ( stream_.transport() == "srt" ) {
    pipeline_str = "srtsrc uri=" + stream_.uri() + " latency=50 ! application/x-rtp ! ";
  } else if ( stream_.transport() == "rtp" ) {
    pipeline_str = "rtpsrc uri=" + stream_.uri() + " ! ";
  } else if ( stream_.transport() == "webrtc" ) {
  } else {
    HCS_LOG_ERROR( "Unsupported stream transport: %s", stream_.transport().toStdString().c_str() );
    unsupported_config_ = true;
    return;
  }
  pipeline_str += "queue name=input_queue ! ";
  if ( stream_.codec() == "h264" ) {
    pipeline_str += "rtph264depay name=depay ! h264parse ! decodebin3 ! ";
  } else if ( stream_.codec() == "h265" ) {
    pipeline_str += "rtph265depay name=depay ! h265parse ! decodebin3 ! ";
  } else {
    HCS_LOG_ERROR( "Unsupported codec: %s", stream_.codec().toStdString().c_str() );
    unsupported_config_ = true;
    return;
  }
  // Use videoconvert as a fallback converter. With NV12 listed first, it will
  // passthrough when the decoder already outputs NV12 (common for hardware decoders),
  // avoiding an expensive CPU colorspace conversion.
#if QT_VERSION_MAJOR >= 6
  pipeline_str += "queue max-size-buffers=1 leaky=downstream ! videoconvert ! "
                  "video/x-raw,format=(string){NV12,I420,RGBA} ! appsink name=sink "
                  "emit-signals=true max-buffers=1 drop=true sync=false";
#else
  pipeline_str += "queue max-size-buffers=1 leaky=downstream ! videoconvert ! "
                  "video/x-raw,format=(string){NV12,I420,BGRA} ! appsink name=sink "
                  "emit-signals=true max-buffers=1 drop=true sync=false";
#endif

  GError *error = nullptr;
  pipeline_ = gst_parse_launch( pipeline_str.toStdString().c_str(), &error );
  if ( !pipeline_ ) {
    HCS_LOG_ERROR( "Failed to parse pipeline: %s", error ? error->message : "unknown error" );
    if ( error )
      g_error_free( error );
    return;
  }

  // Clear stale latency state so measurements from a previous session are not mixed in.
  {
    std::lock_guard<std::mutex> lock( depay_times_mutex_ );
    depay_times_.clear();
  }
  {
    std::lock_guard<std::mutex> lock( latency_times_mutex_ );
    latency_times_.clear();
  }
  // Reset the reported latency to "unknown".
  stats_.network_latency = -1;
  stats_.processing_latency = -1;
  stats_.latency = -1;
  // Disarm the stall watchdog until the first frame of the new session arrives, so a stream that is
  // still connecting is not mistaken for a stall.
  last_sample_time_.store( std::chrono::steady_clock::time_point{}, std::memory_order_relaxed );

  // The streaming-thread callbacks (onNewSample, timestampProbe) receive a heap-allocated weak_ptr
  // to this object instead of a raw `this`. They lock() it per call, so a callback that fires after
  // destruction begins (or on the old pipeline mid-rebuild, see teardownPipeline) drops the work
  // instead of touching freed state. The destroy-notify frees the weak_ptr when the handler/probe
  // is removed or the element is finalized.
  const auto deleteWeak = []( gpointer data ) {
    delete static_cast<std::weak_ptr<GStreamerPipelineBase> *>( data );
  };

  appsink_ = gst_bin_get_by_name( GST_BIN( pipeline_ ), "sink" );
  // Connect new-sample signal
  g_signal_connect_data(
      appsink_, "new-sample", G_CALLBACK( onNewSample ),
      new std::weak_ptr<GStreamerPipelineBase>( weak_from_this() ),
      []( gpointer data, GClosure * ) {
        delete static_cast<std::weak_ptr<GStreamerPipelineBase> *>( data );
      },
      static_cast<GConnectFlags>( 0 ) );

  // Add pad probe to depayer to extract timestamps and compute latency.
  GstElement *depay = gst_bin_get_by_name( GST_BIN( pipeline_ ), "depay" );
  if ( depay ) {
    GstPad *sink_pad = gst_element_get_static_pad( depay, "sink" );
    if ( sink_pad ) {
      timestamp_probe_id_ = gst_pad_add_probe(
          sink_pad,
          static_cast<GstPadProbeType>( GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_FLUSH ),
          timestampProbe, new std::weak_ptr<GStreamerPipelineBase>( weak_from_this() ), deleteWeak );
      gst_object_unref( sink_pad );
    }
    gst_object_unref( depay );
  }

  if ( stream_.transport() == "webrtc" ) {
    // This also creates the webrtcbin which is why it needs to be done before setting the pipeline to playing
    webrtc_client_ = std::make_unique<WebRTCClient>( stream_.uri(), pipeline_ );
  }

  gst_element_set_state( pipeline_, GST_STATE_PLAYING );
}

void GStreamerPipelineBase::teardownPipeline()
{
  // Detaches the webrtcbin and tears it down on a background thread (its ICE/DTLS NULL
  // transition blocks). After this the pipeline holds only the decode chain.
  webrtc_client_.reset();

  if ( !pipeline_ )
    return;

  // Unhook the streaming-thread callbacks from this object. The weak_ptr guard already makes
  // destruction safe, but on a watchdog rebuild this object stays alive while buildPipeline() swaps
  // in a new pipeline, so the old (still-draining) pipeline must be detached or it would deliver
  // stale frames into the new session (a live weak_ptr cannot distinguish old from new). Removing
  // the probe also waits out any in-flight timestampProbe; the destroy-notifies free the heap
  // weak_ptrs.
  if ( appsink_ ) {
    g_signal_handlers_disconnect_matched( appsink_, G_SIGNAL_MATCH_FUNC, 0, 0, nullptr,
                                          reinterpret_cast<gpointer>( onNewSample ), nullptr );
  }
  if ( timestamp_probe_id_ != 0 ) {
    if ( GstElement *depay = gst_bin_get_by_name( GST_BIN( pipeline_ ), "depay" ) ) {
      if ( GstPad *sink_pad = gst_element_get_static_pad( depay, "sink" ) ) {
        gst_pad_remove_probe( sink_pad, timestamp_probe_id_ );
        gst_object_unref( sink_pad );
      }
      gst_object_unref( depay );
    }
    timestamp_probe_id_ = 0;
  }

  // Drive the pipeline to NULL on a detached thread: for srt/rtp this closes the source socket,
  // which can block. The callbacks are detached above, so the offloaded pipeline never touches this
  // object; ownership of the elements moves entirely to the worker thread.
  GstElement *pipeline = pipeline_;
  GstElement *appsink = appsink_;
  pipeline_ = nullptr;
  appsink_ = nullptr;
  std::thread( [pipeline, appsink]() {
    gst_element_set_state( pipeline, GST_STATE_NULL );
    if ( appsink )
      gst_object_unref( appsink );
    gst_object_unref( pipeline );
  } ).detach();
}

bool GStreamerPipelineBase::restartPipeline()
{
  const auto now = std::chrono::steady_clock::now();
  if ( now - last_restart_time_ < RESTART_DEBOUNCE )
    return false;
  last_restart_time_ = now;

  HCS_LOG_WARN( "Recovering %s stream by rebuilding the GStreamer pipeline",
                stream_.transport().toStdString().c_str() );
  teardownPipeline();
  buildPipeline();
  return true;
}

void GStreamerPipelineBase::onWatchdogTick()
{
  if ( !pipeline_ ) {
    // A previous (re)build failed and left no pipeline (e.g. a transient gst_parse_launch/registry
    // hiccup, or the initial build in the constructor). Retry the (debounced) build so the watchdog
    // does not stay permanently inert. A genuinely unsupported transport/codec can never succeed,
    // so skip it to avoid spinning and log-spamming.
    if ( !unsupported_config_ )
      restartPipeline();
    return;
  }

  // Drain the bus (also prevents unconsumed messages from accumulating) and recover on a fatal
  // error or an unexpected EOS, both of which stop frames while the WebSocket stays connected.
  GstBus *bus = gst_pipeline_get_bus( GST_PIPELINE( pipeline_ ) );
  bool fatal = false;
  GstMessage *msg;
  while ( ( msg = gst_bus_pop( bus ) ) != nullptr ) {
    switch ( GST_MESSAGE_TYPE( msg ) ) {
    case GST_MESSAGE_ERROR: {
      GError *err = nullptr;
      gchar *debug = nullptr;
      gst_message_parse_error( msg, &err, &debug );
      HCS_LOG_ERROR( "GStreamer pipeline error: %s (%s)", err ? err->message : "unknown",
                     debug ? debug : "" );
      if ( err )
        g_error_free( err );
      g_free( debug );
      fatal = true;
      break;
    }
    case GST_MESSAGE_EOS:
      HCS_LOG_WARN( "GStreamer pipeline reached EOS unexpectedly on a live stream" );
      // For srt/rtp an EOS means the source ended; a full rebuild is the only recovery. For webrtc
      // the WebRTCClient already self-recovers on WebSocket disconnect / ICE failure, and an EOS
      // can leak onto the bus mid-reconnect (e.g. around webrtcbin removal); treating it as fatal
      // here would tear that session down and fight the client's own recovery. Let the client
      // recover and rely on the frame-stall check below as the backstop if frames never resume.
      if ( stream_.transport() != "webrtc" )
        fatal = true;
      break;
    default:
      break;
    }
    gst_message_unref( msg );
  }
  gst_object_unref( bus );

  // A fatal bus message, or a fatal-triggered restart a previous tick had to defer (its bus
  // message was already consumed, so it cannot recur), recovers by rebuilding. When the debounce
  // rate-limits the rebuild, remember the request and retry on a later tick rather than losing it.
  if ( fatal || pending_restart_ ) {
    pending_restart_ = !restartPipeline();
    return;
  }

  // Frame-stall detection, re-evaluated from live state every tick (so a debounced restart here is
  // simply retried next tick if the condition persists, or dropped if frames resumed).
  const auto now = std::chrono::steady_clock::now();
  const auto last = last_sample_time_.load( std::memory_order_relaxed );
  if ( last != std::chrono::steady_clock::time_point{} && now - last > STALL_TIMEOUT ) {
    HCS_LOG_WARN( "No video frame received for over %lld s, recovering stalled stream",
                  static_cast<long long>( STALL_TIMEOUT.count() ) );
    restartPipeline();
  }
}

GstFlowReturn GStreamerPipelineBase::onNewSample( GstElement *sink, gpointer user_data )
{
  // Lock the weak_ptr so the object stays alive for the duration of this call; if it has expired
  // (destruction started, or this is the old pipeline being torn down on a rebuild) drop the frame.
  auto self = static_cast<std::weak_ptr<GStreamerPipelineBase> *>( user_data )->lock();
  if ( !self )
    return GST_FLOW_OK;

  GstSample *sample;
  g_signal_emit_by_name( sink, "pull-sample", &sample );
  if ( !sample )
    return GST_FLOW_ERROR;

  // Feed the stall watchdog (this runs on a GStreamer streaming thread).
  self->last_sample_time_.store( std::chrono::steady_clock::now(), std::memory_order_relaxed );

  GstBuffer *buffer = gst_sample_get_buffer( sample );
  GstCaps *caps = gst_sample_get_caps( sample );
  GstStructure *s = gst_caps_get_structure( caps, 0 );

  int width, height;
  gst_structure_get_int( s, "width", &width );
  gst_structure_get_int( s, "height", &height );

  // Update stats
  self->stats_.resolution = QSize( width, height );

  // Compute processing latency by correlating buffer PTS with depay arrival time
  GstClockTime pts = GST_BUFFER_PTS( buffer );
  if ( GST_CLOCK_TIME_IS_VALID( pts ) ) {
    constexpr GstClockTime PTS_MATCH_TOLERANCE = 100 * GST_USECOND;
    std::lock_guard<std::mutex> lock( self->depay_times_mutex_ );
    // Pop entries until we find the matching PTS (older entries were dropped)
    const GstClockTime pts_delta = pts > PTS_MATCH_TOLERANCE ? pts - PTS_MATCH_TOLERANCE : 0;
    while ( !self->depay_times_.empty() && self->depay_times_.front().first < pts_delta ) {
      self->depay_times_.pop_front();
    }
    // Only use the entry if it actually belongs to this frame. A mismatch (e.g.
    // leftover entries recorded around a stream discontinuity) would produce a
    // bogus latency from an unrelated arrival time.
    if ( !self->depay_times_.empty() &&
         self->depay_times_.front().first <= pts + PTS_MATCH_TOLERANCE ) {
      auto processing_ns = std::chrono::steady_clock::now() - self->depay_times_.front().second;
      self->stats_.processing_latency =
          std::chrono::duration_cast<std::chrono::milliseconds>( processing_ns ).count();
      self->depay_times_.pop_front();
    }
  }

  // Compute average network latency from samples. latency_times_ is filled on the depayer's
  // streaming thread (timestampProbe), which is a different thread from this appsink callback,
  // so it needs the same guarding as depay_times_.
  {
    std::lock_guard<std::mutex> lock( self->latency_times_mutex_ );
    if ( !self->latency_times_.empty() ) {
      int64_t total_ns = 0;
      for ( size_t i = 0; i < self->latency_times_.size(); ++i )
        total_ns += self->latency_times_[i].second.count();
      // Signed division (size() is unsigned): unsynchronized clocks can make a per-frame latency
      // negative, so keep the average signed and clamp to 0 rather than wrapping to a huge value.
      const int64_t avg_ms =
          total_ns / static_cast<int64_t>( self->latency_times_.size() ) / 1000000; // ns to ms
      self->stats_.network_latency = static_cast<int>( std::max<int64_t>( avg_ms, 0 ) );
      self->stats_.latency = self->stats_.network_latency + self->stats_.processing_latency;
    }
  }

  self->onSampleReady( sample, self->stats_ );

  gst_sample_unref( sample );
  return GST_FLOW_OK;
}

GstPadProbeReturn GStreamerPipelineBase::timestampProbe( GstPad *, GstPadProbeInfo *info,
                                                         gpointer user_data )
{
  // Lock the weak_ptr so the object stays alive for this call; skip if it has expired.
  auto self = static_cast<std::weak_ptr<GStreamerPipelineBase> *>( user_data )->lock();
  if ( !self )
    return GST_PAD_PROBE_OK;

  if ( GST_PAD_PROBE_INFO_TYPE( info ) & GST_PAD_PROBE_TYPE_EVENT_FLUSH ) {
    if ( GST_EVENT_TYPE( GST_PAD_PROBE_INFO_EVENT( info ) ) == GST_EVENT_FLUSH_STOP ) {
      std::lock_guard<std::mutex> lock( self->depay_times_mutex_ );
      self->depay_times_.clear();
    }
    return GST_PAD_PROBE_OK;
  }

  auto buffer = GST_PAD_PROBE_INFO_BUFFER( info );

  {
    std::lock_guard<std::mutex> lock( self->depay_times_mutex_ );
    self->depay_times_.push( { GST_BUFFER_PTS( buffer ), std::chrono::steady_clock::now() } );
  }

  // Extract timestamp from RTP header extension
  GstClockTime capture_time;
  if ( self->ros_time_func_ && rtp_buffer_get_timestamp_extension( buffer, capture_time ) &&
       capture_time > 0 ) {
    int64_t now_ns = self->ros_time_func_();
    // Calculate end-to-end latency wrt ROS time (requires synchronized clocks)
    auto latency = std::chrono::nanoseconds( now_ns - capture_time );
    std::lock_guard<std::mutex> lock( self->latency_times_mutex_ );
    self->latency_times_.push( { capture_time, latency } );
  }

  return GST_PAD_PROBE_OK;
}

} // namespace ros_camera_server_qml_common
