// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "camera_stream.hpp"

namespace ros_camera_server_qml6
{

CameraStream::CameraStream() { }

QVideoSink *CameraStream::videoSink() const { return sink_.get(); }

void CameraStream::setVideoSink( QVideoSink *sink )
{
  if ( sink == sink_ )
    return;
  sink_ = sink;
  if ( sink == nullptr )
    stopStream();
  else
    createStream();
  emit videoSinkChanged();
}

void CameraStream::presentFrame( const QVideoFrame &frame )
{
  if ( sink_ == nullptr )
    return;
  sink_->setVideoFrame( frame );
}

bool CameraStream::hasVideoOutput() const { return sink_ != nullptr; }

} // namespace ros_camera_server_qml6
