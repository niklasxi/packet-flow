import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: table
    color: "#fff"
    property bool follow: false
    function sync() {
        let i = flow.selectedRow();
        view.currentIndex = i;
        if (i >= 0)
            view.positionViewAtIndex(i, ListView.Contain);
    }
    onVisibleChanged: if (visible)
        Qt.callLater(sync)
    Connections {
        target: flow
        function onInspectedPacket() {
            if (table.visible)
                table.sync();
        }
    }
    component Cell: Text {
        font.pixelSize: 11
        color: "#334a66"
        elide: Text.ElideMiddle
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Layout.preferredWidth: 90
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 51
            Layout.leftMargin: 18
            Layout.rightMargin: 12
            Text {
                text: "Packet table"
                font.pixelSize: 18
                font.weight: Font.DemiBold
                color: "#243a59"
            }
            Text {
                text: flow.totals.shown + " matching"
                font.pixelSize: 12
                color: "#657b94"
                Layout.fillWidth: true
            }
            CheckBox {
                visible: flow.live
                text: "Follow latest"
                checked: table.follow
                onToggled: table.follow = checked
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 31
            color: "#edf2f8"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 10
                spacing: 9
                Repeater {
                    model: [["ID", 38], ["Time (s)", 50], ["Source", 95], ["Destination", 95], ["Type", 49], ["Bytes", 41], ["Summary", 112]]
                    Cell {
                        required property var modelData
                        text: modelData[0]
                        Layout.preferredWidth: modelData[1]
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
        ListView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: flow
            reuseItems: true
            keyNavigationEnabled: false
            onCountChanged: if (table.follow && flow.live)
                Qt.callLater(positionViewAtEnd)
            Keys.onUpPressed: {
                if (currentIndex > 0)
                    flow.selectRow(currentIndex - 1);
            }
            Keys.onDownPressed: {
                if (currentIndex < count - 1)
                    flow.selectRow(currentIndex + 1);
            }
            delegate: ItemDelegate {
                id: row
                required property int index
                required property string pid
                required property string elapsed
                required property string src
                required property string dst
                required property string protocol
                required property string sizeLabel
                required property string summary
                required property string tint
                width: ListView.view.width
                height: 33
                padding: 0
                background: Rectangle {
                    color: row.pid === flow.details.id ? "#e5effe" : row.hovered ? "#f0f5fa" : row.index % 2 ? "#f9fbfd" : "#fff"
                }
                contentItem: RowLayout {
                    spacing: 9
                    Cell {
                        Layout.leftMargin: 14
                        text: row.pid
                        Layout.preferredWidth: 38
                    }
                    Cell {
                        text: row.elapsed
                        Layout.preferredWidth: 50
                    }
                    Cell {
                        text: row.src
                        Layout.preferredWidth: 95
                    }
                    Cell {
                        text: row.dst
                        Layout.preferredWidth: 95
                    }
                    Cell {
                        text: row.protocol
                        color: Qt.darker(row.tint, 1.6)
                        font.weight: Font.Bold
                        Layout.preferredWidth: 49
                    }
                    Cell {
                        text: row.sizeLabel
                        Layout.preferredWidth: 41
                    }
                    Cell {
                        text: row.summary
                        Layout.rightMargin: 10
                        Layout.preferredWidth: 112
                        elide: Text.ElideRight
                    }
                }
                onClicked: {
                    table.follow = false;
                    flow.selectRow(index);
                    view.forceActiveFocus();
                }
                Accessible.name: "Packet " + pid + ", " + protocol + ", " + src + " to " + dst
            }
            ScrollBar.vertical: ScrollBar {}
            Text {
                visible: view.count === 0
                anchors.centerIn: parent
                text: flow.busy ? "Loading packets…" : "No matching packets"
                color: "#647b95"
            }
        }
    }
}
