// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_COMMON_COMPAT_HPP
#define ROS_CAMERA_SERVER_QML_COMMON_COMPAT_HPP

#include <QtGlobal>

#if QT_VERSION_MAJOR >= 6
#include <qml6_ros2_plugin/image_transport_manager.hpp>
#include <qml6_ros2_plugin/qobject_ros2.hpp>
#include <qml6_ros2_plugin/ros2.hpp>
namespace hcs_qml_ros2 = qml6_ros2_plugin;
#else
#include <qml_ros2_plugin/image_transport_manager.hpp>
#include <qml_ros2_plugin/qobject_ros2.hpp>
#include <qml_ros2_plugin/ros2.hpp>
namespace hcs_qml_ros2 = qml_ros2_plugin;
#endif

#endif // ROS_CAMERA_SERVER_QML_COMMON_COMPAT_HPP
