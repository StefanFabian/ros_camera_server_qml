// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_COMMON_CAMERA_SERVER_H
#define ROS_CAMERA_SERVER_QML_COMMON_CAMERA_SERVER_H

#include "camera_info.hpp"
#include "compat.hpp"
#include <QImage>
#include <QObject>
#include <mutex>
#include <rclcpp/node.hpp>
#include <ros_camera_server_msgs/msg/announcement.hpp>

namespace ros_camera_server_qml_common
{
class CameraServerSingleton : public QObject
{
  Q_OBJECT
  CameraServerSingleton() = default;

public:
  static CameraServerSingleton &getInstance();

  void initialize();

  //! Thread-safe snapshot. Announcements are processed on a ROS executor thread while consumers
  //! read from the Qt main thread.
  std::vector<CameraInfo> cameras() const;

signals:
  void camerasChanged();

private:
  void onAnnouncement( const ros_camera_server_msgs::msg::Announcement::ConstSharedPtr &msg );

  rclcpp::SubscriptionBase::SharedPtr announcement_sub_;
  mutable std::mutex cameras_mutex_;
  std::vector<CameraInfo> cameras_;
};

class CameraServerSingletonQml : public hcs_qml_ros2::QObjectRos2
{
  Q_OBJECT
  Q_PROPERTY( QVariantList cameras READ cameras NOTIFY camerasChanged )
public:
  CameraServerSingletonQml();

  const QVariantList &cameras() const { return cached_cameras_; }

  Q_INVOKABLE bool saveImage( const QImage &image, const QString &path );

signals:
  void camerasChanged();

private:
  void onRos2Initialized() override;
  void rebuildCameraCache();

  QVariantList cached_cameras_;
};
} // namespace ros_camera_server_qml_common

#endif // ROS_CAMERA_SERVER_QML_COMMON_CAMERA_SERVER_H
