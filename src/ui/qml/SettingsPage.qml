import QtQuick
import QtQuick.Controls.FluentWinUI3

// Binds directly to AppController's properties; changes are persisted immediately,
// so there is no explicit save button.
Page {
    id: page
    title: qsTr("Settings")

    signal backRequested()

    Flickable {
        anchors.fill: parent
        contentHeight: settingsColumn.implicitHeight + 32
        clip: true
        ScrollBar.vertical: ScrollBar {}

    Column {
        id: settingsColumn
        x: 16
        y: 16
        width: parent.width - 32
        spacing: 16

        ToolButton {
            text: qsTr("← Back")
            onClicked: page.backRequested()
        }

        Column {
            width: parent.width
            spacing: 4

            CheckBox {
                id: scaleCheckbox
                text: qsTr("Use the display's scale factor (HiDPI)")
                checked: app.useDisplayScaleFactor
                // onClicked rather than onToggled: CheckBox assigns `checked` itself before
                // onToggled fires, and an imperative assignment to a bound property would
                // permanently break the binding above.
                onClicked: app.useDisplayScaleFactor = checked
            }
            Label {
                width: parent.width
                leftPadding: 32
                wrapMode: Text.WordWrap
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("Tells Windows inside the session to scale its UI to match the display's actual pixel density, instead of always 100% — bigger, more readable text/icons on a HiDPI display. Takes effect live: RdpItem re-reads the setting on every window resize, so an active session picks it up on the next resize without reconnecting.")
            }
        }

        // On hybrid-GPU machines FreeRDP's chosen VAAPI device may have incomplete
        // hardware support, visible as screen regions that stop updating during video.
        // A best-effort default is picked automatically, but it must be possible to
        // disable hardware decoding or choose the GPU explicitly from the UI.
        Column {
            width: parent.width
            spacing: 4

            CheckBox {
                id: hwDecodeCheckbox
                text: qsTr("Use hardware video decoding")
                checked: app.hardwareVideoDecoding
                onClicked: app.hardwareVideoDecoding = checked
            }
            Label {
                width: parent.width
                leftPadding: 32
                wrapMode: Text.WordWrap
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("Uses the GPU to decode video inside the session — much lighter on the CPU, but on some hybrid-GPU laptops it can occasionally get stuck on individual screen regions during video playback. Turning this off falls back to (heavier, but more reliable) software decoding. Applies to the next connection, not the current one.")
            }

            Row {
                spacing: 8
                leftPadding: 32
                enabled: hwDecodeCheckbox.checked
                opacity: enabled ? 1.0 : 0.5

                Label {
                    text: qsTr("GPU:")
                    anchors.verticalCenter: parent.verticalCenter
                }
                ComboBox {
                    id: gpuCombo
                    model: [{label: qsTr("Automatic (recommended)"), path: ""}].concat(app.availableGpuDevices)
                    textRole: "label"
                    valueRole: "path"

                    FontMetrics {
                        id: gpuComboFontMetrics
                        font: gpuCombo.font
                    }
                    // FluentWinUI3's ComboBox uses a fixed default width regardless of
                    // content, so compute the width of the widest model entry (plus
                    // indicator and padding) to avoid clipped GPU names.
                    implicitWidth: {
                        let widest = 0
                        for (let i = 0; i < model.length; i++)
                            widest = Math.max(widest, gpuComboFontMetrics.advanceWidth(model[i].label))
                        return widest + leftPadding + rightPadding + indicator.width + spacing + 24
                    }

                    // ComboBox has no built-in binding from valueRole to the current value;
                    // select the stored choice by index at startup.
                    Component.onCompleted: {
                        for (let i = 0; i < model.length; i++) {
                            if (model[i].path === app.preferredVaapiDevice) {
                                currentIndex = i
                                break
                            }
                        }
                    }
                    onActivated: app.preferredVaapiDevice = model[currentIndex].path
                }
            }
        }

        // Jitter buffer: wait briefly for more frames before drawing instead of drawing
        // as soon as possible. Exposed as a millisecond value rather than an on/off switch.
        Column {
            width: parent.width
            spacing: 4

            Row {
                spacing: 8

                Label {
                    text: qsTr("Frame buffer:")
                    anchors.verticalCenter: parent.verticalCenter
                }
                SpinBox {
                    id: frameBufferSpinBox
                    from: 0
                    to: 1000
                    stepSize: 10
                    value: app.frameBufferMs
                    // onValueModified fires only on user interaction, not on the programmatic
                    // value binding above, so the binding stays intact.
                    onValueModified: app.frameBufferMs = value
                }
                Label {
                    text: qsTr("ms")
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: 0.7
                }
            }
            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("Waits this many milliseconds after the first frame in a burst before actually drawing, so more updates can arrive and be combined into one smoother draw — trading a little extra visual delay for less stutter during bursty updates (video, scrolling, opening windows). 0 = off, draw as soon as possible (default). Takes effect live, from the next burst of updates — no need to reconnect.")
            }
        }

        // Synthetic mouse motion while idle (FreeRDP's /prevent-session-lock). Off by
        // default: whether the Windows lock screen may be held off is the organisation's
        // policy, not ours.
        Column {
            width: parent.width
            spacing: 4

            Row {
                spacing: 8

                Label {
                    text: qsTr("Keep session awake every:")
                    anchors.verticalCenter: parent.verticalCenter
                }
                SpinBox {
                    id: keepAwakeSpinBox
                    from: 0
                    to: 3600
                    stepSize: 30
                    value: app.preventSessionLockSeconds
                    onValueModified: app.preventSessionLockSeconds = value
                }
                Label {
                    text: qsTr("s")
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: 0.7
                }
            }
            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("Sends an invisible mouse movement to the session after this many seconds without input, so the gateway's idle timeout and the Windows lock screen do not kick in during a long read or a meeting. 0 = off (default). Applies to the next connection.")
            }
        }

        // Sign-in / feed options for tenants that need them. Both empty by default.
        Column {
            width: parent.width
            spacing: 4

            Label {
                text: qsTr("Sign-in and feed")
                font.bold: true
            }
            Row {
                spacing: 8
                Label {
                    text: qsTr("Feed discovery URL:")
                    anchors.verticalCenter: parent.verticalCenter
                }
                TextField {
                    id: discoveryUrlField
                    width: 360
                    text: app.feedDiscoveryUrl
                    placeholderText: app.defaultFeedDiscoveryUrl
                    onEditingFinished: app.feedDiscoveryUrl = text
                }
            }
            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("Only needed for sovereign clouds (Azure Government: https://rdweb.wvd.azure.us/api/arm/feeddiscovery, Azure China: https://rdweb.wvd.azure.cn/api/arm/feeddiscovery); leave empty for the public Azure cloud. Those clouds also use their own sign-in authority, set with the VINDAUGA_LOGIN_HOST environment variable; this has not been verified against a sovereign tenant. Applies to the next refresh.")
            }
            Row {
                spacing: 8
                Label {
                    text: qsTr("Sign-in browser identity:")
                    anchors.verticalCenter: parent.verticalCenter
                }
                TextField {
                    id: userAgentField
                    width: 360
                    text: app.signInUserAgent
                    placeholderText: qsTr("Default (Chromium on Linux)")
                    onEditingFinished: app.signInUserAgent = text
                }
            }
            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("User-Agent string sent by the embedded sign-in browser and the token requests. Some organisations' Conditional Access policies only admit certain platforms or browsers, which they recognise from this header. Leave empty unless sign-in is blocked with a 'device platform' message. Applies to the next sign-in.")
            }
        }
    }
    }
}
