// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_COMMON_CAMERA_STREAM_BASE_H
#define ROS_CAMERA_SERVER_QML_COMMON_CAMERA_STREAM_BASE_H

#include "camera_stream_manager.hpp"
#include "compat.hpp"

#include <QImage>
#include <QVideoFrame>

namespace ros_camera_server_qml_common
{

class CameraStreamBase : public hcs_qml_ros2::QObjectRos2
{
  Q_OBJECT
  //! The robot name/id to get the camera stream from.
  Q_PROPERTY( QString robot READ robot WRITE setRobot NOTIFY robotChanged )
  //! The id of the camera to stream.
  Q_PROPERTY( QString cameraId READ cameraId WRITE setCameraId NOTIFY cameraIdChanged )
  //! Whether the subscriber is active or not. Setting to false will shut down subscribers
  Q_PROPERTY( bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged )
  //! Full latency until hand-off of the frame to the video surface in milliseconds. (-1 if not available)
  Q_PROPERTY( int latency READ latency NOTIFY latencyChanged )
  //! Network latency in milliseconds. (-1 if not available)
  Q_PROPERTY( int networkLatency READ networkLatency NOTIFY latencyChanged )
  //! Processing latency on this device (decoding) in milliseconds. (-1 if not available)
  Q_PROPERTY( int processingLatency READ processingLatency NOTIFY latencyChanged )
  //! The index of the stream to use. Default is -1 (auto).
  Q_PROPERTY( int streamIndex READ streamIndex WRITE setStreamIndex NOTIFY streamIndexChanged )
  //! The resolution of the stream.
  Q_PROPERTY( QSize resolution READ resolution NOTIFY streamInfoChanged )
  //! The codec of the stream.
  Q_PROPERTY( QString codec READ codec NOTIFY streamInfoChanged )
  //! The framerate of the stream.
  Q_PROPERTY( qreal framerate READ framerate NOTIFY framerateChanged )
  //! The transport of the stream.
  Q_PROPERTY( QString transport READ transport NOTIFY streamInfoChanged )
  //! Whether the stream is currently paused (frames are dropped but subscription remains active).
  Q_PROPERTY( bool paused READ paused WRITE setPaused NOTIFY pausedChanged )
  //! The preferred display size used for automatic stream resolution selection.
  //! Bind this to the VideoOutput dimensions: preferredSize: Qt.size(videoOutput.width, videoOutput.height)
  Q_PROPERTY( QSize preferredSize READ preferredSize WRITE setPreferredSize NOTIFY preferredSizeChanged )
public:
  CameraStreamBase();

  const QString &robot() const;
  void setRobot( const QString &value );

  const QString &cameraId() const;
  void setCameraId( const QString &value );

  bool enabled() const;
  void setEnabled( bool value );

  int latency() const;

  int networkLatency() const;

  int processingLatency() const;

  int streamIndex() const;
  void setStreamIndex( int value );

  QSize resolution() const;
  QString codec() const;
  qreal framerate() const;
  QString transport() const;

  bool paused() const;
  void setPaused( bool value );

  QSize preferredSize() const;
  void setPreferredSize( const QSize &value );

  //! Resumes playback. Unpauses and re-enables the stream if it was stopped.
  Q_INVOKABLE void play();
  //! Pauses the stream. Frames are dropped but the subscription remains active.
  Q_INVOKABLE void pause();
  //! Stops the stream by disabling the subscription.
  Q_INVOKABLE void stop();
  //! Returns the last received frame as a QImage.
  Q_INVOKABLE QImage grabFrame() const;

signals:
  void robotChanged();
  void enabledChanged();
  void pausedChanged();
  void cameraIdChanged();
  void framerateChanged();
  void latencyChanged();
  void streamIndexChanged();
  void streamInfoChanged();
  void preferredSizeChanged();

protected:
  void onRos2Initialized() override;

  //! Called when a new frame is ready to be presented. Must be implemented by subclasses.
  virtual void presentFrame( const QVideoFrame &frame ) = 0;

  //! Returns true if the video output is ready (sink/surface is set).
  virtual bool hasVideoOutput() const = 0;

  //! Called to create or recreate the stream subscription.
  void createStream();

  //! Called to stop the stream subscription.
  void stopStream();

  std::shared_ptr<CameraStreamManager::SubscriptionBase> subscription_;
  QString robot_;
  QString camera_id_;
  int stream_index_ = -1;
  bool enabled_ = true;
  bool paused_ = false;
  QVideoFrame last_frame_;
  QSize preferred_size_;
  QString codec_;
  QString transport_;
  StreamStatistics current_stats_;
};
} // namespace ros_camera_server_qml_common

#endif // ROS_CAMERA_SERVER_QML_COMMON_CAMERA_STREAM_BASE_H
