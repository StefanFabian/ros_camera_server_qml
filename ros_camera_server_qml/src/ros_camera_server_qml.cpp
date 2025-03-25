// Copyright (c) 2025 Stefan Fabian. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "../../common/camera_server.hpp"
#include "src/qml_elements/camera_stream.hpp"

using namespace ros_camera_server_qml_common;

#include <QQmlExtensionPlugin>
#include <QtQml>

namespace ros_camera_server_qml
{

class RosCameraServerQml : public QQmlExtensionPlugin
{
  Q_OBJECT
  Q_PLUGIN_METADATA( IID QQmlExtensionInterface_iid )
public:
  void registerTypes( const char *uri ) override
  {
    Q_ASSERT( uri == QLatin1String( "Ros2.CameraServer" ) );
    qmlRegisterSingletonType<CameraServerSingletonQml>(
        "Ros2.CameraServer", 1, 0, "CameraServer",
        []( QQmlEngine *engine, QJSEngine *scriptEngine ) -> QObject * {
          Q_UNUSED( engine )
          Q_UNUSED( scriptEngine )
          return new CameraServerSingletonQml;
        } );

    // Image transport
    qmlRegisterType<CameraStream>( "Ros2.CameraServer", 1, 0, "CameraStream" );
  }
};
} // namespace ros_camera_server_qml

#include "ros_camera_server_qml.moc"
