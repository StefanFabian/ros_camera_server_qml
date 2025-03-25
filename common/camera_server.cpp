// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "camera_server.hpp"
#include "logging.hpp"

using namespace ros_camera_server_msgs::msg;

namespace ros_camera_server_qml_common
{

CameraServerSingleton &CameraServerSingleton::getInstance()
{
  static CameraServerSingleton instance;
  return instance;
}

void CameraServerSingleton::initialize()
{
  if ( announcement_sub_ )
    return;
  const auto &node = hcs_qml_ros2::Ros2Qml::getInstance().node();
  if ( !node ) {
    HCS_LOG_ERROR( "Failed to initialize CameraServerSingleton: ROS2 node not initialized" );
    return;
  }
  announcement_sub_ = node->create_subscription<Announcement>(
      "/camera_server_announcement", rclcpp::QoS( 100 ).transient_local(),
      [this]( const Announcement::ConstSharedPtr &msg ) { onAnnouncement( msg ); } );
  HCS_LOG_DEBUG( "CameraServer initialized. Subscribed to /camera_server_announcement" );
}

namespace
{
bool isSame( const CameraStreamInfo &info, const CameraStream &stream )
{
  return info.transport().toStdString() == stream.transport &&
         info.codec().toStdString() == stream.codec && info.uri().toStdString() == stream.uri &&
         info.width() == stream.width && info.height() == stream.height &&
         qFuzzyCompare( info.framerate() + 1.0, stream.framerate + 1.0 );
}

CameraStreamInfo toStreamInfo( const CameraStream &stream )
{
  return { QString::fromStdString( stream.transport ),
           QString::fromStdString( stream.codec ),
           QString::fromStdString( stream.uri ),
           stream.width,
           stream.height,
           stream.framerate };
}

CameraInfo toCameraInfo( const Announcement &msg, const Camera &camera )
{
  QList<CameraStreamInfo> streams;
  streams.reserve( static_cast<int>( camera.streams.size() ) );
  for ( const auto &stream : camera.streams ) { streams.append( toStreamInfo( stream ) ); }
  return { ServerInfo( QString::fromStdString( msg.robot ), QString::fromStdString( msg.server_id ),
                       QString::fromStdString( msg.host ), msg.signaling_port ),
           QString::fromStdString( camera.id ), QString::fromStdString( camera.name ), streams };
}
} // namespace

std::vector<CameraInfo> CameraServerSingleton::cameras() const
{
  std::lock_guard<std::mutex> lock( cameras_mutex_ );
  return cameras_;
}

void CameraServerSingleton::onAnnouncement( const Announcement::ConstSharedPtr &msg )
{
  HCS_LOG_DEBUG( "Parsing new cameraId server announcement from robot: %s", msg->robot.c_str() );
  const QString msg_robot = QString::fromStdString( msg->robot );
  const QString msg_server_id = QString::fromStdString( msg->server_id );

  bool changed = false;
  size_t camera_count;
  {
    std::lock_guard<std::mutex> lock( cameras_mutex_ );
    std::vector<CameraInfo> to_remove;
    for ( auto &camera : cameras_ ) {
      if ( camera.server().robot() != msg_robot || camera.server().serverId() != msg_server_id )
        continue;
      auto it = std::find_if( msg->cameras.begin(), msg->cameras.end(),
                              [&camera]( const Camera &camera_msg ) {
                                return camera_msg.id == camera.cameraId().toStdString();
                              } );
      if ( it == msg->cameras.end() ) {
        to_remove.push_back( camera );
        continue;
      }
      if ( camera.rawStreams().size() != static_cast<int>( it->streams.size() ) ||
           std::mismatch(
               camera.rawStreams().begin(), camera.rawStreams().end(), it->streams.begin(),
               []( const CameraStreamInfo &a, const CameraStream &b ) {
                 return isSame( a, b );
               } ).first != camera.rawStreams().end() ) {
        camera = toCameraInfo( *msg, *it );
        changed = true;
        continue;
      }
      // No change
    }
    if ( !to_remove.empty() ) {
      for ( const auto &camera : to_remove ) {
        cameras_.erase( std::remove( cameras_.begin(), cameras_.end(), camera ), cameras_.end() );
      }
      changed = true;
    }
    // Now add new cameras
    for ( const auto &camera_msg : msg->cameras ) {
      if ( camera_msg.name.size() >= 2 && camera_msg.name.substr( 0, 2 ) == "__" ) {
        continue; // Skip cameras with names starting with "__"
      }
      if ( std::find_if( cameras_.begin(), cameras_.end(),
                         [&camera_msg, &msg_robot]( const CameraInfo &camera ) {
                           return camera.server().robot() == msg_robot &&
                                  camera.cameraId().toStdString() == camera_msg.id;
                         } ) != cameras_.end() )
        continue;
      cameras_.push_back( toCameraInfo( *msg, camera_msg ) );
      changed = true;
    }
    camera_count = cameras_.size();
  }
  if ( changed ) {
    HCS_LOG_DEBUG( "Camera list changed. Now have %zu cameras.", camera_count );
    // Emitted outside the lock so receivers never run while cameras_ is held.
    emit camerasChanged();
  } else {
    HCS_LOG_DEBUG( "Camera list processed. No changes. Count: %zu", camera_count );
  }
}

CameraServerSingletonQml::CameraServerSingletonQml()
{
  connect( &CameraServerSingleton::getInstance(), &CameraServerSingleton::camerasChanged, this,
           &CameraServerSingletonQml::rebuildCameraCache );
  // This object is created per QQmlEngine and is destroyed whenever the engine is (e.g. on a live
  // reload), whereas CameraServerSingleton and its transient local subscription live for the whole
  // process. Without seeding, the cache would stay empty until the next announcement, which may
  // never come since servers announce only once and no new subscription triggers a replay.
  rebuildCameraCache();
}

bool CameraServerSingletonQml::saveImage( const QImage &image, const QString &path )
{
  if ( image.isNull() || path.isEmpty() )
    return false;
  return image.save( path );
}

void CameraServerSingletonQml::onRos2Initialized()
{
  CameraServerSingleton::getInstance().initialize();
  rebuildCameraCache();
}

void CameraServerSingletonQml::rebuildCameraCache()
{
  cached_cameras_.clear();
  const auto &cameras = CameraServerSingleton::getInstance().cameras();
  cached_cameras_.reserve( static_cast<int>( cameras.size() ) );
  for ( const auto &camera : cameras ) {
#if QT_VERSION_MAJOR < 6
    // Use QVariantMaps instead of Q_GADGET instances to avoid Qt5's broken
    // QQmlValueTypeWrapper which creates dangling pointers for nested Q_GADGET
    // property chains (e.g. cameras[i].server.robot).
    QVariantMap server_map;
    server_map["robot"] = camera.server().robot();
    server_map["serverId"] = camera.server().serverId();
    server_map["host"] = camera.server().host();
    server_map["signalingPort"] = camera.server().signalingPort();

    QVariantList stream_list;
    for ( const auto &stream : camera.rawStreams() ) {
      QVariantMap stream_map;
      stream_map["transport"] = stream.transport();
      stream_map["codec"] = stream.codec();
      stream_map["uri"] = stream.uri();
      stream_map["width"] = stream.width();
      stream_map["height"] = stream.height();
      stream_map["framerate"] = stream.framerate();
      stream_list.append( stream_map );
    }

    QVariantMap camera_map;
    camera_map["server"] = server_map;
    camera_map["cameraId"] = camera.cameraId();
    camera_map["name"] = camera.name();
    camera_map["streams"] = stream_list;
    cached_cameras_.append( camera_map );
#else
    cached_cameras_.append( QVariant::fromValue( camera ) );
#endif
  }
  emit camerasChanged();
}
} // namespace ros_camera_server_qml_common
