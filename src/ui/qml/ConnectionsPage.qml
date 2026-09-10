import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Dialogs

// List of saved connections. Connections are imported from .rdp files via the
// file dialog and stored locally; there is no login or feed step.
Page {
    id: page
    title: qsTr("Vindauga — Connections")

    // Emitted when a row is clicked. Navigation itself is driven by AppController's
    // openSessionRequested (not this signal), since AppController looks up the rdpText.
    signal connectionOpened(string id)
    signal settingsRequested()

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Row {
            spacing: 8

            Button {
                text: qsTr("Import .rdp...")
                onClicked: fileDialog.open()
            }
            Button {
                text: qsTr("⚙ Settings")
                onClicked: page.settingsRequested()
            }
        }

        Label {
            visible: listView.count === 0
            width: parent.width
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTr("No saved connections yet. Import an .rdp file to get started.")
        }

        ListView {
            id: listView
            width: parent.width
            height: parent.height - 60
            clip: true
            model: app.connections
            delegate: ItemDelegate {
                width: listView.width
                onClicked: page.connectionOpened(model.id)

                contentItem: Row {
                    spacing: 8
                    Label {
                        width: listView.width - 60
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        text: model.displayName
                        elide: Text.ElideRight
                    }
                    ToolButton {
                        text: "✕"
                        onClicked: app.removeConnection(model.id)
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Select an .rdp file")
        nameFilters: [qsTr("RDP files (*.rdp)"), qsTr("All files (*)")]
        onAccepted: {
            const newId = app.importConnection(selectedFile)
            if (newId.length === 0)
                importFailedDialog.open()
        }
    }

    MessageDialog {
        id: importFailedDialog
        title: qsTr("Import failed")
        text: qsTr("Could not import the .rdp file. Check that it is valid and readable.")
    }
}
