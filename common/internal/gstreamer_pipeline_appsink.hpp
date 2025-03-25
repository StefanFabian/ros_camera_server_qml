// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_COMMON_GSTREAMER_PIPELINE_APPSINK_HPP
#define ROS_CAMERA_SERVER_QML_COMMON_GSTREAMER_PIPELINE_APPSINK_HPP

#include "../camera_stream_manager.hpp"
#include "gstreamer_pipeline_base.hpp"

#include <QVideoFrame>
#include <functional>

namespace ros_camera_server_qml_common
{

class GStreamerPipelineAppSink : public GStreamerPipelineBase
{
public:
  //! Creates an appsink pipeline managed by a shared_ptr whose deleter marshals destruction onto
  //! the main (Qt) thread. The last reference may be released on a GStreamer streaming thread (a
  //! frame callback that locked the weak_ptr), so this keeps the watchdog QTimer from being
  //! destroyed off-thread and the NULL transition from self-joining its own streaming thread.
  static std::shared_ptr<GStreamerPipelineAppSink>
  create( const CameraStreamInfo &stream, const CameraInfo &camera_info,
          const std::function<void( const QVideoFrame &, StreamStatistics )> &callback );

  ~GStreamerPipelineAppSink() override;

protected:
  void onSampleReady( GstSample *sample, const StreamStatistics &stats ) override;

private:
  GStreamerPipelineAppSink(
      const CameraStreamInfo &stream, const CameraInfo &camera_info,
      const std::function<void( const QVideoFrame &, StreamStatistics )> &callback );

  std::function<void( const QVideoFrame &, StreamStatistics )> callback_;
};

} // namespace ros_camera_server_qml_common

#endif // ROS_CAMERA_SERVER_QML_COMMON_GSTREAMER_PIPELINE_APPSINK_HPP
