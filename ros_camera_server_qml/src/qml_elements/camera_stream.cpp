// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "camera_stream.hpp"
#include "../../../common/logging.hpp"

namespace ros_camera_server_qml
{

namespace
{
const char *videoSurfaceErrorToString( QAbstractVideoSurface::Error error )
{
  switch ( error ) {
  case QAbstractVideoSurface::NoError:
    return "NoError";
  case QAbstractVideoSurface::UnsupportedFormatError:
    return "UnsupportedFormatError";
  case QAbstractVideoSurface::IncorrectFormatError:
    return "IncorrectFormatError";
  case QAbstractVideoSurface::StoppedError:
    return "StoppedError";
  case QAbstractVideoSurface::ResourceError:
    return "ResourceError";
  default:
    return "UnknownError";
  }
}
} // namespace

CameraStream::CameraStream() { }

QAbstractVideoSurface *CameraStream::videoSurface() const { return surface_; }

void CameraStream::setVideoSurface( QAbstractVideoSurface *surface )
{
  if ( surface == surface_ )
    return;
  if ( surface_ != nullptr && surface_->isActive() ) {
    surface_->stop();
  }
  surface_ = surface;
  if ( surface == nullptr )
    stopStream();
  else
    createStream();
  emit videoSurfaceChanged();
}

void CameraStream::presentFrame( const QVideoFrame &frame )
{
  if ( surface_ == nullptr )
    return;
  if ( frame.pixelFormat() == QVideoFrame::Format_Invalid ) {
    // Empty/clearing frame — just stop the surface without error.
    if ( surface_->isActive() )
      surface_->stop();
    return;
  }
  const QVideoSurfaceFormat &surface_format = surface_->surfaceFormat();
  if ( surface_format.frameWidth() != frame.width() ||
       surface_format.frameHeight() != frame.height() ||
       surface_format.pixelFormat() != frame.pixelFormat() ) {
    surface_->stop();
  }
  if ( !surface_->isActive() ) {
    format_ = QVideoSurfaceFormat( frame.size(), frame.pixelFormat() );
    if ( !surface_->start( format_ ) ) {
      HCS_LOG_ERROR( "Failed to start video surface: %s",
                     videoSurfaceErrorToString( surface_->error() ) );
      stopStream();
      return;
    }
  }
  surface_->present( frame );
}

bool CameraStream::hasVideoOutput() const { return surface_ != nullptr; }

} // namespace ros_camera_server_qml
