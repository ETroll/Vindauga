import QtQuick

// Stroke icon from an SVG path on the design's 24x24 grid (2 px round stroke). Rendered
// through Qt's SVG image handler from an inline data URI, so the stroke follows the theme
// colour and no image files are needed. Rasterised at twice the device pixel ratio.
Item {
    id: icon

    property string path
    property color color: Theme.text
    property int size: 16

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    Image {
        anchors.fill: parent
        sourceSize: Qt.size(icon.size * Screen.devicePixelRatio * 2,
                            icon.size * Screen.devicePixelRatio * 2)
        smooth: true
        fillMode: Image.PreserveAspectFit
        source: "data:image/svg+xml;utf8,"
            + "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='"
            + icon.color.toString().replace("#", "%23").substring(0, 10)
            + "' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='"
            + icon.path + "'/></svg>"
    }
}
