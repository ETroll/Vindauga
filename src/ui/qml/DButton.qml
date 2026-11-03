import QtQuick
import QtQuick.Controls.FluentWinUI3

// Button in the design's three kinds: primary (accent fill), secondary (outlined),
// ghost (text only). `compact` is the 32 px height used in banners and rows.
Button {
    id: control

    property string kind: "secondary"   // "primary" | "secondary" | "ghost"
    property bool compact: false
    // Icon-only button: set iconPath (24x24 SVG path, see SvgIcon).
    property string iconPath
    property int iconSize: 16
    property color iconColor: Theme.text

    implicitHeight: compact ? 32 : 36
    implicitWidth: iconPath.length > 0 ? implicitHeight
                                       : Math.max(implicitHeight, label.implicitWidth + (compact ? 24 : 28))
    padding: 0
    hoverEnabled: true
    opacity: enabled ? 1.0 : 0.5

    contentItem: Item {
      SvgIcon {
        anchors.centerIn: parent
        visible: control.iconPath.length > 0
        path: control.iconPath
        size: control.iconSize
        color: control.iconColor
      }
      Text {
        id: label
        anchors.fill: parent
        visible: control.iconPath.length === 0
        text: control.text
        font.family: Theme.sans
        font.pixelSize: 13
        font.weight: control.kind === "primary" ? Font.Medium : Font.Normal
        color: control.kind === "primary" ? Theme.accentText
             : control.kind === "ghost" ? Theme.textSoft : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
      }
    }

    background: Rectangle {
        radius: Theme.radiusControl
        color: control.kind === "primary"
                   ? (control.down ? Qt.darker(Theme.accent, 1.15)
                      : control.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
             : control.kind === "ghost"
                   ? (control.hovered ? Theme.hover : "transparent")
                   : (control.hovered ? Theme.hover : Theme.surface)
        border.width: control.kind === "secondary" ? 1 : 0
        border.color: Theme.border
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
