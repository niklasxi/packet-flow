import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    objectName: "inspector"
    color: "#ffffff"
    property var packet: flow.details
    property int tab: 0
    property string openLayer: ""
    property int byteStart: -1
    property int byteLength: 0
    signal collapse
    signal animatePacket
    signal filterPeer(string address)
    function showBytes(offset, len) {
        byteStart = offset;
        byteLength = len;
        tab = 2;
        Qt.callLater(function () {
            bytes.positionViewAtIndex(Math.floor(offset / 8), ListView.Contain);
        });
    }
    Connections {
        target: flow
        function onSelectionChanged() {
            panel.byteStart = -1;
            panel.byteLength = 0;
            panel.openLayer = "";
        }
    }
    component Rule: Rectangle {
        color: "#e1e8f0"
        Layout.fillWidth: true
        Layout.preferredHeight: 1
    }
    component Caption: Text {
        font.pixelSize: 12
        font.weight: Font.Medium
        color: "#5c708a"
        textFormat: Text.PlainText
    }
    component Value: Text {
        font.pixelSize: 14
        font.weight: Font.DemiBold
        color: "#203550"
        wrapMode: Text.WrapAnywhere
        textFormat: Text.PlainText
    }
    component Fact: RowLayout {
        property string icon
        property string title
        property string value
        Layout.fillWidth: true
        spacing: 9
        Glyph {
            symbol: parent.icon
            ink: "#7a8aa2"
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
        }
        Caption {
            text: parent.title
            Layout.fillWidth: true
        }
        Value {
            text: parent.value
            font.pixelSize: 13
        }
    }
    component End: ColumnLayout {
        id: endpoint
        property string label
        property string title
        property string addr
        property string port
        spacing: 4
        Layout.fillWidth: true
        Caption {
            text: endpoint.label
        }
        RowLayout {
            Layout.fillWidth: true
            Value {
                text: endpoint.title
                Layout.fillWidth: true
            }
            ToolButtonPF {
                symbol: "copy"
                hint: "Copy " + endpoint.label.toLowerCase() + " address"
                implicitWidth: 25
                implicitHeight: 24
                onClicked: flow.copy(endpoint.addr)
            }
        }
        Caption {
            text: endpoint.addr + (endpoint.port ? "  ·  port " + endpoint.port : "")
            font.family: "Menlo"
            font.pixelSize: 11
            Layout.fillWidth: true
            wrapMode: Text.WrapAnywhere
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 21
            Layout.rightMargin: 13
            Layout.preferredHeight: 62
            Text {
                text: flow.peer ? "Endpoint details" : "Packet details"
                font.pixelSize: 19
                font.weight: Font.DemiBold
                color: "#172e4e"
                Layout.fillWidth: true
            }
            ToolButtonPF {
                symbol: "panel"
                hint: "Hide inspector"
                onClicked: panel.collapse()
            }
        }
        Rule {}
        ColumnLayout {
            visible: !!panel.packet.id
            Layout.fillWidth: true
            Layout.margins: 20
            spacing: 9
            RowLayout {
                Layout.fillWidth: true
                Rectangle {
                    implicitWidth: badge.implicitWidth + 16
                    implicitHeight: 24
                    radius: 5
                    color: Qt.tint("#ffffff", Qt.alpha(panel.packet.color || "#8290a4", 0.16))
                    Text {
                        id: badge
                        anchors.centerIn: parent
                        text: panel.packet.protocol || ""
                        font.pixelSize: 12
                        font.bold: true
                        color: Qt.darker(panel.packet.color || "#8290a4", 1.65)
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Caption {
                    text: "Packet #" + String(panel.packet.id || "").padStart(4, "0")
                    font.family: "Menlo"
                    font.pixelSize: 11
                }
            }
            Value {
                text: panel.packet.summary || ""
                Layout.fillWidth: true
                font.pixelSize: 16
                Accessible.name: "What: " + text
            }
        }
        RowLayout {
            visible: !!panel.packet.id
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 3
            Layout.bottomMargin: 12
            Repeater {
                model: ["Overview", "Layers", "Bytes"]
                ToolButtonPF {
                    required property string modelData
                    required property int index
                    text: modelData
                    chosen: panel.tab === index
                    Layout.fillWidth: true
                    implicitHeight: 29
                    onClicked: panel.tab = index
                }
            }
        }
        Rule {
            visible: !!panel.packet.id
        }
        ScrollView {
            visible: (!!panel.packet.id && panel.tab !== 2) || !!flow.peer
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 0
                ColumnLayout {
                    visible: !!panel.packet.id && panel.tab === 0
                    Layout.fillWidth: true
                    Layout.margins: 20
                    spacing: 15
                    Fact {
                        icon: "layers"
                        title: "Size"
                        value: panel.packet.size || ""
                    }
                    Fact {
                        icon: "out"
                        title: "Direction"
                        value: panel.packet.direction || ""
                    }
                    Fact {
                        icon: "clock"
                        title: "When"
                        value: "+" + (panel.packet.elapsed || "0.000") + " s"
                    }
                    Caption {
                        text: panel.packet.when || ""
                        font.family: "Menlo"
                        font.pixelSize: 10
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                    Rule {}
                    End {
                        label: "From"
                        title: panel.packet.fromLabel || ""
                        addr: panel.packet.from || ""
                        port: panel.packet.fromPort || ""
                    }
                    End {
                        label: "To"
                        title: panel.packet.toLabel || ""
                        addr: panel.packet.to || ""
                        port: panel.packet.toPort || ""
                    }
                    Rule {
                        visible: panel.packet.original !== panel.packet.captured || panel.packet.status !== "Decoded"
                    }
                    Caption {
                        visible: panel.packet.original !== panel.packet.captured
                        text: "Original  " + (panel.packet.original === "Unavailable" ? "unavailable" : (panel.packet.original || "—") + " B") + "     Captured  " + (panel.packet.captured || "—") + " B"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Caption {
                        text: panel.packet.status || ""
                        visible: text !== "Decoded"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
                ColumnLayout {
                    visible: !!panel.packet.id && panel.tab === 1
                    Layout.fillWidth: true
                    Layout.margins: 14
                    spacing: 4
                    Repeater {
                        model: panel.packet.layers || []
                        ColumnLayout {
                            id: layer
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 2
                            ToolButtonPF {
                                Layout.fillWidth: true
                                text: layer.modelData.name
                                boxed: true
                                symbol: panel.openLayer === text ? "down" : "right"
                                onClicked: panel.openLayer = panel.openLayer === text ? "" : text
                            }
                            Repeater {
                                model: panel.openLayer === layer.modelData.name ? layer.modelData.fields : []
                                ItemDelegate {
                                    id: field
                                    required property var modelData
                                    Layout.fillWidth: true
                                    contentItem: RowLayout {
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 3
                                            Caption {
                                                text: field.modelData.name
                                            }
                                            Value {
                                                text: field.modelData.value
                                                Layout.fillWidth: true
                                                font.pixelSize: 12
                                            }
                                        }
                                        ToolButtonPF {
                                            symbol: "copy"
                                            hint: "Copy field value"
                                            implicitWidth: 25
                                            onClicked: flow.copy(field.modelData.value)
                                        }
                                    }
                                    onClicked: panel.showBytes(modelData.offset, modelData.length)
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Show bytes " + modelData.offset + "–" + (modelData.offset + modelData.length - 1)
                                }
                            }
                        }
                    }
                    Caption {
                        text: "Select a field to inspect its bytes."
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        Layout.topMargin: 9
                    }
                }
                ColumnLayout {
                    visible: !!flow.peer
                    Layout.fillWidth: true
                    Layout.margins: 20
                    spacing: 14
                    Caption {
                        text: "Endpoint address"
                    }
                    Value {
                        text: flow.peer
                        Layout.fillWidth: true
                    }
                    Caption {
                        visible: flow.geo.enabled
                        text: "Approximate IP location"
                    }
                    Value {
                        visible: flow.geo.enabled
                        text: flow.peerLocation
                        Layout.fillWidth: true
                    }
                    ToolButtonPF {
                        text: "Filter to this peer"
                        symbol: "filter"
                        boxed: true
                        onClicked: panel.filterPeer(flow.peer)
                    }
                    ToolButtonPF {
                        text: "Focus endpoint"
                        symbol: "fit"
                        boxed: true
                        onClicked: flow.focusPeer(flow.peer)
                    }
                    ToolButtonPF {
                        text: "Copy address"
                        symbol: "copy"
                        onClicked: flow.copy(flow.peer)
                    }
                }
            }
        }
        ListView {
            id: bytes
            visible: !!panel.packet.id && panel.tab === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            model: visible ? Math.ceil((panel.packet.byteCount || 0) / 8) : 0
            header: Caption {
                leftPadding: 12
                topPadding: 14
                bottomPadding: 10
                text: "Offset       Hex                         ASCII"
                font.family: "Menlo"
                font.pixelSize: 10
            }
            delegate: Row {
                id: line
                required property int index
                property var parts: {
                    let id = panel.packet.id;
                    return flow.byteLine(index);
                }
                leftPadding: 12
                spacing: 4
                height: 19
                Text {
                    width: 34
                    text: line.parts.offset || ""
                    font.family: "Menlo"
                    font.pixelSize: 10
                    color: "#76869a"
                }
                Row {
                    spacing: 1
                    Repeater {
                        model: line.parts.hex || []
                        Rectangle {
                            required property int index
                            required property string modelData
                            property int offset: line.index * 8 + index
                            color: offset >= panel.byteStart && offset < panel.byteStart + panel.byteLength ? "#dbcdf8" : "transparent"
                            width: 16
                            height: 16
                            radius: 2
                            Text {
                                anchors.centerIn: parent
                                text: parent.modelData
                                font.family: "Menlo"
                                font.pixelSize: 10
                                color: "#304863"
                            }
                        }
                    }
                }
                Text {
                    text: line.parts.ascii || ""
                    font.family: "Menlo"
                    font.pixelSize: 10
                    color: "#5d7189"
                    textFormat: Text.PlainText
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
        ColumnLayout {
            visible: !panel.packet.id && !flow.peer
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20
            Caption {
                text: "Select a packet or endpoint to inspect it."
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Item {
                Layout.fillHeight: true
            }
        }
        Rule {
            visible: !!panel.packet.id
        }
        ToolButtonPF {
            visible: !!panel.packet.id
            enabled: {
                let id = panel.packet.id, filter = flow.filterLabel;
                return !!id && !flow.reduced && flow.selectedRow() >= 0;
            }
            Layout.margins: 15
            Layout.alignment: Qt.AlignHCenter
            text: "Animate this packet"
            symbol: "play"
            boxed: true
            onClicked: panel.animatePacket()
        }
    }
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: "#d5e0ed"
    }
}
