cmake_minimum_required(VERSION 3.16)
project(ros_camera_server_qml6)

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ros_camera_server_msgs REQUIRED)
find_package(qml6_ros2_plugin REQUIRED)
find_package(
  Qt6
  COMPONENTS Core Multimedia Qml Quick WebSockets
  REQUIRED)

include(FindPkgConfig)
pkg_check_modules(
  GST
  REQUIRED
  gstreamer-1.0
  gstreamer-app-1.0
  gstreamer-net-1.0
  gstreamer-rtp-1.0
  gstreamer-video-1.0
  gstreamer-webrtc-1.0
  gstreamer-sdp-1.0)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

set(SOURCES
    ${COMMON_DIR}/camera_info.hpp
    ${COMMON_DIR}/camera_server.cpp
    ${COMMON_DIR}/camera_stream_base.hpp
    ${COMMON_DIR}/camera_stream_base.cpp
    ${COMMON_DIR}/camera_stream_manager.cpp
    ${COMMON_DIR}/internal/gstreamer_pipeline_appsink.cpp
    ${COMMON_DIR}/internal/gstreamer_pipeline_base.cpp
    ${COMMON_DIR}/internal/smart_camera_stream.cpp
    ${COMMON_DIR}/internal/webrtc_client.cpp
    src/qml_elements/camera_stream.hpp
    src/qml_elements/camera_stream.cpp
    src/ros_camera_server_qml6.cpp)

add_library(ros_camera_server_qml6 SHARED ${SOURCES})
target_include_directories(ros_camera_server_qml6 PUBLIC ${GST_INCLUDE_DIRS})
target_link_libraries(
  ros_camera_server_qml6
  ${GST_LIBRARIES}
  Qt6::Core
  Qt6::Multimedia
  Qt6::Qml
  Qt6::Quick
  Qt6::WebSockets)
target_link_libraries(ros_camera_server_qml6 ${ros_camera_server_msgs_TARGETS}
                      qml6_ros2_plugin::qml6_ros2_plugin)

install(TARGETS ros_camera_server_qml6
        DESTINATION share/${PROJECT_NAME}/Ros2/CameraServer)
install(FILES qmldir DESTINATION share/${PROJECT_NAME}/Ros2/CameraServer)

ament_environment_hooks(register_qml_module.dsv.in)

# Install rqml plugin
install(DIRECTORY qml DESTINATION share/${PROJECT_NAME})
# Register rqml plugin
ament_index_register_resource(rqml_plugin CONTENT_FILE rqml_plugins.yaml.in)

ament_package()
