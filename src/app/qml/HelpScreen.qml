import QtQuick
import GameScrcpy.Theme

Rectangle {
    id: frame

    property string kind: "window"
    property string scene: "fps"
    property bool dim: false
    default property alias content: screen.data
    readonly property real bezel: frame.kind === "phone" ? Theme.space.xs
                                : frame.kind === "monitor" ? Theme.space.xs - Theme.size.stroke
                                : Theme.size.stroke

    radius: frame.kind === "phone" ? Theme.radius.xl : frame.kind === "monitor" ? Theme.radius.sm : Theme.radius.lg
    color: frame.kind === "window" ? "#000000" : Theme.colors.bgElevated
    border.width: Theme.size.stroke
    border.color: Theme.colors.strokeStrong

    Rectangle {
        id: screen

        anchors.fill: parent
        anchors.margins: frame.bezel
        radius: Math.max(0, frame.radius - frame.bezel)
        color: "#000000"
        clip: true

        HelpScene {
            anchors.fill: parent
            kind: frame.scene
            visible: frame.scene !== ""
        }

        Rectangle {
            anchors.fill: parent
            visible: frame.dim
            color: Theme.colors.canvasDim
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.bottom
        visible: frame.kind === "monitor"
        width: Math.max(Theme.space.xl, parent.width * 0.4)
        height: Theme.space.sm + Theme.space.xxs
        radius: Theme.radius.xs
        color: Theme.colors.bgElevated
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong
    }
}
