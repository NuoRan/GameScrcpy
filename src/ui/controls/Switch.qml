import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme

T.Switch {
    id: control

    implicitWidth: text === "" ? Theme.size.switchWidth : leftPadding + implicitContentWidth
    implicitHeight: Math.max(Theme.size.switchHeight, implicitContentHeight)
    padding: 0
    leftPadding: text === "" ? 0 : Theme.size.switchWidth + spacing
    spacing: Theme.space.md
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity
    font.pixelSize: Theme.type.label.size

    Accessible.onToggleAction: {
        if (!control.enabled)
            return
        control.toggle()
        control.toggled()
    }
    Accessible.onPressAction: {
        if (!control.enabled)
            return
        control.toggle()
        control.toggled()
    }

    indicator: Rectangle {
        id: track

        x: 0
        y: (control.height - height) / 2
        implicitWidth: Theme.size.switchWidth
        implicitHeight: Theme.size.switchHeight
        radius: height / 2
        color: control.checked ? Theme.colors.accent : Theme.colors.bgSunken
        border.width: control.checked ? 0 : Theme.size.stroke
        border.color: Theme.colors.strokeStrong

        Behavior on color {
            ColorAnimation {
                duration: Theme.motion.fast
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
        }

        StateLayer {
            anchors.fill: parent
            radius: track.radius
            hovered: control.hovered
            pressed: control.down
        }

        Rectangle {
            readonly property real inset: (track.height - height) / 2

            x: inset + control.visualPosition * (track.width - width - 2 * inset)
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.size.switchKnob
            height: width
            radius: width / 2
            color: control.checked ? Theme.colors.textOnAccent : Theme.colors.textSecondary

            Behavior on x {
                enabled: !control.down
                NumberAnimation {
                    duration: Theme.motion.base
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: Theme.motion.emphasized
                }
            }
        }

        FocusRing {
            visible: control.visualFocus
            baseRadius: track.radius
        }
    }

    contentItem: Text {
        visible: control.text !== ""
        textFormat: Text.PlainText
        text: control.text
        font: control.font
        color: Theme.colors.textPrimary
        verticalAlignment: Text.AlignVCenter
    }
}
