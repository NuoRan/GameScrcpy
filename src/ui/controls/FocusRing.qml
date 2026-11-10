import QtQuick
import GameScrcpy.Theme

Rectangle {
    property real baseRadius: 0
    readonly property int gap: Theme.size.focusRing

    anchors.fill: parent
    anchors.margins: -2 * gap
    radius: baseRadius + 2 * gap
    color: "transparent"
    border.width: gap
    border.color: Theme.colors.accent
}
