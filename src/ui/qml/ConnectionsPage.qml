import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Shapes

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

    background: Rectangle { color: Theme.ground }

    function initials(name) {
        const local = name.split("@")[0].split(/[._\- ]+/).filter(s => s.length > 0)
        if (local.length === 0)
            return "?"
        return (local[0][0] + (local.length > 1 ? local[local.length - 1][0] : "")).toUpperCase()
    }

    header: Rectangle {
        implicitHeight: headerRow.implicitHeight + 28
        color: Theme.bar

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            id: headerRow
            anchors.fill: parent
            anchors.leftMargin: Theme.gutter
            anchors.rightMargin: Theme.gutter
            spacing: 8

            Image {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                source: "qrc:/vindauga/vindauga.png"
                sourceSize: Qt.size(64, 64)
                fillMode: Image.PreserveAspectFit
                smooth: true
            }
            ColumnLayout {
                Layout.leftMargin: 4
                spacing: 0
                Text {
                    text: qsTr("Vindauga")
                    color: Theme.text
                    font.family: Theme.sans
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Azure Virtual Desktop · Windows 365")
                    color: Theme.muted
                    font.family: Theme.sans
                    font.pixelSize: 12
                }
            }
            Item { Layout.fillWidth: true }

            DButton {
                text: qsTr("Import .rdp")
                onClicked: fileDialog.open()
            }
            DButton {
                iconPath: "M12 9a3 3 0 1 0 0 6a3 3 0 0 0 0-6zM19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"
                iconSize: 18
                Accessible.name: qsTr("Settings")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Settings")
                onClicked: page.settingsRequested()
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 24
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                color: Theme.border
            }

            // Account: avatar + name + sign out, or a sign-in button.
            DButton {
                visible: app.feedAvailable && !app.signedIn
                enabled: !app.authBusy
                kind: "primary"
                text: app.authBusy ? qsTr("Signing in…") : qsTr("Sign in")
                onClicked: app.signIn()
            }
            RowLayout {
                visible: app.signedIn
                spacing: 10
                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 14
                    color: Theme.dark ? "#2C3B5C" : "#DCE7FB"
                    Text {
                        anchors.centerIn: parent
                        text: page.initials(app.accountName)
                        color: Theme.dark ? "#B7CDFA" : "#1E4C9F"
                        font.family: Theme.sans
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Text {
                        text: app.accountName
                        color: Theme.text
                        font.family: Theme.sans
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }
                    Text {
                        text: qsTr("Sign out")
                        color: signOutArea.containsMouse ? Theme.link : Theme.muted
                        font.family: Theme.sans
                        font.pixelSize: 11
                        MouseArea {
                            id: signOutArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: app.signOut()
                        }
                    }
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: footerRow.implicitHeight + 16
        color: Theme.bar

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            id: footerRow
            anchors.fill: parent
            anchors.leftMargin: Theme.gutter
            anchors.rightMargin: Theme.gutter
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 7
                Layout.preferredHeight: 7
                radius: 4
                color: app.signedIn ? Theme.ok : Theme.muted
            }
            Text {
                text: app.signedIn ? qsTr("Signed in") : qsTr("Not signed in")
                color: Theme.muted
                font.family: Theme.sans
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("Vindauga %1").arg(app.version)
                color: Theme.muted
                font.family: Theme.mono
                font.pixelSize: 12
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: contentWrap.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Item {
            id: contentWrap
            width: flick.width
            implicitHeight: banners.implicitHeight + content.implicitHeight + 36 + 48

            // Update banner is full-bleed under the header, like in the design.
            ColumnLayout {
                id: banners
                width: parent.width
                spacing: 0
                UpdateBanner {
                    Layout.fillWidth: true
                }
            }

            ColumnLayout {
                id: content
                y: banners.implicitHeight + 36
                width: Math.min(parent.width - 2 * Theme.gutter, Theme.contentMax - 2 * Theme.gutter)
                x: (parent.width - width) / 2
                spacing: 36

                // Sign-in / feed problems (AADSTS explanations, unreachable feed, ...).
                Rectangle {
                    visible: app.feedStatus.length > 0
                    Layout.fillWidth: true
                    implicitHeight: statusLabel.implicitHeight + 24
                    radius: Theme.radiusControl
                    color: Theme.warnTint
                    border.color: Theme.warn
                    border.width: 1
                    Text {
                        id: statusLabel
                        anchors.fill: parent
                        anchors.margins: 12
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.family: Theme.sans
                        font.pixelSize: 13
                        text: app.feedStatus
                    }
                }

                // Desktops and apps from the feed.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("Desktops and apps")
                            color: Theme.text
                            font.family: Theme.sans
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: app.signedIn
                            color: Theme.muted
                            font.family: Theme.sans
                            font.pixelSize: 12
                            text: app.feedBusy ? qsTr("Refreshing the list…")
                                : (isNaN(app.feedRefreshedAt.getTime()) ? qsTr("List not fetched yet")
                                   : qsTr("Updated %1").arg(Qt.formatDateTime(app.feedRefreshedAt, "ddd hh:mm")))
                        }
                        DButton {
                            visible: app.signedIn
                            compact: true
                            enabled: !app.feedBusy
                            text: qsTr("Refresh")
                            onClicked: app.refreshFeed()
                        }
                    }

                    GridLayout {
                        id: resourceGrid
                        Layout.fillWidth: true
                        columns: Math.max(1, Math.min(3, Math.floor((width + columnSpacing) / (260 + columnSpacing))))
                        columnSpacing: 16
                        rowSpacing: 16

                        Repeater {
                            id: resourceRepeater
                            model: app.resources
                            delegate: Rectangle {
                                id: card
                                required property var model
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                Layout.preferredHeight: 180
                                Layout.fillHeight: true
                                radius: Theme.radiusCard
                                color: Theme.surface
                                border.color: Theme.border
                                border.width: 1

                                ColumnLayout {
                                    id: cardColumn
                                    anchors.fill: parent
                                    anchors.margins: 18
                                    spacing: 16

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 12
                                        Rectangle {
                                            Layout.preferredWidth: 40
                                            Layout.preferredHeight: 40
                                            radius: 10
                                            color: Theme.accentTint
                                            Image {
                                                id: resIcon
                                                anchors.centerIn: parent
                                                width: 24
                                                height: 24
                                                source: card.model.iconSource
                                                visible: status === Image.Ready
                                                smooth: true
                                            }
                                            SvgIcon {
                                                anchors.centerIn: parent
                                                visible: !resIcon.visible
                                                size: 22
                                                color: Theme.link
                                                path: card.model.type === "RemoteApp"
                                                    ? "M4 4h16v16H4zM4 9h16"
                                                    : "M4 4h16a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zM8 21h8M12 17v4"
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 0
                                            Text {
                                                Layout.fillWidth: true
                                                text: card.model.title
                                                elide: Text.ElideRight
                                                color: Theme.text
                                                font.family: Theme.sans
                                                font.pixelSize: 15
                                                font.weight: Font.DemiBold
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: card.model.workspaceName.length > 0
                                                      ? qsTr("%1 · %2").arg(card.model.type).arg(card.model.workspaceName)
                                                      : card.model.type
                                                elide: Text.ElideRight
                                                color: Theme.muted
                                                font.family: Theme.sans
                                                font.pixelSize: 12
                                            }
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        color: Theme.muted
                                        font.family: Theme.mono
                                        font.pixelSize: 11
                                        text: [app.useDisplayScaleFactor ? qsTr("HiDPI") : qsTr("1×"),
                                               app.hardwareVideoDecoding ? qsTr("GPU decode") : qsTr("software decode")].join(" · ")
                                    }
                                    Item { Layout.fillHeight: true }
                                    DButton {
                                        Layout.fillWidth: true
                                        kind: "primary"
                                        text: qsTr("Connect")
                                        onClicked: page.resourceOpened(card.model.id)
                                    }
                                }
                            }
                        }

                        // Placeholder card, as in the design: dashed outline with a plus.
                        Item {
                            id: placeholder
                            Layout.fillWidth: true
                            Layout.preferredWidth: 100
                            Layout.preferredHeight: 180
                            Layout.fillHeight: true

                            Shape {
                                anchors.fill: parent
                                preferredRendererType: Shape.CurveRenderer
                                ShapePath {
                                    strokeColor: Theme.dashed
                                    strokeWidth: 1.5
                                    strokeStyle: ShapePath.DashLine
                                    dashPattern: [4, 4]
                                    fillColor: "transparent"
                                    PathRectangle {
                                        x: 1
                                        y: 1
                                        width: placeholder.width - 2
                                        height: placeholder.height - 2
                                        radius: Theme.radiusCard
                                    }
                                }
                            }
                            ColumnLayout {
                                anchors.centerIn: parent
                                width: parent.width - 36
                                spacing: 8
                                SvgIcon {
                                    Layout.alignment: Qt.AlignHCenter
                                    size: 22
                                    color: Theme.muted
                                    path: "M12 5v14M5 12h14"
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    color: Theme.textSoft
                                    font.family: Theme.sans
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    text: app.feedAvailable && !app.signedIn
                                          ? qsTr("Sign in to see your desktops")
                                          : qsTr("More resources appear here")
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    color: Theme.muted
                                    font.family: Theme.sans
                                    font.pixelSize: 12
                                    text: app.feedAvailable && !app.signedIn
                                          ? qsTr("Sign in with your work account to see the desktops and apps assigned to you, or import a .rdp file.")
                                          : (app.signedIn && !app.feedBusy && resourceRepeater.count === 0 && app.feedStatus.length === 0
                                             ? qsTr("No desktops or apps are assigned to this account. Ask your IT department, or import a .rdp file.")
                                             : qsTr("Published by your organisation in the feed, or import a .rdp file."))
                                }
                            }
                        }
                    }
                }

                // Imported .rdp files.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("Imported .rdp files")
                            color: Theme.text
                            font.family: Theme.sans
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: qsTr("Stored locally · not part of the feed")
                            color: Theme.muted
                            font.family: Theme.sans
                            font.pixelSize: 12
                        }
                    }

                    Text {
                        visible: connectionList.count === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.muted
                        font.family: Theme.sans
                        font.pixelSize: 13
                        text: qsTr("No imported .rdp files. Use «Import .rdp» if you have one from your IT department.")
                    }

                    Rectangle {
                        visible: connectionList.count > 0
                        Layout.fillWidth: true
                        implicitHeight: connectionList.contentHeight + 2
                        radius: Theme.radiusCard
                        color: Theme.surface
                        border.color: Theme.border
                        border.width: 1
                        clip: true

                        ListView {
                            id: connectionList
                            anchors.fill: parent
                            anchors.margins: 1
                            interactive: false
                            model: app.connections
                            delegate: Item {
                                id: row
                                required property var model
                                required property int index
                                width: connectionList.width
                                height: 56

                                Rectangle {
                                    visible: row.index > 0
                                    width: parent.width
                                    height: 1
                                    color: Theme.divider
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 16
                                    anchors.rightMargin: 16
                                    spacing: 14

                                    Rectangle {
                                        Layout.preferredWidth: 32
                                        Layout.preferredHeight: 32
                                        radius: 8
                                        color: Theme.ground
                                        SvgIcon {
                                            anchors.centerIn: parent
                                            size: 16
                                            color: Theme.textSoft
                                            path: "M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9zM14 3v6h6"
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0
                                        Text {
                                            Layout.fillWidth: true
                                            text: row.model.displayName
                                            elide: Text.ElideRight
                                            color: Theme.text
                                            font.family: Theme.sans
                                            font.pixelSize: 14
                                            font.weight: Font.Medium
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            color: Theme.muted
                                            font.family: Theme.mono
                                            font.pixelSize: 12
                                            text: {
                                                let t = isNaN(row.model.addedAt.getTime()) ? qsTr("Imported .rdp file")
                                                    : qsTr("Imported %1").arg(Qt.formatDate(row.model.addedAt, "d MMM"))
                                                if (!isNaN(row.model.lastUsedAt.getTime()))
                                                    t += qsTr(" · last used %1").arg(Qt.formatDate(row.model.lastUsedAt, "d MMM"))
                                                return t
                                            }
                                        }
                                    }
                                    DButton {
                                        compact: true
                                        text: qsTr("Connect")
                                        onClicked: page.connectionOpened(row.model.id)
                                    }
                                    DButton {
                                        compact: true
                                        kind: "ghost"
                                        iconPath: "M3 6h18M8 6V4h8v2M6 6l1 14h10l1-14"
                                        iconColor: Theme.muted
                                        Accessible.name: qsTr("Remove %1").arg(row.model.displayName)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Remove")
                                        onClicked: app.removeConnection(row.model.id)
                                    }
                                }
                            }
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
