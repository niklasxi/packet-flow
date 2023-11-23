import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: strip
    implicitHeight: 74
    color: "#f8faff"
    function clockText(t) {
        let n = Math.max(0, Math.floor(t));
        return Math.floor(n / 60) + ":" + String(n % 60).padStart(2, "0") + "." + Math.floor((t % 1) * 10);
    }
    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: "#d4dfed"
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 19
        anchors.rightMargin: 20
        spacing: 12
        RowLayout {
            spacing: 4
            ToolButtonPF {
                visible: !flow.live
                symbol: "restart"
                boxed: true
                hint: "Restart playback"
                enabled: flow.duration > 0 && !flow.busy
                onClicked: flow.restart()
            }
            ToolButtonPF {
                symbol: flow.playing ? "pause" : "play"
                boxed: true
                hint: flow.live ? (flow.playing ? "Pause view" : "Go live") : (flow.playing ? "Pause playback" : "Play capture")
                enabled: !flow.busy && (flow.live ? flow.running : flow.duration > 0)
                onClicked: flow.toggle()
            }
        }
        ColumnLayout {
            spacing: 3
            Text {
                text: flow.live ? "Live activity" : "Replay timeline"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: "#263d5d"
            }
            Text {
                text: strip.clockText(flow.position) + (flow.live ? "" : "  /  " + strip.clockText(flow.duration))
                font.family: "Menlo"
                font.pixelSize: 10
                color: "#5c708b"
            }
        }
        Item {
            id: track
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Canvas {
                id: wave
                anchors.fill: parent
                property var bins: flow.histogram
                onBinsChanged: requestPaint()
                onPaint: {
                    let c = getContext("2d");
                    c.reset();
                    let w = width / Math.max(bins.length, 1);
                    let max = Math.max(1, ...bins);
                    for (let i = 0; i < bins.length; i++) {
                        let h = bins[i] ? 3 + Math.log(1 + bins[i]) / Math.log(1 + max) * 25 : 2;
                        c.fillStyle = bins[i] ? "#c2a6ed" : "#dfe4ee";
                        c.fillRect(i * w + 1, height - 8 - h, Math.max(1, w - 2), h);
                    }
                    c.fillStyle = "#d1dceb";
                    c.fillRect(0, height - 6, width, 1);
                }
            }
            Rectangle {
                visible: !flow.live && flow.duration > 0
                x: Math.min(track.width - 2, track.width * flow.position / Math.max(.001, flow.duration))
                y: 6
                width: 2
                height: 33
                color: "#8954cc"
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.top
                    width: 7
                    height: 7
                    radius: 4
                    color: parent.color
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: !flow.live && !flow.busy && flow.duration > 0
                function go(x) {
                    flow.seek(Math.max(0, Math.min(1, x / width)) * flow.duration);
                }
                onPressed: e => go(e.x)
                onPositionChanged: e => {
                    if (pressed)
                        go(e.x);
                }
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.Slider
                Accessible.name: "Seek replay timeline"
            }
        }
        ToolButtonPF {
            visible: !flow.live
            boxed: true
            text: flow.speed + "×"
            hint: "Playback speed"
            onClicked: speeds.popup()
            Menu {
                id: speeds
                Repeater {
                    model: [0.25, 0.5, 1, 2, 4]
                    MenuItem {
                        required property real modelData
                        text: modelData + "×"
                        checkable: true
                        checked: flow.speed === modelData
                        onTriggered: flow.speed = modelData
                    }
                }
            }
        }
    }
}
