import QtQuick
import QtQuick.Controls.FluentWinUI3
import Vindauga

ApplicationWindow {
    id: root
    width: 900
    height: 650
    visible: true
    title: qsTr("Vindauga")

    AppController { id: app }
    UpdateController { id: updater }

    // Theme follows the Appearance setting (System/Light/Dark).
    Binding { target: Theme; property: "appearance"; value: app.appearance }
    // Make Qt's own controls (text fields, spin boxes, combo boxes, scroll bars) follow the
    // chosen scheme too, not only the desktop's. Qt.Unknown = back to the desktop's setting.
    Binding {
        target: Qt.styleHints
        property: "colorScheme"
        value: app.appearance === 1 ? Qt.Light : (app.appearance === 2 ? Qt.Dark : Qt.Unknown)
    }
    color: Theme.ground
    // Explicit palette so every Qt control (text fields, spin boxes, combo boxes) uses the
    // app's scheme even when it differs from the desktop's.
    palette.window: Theme.ground
    palette.windowText: Theme.text
    palette.base: Theme.surface
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.placeholderText: Theme.muted
    palette.highlight: Theme.accent
    palette.highlightedText: "#FFFFFF"
    palette.mid: Theme.border
    palette.toolTipBase: Theme.surface
    palette.toolTipText: Theme.text
    font.family: Theme.sans

    // The sign-in web view is overlaid on this window rather than opened separately.
    Component.onCompleted: app.setHostWindow(root)

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: connectionsPageComponent
    }

    Component {
        id: connectionsPageComponent
        ConnectionsPage {
            onConnectionOpened: (id) => app.openConnection(id)
            onResourceOpened: (id) => app.openResource(id)
            onSettingsRequested: stack.push(settingsPageComponent)
        }
    }

    Component {
        id: sessionPageComponent
        SessionPage {
            onBackRequested: stack.pop()
        }
    }

    Component {
        id: settingsPageComponent
        SettingsPage {
            onBackRequested: stack.pop()
        }
    }

    Connections {
        target: app
        function onOpenSessionRequested(rdpText, connectionId) {
            stack.push(sessionPageComponent, { rdpText: rdpText, connectionId: connectionId })
        }
    }
}
