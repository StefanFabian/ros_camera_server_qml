
# ROS Camera Server QML6 Example

This example demonstrates how to use the ROS Camera Server QML6 module to display a video stream in a QML application.

## Dependencies

The following Qt6 QML modules are required:

- qml6-module-qtquick-controls
- qml6-module-qtquick-layouts
- qml6-module-qtquick-templates
- qml6-module-qtquick-window
- qml6-module-qtqml-workerscript

On Ubuntu/Debian, you can install them with:

```sh
sudo apt-get install \
  qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts \
  qml6-module-qtquick-templates \
  qml6-module-qtquick-window \
  qml6-module-qtqml-workerscript
```

You will also need the ROS Camera Server QML6 plugin and its dependencies built and available in your QML import path.

## Running the Example

1. Ensure all dependencies are installed and the ROS Camera Server QML6 plugin is built.
2. Resource your environment or open a new terminal
3. Run the example from this directory

```sh
qmlscene -qt=qt6 ./video_stream.qml
```

If it can't find it but you did install `qmlscene-qt6`, you can try the following on Ubuntu/Debian Linux:

```sh
/usr/lib/qt6/bin/qmlscene ./video_stream.qml
```

This will launch the example application window where you can select a robot / camera server and then a camera.
You can use the example server launch file in `ros_camera_server` which should stream your webcam if it is available as `/dev/video0`.

## Notes

- Make sure your ROS2 environment is sourced if you are using ROS2 integration.
- The example expects the ROS Camera Server QML6 plugin to be discoverable by the QML engine.
