import QtQuick
import GameScrcpy.Theme

Rectangle {
    id: frame

    property bool focused: false
    property bool error: false
    property bool warning: false
    property bool hovered: false
    property bool bare: false

    readonly property bool plain: bare && !focused && !error

    radius: Theme.radius.sm
    color: plain ? "transparent" : Theme.colors.bgSunken
    border.width: Theme.size.stroke
    border.color: {
        if (error)
            return Theme.colors.danger
        if (focused)
            return Theme.colors.accent
        if (warning)
            return Theme.colors.warningLine
        if (plain)
            return hovered ? Theme.colors.strokeDefault : "transparent"
        return hovered ? Theme.colors.strokeStrong : Theme.colors.strokeDefault
    }

    Behavior on border.color {
        ColorAnimation {
            duration: Theme.motion.fast
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.standard
        }
    }

    Rectangle {
        visible: frame.focused || frame.error
        anchors.fill: parent
        anchors.margins: -Theme.size.fieldGlow
        radius: frame.radius + Theme.size.fieldGlow
        color: "transparent"
        border.width: Theme.size.fieldGlow
        border.color: frame.error ? Theme.colors.dangerSubtle : Theme.colors.accentSubtle
    }
}
