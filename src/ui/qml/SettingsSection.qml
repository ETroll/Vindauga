import QtQuick
import QtQuick.Layouts

// Card with a title and a stack of SettingsRow children.
Rectangle {
    id: section

    property string title
    property string subtitle
    property alias badge: badgeText.text
    default property alias rows: rowsColumn.data

    Layout.fillWidth: true
    implicitHeight: inner.implicitHeight
    radius: Theme.radiusCard
    color: Theme.surface
    border.color: Theme.border
    border.width: 1
    clip: true

    ColumnLayout {
        id: inner
        width: parent.width
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 16
            Layout.bottomMargin: 12
            ColumnLayout {
                spacing: 2
                Text {
                    text: section.title
                    color: Theme.text
                    font.family: Theme.sans
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    visible: section.subtitle.length > 0
                    text: section.subtitle
                    color: Theme.muted
                    font.family: Theme.sans
                    font.pixelSize: 12
                }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                visible: badgeText.text.length > 0
                implicitWidth: badgeText.implicitWidth + 16
                implicitHeight: 20
                radius: 10
                color: Theme.warnTint
                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    color: Theme.warn
                    font.family: Theme.sans
                    font.pixelSize: 12
                }
            }
        }

        ColumnLayout {
            id: rowsColumn
            Layout.fillWidth: true
            spacing: 0
        }
    }
}
