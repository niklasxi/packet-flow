import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PacketFlow 1.0

ApplicationWindow {
    id: win
    width: 1180
    height: 752
    minimumWidth: 840
    minimumHeight: 560
    visible: true
    title: "Packet Flow"
    color: "#f6f9fd"
    palette.window: "#f6f9fd"
    palette.windowText: "#213957"
    palette.text: "#213957"
    palette.base: "#ffffff"
    palette.button: "#edf2f8"
    palette.buttonText: "#213957"
    palette.highlight: "#c6d9f3"
    palette.highlightedText: "#142e50"
    palette.placeholderText: "#71839a"
    property bool showMap: true
    property bool inspectorOpen: true
    property color navy: "#192e4c"
    Sheets {
        id: sheets
        onResetMapLayout: map.resetLayout()
    }
    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: sheets.openCapture()
    }
    Shortcut {
        sequence: "Space"
        enabled: !sheets.dialogOpen && !sourceMenu.visible && !options.visible
        onActivated: flow.toggle()
    }
    Shortcut {
        sequence: "Ctrl+1"
        onActivated: win.showMap = true
    }
    Shortcut {
        sequence: "Ctrl+2"
        onActivated: win.showMap = false
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: sheets.showFilter()
    }
    Shortcut {
        sequence: "Right"
        enabled: !sheets.dialogOpen
        onActivated: flow.selectRow(Math.max(0, flow.selectedRow() + 1))
    }
    Shortcut {
        sequence: "Left"
        enabled: !sheets.dialogOpen
        onActivated: flow.selectRow(Math.max(0, flow.selectedRow() - 1))
    }
    Connections {
        target: flow
        function onInspectedPacket() {
            win.inspectorOpen = true;
        }
    }
    component Divider: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 22
        color: "#425774"
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 59
            color: win.navy
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 14
                spacing: 9
                Rectangle {
                    implicitWidth: 31
                    implicitHeight: 31
                    radius: 8
                    color: "#e2ebff"
                    Glyph {
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        symbol: "logo"
                        ink: "#2e568e"
                    }
                }
                Text {
                    text: "Packet Flow"
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    color: "#fff"
                    Layout.rightMargin: 10
                }
                Divider {}
                ToolButtonPF {
                    id: source
                    text: flow.source
                    symbol: "down"
                    dark: true
                    hint: "Choose capture source"
                    onClicked: sourceMenu.popup()
                    Menu {
                        id: sourceMenu
                        MenuItem {
                            text: "Try demo"
                            onTriggered: flow.tryDemo()
                        }
                        MenuItem {
                            text: "Open capture…"
                            onTriggered: sheets.openCapture()
                        }
                        MenuItem {
                            text: "Live interfaces…"
                            onTriggered: sheets.interfaces()
                        }
                    }
                }
                Divider {}
                ToolButtonPF {
                    dark: true
                    text: flow.live ? (flow.playing ? "Pause view" : "Go live") : (flow.finished ? "Replay" : flow.playing ? "Pause view" : "Play view")
                    symbol: flow.playing ? "pause" : "play"
                    onClicked: flow.toggle()
                    enabled: !flow.busy && (flow.live ? flow.running : flow.duration > 0)
                }
                ToolButtonPF {
                    dark: true
                    text: win.width < 990 ? "Filter" : "Filter packets"
                    symbol: "filter"
                    chosen: !!flow.filterLabel
                    onClicked: sheets.showFilter()
                }
                ToolButtonPF {
                    visible: flow.running
                    dark: true
                    text: "Stop"
                    symbol: "stop"
                    onClicked: flow.stopSource()
                }
                Item {
                    Layout.fillWidth: true
                }
                Rectangle {
                    color: "#2a4060"
                    radius: 7
                    implicitWidth: segments.implicitWidth + 6
                    implicitHeight: 35
                    RowLayout {
                        id: segments
                        anchors.centerIn: parent
                        spacing: 2
                        ToolButtonPF {
                            text: "Map"
                            symbol: "map"
                            dark: true
                            chosen: win.showMap
                            implicitHeight: 29
                            onClicked: win.showMap = true
                        }
                        ToolButtonPF {
                            text: "Packets"
                            symbol: "table"
                            dark: true
                            chosen: !win.showMap
                            implicitHeight: 29
                            onClicked: win.showMap = false
                        }
                    }
                }
                ToolButtonPF {
                    dark: true
                    symbol: "dots"
                    hint: "Session options"
                    onClicked: options.popup()
                    Menu {
                        id: options
                        MenuItem {
                            text: "Export filtered CSV…"
                            enabled: flow.totals.shown !== "0"
                            onTriggered: sheets.exportCsv()
                        }
                        MenuItem {
                            text: "Session statistics"
                            onTriggered: sheets.stats()
                        }
                        MenuItem {
                            text: "IP locations…"
                            onTriggered: sheets.showLocations()
                        }
                        MenuItem {
                            text: "Reduce motion"
                            checkable: true
                            checked: flow.reduced
                            onTriggered: flow.reduced = checked
                        }
                        MenuItem {
                            text: win.inspectorOpen ? "Hide inspector" : "Show inspector"
                            onTriggered: win.inspectorOpen = !win.inspectorOpen
                        }
                    }
                }
            }
        }
        Rectangle {
            visible: !!flow.message || flow.busy
            Layout.fillWidth: true
            implicitHeight: Math.max(39, notice.implicitHeight + 12)
            color: "#fff5dc"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 14
                Text {
                    id: notice
                    text: flow.message || "Loading capture · " + Math.round(flow.progress * 100) + "%"
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    font.pixelSize: 12
                    color: "#705323"
                }
                ToolButtonPF {
                    visible: flow.busy
                    text: "Cancel import"
                    onClicked: flow.stopSource()
                }
                ToolButtonPF {
                    visible: !!flow.message
                    symbol: "close"
                    hint: "Dismiss message"
                    onClicked: flow.dismiss()
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Item {
                    visible: win.showMap
                    anchors.fill: parent
                    PacketMap {
                        id: map
                        anchors.fill: parent
                        session: flow
                        legendArea: Qt.rect(legend.x, legend.y + legend.height * (1 - legend.scale),
                                            legend.width * legend.scale, legend.height * legend.scale)
                        clip: true
                        Accessible.role: Accessible.Graphic
                        Accessible.name: "Packet map. Use Endpoints or the packet table to inspect activity."
                    }
                    RowLayout {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 16
                        spacing: 9
                        Text {
                            text: "Packet map"
                            font.pixelSize: 19
                            font.weight: Font.DemiBold
                            color: "#1f3657"
                        }
                        Rectangle {
                            implicitWidth: 1
                            implicitHeight: 16
                            color: "#cdd9e6"
                        }
                        ToolButtonPF {
                            text: "Endpoints"
                            symbol: "nodes"
                            hint: "Choose endpoint groups and the map peer limit"
                            onClicked: sheets.endpoints()
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: flow.intervalSkipped > 0
                            text: flow.intervalSkipped + " more packets this interval"
                            color: "#5f6e84"
                            font.pixelSize: 11
                        }
                        ToolButtonPF {
                            visible: !win.inspectorOpen
                            symbol: "panel"
                            hint: "Show inspector"
                            boxed: true
                            onClicked: win.inspectorOpen = true
                        }
                    }
                    Text {
                        anchors.top: parent.top
                        anchors.topMargin: 57
                        anchors.left: parent.left
                        anchors.leftMargin: 18
                        visible: flow.mapGroups !== PacketSession.AllGroups || !!flow.filterLabel || (flow.source === "File" && !flow.focus) || flow.totals.hidden > 0
                        text: [flow.mapGroups !== PacketSession.AllGroups ? flow.mapGroupLabel : "", flow.filterLabel ? "Filter · " + flow.filterLabel : "", flow.source === "File" && !flow.focus ? "Choose a focus in Endpoints" : "", flow.totals.hidden > 0 ? flow.totals.hidden + " addresses off map" : ""].filter(Boolean).join("   ·   ")
                        color: "#4c6380"
                        font.pixelSize: 11
                        textFormat: Text.PlainText
                    }
                    Text {
                        visible: flow.live && flow.totals.retained === "0"
                        anchors.centerIn: parent
                        text: flow.running ? "Waiting for packets…" : "No captured packets"
                        color: "#657b94"
                    }
                    Legend {
                        id: legend
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 16
                        scale: win.height < 625 ? 0.84 : 1
                        transformOrigin: Item.BottomLeft
                    }
                    RowLayout {
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        anchors.margins: 15
                        spacing: 3
                        ToolButtonPF {
                            symbol: "minus"
                            boxed: true
                            hint: "Zoom out"
                            onClicked: map.scaleBy(1 / 1.2)
                        }
                        Text {
                            text: map.zoomPercent + "%"
                            font.pixelSize: 10
                            font.family: "Menlo"
                            color: "#596d87"
                            Layout.minimumWidth: 34
                            horizontalAlignment: Text.AlignHCenter
                        }
                        ToolButtonPF {
                            symbol: "plus"
                            boxed: true
                            hint: "Zoom in"
                            onClicked: map.scaleBy(1.2)
                        }
                        ToolButtonPF {
                            symbol: "fit"
                            boxed: true
                            hint: "Fit view"
                            onClicked: map.fit()
                        }
                    }
                }
                Packets {
                    anchors.fill: parent
                    visible: !win.showMap
                }
            }
            Inspector {
                Layout.preferredWidth: win.width < 990 ? 253 : 285
                Layout.fillHeight: true
                visible: win.inspectorOpen
                onCollapse: win.inspectorOpen = false
                onAnimatePacket: {
                    win.showMap = true;
                    flow.animateSelected();
                }
                onFilterPeer: address => sheets.filterPeer(address)
            }
        }
        Timeline {
            Layout.fillWidth: true
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 30
            color: "#edf2f8"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 13
                spacing: 8
                Rectangle {
                    width: 7
                    height: 7
                    radius: 4
                    color: flow.playing ? "#42aa86" : "#92a1b5"
                }
                Text {
                    text: flow.status
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    color: "#39506e"
                }
                Text {
                    text: "·  " + flow.name
                    font.pixelSize: 11
                    color: "#61758e"
                    elide: Text.ElideRight
                    Layout.maximumWidth: 220
                    textFormat: Text.PlainText
                }
                Item {
                    Layout.fillWidth: true
                }
                Text {
                    text: flow.totals.shown + " shown  /  " + flow.totals.retained + " retained" + (flow.totals.limited ? " · prefix" : "")
                    font.pixelSize: 11
                    color: "#526982"
                }
                ToolButtonPF {
                    symbol: "info"
                    hint: "Session statistics"
                    implicitHeight: 24
                    onClicked: sheets.stats()
                }
            }
        }
    }
}
