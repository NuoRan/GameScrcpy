import QtQuick
import GameScrcpy.Theme

Rectangle {
    property bool hovered: false
    property bool pressed: false

    color: pressed ? Theme.colors.statePress : Theme.colors.stateHover
    opacity: hovered || pressed ? 1 : 0

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.motion.fast
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.standard
        }
    }
}
