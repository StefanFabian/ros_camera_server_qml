// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_COMMON_GSTREAMER_PIPELINE_BASE_HPP
#define ROS_CAMERA_SERVER_QML_COMMON_GSTREAMER_PIPELINE_BASE_HPP

#include "../camera_info.hpp"
#include "../ring_buffer.hpp"
#include "../stream_statistics.hpp"

#include <QTimer>
#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace ros_camera_server_qml_common
{
class WebRTCClient;

class GStreamerPipelineBase : public std::enable_shared_from_this<GStreamerPipelineBase>
{
public:
  using RosTimeFunc = std::function<int64_t()>;

  GStreamerPipelineBase( const CameraStreamInfo &stream, const CameraInfo &camera_info,
                         RosTimeFunc ros_time_func );
  virtual ~GStreamerPipelineBase();

  GStreamerPipelineBase( const GStreamerPipelineBase & ) = delete;
  GStreamerPipelineBase &operator=( const GStreamerPipelineBase & ) = delete;

protected:
  virtual void onSampleReady( GstSample *sample, const StreamStatistics &stats ) = 0;

  //! Builds the pipeline and starts the watchdog. Must be called exactly once, after the managing
  //! shared_ptr exists (the streaming-thread callbacks capture weak_from_this()), so it cannot run
  //! from the constructor. The create() factory of the concrete subclass calls it.
  void start();

private:
  static GstFlowReturn onNewSample( GstElement *sink, gpointer user_data );
  static GstPadProbeReturn timestampProbe( GstPad *pad, GstPadProbeInfo *info, gpointer user_data );

  //! Constructs the GStreamer pipeline (and WebRTC client for webrtc streams) from stream_ and
  //! starts it. Used both for the initial build and to rebuild after a recovery.
  void buildPipeline();
  //! Unhooks the streaming-thread callbacks from this object (so a rebuild's old pipeline cannot
  //! deliver stale frames into the new session) and then drives the pipeline to NULL on a detached
  //! background thread, so a blocking srt/rtp source socket close does not stall the caller.
  void teardownPipeline();
  //! Rebuilds the pipeline to recover a frozen or failed stream. Debounced so a persistently
  //! failing stream cannot spin in a tight rebuild loop. Returns true if the rebuild ran, false
  //! if it was skipped by the debounce (the caller must remember to retry, see pending_restart_).
  bool restartPipeline();
  //! Periodic health check (main thread): drains the bus for fatal errors and detects a frame
  //! stall (frozen video while the WebSocket stays connected). Recovers via restartPipeline().
  void onWatchdogTick();

  static constexpr int WATCHDOG_INTERVAL_MS = 1000;
  //! No frame for this long while the pipeline is running is treated as a stall. Generous so
  //! low-framerate streams do not false-trigger.
  static constexpr std::chrono::seconds STALL_TIMEOUT{ 10 };
  //! Minimum spacing between recovery rebuilds.
  static constexpr std::chrono::seconds RESTART_DEBOUNCE{ 5 };

  CameraStreamInfo stream_;
  GstElement *pipeline_ = nullptr;
  GstElement *appsink_ = nullptr;
  //! Id of the latency probe on the depayer sink pad, kept so teardownPipeline can remove it (which
  //! also waits out any in-flight timestampProbe) before the pipeline is offloaded for disposal.
  gulong timestamp_probe_id_ = 0;

  RosTimeFunc ros_time_func_;
  std::mutex latency_times_mutex_;
  RingBuffer<std::pair<GstClockTime, std::chrono::nanoseconds>, 30> latency_times_;
  std::mutex depay_times_mutex_;
  RingBuffer<std::pair<GstClockTime, std::chrono::steady_clock::time_point>, 30> depay_times_;
  StreamStatistics stats_;

  std::unique_ptr<WebRTCClient> webrtc_client_;

  QTimer watchdog_timer_;
  //! Time the last appsink sample arrived (written on the streaming thread, read by the
  //! watchdog on the main thread). Reset to the epoch on (re)build so the stall check only
  //! arms after the first frame, preventing rebuild loops while a stream is still connecting.
  std::atomic<std::chrono::steady_clock::time_point> last_sample_time_{};
  std::chrono::steady_clock::time_point last_restart_time_{};
  //! Set when a fatal-error restart was deferred by the debounce. The bus message that triggered
  //! it is consumed on detection and will not recur, so the watchdog must retry once the debounce
  //! window elapses (main thread only).
  bool pending_restart_ = false;
  //! Set when buildPipeline aborts on an unsupported transport/codec, a permanent condition that
  //! can never succeed. The watchdog uses it to skip retrying (and log-spamming) such a build,
  //! while still retrying a build that failed for a possibly-transient reason (main thread only).
  bool unsupported_config_ = false;
};

} // namespace ros_camera_server_qml_common

#endif // ROS_CAMERA_SERVER_QML_COMMON_GSTREAMER_PIPELINE_BASE_HPP
