// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_CAMERA_STREAM_H
#define ROS_CAMERA_SERVER_QML_CAMERA_STREAM_H

#include "../../../common/camera_stream_base.hpp"

#include <QAbstractVideoSurface>
#include <QVideoSurfaceFormat>

namespace ros_camera_server_qml
{

class CameraStream : public ros_camera_server_qml_common::CameraStreamBase
{
  Q_OBJECT
  //! Interface for QML. This is the surface the images are passed to.
  Q_PROPERTY( QAbstractVideoSurface *videoSurface READ videoSurface WRITE setVideoSurface NOTIFY
                  videoSurfaceChanged )
public:
  CameraStream();

  QAbstractVideoSurface *videoSurface() const;
  void setVideoSurface( QAbstractVideoSurface *surface );

signals:
  void videoSurfaceChanged();

protected:
  void presentFrame( const QVideoFrame &frame ) override;
  bool hasVideoOutput() const override;

private:
  QVideoSurfaceFormat format_;
  QAbstractVideoSurface *surface_ = nullptr;
};
} // namespace ros_camera_server_qml

#endif // ROS_CAMERA_SERVER_QML_CAMERA_STREAM_H
