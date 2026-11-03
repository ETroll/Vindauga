import QtQuick
import QtQuick.Controls.FluentWinUI3

// 44x26 toggle switch from the design.
Switch {
    id: control

    implicitWidth: 44
    implicitHeight: 26
    padding: 0
    text: ""

    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 26
        radius: 13
        color: control.checked ? Theme.accent : Theme.switchOff
        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            y: 3
            x: control.checked ? 21 : 3
            width: 20
            height: 20
            radius: 10
            color: "#FFFFFF"
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {}
    background: null

    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
