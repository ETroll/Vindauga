import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Dialogs
import QtQuick.Layouts

// Home page. Two sources of connections:
//  - the feed: after an Entra sign-in, every desktop and app the account is entitled to,
//    as a grid of cards (AppController.resources; cached locally, refreshed in the
//    background);
//  - imported .rdp files (AppController.connections), the original workflow, kept for
//    people who get the file from IT or have no feed access.
// Navigation into a session is driven by AppController's openSessionRequested, not by the
// signals below, since AppController looks up or downloads the rdpText.
Page {
    id: page
    title: qsTr("Vindauga")

    signal connectionOpened(string id)
    signal resourceOpened(string id)
    signal settingsRequested()

    Flickable {
        anchors.fill: parent
        contentHeight: content.implicitHeight + 32
        clip: true
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: content
            x: 16
            y: 16
            width: parent.width - 32
            spacing: 16

            // Header: account state on the left, actions on the right.
            RowLayout {
                width: parent.width
                spacing: 8

                Column {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: app.signedIn ? app.accountName
                                           : (app.authBusy ? qsTr("Signing in…") : qsTr("Not signed in"))
                        font.bold: true
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Label {
                        visible: app.signedIn
                        opacity: 0.7
                        font.pixelSize: 12
                        text: app.feedBusy ? qsTr("Refreshing the list…")
                            : (isNaN(app.feedRefreshedAt.getTime()) ? qsTr("List not fetched yet")
                               : qsTr("List updated %1").arg(Qt.formatDateTime(app.feedRefreshedAt, "ddd hh:mm")))
                    }
                }
                Button {
                    visible: app.feedAvailable && !app.signedIn
                    enabled: !app.authBusy
                    highlighted: true
                    text: qsTr("Sign in")
                    onClicked: app.signIn()
                }
                Button {
                    visible: app.signedIn
                    enabled: !app.feedBusy
                    text: qsTr("⟳ Refresh")
                    onClicked: app.refreshFeed()
                }
                Button {
                    visible: app.signedIn
                    text: qsTr("Sign out")
                    onClicked: app.signOut()
                }
                Button {
                    text: qsTr("Import .rdp…")
                    onClicked: fileDialog.open()
                }
                Button {
                    text: qsTr("⚙ Settings")
                    onClicked: page.settingsRequested()
                }
            }

            // Sign-in / feed problems (AADSTS explanations, unreachable feed, ...).
            Rectangle {
                visible: app.feedStatus.length > 0
                width: parent.width
                height: statusLabel.implicitHeight + 16
                radius: 6
                color: "#33ff9800"
                border.color: "#66ff9800"
                Label {
                    id: statusLabel
                    anchors.fill: parent
                    anchors.margins: 8
                    wrapMode: Text.WordWrap
                    text: app.feedStatus
                }
            }

            // Welcome card when nothing is known yet.
            Column {
                visible: app.feedAvailable && !app.signedIn && !app.authBusy && resourceGrid.count === 0
                width: parent.width
                spacing: 8
                Label {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Sign in with your work account to see the desktops and apps assigned to you. No .rdp file is needed; the connection is set up from Microsoft's feed, and later connections need no browser sign-in at all.")
                }
            }

            // Feed resources.
            Label {
                visible: resourceGrid.count > 0
                text: qsTr("Your desktops and apps")
                font.bold: true
                font.pixelSize: 15
            }
            Label {
                visible: app.signedIn && !app.feedBusy && resourceGrid.count === 0 && app.feedStatus.length === 0
                width: parent.width
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("No desktops or apps are assigned to this account. Ask your IT department, or import an .rdp file below.")
            }
            GridView {
                id: resourceGrid
                visible: count > 0
                width: parent.width
                height: Math.ceil(count / Math.max(1, Math.floor(width / cellWidth))) * cellHeight
                interactive: false
                cellWidth: 220
                cellHeight: 92
                model: app.resources
                delegate: Item {
                    width: resourceGrid.cellWidth
                    height: resourceGrid.cellHeight

                    Rectangle {
                        id: card
                        anchors.fill: parent
                        anchors.margins: 6
                        radius: 8
                        // Explicit translucent colours rather than the palette: the
                        // FluentWinUI3 palette's alternateBase is white in some colour
                        // schemes, which made the white card text unreadable.
                        color: cardArea.containsMouse ? "#33ffffff" : "#1affffff"
                        border.color: "#40ffffff"
                        border.width: 1

                        Row {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            Item {
                                width: 32
                                height: 32
                                anchors.verticalCenter: parent.verticalCenter
                                Image {
                                    anchors.fill: parent
                                    source: model.iconSource
                                    visible: status === Image.Ready
                                    smooth: true
                                }
                                // Placeholder until the icon has been fetched.
                                Rectangle {
                                    anchors.fill: parent
                                    visible: model.iconSource.toString().length === 0
                                    radius: 6
                                    color: "#33ffffff"
                                    Label {
                                        anchors.centerIn: parent
                                        text: model.type === "RemoteApp" ? "▣" : "🖥"
                                    }
                                }
                            }
                            Column {
                                width: parent.width - 44
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Label {
                                    width: parent.width
                                    text: model.title
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Label {
                                    width: parent.width
                                    text: model.workspaceName.length > 0 ? model.workspaceName : model.type
                                    opacity: 0.7
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        MouseArea {
                            id: cardArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.resourceOpened(model.id)
                        }
                    }
                }
            }

            // Imported .rdp files.
            Label {
                visible: connectionList.count > 0
                text: qsTr("Imported .rdp files")
                font.bold: true
                font.pixelSize: 15
            }
            Label {
                visible: connectionList.count === 0 && (!app.feedAvailable || (!app.signedIn && !app.authBusy))
                width: parent.width
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("No imported .rdp files. Use «Import .rdp…» if you have one from your IT department.")
            }
            ListView {
                id: connectionList
                visible: count > 0
                width: parent.width
                height: contentHeight
                interactive: false
                model: app.connections
                delegate: ItemDelegate {
                    width: connectionList.width
                    onClicked: page.connectionOpened(model.id)

                    contentItem: Row {
                        spacing: 8
                        Label {
                            width: connectionList.width - 60
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
