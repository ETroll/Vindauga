import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// Update notice on the start page, driven by UpdateController (`updater`, from Main.qml).
// Same look as the sign-in problem card next to it (translucent tinted rectangle), in
// the accent blue rather than the warning orange. Hidden when there is nothing to say.
Rectangle {
    id: banner

    readonly property bool failed: updater.state === UpdateController.Failed
    readonly property bool working: updater.state === UpdateController.Downloading
                                    || updater.state === UpdateController.Installing
                                    || updater.state === UpdateController.Restarting

    visible: updater.state !== UpdateController.Idle
    height: visible ? layout.implicitHeight + 24 : 0
    radius: 6
    color: failed ? "#33ff9800" : "#330078d4"
    border.color: failed ? "#66ff9800" : "#660078d4"

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // Headline
        Label {
            Layout.fillWidth: true
            font.bold: true
            wrapMode: Text.WordWrap
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

        // Downloading: progress. Installing: the password prompt is a separate window
        // that can end up behind this one, so say what is going on.
        ProgressBar {
            Layout.fillWidth: true
            visible: updater.state === UpdateController.Downloading
            indeterminate: updater.progress < 0
            value: Math.max(0, updater.progress)
        }
        Label {
            Layout.fillWidth: true
            visible: updater.state === UpdateController.Installing
            wrapMode: Text.WordWrap
            opacity: 0.8
            font.pixelSize: 12
            text: qsTr("Enter your password in the system prompt to continue. If you do not see it, look for another window.")
        }

        // Failure details, and the command to run by hand when no password prompt can be shown.
        Label {
            Layout.fillWidth: true
            visible: banner.failed
            wrapMode: Text.WordWrap
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
                font.family: "monospace"
            }
            Button {
                text: qsTr("Copy")
                onClicked: updater.copyManualCommand()
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            visible: !banner.working || updater.state === UpdateController.Downloading

            Button {
                visible: updater.state === UpdateController.Available && updater.canInstall
                         || banner.failed && updater.canInstall
                highlighted: true
                // The sign-in view is overlaid on this page; restarting would cut it off.
                enabled: !app.authBusy
                text: banner.failed ? qsTr("Try again") : qsTr("Update now")
                onClicked: updater.install()
            }
            Button {
                visible: updater.state === UpdateController.Available && !updater.canInstall
                highlighted: true
                text: qsTr("View release")
                onClicked: updater.openReleasePage()
            }
            Button {
                visible: updater.state === UpdateController.Downloading
                text: qsTr("Cancel")
                onClicked: updater.cancel()
            }
            Button {
                visible: updater.state === UpdateController.Available && updater.releaseNotes.length > 0
                flat: true
                text: qsTr("What's new")
                onClicked: notesDialog.open()
            }
            Button {
                visible: updater.state === UpdateController.Available
                flat: true
                text: qsTr("Later")
                onClicked: updater.dismiss()
            }
            Button {
                visible: updater.state === UpdateController.Available
                flat: true
                text: qsTr("Skip this version")
                onClicked: updater.skipVersion()
            }
            Button {
                visible: banner.failed
                flat: true
                text: qsTr("Dismiss")
                onClicked: updater.dismiss()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: updater.state === UpdateController.Available && !updater.canInstall
            wrapMode: Text.WordWrap
            opacity: 0.7
            font.pixelSize: 12
            text: qsTr("This copy of Vindauga cannot install the update itself (it is not an installed Vindauga package, or the release has no checksum for it). Use «View release» to get the new version.")
        }
    }

    // Release notes in a modal rather than inline: they can be long.
    Dialog {
        id: notesDialog
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 48)
        height: Math.min(460, parent.height - 48)
        title: qsTr("What's new in Vindauga %1").arg(updater.availableVersion)
        standardButtons: Dialog.Close

        ScrollView {
            anchors.fill: parent
            clip: true
            Label {
                width: notesDialog.availableWidth - 16
                wrapMode: Text.WordWrap
                textFormat: Text.MarkdownText
                text: updater.releaseNotes
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }
    }
}
