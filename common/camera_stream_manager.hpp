// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_COMMON_CAMERA_STREAM_MANAGER_H
#define ROS_CAMERA_SERVER_QML_COMMON_CAMERA_STREAM_MANAGER_H

#include "camera_info.hpp"
#include "ring_buffer.hpp"
#include "stream_statistics.hpp"

#include <QSize>
#include <QVideoFrame>
#include <chrono>
#include <functional>
#include <memory>

namespace ros_camera_server_qml_common
{
class SmartCameraStream;

class CameraStreamManager
{
  CameraStreamManager() = default;

public:
  using FrameCallback = std::function<void( const QVideoFrame &, StreamStatistics )>;

  static CameraStreamManager &getInstance();

  struct SubscriptionBase {
    virtual ~SubscriptionBase() = default;
    QString transport;
    QString codec;
  };

  std::shared_ptr<CameraStreamManager::SubscriptionBase>
  createSubscription( const std::string &robot, const std::string &camera_id,
                      const CameraStreamManager::FrameCallback &callback, int stream_index = -1,
                      const QSize &preferred_size = QSize() );

  void updatePreferredSize( const SubscriptionBase *subscription, const QSize &preferred_size );

private:
  struct Subscription;
  void removeCallback( const Subscription *subscription );

  struct StreamData {
    using Ptr = std::unique_ptr<StreamData>;
    StreamData( const std::string &robot, const std::string &camera_id, int stream_index,
                const QSize &preferred_size );

    std::unique_ptr<SmartCameraStream> stream;
    std::vector<Subscription *> subscriptions;
    RingBuffer<std::chrono::steady_clock::time_point, 10> frame_times_;
  };

  static QSize largestPreferredSize( const StreamData &stream_data );

  std::map<std::string, StreamData::Ptr> streams_;
};
} // namespace ros_camera_server_qml_common

#endif // ROS_CAMERA_SERVER_QML_COMMON_CAMERA_STREAM_MANAGER_H
