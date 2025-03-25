// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_COMMON_SMART_CAMERA_STREAM_H
#define ROS_CAMERA_SERVER_QML_COMMON_SMART_CAMERA_STREAM_H

#include "../camera_stream_manager.hpp"
#include <QObject>
#include <QVideoFrame>
#include <functional>
#include <memory>

namespace ros_camera_server_qml_common
{

class SmartCameraStream : public QObject
{
  Q_OBJECT
public:
  SmartCameraStream( std::string robot, std::string camera_id, int stream_index,
                     const QSize &preferred_size,
                     const std::function<void( const QVideoFrame &, StreamStatistics )> &callback );
  ~SmartCameraStream() override;

  const CameraStreamInfo &currentStream() const;

  //! Returns true if a stream switch is in progress (old stream still alive).
  bool hasPendingSwitch() const;

  //! Releases the previous stream handle and emits currentStreamChanged.
  void releasePreviousStream();

  void setPreferredSize( const QSize &size );

signals:
  void currentStreamChanged();

private:
  void updateStream();

  std::shared_ptr<void>
  subscribeROS( const QString &topic, const std::string &transport,
                const std::function<void( const QVideoFrame &, StreamStatistics )> &callback ) const;

  std::string robot_;
  std::string camera_id_;
  int stream_index_;
  QSize preferred_size_;
  std::function<void( const QVideoFrame &, StreamStatistics )> callback_;
  struct Data;
  std::unique_ptr<Data> data_;
};

} // namespace ros_camera_server_qml_common

#endif // ROS_CAMERA_SERVER_QML_COMMON_SMART_CAMERA_STREAM_H
