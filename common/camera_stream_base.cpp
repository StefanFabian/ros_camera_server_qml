// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "camera_stream_base.hpp"
#include "camera_server.hpp"
#include "logging.hpp"

namespace ros_camera_server_qml_common
{

CameraStreamBase::CameraStreamBase() { }

const QString &CameraStreamBase::robot() const { return robot_; }

void CameraStreamBase::setRobot( const QString &value )
{
  if ( robot_ == value )
    return;
  robot_ = value;
  createStream();
  emit robotChanged();
}

const QString &CameraStreamBase::cameraId() const { return camera_id_; }

void CameraStreamBase::setCameraId( const QString &value )
{
  if ( camera_id_ == value )
    return;
  camera_id_ = value;
  createStream();
  emit cameraIdChanged();
}

bool CameraStreamBase::enabled() const { return enabled_; }

void CameraStreamBase::setEnabled( bool value )
{
  if ( enabled_ == value )
    return;
  enabled_ = value;
  if ( !value ) {
    stopStream();
  } else {
    createStream();
  }
  emit enabledChanged();
}

bool CameraStreamBase::paused() const { return paused_; }

void CameraStreamBase::setPaused( bool value )
{
  if ( paused_ == value )
    return;
  paused_ = value;
  emit pausedChanged();
}

void CameraStreamBase::play()
{
  setPaused( false );
  setEnabled( true );
}

void CameraStreamBase::pause() { setPaused( true ); }

void CameraStreamBase::stop() { setEnabled( false ); }

QImage CameraStreamBase::grabFrame() const
{
  if ( !last_frame_.isValid() )
    return {};
#if QT_VERSION_MAJOR >= 6
  return last_frame_.toImage();
#else
  return last_frame_.image();
#endif
}

int CameraStreamBase::streamIndex() const { return stream_index_; }

void CameraStreamBase::setStreamIndex( int value )
{
  if ( stream_index_ == value )
    return;
  stream_index_ = value;
  createStream();
  emit streamIndexChanged();
}

QSize CameraStreamBase::resolution() const { return current_stats_.resolution; }

QString CameraStreamBase::codec() const { return codec_; }

qreal CameraStreamBase::framerate() const { return current_stats_.framerate; }

QString CameraStreamBase::transport() const { return transport_; }

QSize CameraStreamBase::preferredSize() const { return preferred_size_; }

void CameraStreamBase::setPreferredSize( const QSize &value )
{
  if ( preferred_size_ == value )
    return;
  preferred_size_ = value;
  if ( subscription_ )
    CameraStreamManager::getInstance().updatePreferredSize( subscription_.get(), preferred_size_ );
  else
    createStream();
  emit preferredSizeChanged();
}

int CameraStreamBase::latency() const { return current_stats_.latency; }

int CameraStreamBase::networkLatency() const { return current_stats_.network_latency; }

int CameraStreamBase::processingLatency() const { return current_stats_.processing_latency; }

void CameraStreamBase::createStream()
{
  if ( robot_.isEmpty() || camera_id_.isEmpty() || !hasVideoOutput() || !enabled_ )
    return;
  HCS_LOG_DEBUG( "Creating subscription for %s//%s", robot_.toStdString().c_str(),
                 camera_id_.toStdString().c_str() );
  subscription_ = CameraStreamManager::getInstance().createSubscription(
      robot_.toStdString(), camera_id_.toStdString(),
      [this]( const QVideoFrame &frame, StreamStatistics stats ) {
        QMetaObject::invokeMethod( this, [this, frame, stats]() {
          if ( paused_ )
            return;
          bool changed = false;
          if ( current_stats_.resolution != stats.resolution ) {
            current_stats_.resolution = stats.resolution;
            changed = true;
          }
          if ( subscription_ && subscription_->transport != transport_ ) {
            transport_ = subscription_->transport;
            changed = true;
          }
          if ( subscription_ && subscription_->codec != codec_ ) {
            codec_ = subscription_->codec;
            changed = true;
          }

          current_stats_.framerate = stats.framerate;
          current_stats_.latency = stats.latency;
          current_stats_.network_latency = stats.network_latency;
          current_stats_.processing_latency = stats.processing_latency;

          if ( changed )
            emit streamInfoChanged();
          emit framerateChanged();
          emit latencyChanged();

          last_frame_ = frame;
          presentFrame( frame );
        } );
      },
      stream_index_, preferred_size_ );

  // Reset frame and statistics until the first frame arrives to avoid showing stale data from a previous stream.
  QMetaObject::invokeMethod(
      this,
      [this]() {
        presentFrame( QVideoFrame() );
        current_stats_ = {};
        codec_.clear();
        transport_.clear();
        emit streamInfoChanged();
        emit framerateChanged();
        emit latencyChanged();
      },
      Qt::QueuedConnection ); // Clear previous frame
}

void CameraStreamBase::stopStream() { subscription_.reset(); }

void CameraStreamBase::onRos2Initialized() { CameraServerSingleton::getInstance().initialize(); }

} // namespace ros_camera_server_qml_common
