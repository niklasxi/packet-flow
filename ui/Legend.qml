import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: legend
    implicitWidth: 208
    implicitHeight: 117
    color: "#f9fbfe"
    border.color: "#d2deed"
    radius: 8
    property bool help: false
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 11
        spacing: 5
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "Map legend"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: "#2a405e"
                Layout.fillWidth: true
            }
            ToolButtonPF {
                symbol: "info"
                hint: "Arrow motion illustrates direction. Read captured timing in packet details."
                implicitWidth: 18
                implicitHeight: 18
                onClicked: legend.help = !legend.help
            }
        }
        GridLayout {
            columns: 3
            columnSpacing: 11
            rowSpacing: 4
            Repeater {
                model: ["TCP", "UDP", "DNS", "ICMP", "ARP", "Unknown"]
                RowLayout {
                    id: key
                    required property string modelData
                    spacing: 5
                    Rectangle {
                        width: 7
                        height: 7
                        radius: 2
                        color: flow.protocolColor(key.modelData)
                    }
                    Text {
                        text: key.modelData
                        font.pixelSize: 10
                        color: "#435a78"
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#e0e8f2"
        }
        RowLayout {
            spacing: 8
            Repeater {
                model: [["64 B", 1.5], ["512 B", 4], ["1,500 B", 6]]
                ColumnLayout {
                    id: sample
                    required property var modelData
                    spacing: 1
                    Canvas {
                        Layout.preferredWidth: 43
                        Layout.preferredHeight: 10
                        onPaint: {
                            let c = getContext("2d");
                            c.reset();
                            c.fillStyle = "#71849d";
                            let w = sample.modelData[1];
                            c.fillRect(1, 5 - w / 2, 24, w);
                            c.beginPath();
                            c.moveTo(24, 0);
                            c.lineTo(34, 5);
                            c.lineTo(24, 10);
                            c.fill();
                        }
                    }
                    Text {
                        text: sample.modelData[0]
                        font.pixelSize: 9
                        color: "#536983"
                    }
                }
            }
        }
    }
    ToolTip {
        visible: legend.help
        text: "Hue shows decoded protocol. Width shows original length, or captured length when unavailable. Motion duration is illustrative. Links connect observed endpoints."
        width: 245
        timeout: 6500
        onClosed: legend.help = false
    }
}
