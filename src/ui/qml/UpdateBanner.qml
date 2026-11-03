import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// Update notice on the start page, driven by UpdateController (`updater`, from Main.qml).
// Full-width bar under the header, in the accent tint (warning tint on failure). Hidden
// when there is nothing to say.
Rectangle {
    id: banner

    readonly property bool failed: updater.state === UpdateController.Failed
    readonly property bool working: updater.state === UpdateController.Downloading
                                    || updater.state === UpdateController.Installing
                                    || updater.state === UpdateController.Restarting

    visible: updater.state !== UpdateController.Idle
    implicitHeight: visible ? layout.implicitHeight + 20 : 0
    color: failed ? Theme.warnTint : Theme.bannerBg

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: failed ? Theme.warn : Theme.bannerBorder
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        anchors.leftMargin: Theme.gutter
        anchors.rightMargin: Theme.gutter
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            SvgIcon {
                size: 18
                color: banner.failed ? Theme.warn : Theme.link
                path: "M12 3v12M6 9l6-6 6 6M4 21h16"
            }
            // Headline
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text
                font.family: Theme.sans
                font.pixelSize: 13
                font.weight: Font.DemiBold
                text: {
                    switch (updater.state) {
                    case UpdateController.Downloading: return qsTr("Downloading Vindauga %1…").arg(updater.availableVersion)
                    case UpdateController.Installing: return qsTr("Installing Vindauga %1…").arg(updater.availableVersion)
                    case UpdateController.Restarting: return qsTr("Updated. Restarting Vindauga…")
                    case UpdateController.Failed: return qsTr("The update did not finish")
                    default: return qsTr("Vindauga %1 is available").arg(updater.availableVersion)
                    }
                }
            }

            Flow {
                Layout.alignment: Qt.AlignRight
                spacing: 6
                visible: !banner.working || updater.state === UpdateController.Downloading

                DButton {
                    visible: updater.state === UpdateController.Available && updater.releaseNotes.length > 0
                    compact: true
                    kind: "ghost"
                    text: qsTr("What's new")
                    onClicked: notesDialog.open()
                }
                DButton {
                    visible: updater.state === UpdateController.Available
                    compact: true
                    kind: "ghost"
                    text: qsTr("Skip this version")
                    onClicked: updater.skipVersion()
                }
                DButton {
                    visible: updater.state === UpdateController.Available
                    compact: true
                    kind: "ghost"
                    text: qsTr("Later")
                    onClicked: updater.dismiss()
                }
                DButton {
                    visible: banner.failed
                    compact: true
                    kind: "ghost"
                    text: qsTr("Dismiss")
                    onClicked: updater.dismiss()
                }
                DButton {
                    visible: updater.state === UpdateController.Downloading
                    compact: true
                    text: qsTr("Cancel")
                    onClicked: updater.cancel()
                }
                DButton {
                    visible: updater.state === UpdateController.Available && !updater.canInstall
                    compact: true
                    kind: "primary"
                    text: qsTr("View release")
                    onClicked: updater.openReleasePage()
                }
                DButton {
                    visible: (updater.state === UpdateController.Available || banner.failed) && updater.canInstall
                    compact: true
                    kind: "primary"
                    // The sign-in view is overlaid on this page; restarting would cut it off.
                    enabled: !app.authBusy
                    text: banner.failed ? qsTr("Try again") : qsTr("Update now")
                    onClicked: updater.install()
                }
            }
        }

        // Downloading: progress. Installing: the password prompt is a separate window
        // that can end up behind this one, so say what is going on.
        ProgressBar {
            Layout.fillWidth: true
            visible: updater.state === UpdateController.Downloading
            indeterminate: updater.progress < 0
            value: Math.max(0, updater.progress)
        }
        Text {
            Layout.fillWidth: true
            visible: updater.state === UpdateController.Installing
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.family: Theme.sans
            font.pixelSize: 12
            text: qsTr("Enter your password in the system prompt to continue. If you do not see it, look for another window.")
        }

        // Failure details, and the command to run by hand when no password prompt can be shown.
        Text {
            Layout.fillWidth: true
            visible: banner.failed
            wrapMode: Text.WordWrap
            color: Theme.text
            font.family: Theme.sans
            font.pixelSize: 13
            text: updater.errorText
        }
        RowLayout {
            Layout.fillWidth: true
            visible: banner.failed && updater.manualCommand.length > 0
            TextField {
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                text: updater.manualCommand
                font.family: Theme.mono
            }
            DButton {
                compact: true
                text: qsTr("Copy")
                onClicked: updater.copyManualCommand()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: updater.state === UpdateController.Available && !updater.canInstall
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.family: Theme.sans
            font.pixelSize: 12
            text: qsTr("This copy of Vindauga cannot install the update itself (it is not an installed Vindauga package, or the release has no checksum for it). Use «View release» to get the new version.")
        }
    }

    // Release notes in a modal rather than inline: they can be long. Header and footer are
    // built here (not the style's defaults) so the bars run edge to edge like the design.
    Dialog {
        id: notesDialog
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(640, parent.width - 48)
        height: Math.min(implicitHeight, parent.height - 48)
        padding: 0
        topPadding: 0
        bottomPadding: 0
        leftPadding: 0
        rightPadding: 0
        topInset: 0
        bottomInset: 0
        leftInset: 0
        rightInset: 0

        background: Rectangle {
            radius: Theme.radiusDialog
            color: Theme.surface
            border.color: Theme.border
        }
        Overlay.modal: Rectangle { color: Theme.scrim }

        header: Item {
            implicitHeight: headerColumn.implicitHeight + 36
            ColumnLayout {
                id: headerColumn
                anchors.fill: parent
                anchors.margins: 24
                anchors.topMargin: 20
                anchors.bottomMargin: 16
                spacing: 4
                Text {
                    text: qsTr("What's new")
                    color: Theme.text
                    font.family: Theme.sans
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                RowLayout {
                    spacing: 8
                    Rectangle {
                        implicitWidth: versionText.implicitWidth + 16
                        implicitHeight: 22
                        radius: 6
                        color: Theme.border
                        Text {
                            id: versionText
                            anchors.centerIn: parent
                            text: updater.availableVersion
                            color: Theme.text
                            font.family: Theme.mono
                            font.pixelSize: 12
                        }
                    }
                    Text {
                        text: qsTr("you have %1").arg(app.version)
                        color: Theme.muted
                        font.family: Theme.sans
                        font.pixelSize: 12
                    }
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        contentItem: ScrollView {
            id: notesScroll
            implicitHeight: notesLabel.implicitHeight + 40
            clip: true
            contentWidth: availableWidth
            Label {
                id: notesLabel
                x: 24
                y: 20
                width: notesScroll.availableWidth - 48
                wrapMode: Text.WordWrap
                textFormat: Text.MarkdownText
                color: Theme.text
                linkColor: Theme.link
                font.family: Theme.sans
                font.pixelSize: 13
                text: updater.releaseNotes
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }

        footer: Rectangle {
            implicitHeight: 64
            color: Theme.bar
            // Rounded bottom corners come from clipping to the dialog's own radius.
            radius: Theme.radiusDialog
            Rectangle {
                width: parent.width
                height: parent.radius
                color: parent.color
            }
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 8
                Text {
                    text: qsTr("Full release on GitHub")
                    color: linkArea.containsMouse ? Theme.text : Theme.link
                    font.family: Theme.sans
                    font.pixelSize: 13
                    MouseArea {
                        id: linkArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: updater.openReleasePage()
                    }
                }
                Item { Layout.fillWidth: true }
                DButton {
                    text: qsTr("Close")
                    onClicked: notesDialog.close()
                }
                DButton {
                    visible: updater.state === UpdateController.Available && updater.canInstall
                    kind: "primary"
                    enabled: !app.authBusy
                    text: qsTr("Update to %1").arg(updater.availableVersion)
                    onClicked: {
                        notesDialog.close()
                        updater.install()
                    }
                }
            }
        }
    }
}
