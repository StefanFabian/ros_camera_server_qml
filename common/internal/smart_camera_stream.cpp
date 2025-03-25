// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "smart_camera_stream.hpp"

#include "../camera_server.hpp"
#include "../compat.hpp"
#include "../logging.hpp"

#include "gstreamer_pipeline_appsink.hpp"

namespace ros_camera_server_qml_common
{

struct SmartCameraStream::Data {
  CameraStreamInfo stream;
  std::shared_ptr<void> handle;
  std::shared_ptr<void> previous_handle;
  size_t generation = 0;
  size_t previous_generation = 0;
};

SmartCameraStream::SmartCameraStream(
    std::string robot, std::string camera_id, int stream_index, const QSize &preferred_size,
    const std::function<void( const QVideoFrame &, StreamStatistics )> &callback )
    : robot_( std::move( robot ) ), camera_id_( std::move( camera_id ) ),
      stream_index_( stream_index ), preferred_size_( preferred_size ), callback_( callback )
{
  data_ = std::make_unique<Data>();
  connect( &CameraServerSingleton::getInstance(), &CameraServerSingleton::camerasChanged, this,
           &SmartCameraStream::updateStream );
  updateStream();
}

SmartCameraStream::~SmartCameraStream() = default;

const CameraStreamInfo &SmartCameraStream::currentStream() const { return data_->stream; }

bool SmartCameraStream::hasPendingSwitch() const { return data_->previous_handle != nullptr; }

void SmartCameraStream::releasePreviousStream()
{
  HCS_LOG_DEBUG( "Releasing previous stream for %s//%s, new stream is %s %dx%d", robot_.c_str(),
                 camera_id_.c_str(), data_->stream.transport().toStdString().c_str(),
                 data_->stream.width(), data_->stream.height() );
  // This is invoked from the new stream's frame callback, i.e. on a GStreamer streaming thread (or
  // a ROS executor thread), not this object's (main) thread. Tear down must happen on the main
  // thread to safely touch QObject internals.
  QMetaObject::invokeMethod(
      this,
      [this, handle = std::move( data_->previous_handle )]() mutable {
        handle.reset();
        emit currentStreamChanged();
      },
      Qt::QueuedConnection );
}

void SmartCameraStream::setPreferredSize( const QSize &size )
{
  if ( preferred_size_ == size )
    return;
  preferred_size_ = size;
  updateStream();
}

void SmartCameraStream::updateStream()
{
  const auto &cameras = CameraServerSingleton::getInstance().cameras();
  const QString q_robot = QString::fromStdString( robot_ );
  const QString q_camera_id = QString::fromStdString( camera_id_ );
  const auto it = std::find_if(
      cameras.begin(), cameras.end(), [&q_robot, &q_camera_id]( const CameraInfo &info ) {
        return info.server().robot() == q_robot && info.cameraId() == q_camera_id;
      } );
  if ( it == cameras.end() )
    return;
  const auto &camera_info = *it;
  CameraStreamInfo chosen_stream;
  if ( stream_index_ >= 0 && stream_index_ < camera_info.rawStreams().count() ) {
    chosen_stream = camera_info.rawStreams().at( stream_index_ );
  } else {
    // Auto-selection: prefer SRT transport, then pick stream whose resolution
    // best fits the preferred display size (smallest stream >= preferred width,
    // or largest available if none are large enough).
    const auto &streams = camera_info.rawStreams();
    if ( streams.isEmpty() )
      return;

    // Partition by transport preference: SRT > WebRTC > others
    auto transportScore = []( const QString &t ) -> int {
      if ( t == "srt" )
        return 2;
      if ( t == "webrtc" )
        return 1;
      return 0;
    };
    int best_transport = 0;
    for ( const auto &s : streams )
      best_transport = std::max( best_transport, transportScore( s.transport() ) );

    // Filter to best transport
    QList<CameraStreamInfo> candidates;
    for ( const auto &s : streams ) {
      if ( transportScore( s.transport() ) == best_transport )
        candidates.append( s );
    }

    if ( candidates.size() == 1 || !preferred_size_.isValid() || preferred_size_.isEmpty() ) {
      chosen_stream = candidates.first();
    } else {
      // Pick smallest stream that doesn't require upscaling when displayed.
      // Depending on aspect ratio, either width or height is the limiting
      // dimension. If none are large enough, pick the largest available.
      const int pw = preferred_size_.width();
      const int ph = preferred_size_.height();
      const CameraStreamInfo *best_fit = nullptr;
      const CameraStreamInfo *largest = nullptr;
      for ( const auto &s : candidates ) {
        // Determine whether the stream or window aspect ratio is wider.
        // sw/sh > pw/ph  ⟺  sw*ph > sh*pw  (no floating-point needed)
        bool adequate;
        if ( s.width() * ph > s.height() * pw ) {
          // Stream wider than window → width-limited display → check width
          adequate = s.width() >= pw;
        } else {
          // Stream taller than window → height-limited display → check height
          adequate = s.height() >= ph;
        }
        if ( adequate ) {
          if ( !best_fit || s.width() * s.height() < best_fit->width() * best_fit->height() )
            best_fit = &s;
        }
        if ( !largest || s.width() * s.height() > largest->width() * largest->height() )
          largest = &s;
      }
      chosen_stream = best_fit ? *best_fit : *largest;
    }
  }

  if ( chosen_stream == data_->stream )
    return;

  HCS_LOG_DEBUG( "Switching stream for %s//%s from %s %dx%d to %s %dx%d", robot_.c_str(),
                 camera_id_.c_str(), data_->stream.transport().toStdString().c_str(),
                 data_->stream.width(), data_->stream.height(),
                 chosen_stream.transport().toStdString().c_str(), chosen_stream.width(),
                 chosen_stream.height() );

  if ( data_->previous_handle ) {
    // Rapid switch: previous handle is still producing frames.
    // Discard the intermediate handle (never produced) and keep previous.
    data_->handle.reset();
  } else {
    // Normal switch: current handle is producing, move it to previous.
    data_->previous_generation = data_->generation;
    data_->previous_handle = std::move( data_->handle );
  }

  // Each handle gets a generation-tagged callback. The previous handle's
  // frames keep flowing (smooth video) until the current generation produces
  // its first frame and releases the previous handle.
  size_t gen = ++data_->generation;
  auto callback = [this, gen]( const QVideoFrame &frame, StreamStatistics stats ) {
    if ( gen == data_->generation ) {
      // Frame from the current (new) handle — release old if still pending
      if ( data_->previous_handle )
        releasePreviousStream();
      callback_( frame, stats );
    } else if ( gen == data_->previous_generation && data_->previous_handle ) {
      // Frame from the previous handle — forward until new handle takes over
      callback_( frame, stats );
    }
    // Frames from older generations are silently discarded
  };

  if ( chosen_stream.transport() == "ros2" ) {
    data_->handle =
        subscribeROS( chosen_stream.uri(), chosen_stream.codec().toStdString(), callback );
    HCS_LOG_DEBUG( "Created new ros subscription for %s with transport %s",
                   chosen_stream.uri().toStdString().c_str(),
                   chosen_stream.codec().toStdString().c_str() );
  } else {
    data_->handle = GStreamerPipelineAppSink::create( chosen_stream, camera_info, callback );
  }
  data_->stream = chosen_stream;
}

std::shared_ptr<void> SmartCameraStream::subscribeROS(
    const QString &topic, const std::string &transport,
    const std::function<void( const QVideoFrame &, StreamStatistics )> &frame_callback ) const
{
  const auto &node = hcs_qml_ros2::Ros2Qml::getInstance().node();
  if ( node == nullptr )
    return nullptr;
  auto callback = [frame_callback, node]( const QVideoFrame &frame,
                                          const hcs_qml_ros2::ImageInformation &info ) {
    StreamStatistics stats;
    stats.latency = ( node->now() - info.timestamp ).seconds() * 1000;
    stats.resolution = frame.size();
    frame_callback( frame, stats );
  };
  return hcs_qml_ros2::ImageTransportManager::getInstance().subscribe(
      node, topic, 1,
      image_transport::TransportHints( node.get(), transport.empty() ? "raw" : transport ), callback
#if QT_VERSION_MAJOR < 6
      ,
      { QVideoFrame::Format_ARGB32, QVideoFrame::Format_RGB32 }
#endif
  );
}

} // namespace ros_camera_server_qml_common
