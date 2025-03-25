// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifndef ROS_CAMERA_SERVER_QML_COMMON_CAMERA_INFO_H
#define ROS_CAMERA_SERVER_QML_COMMON_CAMERA_INFO_H

#include <QObject>
#include <QVariant>

namespace ros_camera_server_qml_common
{

class CameraStreamInfo
{
  Q_GADGET
  //! The transport of the stream. Currently supported: ros2, srt, rtp, webrtc
  Q_PROPERTY( QString transport READ transport CONSTANT )
  //! The codec of the stream. Currently supported: h264, h265
  Q_PROPERTY( QString codec READ codec CONSTANT )
  Q_PROPERTY( QString uri READ uri CONSTANT )
  Q_PROPERTY( quint16 width READ width CONSTANT )
  Q_PROPERTY( quint16 height READ height CONSTANT )
  Q_PROPERTY( qreal framerate READ framerate CONSTANT )
public:
  CameraStreamInfo() = default;

  CameraStreamInfo( QString transport, QString codec, QString uri, quint16 width, quint16 height,
                    qreal framerate )
      : type_( std::move( transport ) ), codec_( std::move( codec ) ), uri_( std::move( uri ) ),
        width_( width ), height_( height ), framerate_( framerate )
  {
  }

  const QString &transport() const { return type_; }

  const QString &codec() const { return codec_; }

  const QString &uri() const { return uri_; }

  quint16 width() const { return width_; }

  quint16 height() const { return height_; }

  qreal framerate() const { return framerate_; }

  friend bool operator==( const CameraStreamInfo &lhs, const CameraStreamInfo &rhs )
  {
    return lhs.type_ == rhs.type_ && lhs.codec_ == rhs.codec_ && lhs.uri_ == rhs.uri_ &&
           lhs.width_ == rhs.width_ && lhs.height_ == rhs.height_ &&
           qFuzzyCompare( lhs.framerate_ + 1.0, rhs.framerate_ + 1.0 );
  }

private:
  QString type_;
  QString codec_;
  QString uri_;
  quint16 width_ = 0;
  quint16 height_ = 0;
  qreal framerate_ = 0;
};

class ServerInfo
{
  Q_GADGET
  Q_PROPERTY( QString robot READ robot CONSTANT )
  Q_PROPERTY( QString serverId READ serverId CONSTANT )
  Q_PROPERTY( QString host READ host CONSTANT )
  Q_PROPERTY( quint16 signalingPort READ signalingPort CONSTANT )

public:
  ServerInfo() = default;

  ServerInfo( QString robot, QString server_id, QString host, quint16 signaling_port )
      : robot_( std::move( robot ) ), server_id_( std::move( server_id ) ),
        host_( std::move( host ) ), signaling_port_( signaling_port )
  {
  }

  const QString &robot() const { return robot_; }

  const QString &serverId() const { return server_id_; }

  const QString &host() const { return host_; }

  quint16 signalingPort() const { return signaling_port_; }

  friend bool operator==( const ServerInfo &lhs, const ServerInfo &rhs )
  {
    return lhs.robot_ == rhs.robot_ && lhs.server_id_ == rhs.server_id_ && lhs.host_ == rhs.host_ &&
           lhs.signaling_port_ == rhs.signaling_port_;
  }

private:
  QString robot_;
  QString server_id_;
  QString host_;
  quint16 signaling_port_ = 0;
};

class CameraInfo
{
  Q_GADGET
  Q_PROPERTY( ServerInfo server READ server CONSTANT )
  Q_PROPERTY( QString cameraId READ cameraId CONSTANT )
  Q_PROPERTY( QString name READ name CONSTANT )
  Q_PROPERTY( QVariantList streams READ streams CONSTANT )

public:
  CameraInfo() = default;

  CameraInfo( ServerInfo server, QString id, QString name, QList<CameraStreamInfo> streams )
      : server_( std::move( server ) ), camera_( std::move( id ) ), name_( std::move( name ) ),
        streams_( std::move( streams ) )
  {
  }

  const ServerInfo &server() const { return server_; }

  const QString &cameraId() const { return camera_; }

  const QString &name() const { return name_; }

  QVariantList streams() const
  {
    QVariantList list;
    for ( const auto &stream : streams_ ) { list.append( QVariant::fromValue( stream ) ); }
    return list;
  }

  const QList<CameraStreamInfo> &rawStreams() const { return streams_; }

  friend bool operator==( const CameraInfo &lhs, const CameraInfo &rhs )
  {
    return lhs.server_ == rhs.server_ && lhs.camera_ == rhs.camera_;
  }

private:
  ServerInfo server_;
  QString camera_;
  QString name_;
  QList<CameraStreamInfo> streams_;
};

} // namespace ros_camera_server_qml_common

Q_DECLARE_METATYPE( ros_camera_server_qml_common::CameraStreamInfo )
Q_DECLARE_METATYPE( ros_camera_server_qml_common::ServerInfo )
Q_DECLARE_METATYPE( ros_camera_server_qml_common::CameraInfo )

#endif // ROS_CAMERA_SERVER_QML_COMMON_CAMERA_INFO_H
