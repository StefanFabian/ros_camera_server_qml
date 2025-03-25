// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_COMMON_LOGGING_HPP
#define ROS_CAMERA_SERVER_QML_COMMON_LOGGING_HPP

#include <rclcpp/logging.hpp>

#define HCS_LOG_NAME "ros_camera_server_qml"

#define HCS_LOG_DEBUG( ... ) RCLCPP_DEBUG( rclcpp::get_logger( HCS_LOG_NAME ), __VA_ARGS__ )
#define HCS_LOG_INFO( ... ) RCLCPP_INFO( rclcpp::get_logger( HCS_LOG_NAME ), __VA_ARGS__ )
#define HCS_LOG_WARN( ... ) RCLCPP_WARN( rclcpp::get_logger( HCS_LOG_NAME ), __VA_ARGS__ )
#define HCS_LOG_ERROR( ... ) RCLCPP_ERROR( rclcpp::get_logger( HCS_LOG_NAME ), __VA_ARGS__ )
#define HCS_LOG_FATAL( ... ) RCLCPP_FATAL( rclcpp::get_logger( HCS_LOG_NAME ), __VA_ARGS__ )

#define HCS_LOG_DEBUG_ONCE( ... )                                                                  \
  RCLCPP_DEBUG_ONCE( rclcpp::get_logger( HCS_LOG_NAME ), __VA_ARGS__ )
#define HCS_LOG_INFO_ONCE( ... ) RCLCPP_INFO_ONCE( rclcpp::get_logger( HCS_LOG_NAME ), __VA_ARGS__ )
#define HCS_LOG_WARN_ONCE( ... ) RCLCPP_WARN_ONCE( rclcpp::get_logger( HCS_LOG_NAME ), __VA_ARGS__ )
#define HCS_LOG_ERROR_ONCE( ... )                                                                  \
  RCLCPP_ERROR_ONCE( rclcpp::get_logger( HCS_LOG_NAME ), __VA_ARGS__ )
#define HCS_LOG_FATAL_ONCE( ... )                                                                  \
  RCLCPP_FATAL_ONCE( rclcpp::get_logger( HCS_LOG_NAME ), __VA_ARGS__ )

#endif // ROS_CAMERA_SERVER_QML_COMMON_LOGGING_HPP
