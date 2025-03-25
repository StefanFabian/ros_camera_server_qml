// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_CAMERA_STREAM_H
#define ROS_CAMERA_SERVER_QML_CAMERA_STREAM_H

#include "../../../common/camera_stream_base.hpp"

#include <QPointer>
#include <QVideoSink>

namespace ros_camera_server_qml6
{

class CameraStream : public ros_camera_server_qml_common::CameraStreamBase
{
  Q_OBJECT
  //! Interface for QML. This is the sink the images are passed to.
  Q_PROPERTY( QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged )
public:
  CameraStream();

  QVideoSink *videoSink() const;
  void setVideoSink( QVideoSink *sink );

signals:
  void videoSinkChanged();

protected:
  void presentFrame( const QVideoFrame &frame ) override;
  bool hasVideoOutput() const override;

private:
  QPointer<QVideoSink> sink_;
};
} // namespace ros_camera_server_qml6

#endif // ROS_CAMERA_SERVER_QML_CAMERA_STREAM_H
