cmake_minimum_required(VERSION 3.16)
project(ros_camera_server_qml)

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ros_camera_server_msgs REQUIRED)
find_package(qml_ros2_plugin REQUIRED)
find_package(
  Qt5
  COMPONENTS Core Multimedia Qml WebSockets
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
    src/ros_camera_server_qml.cpp)

add_library(ros_camera_server_qml SHARED ${SOURCES})
target_include_directories(ros_camera_server_qml PUBLIC ${GST_INCLUDE_DIRS})
target_link_libraries(ros_camera_server_qml ${GST_LIBRARIES} Qt5::Core
                      Qt5::Multimedia Qt5::Qml Qt5::WebSockets)
target_link_libraries(ros_camera_server_qml ${ros_camera_server_msgs_TARGETS}
                      qml_ros2_plugin::qml_ros2_plugin)

install(TARGETS ros_camera_server_qml
        DESTINATION share/${PROJECT_NAME}/Ros2/CameraServer)
install(FILES qmldir DESTINATION share/${PROJECT_NAME}/Ros2/CameraServer)

ament_environment_hooks(register_qml_module.dsv.in)

ament_package()
