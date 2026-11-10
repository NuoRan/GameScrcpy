import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.CheckBox {
    id: control

    implicitWidth: text === "" ? Theme.size.checkbox : leftPadding + implicitContentWidth
    implicitHeight: Math.max(Theme.size.checkbox, implicitContentHeight)
    padding: 0
    leftPadding: text === "" ? 0 : Theme.size.checkbox + spacing
    spacing: Theme.space.sm
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
        id: box

        readonly property bool filled: control.checkState !== Qt.Unchecked

        x: 0
        y: (control.height - height) / 2
        implicitWidth: Theme.size.checkbox
        implicitHeight: Theme.size.checkbox
        radius: Theme.radius.xs
        color: filled ? Theme.colors.accent : Theme.colors.bgSunken
        border.width: filled ? 0 : Theme.size.stroke
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
            radius: box.radius
            hovered: control.hovered
            pressed: control.down
        }

        Icon {
            anchors.centerIn: parent
            visible: box.filled
            name: control.checkState === Qt.PartiallyChecked ? "minus" : "check"
            size: Theme.size.checkbox - Theme.space.xs
            color: Theme.colors.textOnAccent
        }

        FocusRing {
            visible: control.visualFocus
            baseRadius: box.radius
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
