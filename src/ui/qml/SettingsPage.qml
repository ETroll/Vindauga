import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// Binds directly to AppController's properties; changes are persisted immediately,
// so there is no explicit save button.
Page {
    id: page
    title: qsTr("Settings")

    signal backRequested()

    background: Rectangle { color: Theme.ground }

    function scrollTo(item) {
        flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height),
                                  item.mapToItem(flick.contentItem, 0, 0).y)
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
            spacing: 12

            DButton {
                iconPath: "M19 12H5M12 19l-7-7 7-7"
                iconSize: 18
                Accessible.name: qsTr("Back")
                onClicked: page.backRequested()
            }
            ColumnLayout {
                spacing: 0
                Text {
                    text: qsTr("Settings")
                    color: Theme.text
                    font.family: Theme.sans
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Changes save automatically")
                    color: Theme.muted
                    font.family: Theme.sans
                    font.pixelSize: 12
                }
            }
            Item { Layout.fillWidth: true }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: layoutRoot.implicitHeight + 64
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        RowLayout {
            id: layoutRoot
            width: Math.min(flick.width - 2 * Theme.gutter, Theme.contentMax - 2 * Theme.gutter)
            x: (flick.width - width) / 2
            y: Theme.gutter
            spacing: 32

            // Section nav (hidden on narrow windows)
            ColumnLayout {
                visible: layoutRoot.width > 720
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: 200
                Layout.minimumWidth: 200
                Layout.maximumWidth: 200
                spacing: 2

                Repeater {
                    model: [
                        { label: qsTr("Updates"), target: updatesSection,
                          icon: "M12 3v12M6 9l6-6 6 6M4 21h16" },
                        { label: qsTr("Display"), target: displaySection,
                          icon: "M4 4h16a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zM8 21h8M12 17v4" },
                        { label: qsTr("Performance"), target: performanceSection,
                          icon: "M13 2L4 14h7l-1 8 9-12h-7z" },
                        { label: qsTr("Session"), target: sessionSection,
                          icon: "M12 3a9 9 0 1 0 0 18a9 9 0 0 0 0-18zM12 7v5l3 2" },
                        { label: qsTr("Sign-in and feed"), target: signinSection,
                          icon: "M5 11h14a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-6a2 2 0 0 1 2-2zM7 11V7a5 5 0 0 1 10 0v4" },
                        { label: qsTr("About"), target: aboutSection,
                          icon: "M12 3a9 9 0 1 0 0 18a9 9 0 0 0 0-18zM12 16v-4M12 8h.01" }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 36
                        radius: Theme.radiusControl
                        color: navArea.containsMouse ? Theme.hover : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            spacing: 10
                            SvgIcon { size: 16; color: Theme.textSoft; path: modelData.icon }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.label
                                color: Theme.textSoft
                                font.family: Theme.sans
                                font.pixelSize: 13
                            }
                        }
                        MouseArea {
                            id: navArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.scrollTo(modelData.target)
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 100
                Layout.alignment: Qt.AlignTop
                spacing: 24

                // ---- Updates
                SettingsSection {
                    id: updatesSection
                    title: qsTr("Updates")
                    subtitle: qsTr("Only copies installed from the .deb or Arch package can update themselves.")

                    SettingsRow {
                        title: qsTr("Check for updates")
                        description: qsTr("Looks on GitHub (github.com/ETroll/Vindauga) at most once a day. Nothing is downloaded or installed until you choose «Update now». GitHub sees your IP address when Vindauga checks.")
                        DSwitch {
                            checked: updater.checkForUpdates
                            // onClicked, not onToggled, to keep the `checked` binding intact.
                            onClicked: updater.checkForUpdates = checked
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: updateStatusRow.implicitHeight + 24
                        color: updater.state === UpdateController.Available ? Theme.bannerBg : "transparent"
                        Rectangle {
                            width: parent.width
                            height: 1
                            color: updater.state === UpdateController.Available ? Theme.bannerBorder : Theme.divider
                        }
                        RowLayout {
                            id: updateStatusRow
                            anchors.fill: parent
                            anchors.leftMargin: 20
                            anchors.rightMargin: 20
                            spacing: 12
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                font.family: Theme.sans
                                font.pixelSize: 13
                                text: updater.state === UpdateController.Available
                                      ? qsTr("<b>%1 is available</b> · installed %2").arg(updater.availableVersion).arg(app.version)
                                      : (updater.statusText.length > 0 ? updater.statusText
                                         : (isNaN(updater.lastChecked.getTime()) ? qsTr("Installed %1").arg(app.version)
                                            : qsTr("Last checked %1").arg(Qt.formatDateTime(updater.lastChecked, "ddd d MMM hh:mm"))))
                            }
                            DButton {
                                compact: true
                                text: qsTr("Check now")
                                enabled: !updater.checking
                                onClicked: updater.checkNow()
                            }
                            DButton {
                                visible: updater.state === UpdateController.Available && updater.canInstall
                                compact: true
                                kind: "primary"
                                enabled: !app.authBusy
                                text: qsTr("Update now")
                                onClicked: updater.install()
                            }
                        }
                    }
                }

                // ---- Display
                SettingsSection {
                    id: displaySection
                    title: qsTr("Display")

                    SettingsRow {
                        title: qsTr("Match the display's scale factor (HiDPI)")
                        description: qsTr("Windows inside the session scales its UI to your display. Applies live on the next window resize.")
                        DSwitch {
                            checked: app.useDisplayScaleFactor
                            // onClicked rather than onToggled: the control assigns `checked`
                            // itself before onToggled fires, and an imperative assignment to a
                            // bound property would permanently break the binding above.
                            onClicked: app.useDisplayScaleFactor = checked
                        }
                    }
                    SettingsRow {
                        title: qsTr("Appearance")
                        description: qsTr("Follows the desktop's light/dark preference by default.")
                        RowLayout {
                            spacing: 0
                            Repeater {
                                model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                                delegate: DButton {
                                    required property string modelData
                                    required property int index
                                    compact: true
                                    kind: app.appearance === index ? "primary" : "secondary"
                                    text: modelData
                                    onClicked: app.appearance = index
                                }
                            }
                        }
                    }
                }

                // ---- Performance
                // On hybrid-GPU machines FreeRDP's chosen VAAPI device may have incomplete
                // hardware support, visible as screen regions that stop updating during video.
                // A best-effort default is picked automatically, but it must be possible to
                // disable hardware decoding or choose the GPU explicitly from the UI.
                SettingsSection {
                    id: performanceSection
                    title: qsTr("Performance")

                    SettingsRow {
                        title: qsTr("Hardware video decoding")
                        description: qsTr("Much lighter on the CPU. Turn off if video regions get stuck on hybrid-GPU laptops. Applies to the next connection.")
                        DSwitch {
                            id: hwDecodeSwitch
                            checked: app.hardwareVideoDecoding
                            onClicked: app.hardwareVideoDecoding = checked
                        }
                    }
                    SettingsRow {
                        indent: 20
                        title: qsTr("GPU")
                        description: qsTr("Device used for decoding.")
                        enabled: hwDecodeSwitch.checked
                        opacity: enabled ? 1.0 : 0.5
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
                    // Jitter buffer: wait briefly for more frames before drawing instead of
                    // drawing as soon as possible. Exposed as milliseconds, not an on/off switch.
                    SettingsRow {
                        title: qsTr("Frame buffer")
                        description: qsTr("Waits this long after the first frame in a burst so updates combine into one draw. A little latency for less stutter. 0 = draw immediately. Takes effect from the next burst, no reconnect needed.")
                        SpinBox {
                            from: 0
                            to: 1000
                            stepSize: 10
                            value: app.frameBufferMs
                            // onValueModified fires only on user interaction, not on the
                            // programmatic binding above, so the binding stays intact.
                            onValueModified: app.frameBufferMs = value
                        }
                        Text {
                            text: qsTr("ms")
                            color: Theme.muted
                            font.family: Theme.sans
                            font.pixelSize: 12
                        }
                    }
                }

                // ---- Session
                // Synthetic mouse motion while idle (FreeRDP's /prevent-session-lock). Off by
                // default: whether the Windows lock screen may be held off is the
                // organisation's policy, not ours.
                SettingsSection {
                    id: sessionSection
                    title: qsTr("Session")

                    SettingsRow {
                        title: qsTr("Keep session awake")
                        description: qsTr("Sends an invisible mouse movement after this many seconds without input, so the gateway's idle timeout and the Windows lock screen don't kick in during a long read or a meeting. Applies to the next connection.")
                        SpinBox {
                            visible: app.preventSessionLockSeconds > 0
                            from: 30
                            to: 3600
                            stepSize: 30
                            value: app.preventSessionLockSeconds
                            onValueModified: app.preventSessionLockSeconds = value
                        }
                        Text {
                            visible: app.preventSessionLockSeconds > 0
                            text: qsTr("s")
                            color: Theme.muted
                            font.family: Theme.sans
                            font.pixelSize: 12
                        }
                        DSwitch {
                            checked: app.preventSessionLockSeconds > 0
                            // 0 = off in the backend; 300 s is the design's default interval.
                            onClicked: app.preventSessionLockSeconds = checked ? 300 : 0
                        }
                    }
                }

                // ---- Sign-in / feed options for tenants that need them. Both empty by default.
                SettingsSection {
                    id: signinSection
                    title: qsTr("Sign-in and feed")
                    badge: qsTr("Advanced · leave empty for public Azure")

                    SettingsRow {
                        stacked: true
                        title: qsTr("Feed discovery URL")
                        description: qsTr("Only for sovereign clouds (Azure Government: https://rdweb.wvd.azure.us/api/arm/feeddiscovery, Azure China: https://rdweb.wvd.azure.cn/api/arm/feeddiscovery). Those clouds also use their own sign-in authority, set with the VINDAUGA_LOGIN_HOST environment variable; this has not been verified against a sovereign tenant. Applies to the next refresh.")
                        TextField {
                            Layout.fillWidth: true
                            font.family: Theme.mono
                            font.pixelSize: 12
                            text: app.feedDiscoveryUrl
                            placeholderText: app.defaultFeedDiscoveryUrl
                            onEditingFinished: app.feedDiscoveryUrl = text
                        }
                    }
                    SettingsRow {
                        stacked: true
                        title: qsTr("Sign-in browser identity")
                        description: qsTr("User-Agent string sent by the embedded sign-in browser and the token requests. Some organisations' Conditional Access policies only admit certain platforms or browsers, which they recognise from this header. Leave empty unless sign-in is blocked with a 'device platform' message. Applies to the next sign-in.")
                        TextField {
                            Layout.fillWidth: true
                            font.family: Theme.mono
                            font.pixelSize: 12
                            text: app.signInUserAgent
                            placeholderText: qsTr("Default (Chromium on Linux)")
                            onEditingFinished: app.signInUserAgent = text
                        }
                    }
                }

                // ---- About
                Rectangle {
                    id: aboutSection
                    Layout.fillWidth: true
                    implicitHeight: aboutRow.implicitHeight + 32
                    radius: Theme.radiusCard
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1

                    RowLayout {
                        id: aboutRow
                        anchors.fill: parent
                        anchors.margins: 16
                        anchors.leftMargin: 20
                        anchors.rightMargin: 20
                        spacing: 12
                        Image {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            source: "qrc:/vindauga/vindauga.png"
                            sourceSize: Qt.size(72, 72)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                text: qsTr("Vindauga %1").arg(app.version)
                                color: Theme.text
                                font.family: Theme.sans
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                textFormat: Text.StyledText
                                linkColor: Theme.link
                                color: Theme.muted
                                font.family: Theme.sans
                                font.pixelSize: 12
                                text: qsTr("Native Linux client for Azure Virtual Desktop and Windows 365 · <a href=\"https://github.com/ETroll/Vindauga\">github.com/ETroll/Vindauga</a>")
                                onLinkActivated: (link) => Qt.openUrlExternally(link)
                            }
                        }
                    }
                }
            }
        }
    }
}
