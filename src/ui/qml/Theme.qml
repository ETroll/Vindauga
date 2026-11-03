pragma Singleton
import QtQuick

// Design tokens from the new UI design (light + dark). Only colours change between the
// schemes; type scale, radii and spacing are shared. Main.qml sets `appearance` from
// AppController (0 = follow the desktop, 1 = light, 2 = dark).
QtObject {
    id: theme

    property int appearance: 0
    readonly property bool dark: appearance === 2
        || (appearance === 0 && Qt.styleHints.colorScheme === Qt.Dark)

    // Colours
    readonly property color ground:      dark ? "#141518" : "#F4F4F1"
    readonly property color surface:     dark ? "#1D1F25" : "#FFFFFF"
    readonly property color bar:         dark ? "#1A1C21" : "#FFFFFF"
    readonly property color border:      dark ? "#2C2F37" : "#E3E2DD"
    readonly property color divider:     dark ? "#2C2F37" : "#EDECE7"
    readonly property color dashed:      dark ? "#3A3E48" : "#D5D4CE"
    readonly property color text:        dark ? "#ECECEA" : "#1A1A1A"
    readonly property color textSoft:    dark ? "#C9CBD0" : "#4A4A46"
    readonly property color muted:       dark ? "#A2A4AB" : "#6B6B66"
    readonly property color accent:      dark ? "#2F6FE4" : "#2A62CC"
    readonly property color accentText:  "#FFFFFF"
    readonly property color link:        dark ? "#8FB3F7" : "#2A62CC"
    readonly property color accentTint:  dark ? "#243352" : "#EEF3FD"
    readonly property color bannerBg:    dark ? "#1B2437" : "#EEF3FD"
    readonly property color bannerBorder: dark ? "#2C3B5C" : "#D2DFF7"
    readonly property color ok:          dark ? "#5CC58A" : "#1F7A4D"
    readonly property color okTint:      dark ? "#1B2E24" : "#E6F4EC"
    readonly property color warn:        dark ? "#E0B45A" : "#9A6400"
    readonly property color warnTint:    dark ? "#2E2715" : "#FBF1DA"
    readonly property color switchOff:   dark ? "#3A3E48" : "#C9C8C2"
    readonly property color hover:       dark ? "#262930" : "#F4F4F1"
    readonly property color scrim:       dark ? Qt.rgba(8/255, 9/255, 12/255, 0.6)
                                              : Qt.rgba(244/255, 244/255, 241/255, 0.72)

    // Geometry
    readonly property int radiusControl: 8
    readonly property int radiusCard: 12
    readonly property int radiusDialog: 14
    readonly property int gutter: 32
    readonly property int contentMax: 1120

    // Type. IBM Plex is used when installed, otherwise the system font.
    // TODO: bundle IBM Plex Sans/Mono (OFL) so the design looks the same everywhere.
    readonly property string sans: Qt.fontFamilies().indexOf("IBM Plex Sans") >= 0
        ? "IBM Plex Sans" : Qt.application.font.family
    readonly property string mono: Qt.fontFamilies().indexOf("IBM Plex Mono") >= 0
        ? "IBM Plex Mono" : "monospace"
}
