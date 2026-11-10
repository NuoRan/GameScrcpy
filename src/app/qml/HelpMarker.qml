import QtQuick
import GameScrcpy.Theme

Rectangle {
    id: marker

    property int number: 1

    width: Theme.size.helpMarker + 2 * Theme.space.xs
    height: width
    radius: width / 2
    color: Qt.rgba(Theme.colors.bgSunken.r, Theme.colors.bgSunken.g, Theme.colors.bgSunken.b, 0.75)

    Accessible.ignored: true

    Rectangle {
        anchors.centerIn: parent
        width: Theme.size.helpMarker
        height: width
        radius: width / 2
        color: Theme.colors.accent

        Text {
            anchors.centerIn: parent
            textFormat: Text.PlainText
            text: marker.number
            color: Theme.colors.textOnAccent
            font.pixelSize: Theme.type.caption.size
            font.weight: Font.Bold
            font.features: ({ "tnum": 1 })
        }
    }
}
