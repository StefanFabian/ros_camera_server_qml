import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Ros2.CameraServer
import Ros2

ApplicationWindow {
    visible: true
    width: 800
    height: 600
    title: "Ros Camera Stream Example"

    Component.onCompleted: {
        Ros2.init("video_stream_example")
        // Ros2.getLogger("ros_camera_server_qml").setLoggerLevel(Ros2LoggerLevel.Debug)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: "Robot:"
                verticalAlignment: Text.AlignVCenter
            }
            ComboBox {
                id: robotCombo
                Layout.preferredWidth: 200
                textRole: "text"
                model: ListModel {}

                onActivated: updateCameraList()

                Component.onCompleted: updateRobotList()

                Connections {
                    target: CameraServer
                    function onCamerasChanged() {
                        Ros2.info("Cameras changed. Updating controls.")
                        updateRobotList()
                    }
                }
            }

            Label {
                text: "Camera:"
                verticalAlignment: Text.AlignVCenter
            }
            ComboBox {
                id: cameraCombo
                Layout.preferredWidth: 200
                textRole: "name"
                model: ListModel {}
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"

            VideoOutput {
                id: videoOutput
                anchors.fill: parent
            }

            Text {
                anchors.centerIn: parent
                text: "No Signal"
                color: "white"
                visible: stream.robot === "" || stream.cameraId === ""
            }
        }

        Label {
            text: "Latency: " + stream.latency + "ms"
        }
    }

    CameraStream {
        id: stream
        videoSink: videoOutput.videoSink
        robot: robotCombo.currentText
        cameraId: cameraCombo.count > 0 && cameraCombo.currentIndex >= 0 ? cameraCombo.model.get(cameraCombo.currentIndex).cameraId : ""
        enabled: true
        preferredSize: Qt.size(videoOutput.width, videoOutput.height)
    }

    function updateRobotList() {
        var currentRobot = robotCombo.currentText
        var robots = new Set()
        var cameras = CameraServer.cameras
        // console.log("Updating robot list. Found " + cameras.length + " cameras.")

        for (var i = 0; i < cameras.length; i++) {
            robots.add(cameras[i].server.robot)
        }

        robotCombo.model.clear()
        var sortedRobots = Array.from(robots).sort()
        for (var j = 0; j < sortedRobots.length; j++) {
            robotCombo.model.append({text: sortedRobots[j]})
        }

        // Restore selection or select first
        var index = -1
        for (var k = 0; k < robotCombo.model.count; k++) {
             if (robotCombo.model.get(k).text === currentRobot) {
                 index = k;
                 break;
             }
        }

        if (index >= 0) {
            robotCombo.currentIndex = index
        } else if (robotCombo.model.count > 0) {
            robotCombo.currentIndex = 0
        }

        updateCameraList()
    }

    function updateCameraList() {
        var currentRobot = robotCombo.currentText
        var currentCameraId = stream.cameraId // Use current stream ID to try to preserve selection

        cameraCombo.model.clear()
        if (!currentRobot) return

        var cameras = CameraServer.cameras
        var foundCamera = false
        var newIndex = 0

        for (var i = 0; i < cameras.length; i++) {
            if (cameras[i].server.robot === currentRobot) {
                cameraCombo.model.append({
                    name: cameras[i].name,
                    cameraId: cameras[i].cameraId
                })
                // console.log("Added camera: " + cameras[i].name + " ID: " + cameras[i].cameraId)
                if (cameras[i].cameraId === currentCameraId) {
                    newIndex = cameraCombo.model.count - 1
                    foundCamera = true
                }
            }
        }

        if (cameraCombo.model.count > 0) {
            cameraCombo.currentIndex = newIndex
        }
    }
}
