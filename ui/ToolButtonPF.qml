import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AbstractButton {
    id: b
    property string symbol: ""
    property bool dark: false
    property bool chosen: false
    property bool boxed: false
    property string hint: ""
    property color normalInk: dark ? "#e3edfc" : "#3b506e"
    implicitWidth: contentItem.implicitWidth + (text ? 20 : 14)
    implicitHeight: 33
    padding: 0
    hoverEnabled: true
    Accessible.name: text || hint
    ToolTip.visible: hovered && hint.length > 0
    ToolTip.text: hint
    ToolTip.delay: 450
    background: Rectangle {
        radius: 6
        color: b.chosen ? (b.dark ? "#edf3fc" : "#e7eef9") : b.down || b.hovered ? (b.dark ? "#344a69" : "#edf2f9") : b.boxed ? (b.dark ? "#2b405f" : "#ffffff") : "transparent"
        border.width: b.visualFocus || b.boxed ? 1 : 0
        border.color: b.visualFocus ? "#8965d9" : b.dark ? "#4b607d" : "#d4dfec"
    }
    contentItem: RowLayout {
        spacing: b.text && b.symbol ? 7 : 0
        Item {
            Layout.fillWidth: true
        }
        Glyph {
            visible: !!b.symbol
            symbol: b.symbol
            ink: b.chosen && b.dark ? "#203552" : b.normalInk
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
        }
        Text {
            visible: !!b.text
            text: b.text
            color: b.chosen && b.dark ? "#203552" : b.normalInk
            font.pixelSize: 13
            font.weight: Font.DemiBold
            textFormat: Text.PlainText
        }
        Item {
            Layout.fillWidth: true
        }
    }
    opacity: enabled ? 1 : 0.43
}
