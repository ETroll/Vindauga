import QtQuick
import QtQuick.Controls.FluentWinUI3
import Vindauga

// Full-surface RdpItem with an auto-hidden "notch" toolbar revealed by moving the
// mouse to the top edge, so no permanent screen space is taken during a session. A
// failed connection shows an error card with retry/back instead of a blank window.
// RdpItem owns the RdpSession, AadWebAuthenticator and credential dialog internally;
// this page only supplies rdpText and starts the connection.
Page {
    id: page
    property string rdpText: ""
    property string connectionId: ""
    property string errorText: ""
    // True from startConnecting() until either sessionConnected or sessionDisconnected;
    // drives the connecting overlay.
    property bool sessionActive: false
    // Set from RdpItem::sessionReconnecting (FreeRDP's own retry mechanism). Empty means
    // no specific reason has been reported yet, so the overlay shows a generic text.
    property string reconnectingText: ""

    signal backRequested()

    background: Rectangle { color: "black" }

    // Maps known FreeRDP error strings to user-friendly text. Not exhaustive; unknown
    // reasons are shown as-is.
    function friendlyError(reason) {
        if (reason.indexOf("Logon failed") !== -1)
            return qsTr("Wrong username or password. Click «Try again» to re-enter it.")
        if (reason.indexOf("Starting your VM") !== -1 || reason.indexOf("TARGET_BOOTING") !== -1)
            return qsTr("The desktop took too long to start (Azure boots it automatically). Try again in a few minutes.")
        if (reason.indexOf("Cancelled") !== -1)
            return qsTr("Cancelled.")
        // freerdp_get_last_error_string() returns a literal "Success." when the session
        // was torn down by a mid-session protocol error (e.g. an unexpected PDU) without
        // an ERRCONNECT code being set. That message is meaningless on its own, so show
        // a generic interruption text instead.
        if (reason.trim().toLowerCase() === "success.")
            return qsTr("The connection was unexpectedly interrupted (unknown cause). Click «Try again».")
        return reason
    }

    RdpItem {
        id: rdpItem
        anchors.fill: parent
        focus: true
        // Live binding: RdpItem re-reads this on every window resize, so toggling it
        // mid-session takes effect at the next resize.
        useDisplayScaleFactor: app.useDisplayScaleFactor
        // Live binding: RdpItem re-reads this for every burst of frames, so a change
        // takes effect from the next burst.
        frameBufferMs: app.frameBufferMs
        onSessionConnected: {
            page.sessionActive = true
            page.reconnectingText = ""
        }
        onSessionDisconnected: (reason) => {
            page.sessionActive = false
            page.reconnectingText = ""
            if (reason.length > 0)
                page.errorText = reason
        }
        // `what` is FreeRDP's own description of what is being retried (e.g. "Starting
        // your VM. It may take up to 5 minutes." for a deallocated session host).
        // maxAttempts <= 1 means FreeRDP gave no meaningful cap, so omit the "(x/y)" suffix.
        onSessionReconnecting: (what, attempt, maxAttempts) => {
            page.reconnectingText = maxAttempts > 1 ? (what + " (" + attempt + "/" + maxAttempts + ")") : what
        }
    }

    function startConnecting() {
        page.errorText = ""
        page.sessionActive = false
        page.reconnectingText = ""
        rdpItem.connectToSession(page.rdpText, page.connectionId)
    }

    Component.onCompleted: page.startConnecting()

    // Invisible hover zone along the top edge that reveals the notch.
    Item {
        id: topReveal
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: notch.width + 80
        height: 14

        MouseArea {
            id: revealArea
            anchors.fill: parent
            hoverEnabled: true
            onEntered: notch.revealed = true
            onExited: hideTimer.restart()
        }
    }

    Rectangle {
        id: notch
        property bool revealed: false
        width: 200
        height: 36
        radius: height / 2
        color: "#e6202024"
        border.color: "#33ffffff"
        border.width: 1
        anchors.horizontalCenter: parent.horizontalCenter
        y: revealed ? 8 : -height
        Behavior on y {
            NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
        }

        Row {
            anchors.centerIn: parent
            spacing: 6
            Label { text: "⟵"; color: "white"; font.pixelSize: 14 }
            Label { text: qsTr("End session"); color: "white"; font.pixelSize: 13 }
        }

        MouseArea {
            id: notchArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: notch.revealed = true
            onExited: hideTimer.restart()
            onClicked: page.backRequested()
        }
    }

    Timer {
        id: hideTimer
        interval: 400
        onTriggered: {
            if (!revealArea.containsMouse && !notchArea.containsMouse)
                notch.revealed = false
        }
    }

    // Spinner + status text while waiting for the connection. Covers both the short
    // AAD/RDSTLS negotiation and the long wait while Azure boots a deallocated session
    // host (up to several minutes). Hidden once the session connects or the error card
    // below takes over.
    Column {
        anchors.centerIn: parent
        visible: !page.sessionActive && page.errorText.length === 0
        spacing: 16

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: visible
        }
        Label {
            width: Math.min(360, page.width - 48)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "white"
            text: page.reconnectingText.length > 0 ? page.reconnectingText : qsTr("Connecting…")
        }
    }

    // Error card shown when the session ends unexpectedly (wrong password, host
    // unavailable after all retries, ...). "Try again" reuses the same RdpItem.
    Rectangle {
        anchors.centerIn: parent
        visible: page.errorText.length > 0
        width: Math.min(420, parent.width - 48)
        height: errorColumn.implicitHeight + 32
        radius: 12
        color: "#e6202024"
        border.color: "#33ffffff"
        border.width: 1

        Column {
            id: errorColumn
            anchors.centerIn: parent
            width: parent.width - 32
            spacing: 12

            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                color: "white"
                font.bold: true
                text: qsTr("Connection failed")
            }
            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                color: "#dddddd"
                text: page.friendlyError(page.errorText)
            }
            Row {
                spacing: 8
                Button {
                    text: qsTr("Try again")
                    onClicked: page.startConnecting()
                }
                Button {
                    text: qsTr("Back to connections")
                    onClicked: page.backRequested()
                }
            }
        }
    }
}
