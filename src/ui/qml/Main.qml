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

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: connectionsPageComponent
    }

    Component {
        id: connectionsPageComponent
        ConnectionsPage {
            onConnectionOpened: (id) => app.openConnection(id)
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
