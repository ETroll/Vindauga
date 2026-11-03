import QtQuick
import QtQuick.Layouts

// One setting: label + description on the left, the control (default child) on the right.
Item {
    id: row

    property string title
    property string description
    property int indent: 0
    // Put the control under the text instead of beside it (long text fields).
    property bool stacked: false
    default property alias control: controlHolder.data

    Layout.fillWidth: true
    implicitHeight: grid.implicitHeight + 28

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.divider
    }

    GridLayout {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: 20 + row.indent
        anchors.rightMargin: 20
        anchors.topMargin: 14
        anchors.bottomMargin: 14
        columns: row.stacked ? 1 : 2
        columnSpacing: 16
        rowSpacing: 10

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: row.title
                wrapMode: Text.WordWrap
                color: Theme.text
                font.family: Theme.sans
                font.pixelSize: 14
                font.weight: Font.Medium
            }
            Text {
                Layout.fillWidth: true
                visible: row.description.length > 0
                text: row.description
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.family: Theme.sans
                font.pixelSize: 12
                textFormat: Text.StyledText
                linkColor: Theme.link
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }
        RowLayout {
            id: controlHolder
            Layout.alignment: row.stacked ? Qt.AlignLeft : (Qt.AlignRight | Qt.AlignVCenter)
            Layout.fillWidth: row.stacked
            spacing: 10
        }
    }
}
