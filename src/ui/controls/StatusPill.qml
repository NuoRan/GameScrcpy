import QtQuick
import GameScrcpy.Theme

Rectangle {
    id: pill

    property string text
    property string tone: "idle"

    readonly property color foreground: {
        switch (tone) {
        case "live":
            return Theme.colors.accentText
        case "ok":
            return Theme.colors.successText
        case "warn":
            return Theme.colors.warningText
        case "bad":
            return Theme.colors.dangerText
        default:
            return Theme.colors.textSecondary
        }
    }

    implicitWidth: row.implicitWidth + 2 * Theme.space.sm
    implicitHeight: Theme.size.pill
    radius: height / 2
    color: {
        switch (tone) {
        case "live":
            return Theme.colors.accentSubtle
        case "ok":
            return Theme.colors.successSubtle
        case "warn":
            return Theme.colors.warningSubtle
        case "bad":
            return Theme.colors.dangerSubtle
        default:
            return Theme.colors.statePress
        }
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Row {
        id: row

        anchors.centerIn: parent
        spacing: Theme.space.xs + Theme.space.xxs

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.tone !== "idle"
            width: Theme.size.pillDot
            height: width
            radius: width / 2
            color: pill.foreground

            SequentialAnimation on opacity {
                running: pill.tone === "live" && !Theme.reducedMotion && pill.visible && pill.Window.active
                loops: Animation.Infinite
                alwaysRunToEnd: true

                NumberAnimation {
                    to: Theme.motion.pulseLow
                    duration: Theme.motion.pulse
                    easing.type: Easing.InOutSine
                }
                NumberAnimation {
                    to: 1
                    duration: Theme.motion.pulse
                    easing.type: Easing.InOutSine
                }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: pill.text
            color: pill.foreground
            font.pixelSize: Theme.type.micro.size
            font.weight: Theme.type.micro.weight
        }
    }
}
