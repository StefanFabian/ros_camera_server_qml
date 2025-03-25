# ROS Camera Server QML

QML plugin for [ROS Camera Server](https://github.com/StefanFabian/ros_camera_server).
It discovers the cameras announced by all camera servers on the network and streams them into Qt Quick user interfaces with low latency.

## ✨ Features

* 🔍 **Automatic Discovery**: Lists all cameras announced by any camera server on the network
* 🧠 **Automatic Stream Selection**: Picks the best transport (SRT > WebRTC > others) and the smallest resolution that fits the display size
* 🔀 **Seamless Switching**: Keeps the previous stream visible until the newly selected stream delivers its first frame
* ♻️ **Shared Subscriptions**: Multiple `CameraStream` elements showing the same camera share a single connection and decoder
* 📡 **Multiple Transports**: SRT, WebRTC, RTP and native ROS 2 image topics
* 🕐 **Latency Statistics**: Reports network, processing and total latency per stream
* 🧩 **Qt5 and Qt6**: Separate plugins with the same QML API, built from a shared code base

## 📦 Packages

| Package                  | Qt  | Depends on                                                           |
| ------------------------ | --- | -------------------------------------------------------------------- |
| `ros_camera_server_qml6` | Qt6 | [qml6_ros2_plugin](https://github.com/StefanFabian/qml6_ros2_plugin) |
| `ros_camera_server_qml`  | Qt5 | [qml_ros2_plugin](https://github.com/StefanFabian/qml_ros2_plugin)   |

`ros_camera_server_qml6` also registers a camera viewer plugin for [RQml](https://github.com/StefanFabian/rqml).

## 🛠️ Building from Source

Requires at least ROS 2 Jazzy.

1. **Create a ROS 2 workspace** (if you haven't already):
   ```bash
   mkdir -p ~/ros2_ws/src
   cd ~/ros2_ws/src
   ```

2. **Clone the repositories and install dependencies**:
   ```bash
   git clone https://github.com/StefanFabian/ros_camera_server_qml.git
   git clone https://github.com/StefanFabian/ros_camera_server.git
   git clone https://github.com/StefanFabian/gstreamer_ros_babel_fish.git
   git clone https://github.com/StefanFabian/qml6_ros2_plugin.git  # Qt6
   git clone https://github.com/StefanFabian/qml_ros2_plugin.git   # Qt5
   rosdep install --from-paths . --ignore-src -r -y
   ```

3. **Build the workspace and source it again to register the new packages**:
   ```bash
   cd ~/ros2_ws
   colcon build --symlink-install --packages-up-to ros_camera_server_qml6  # or ros_camera_server_qml for Qt5
   source install/setup.bash
   ```

## 🚀 Usage

```qml
import QtMultimedia
import Ros2.CameraServer

VideoOutput {
    id: videoOutput
    anchors.fill: parent
}

CameraStream {
    id: stream
    robot: CameraServer.cameras[0].server.robot
    cameraId: CameraServer.cameras[0].cameraId
    videoSink: videoOutput.videoSink                                // Qt6
    preferredSize: Qt.size(videoOutput.width, videoOutput.height)  // Drives the resolution selection
}
```

With Qt5, import `Ros2.CameraServer 1.0`, drop the `videoSink` binding and set `source: stream` on the `VideoOutput` instead.

A complete example with robot and camera selection is in `examples/video_stream.qml` of each package.
See the README next to it for how to run it.
To get a camera to stream, run the example config of `ros_camera_server`, which streams `/dev/video0`:

```bash
ros2 launch ros_camera_server server.launch.yaml
```

## 🧩 QML API

### CameraServer (singleton)

| Property / Method        | Type               | Description                                        |
| ------------------------ | ------------------ | -------------------------------------------------- |
| `cameras`                | `list<CameraInfo>` | All cameras announced by the camera servers        |
| `saveImage(image, path)` | `bool`             | Saves an image (e.g. from `grabFrame()`) to `path` |

### CameraInfo

| Property   | Type                     | Description                       |
| ---------- | ------------------------ | --------------------------------- |
| `server`   | `ServerInfo`             | Server that provides this camera  |
| `cameraId` | `string`                 | Camera id, unique per robot       |
| `name`     | `string`                 | Human-readable camera name        |
| `streams`  | `list<CameraStreamInfo>` | Streams available for this camera |

### ServerInfo

| Property        | Type     | Description                      |
| --------------- | -------- | -------------------------------- |
| `robot`         | `string` | Robot name, unique per network   |
| `serverId`      | `string` | Id of the camera server instance |
| `host`          | `string` | Address of the server            |
| `signalingPort` | `uint`   | WebRTC signaling port            |

### CameraStreamInfo

| Property    | Type     | Description                                                        |
| ----------- | -------- | ------------------------------------------------------------------ |
| `transport` | `string` | `srt`, `webrtc`, `rtp` or `ros2`                                   |
| `codec`     | `string` | Codec of the stream. For `ros2`, the image transport               |
| `uri`       | `string` | Stream URI, e.g. `srt://192.168.1.100:7100`. For `ros2`, the topic |
| `width`     | `uint`   | Image width                                                        |
| `height`    | `uint`   | Image height                                                       |
| `framerate` | `real`   | Frame rate                                                         |

### CameraStream

| Property            | Type                    | Description                                                                                   |
| ------------------- | ----------------------- | --------------------------------------------------------------------------------------------- |
| `robot`             | `string`                | Robot to stream from                                                                          |
| `cameraId`          | `string`                | Camera to stream                                                                              |
| `videoSink`         | `QVideoSink`            | Qt6 only. Bind to `VideoOutput.videoSink`                                                     |
| `videoSurface`      | `QAbstractVideoSurface` | Qt5 only. Set implicitly by `VideoOutput { source: stream }`                                  |
| `enabled`           | `bool`                  | Subscribes to the stream while `true` (default)                                               |
| `paused`            | `bool`                  | Drops frames while keeping the subscription active                                            |
| `streamIndex`       | `int`                   | Index into `CameraInfo.streams`, `-1` for automatic selection (default)                       |
| `preferredSize`     | `size`                  | Display size used for the automatic resolution selection                                      |
| `resolution`        | `size`                  | Read-only. Resolution of the active stream                                                    |
| `codec`             | `string`                | Read-only. Codec of the active stream                                                         |
| `transport`         | `string`                | Read-only. Transport of the active stream                                                     |
| `framerate`         | `real`                  | Read-only. Measured frame rate                                                                |
| `latency`           | `int`                   | Read-only. Total latency until the frame is handed to the video output in ms, `-1` if unknown |
| `networkLatency`    | `int`                   | Read-only. Latency from the server to this device in ms, `-1` if unknown                      |
| `processingLatency` | `int`                   | Read-only. Decoding latency on this device in ms, `-1` if unknown                             |

| Method        | Description                                 |
| ------------- | ------------------------------------------- |
| `play()`      | Unpauses and re-enables the stream          |
| `pause()`     | Same as setting `paused` to `true`          |
| `stop()`      | Same as setting `enabled` to `false`        |
| `grabFrame()` | Returns the last received frame as an image |

## 📄 License

This project is licensed under the **MIT** License.
Note that the [ROS Camera Server](https://github.com/StefanFabian/ros_camera_server) itself is licensed under AGPL-3.0-only.
This plugin only depends on its MIT-licensed message definitions.
