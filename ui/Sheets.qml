import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import PacketFlow 1.0

Item {
    id: sheets
    objectName: "sheets"
    signal resetMapLayout
    readonly property bool dialogOpen: filters.visible || peers.visible || locations.visible || geoFile.visible || liveDialog.visible || statsDialog.visible || openFile.visible || saveFile.visible
    property string protocolValue: "All"
    property var applied: ({
            protocol: "All",
            peer: "",
            direction: 0,
            lo: "",
            hi: "",
            expression: ""
        })
    function restore() {
        protocolValue = applied.protocol;
        peerInput.text = applied.peer;
        dir.currentIndex = applied.direction;
        lo.text = applied.lo;
        hi.text = applied.hi;
        expr.text = applied.expression;
    }
    function showFilter() {
        restore();
        errorText.text = "";
        filters.open();
    }
    function filterPeer(addr) {
        restore();
        peerInput.text = addr;
        apply();
    }
    function openCapture() {
        openFile.open();
    }
    function exportCsv() {
        saveFile.open();
    }
    function interfaces() {
        ifaceList.model = flow.listInterfaces();
        ifaceList.currentIndex = -1;
        liveDialog.open();
    }
    function endpoints() {
        search.text = "";
        peers.open();
    }
    function stats() {
        statsDialog.open();
    }
    function showLocations() {
        peers.close();
        locations.open();
    }
    function clearForm() {
        protocolValue = "All";
        peerInput.text = "";
        dir.currentIndex = 0;
        lo.text = "";
        hi.text = "";
        expr.text = "";
    }
    function apply() {
        let error = flow.applyFilter(protocolValue, peerInput.text, dir.currentText, lo.text, hi.text, expr.text);
        errorText.text = error;
        if (error)
            filters.open();
        else {
            applied = {
                protocol: protocolValue,
                peer: peerInput.text,
                direction: dir.currentIndex,
                lo: lo.text,
                hi: hi.text,
                expression: expr.text
            };
            filters.close();
        }
    }
    Connections {
        target: flow
        function onNewSession() {
            sheets.applied = {
                protocol: "All",
                peer: "",
                direction: 0,
                lo: "",
                hi: "",
                expression: ""
            };
            sheets.restore();
        }
    }
    FileDialog {
        id: openFile
        title: "Open capture"
        nameFilters: ["Packet captures (*.pcap *.pcapng *.cap)", "All files (*)"]
        onAccepted: flow.openFile(selectedFile)
    }
    FileDialog {
        id: geoFile
        title: "Choose an IP location database"
        nameFilters: ["City or Country databases (*.mmdb)"]
        onAccepted: flow.geo.chooseDatabase(selectedFile)
    }
    FileDialog {
        id: saveFile
        title: "Export filtered packets"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: ["CSV (*.csv)"]
        onAccepted: flow.exportCsv(selectedFile)
    }
    component Sheet: Dialog {
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 22
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 12
            color: "#fff"
            border.color: "#c9d6e6"
        }
        header: Text {
            text: parent.title
            font.pixelSize: 20
            font.weight: Font.DemiBold
            color: "#193153"
            leftPadding: 22
            topPadding: 19
            bottomPadding: 4
        }
    }
    component Note: Text {
        font.pixelSize: 12
        color: "#526a86"
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        textFormat: Text.PlainText
    }
    component LocationToggle: CheckBox {
        text: "Show IP locations"
        checked: flow.geo.enabled
        nextCheckState: function () {
            let enable = !flow.geo.enabled;
            flow.geo.enabled = enable;
            if (enable && !flow.geo.enabled && !locations.visible)
                sheets.showLocations();
            return flow.geo.enabled ? Qt.Checked : Qt.Unchecked;
        }
    }
    Sheet {
        id: locations
        objectName: "ipLocations"
        title: "IP locations"
        width: Math.min(440, parent.width - 32)
        contentItem: ColumnLayout {
            spacing: 10
            LocationToggle {}
            Note {
                text: "Show an approximate city, country and flag for public IP addresses. Lookups use a database on this Mac. Private addresses have no geographic location in the database."
            }
            Label {
                text: flow.geo.databaseName || "No database selected"
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Note {
                visible: !!flow.geo.error
                text: flow.geo.error
                color: "#8b491a"
            }
            ToolButtonPF {
                text: "Choose database…"
                boxed: true
                onClicked: geoFile.open()
            }
            Note {
                text: "Choose an uncompressed City or Country .mmdb file to enable lookups."
            }
            Note {
                textFormat: Text.StyledText
                text: '<a href="https://db-ip.com/db/download/ip-to-city-lite">Download DB-IP City Lite</a>'
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
            Note {
                visible: flow.geo.usesDbIp
                textFormat: Text.StyledText
                text: '<a href="https://db-ip.com/">IP Geolocation by DB-IP</a>'
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
            ToolButtonPF {
                Layout.alignment: Qt.AlignRight
                text: "Done"
                boxed: true
                onClicked: locations.close()
            }
        }
    }
    Sheet {
        id: filters
        objectName: "filters"
        title: "Filter packets"
        width: 440
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Note {
                text: "Display filters apply to the map, packet table, and CSV export."
            }
            Label {
                text: "Protocol"
            }
            GridLayout {
                columns: 4
                rowSpacing: 4
                columnSpacing: 5
                Repeater {
                    model: ["All", "TCP", "UDP", "DNS", "ICMP", "ARP", "Unknown"]
                    ToolButtonPF {
                        required property string modelData
                        text: modelData
                        boxed: true
                        chosen: sheets.protocolValue === text
                        normalInk: text === "All" ? "#3b506e" : Qt.darker(flow.protocolColor(text), 1.65)
                        implicitHeight: 28
                        onClicked: sheets.protocolValue = text
                    }
                }
            }
            Label {
                text: "Peer address"
            }
            TextField {
                id: peerInput
                placeholderText: "Any endpoint"
                Layout.fillWidth: true
                selectByMouse: true
            }
            Label {
                text: "Direction relative to local or focused endpoint"
            }
            ComboBox {
                id: dir
                model: ["Any", "Outgoing", "Incoming", "Other"]
                Layout.fillWidth: true
            }
            Label {
                text: "Packet size (bytes)"
            }
            RowLayout {
                TextField {
                    id: lo
                    placeholderText: "Minimum"
                    Layout.fillWidth: true
                }
                TextField {
                    id: hi
                    placeholderText: "Maximum"
                    Layout.fillWidth: true
                }
            }
            Label {
                text: "Expression"
            }
            TextField {
                id: expr
                placeholderText: "protocol = dns"
                Layout.fillWidth: true
                onAccepted: sheets.apply()
            }
            Note {
                text: "Use protocol = dns, port = 53, length >= 512, or length <= 1500. Combine one expression with the controls above."
            }
            Note {
                id: errorText
                color: "#aa402d"
                visible: !!text
            }
            RowLayout {
                Layout.fillWidth: true
                ToolButtonPF {
                    text: "Clear"
                    onClicked: {
                        sheets.clearForm();
                        sheets.apply();
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                ToolButtonPF {
                    text: "Cancel"
                    onClicked: filters.close()
                }
                ToolButtonPF {
                    text: "Apply filter"
                    chosen: true
                    onClicked: sheets.apply()
                }
            }
        }
    }
    Sheet {
        id: peers
        objectName: "endpoints"
        title: "Endpoints"
        width: Math.min(510, parent.width - 32)
        height: Math.min(630, parent.height - 24)
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Maximum peers on map"
                    Layout.fillWidth: true
                }
                SpinBox {
                    id: limitBox
                    objectName: "peerLimit"
                    from: 4
                    to: 100
                    editable: true
                    value: flow.peerLimit
                    Accessible.name: "Maximum peers on map"
                    onValueModified: flow.peerLimit = value
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Hide idle peers after"
                }
                Slider {
                    objectName: "idleTimeoutSlider"
                    Layout.fillWidth: true
                    from: 0
                    to: 120
                    stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: flow.idleSeconds
                    Accessible.name: "Hide idle peers after"
                    Accessible.description: "Seconds since the last sent or received packet. Zero turns the timer off."
                    onMoved: flow.idleSeconds = Math.round(value)
                }
                Label {
                    text: flow.idleSeconds ? flow.idleSeconds + " s" : "Off"
                    Layout.minimumWidth: 40
                    horizontalAlignment: Text.AlignRight
                }
            }
            Label {
                text: "Show endpoint groups"
            }
            RowLayout {
                spacing: 4
                Repeater {
                    model: [
                        {
                            name: "All",
                            mask: PacketSession.AllGroups
                        },
                        {
                            name: "Web servers",
                            mask: PacketSession.WebServers
                        },
                        {
                            name: "DNS servers",
                            mask: PacketSession.DnsServers
                        },
                        {
                            name: "Other",
                            mask: PacketSession.OtherPeers
                        }
                    ]
                    ToolButtonPF {
                        required property var modelData
                        text: modelData.name
                        objectName: "group" + modelData.mask
                        boxed: true
                        chosen: flow.mapGroups === PacketSession.AllGroups ? modelData.mask === PacketSession.AllGroups : modelData.mask !== PacketSession.AllGroups && !!(flow.mapGroups & modelData.mask)
                        onClicked: {
                            if (modelData.mask === PacketSession.AllGroups || flow.mapGroups === PacketSession.AllGroups)
                                flow.mapGroups = modelData.mask;
                            else
                                flow.mapGroups = flow.mapGroups ^ modelData.mask;
                        }
                    }
                }
            }
            Note {
                text: "Web server roles are inferred from TCP 80/443 or UDP 443. DNS servers use port 53 in decoded DNS packets."
            }
            RowLayout {
                Layout.fillWidth: true
                LocationToggle {
                    objectName: "endpointLocationsToggle"
                    Layout.fillWidth: true
                }
                ToolButtonPF {
                    text: "Database…"
                    onClicked: sheets.showLocations()
                }
            }
            TextField {
                id: search
                Layout.fillWidth: true
                placeholderText: "Search address or name"
            }
            ListView {
                id: endpointList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: {
                    let totals = flow.totals, groups = flow.mapGroups;
                    return peers.visible ? flow.endpoints(search.text) : [];
                }
                delegate: ItemDelegate {
                    id: ep
                    required property var modelData
                    width: ListView.view.width
                    contentItem: RowLayout {
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Note {
                                text: ep.modelData.label
                                font.bold: true
                                color: "#273e60"
                            }
                            Note {
                                text: ep.modelData.address !== ep.modelData.label ? ep.modelData.address + " · " + ep.modelData.roles : ep.modelData.roles
                                font.pixelSize: 11
                            }
                            Note {
                                visible: !!ep.modelData.location
                                text: ep.modelData.location
                            }
                        }
                        Note {
                            text: ep.modelData.count + " pkts"
                            Layout.fillWidth: false
                        }
                    }
                    onClicked: {
                        flow.selectPeer(modelData.address);
                        peers.close();
                    }
                }
                ScrollBar.vertical: ScrollBar {}
                Note {
                    visible: endpointList.count === 0
                    anchors.centerIn: parent
                    text: "No matching endpoints"
                }
            }
            Note {
                text: flow.totals.mapPeers + " peers on map · " + flow.totals.groupMatches + " matching addresses · up to 250 search results"
            }
            Note {
                text: "Drag a card to move it. Drag the background to pan."
            }
            RowLayout {
                Layout.fillWidth: true
                ToolButtonPF {
                    text: "Reset layout"
                    onClicked: sheets.resetMapLayout()
                }
                Item {
                    Layout.fillWidth: true
                }
                ToolButtonPF {
                    text: "Done"
                    boxed: true
                    onClicked: peers.close()
                }
            }
        }
    }
    Sheet {
        id: liveDialog
        title: "Start live capture"
        width: 480
        height: 450
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Note {
                text: "Choose an interface for passive capture. Promiscuous mode is off."
            }
            ListView {
                id: ifaceList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                delegate: ItemDelegate {
                    id: iface
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    contentItem: ColumnLayout {
                        Note {
                            text: iface.modelData.name + (iface.modelData.description ? "  ·  " + iface.modelData.description : "")
                            font.bold: true
                        }
                        Note {
                            text: iface.modelData.addresses || "No address reported"
                            font.pixelSize: 11
                        }
                    }
                    onClicked: ifaceList.currentIndex = index
                }
                ScrollBar.vertical: ScrollBar {}
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                ToolButtonPF {
                    text: "Cancel"
                    onClicked: liveDialog.close()
                }
                ToolButtonPF {
                    text: "Start capture"
                    boxed: true
                    enabled: ifaceList.currentIndex >= 0
                    onClicked: {
                        flow.startLive(ifaceList.model[ifaceList.currentIndex].name);
                        liveDialog.close();
                    }
                }
            }
        }
    }
    Sheet {
        id: statsDialog
        objectName: "stats"
        title: "Session statistics"
        width: 390
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Repeater {
                model: [["Delivered to application", "delivered"], ["Retained packets", "retained"], ["Matching display filter", "shown"], ["Backend buffer drops", "backend"], ["Discarded before ingestion", "discarded"], ["Evicted from retention", "evicted"], ["Animations skipped by map", "skipped"]]
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Note {
                        text: parent.modelData[0]
                    }
                    Label {
                        text: flow.totals[parent.modelData[1]]
                        font.bold: true
                    }
                }
            }
            Note {
                text: flow.totals.limited ? "File limit reached. Totals cover the loaded prefix." : "Counts cover the current session. Map limits affect animation only."
            }
            ToolButtonPF {
                Layout.alignment: Qt.AlignRight
                text: "Close"
                boxed: true
                onClicked: statsDialog.close()
            }
        }
    }
}
