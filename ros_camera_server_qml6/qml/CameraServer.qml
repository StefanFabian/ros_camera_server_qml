import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import RQml.Elements
import RQml.Fonts
import Ros2.CameraServer
import Ros2

Rectangle {
    id: root
    // Set the minimum size for this plugin's dock widget
    property var kddockwidgets_min_size: Qt.size(640, 320)
    color: palette.base

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
                Layout.preferredWidth: 160
                textRole: "text"
                model: ListModel {}

                onActivated: updateCameraList()

                Component.onCompleted: updateRobotList()

                Connections {
                    target: CameraServer
                    function onCamerasChanged() {
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

                onActivated: updateStreamList()
            }

            Item { Layout.fillWidth: true }

            IconToggleButton {
                checked: !stream.paused && stream.enabled
                onToggled: {
                    if (checked)
                        stream.play()
                    else
                        stream.pause()
                }
                iconOn: IconFont.iconPause
                iconOff: IconFont.iconPlay
                tooltipTextOn: qsTr("Click to pause")
                tooltipTextOff: qsTr("Click to start")
            }

            IconButton {
                tooltipText: qsTr("Save Image")
                text: IconFont.iconSave
                onClicked: fileDialog.saveImage()
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

        RowLayout {
            spacing: 10

            Label {
                text: "Stream:"
                verticalAlignment: Text.AlignVCenter
            }
            ComboBox {
                id: streamCombo
                Layout.preferredWidth: 200
                textRole: "description"
                model: ListModel {}
            }

            Item { Layout.fillWidth: true }

            ColumnLayout {
                Label {
                    function pad(value, width) {
                        let s = String(value);
                        while (s.length < width) s = " " + s;
                        return s;
                    }
                    text: (stream.codec ? stream.codec.toUpperCase() : "Unknown") +
                            " (" + (stream.transport ? stream.transport.toUpperCase() : "?") + ")" +
                            " | " + stream.resolution.width + "x" + stream.resolution.height +
                            " @ " + pad(stream.framerate, 3) + "fps"
                    visible: stream.codec !== ""
                    font.family: "monospace"
                }

                Label {
                    function pad(value, width) {
                        let s = String(value);
                        while (s.length < width) s = " " + s;
                        return s;
                    }
                    text: "Latency: " + (stream.latency >= 0 ? pad(stream.latency, 4) + "ms" : " N/A") +
                            " (Net: " + (stream.networkLatency >= 0 ? pad(stream.networkLatency, 4) + "ms" : "   ?") +
                            ", Proc: " + (stream.processingLatency >= 0 ? pad(stream.processingLatency, 3) + "ms" : "  ?") + ")"
                    visible: stream.streamCodec !== ""
                    font.family: "monospace"
                }
            }
        }
    }

    CameraStream {
        id: stream
        videoSink: videoOutput.videoSink
        robot: robotCombo.currentText
        cameraId: cameraCombo.count > 0 && cameraCombo.currentIndex >= 0 ? cameraCombo.model.get(cameraCombo.currentIndex).cameraId : ""
        streamIndex: streamCombo.count > 0 && streamCombo.currentIndex >= 0 ? streamCombo.model.get(streamCombo.currentIndex).index : -1
        enabled: true
        // Preferred size is used to automatically choose a stream if multiple are available.
        preferredSize: Qt.size(videoOutput.width, videoOutput.height)
    }

    property var grabbedFrame: null

    FileDialog {
        id: fileDialog
        title: qsTr("Save Image")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.bmp)"]
        onAccepted: {
            let path = fileDialog.selectedFile.toString();
            if (path.startsWith("file://"))
                path = path.slice(7);
            CameraServer.saveImage(root.grabbedFrame, path);
            root.grabbedFrame = null;
        }
        onRejected: {
            root.grabbedFrame = null;
        }

        function saveImage() {
            root.grabbedFrame = stream.grabFrame();
            open();
        }
    }

    function updateRobotList() {
        var currentRobot = robotCombo.currentText
        var robots = new Set()
        var cameras = CameraServer.cameras

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
        var currentCameraId = stream.cameraId

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
                if (cameras[i].cameraId === currentCameraId) {
                    newIndex = cameraCombo.model.count - 1
                    foundCamera = true
                }
            }
        }

        if (cameraCombo.model.count > 0) {
            cameraCombo.currentIndex = newIndex
        }
        updateStreamList()
    }

    function updateStreamList() {
        var currentRobot = robotCombo.currentText
        var currentCameraId = ""
        if (cameraCombo.currentIndex >= 0 && cameraCombo.model.count > 0) {
            currentCameraId = cameraCombo.model.get(cameraCombo.currentIndex).cameraId
        }

        var currentStreamIndex = stream.streamIndex

        streamCombo.model.clear()

        // Add "Auto" option
        streamCombo.model.append({
            description: "Auto",
            index: -1
        })

        if (!currentRobot || !currentCameraId) {
            streamCombo.currentIndex = 0
            return
        }

        var cameras = CameraServer.cameras
        var camera = null
        for (var i = 0; i < cameras.length; i++) {
            if (cameras[i].server.robot === currentRobot && cameras[i].cameraId === currentCameraId) {
                camera = cameras[i]
                break
            }
        }

        if (camera) {
            var streams = camera.streams
            for (var j = 0; j < streams.length; j++) {
                var s = streams[j]
                streamCombo.model.append({
                    description: s.transport.toUpperCase() + " (" + s.width + "x" + s.height + ", " + s.codec + ")",
                    index: j
                })
            }
        }

        // Try to restore selection
        var newIndex = 0
        for (var k = 0; k < streamCombo.model.count; k++) {
            if (streamCombo.model.get(k).index === currentStreamIndex) {
                 newIndex = k
                 break
            }
        }
        streamCombo.currentIndex = newIndex
    }
}
