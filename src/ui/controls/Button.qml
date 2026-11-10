pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.Button {
    id: control

    property string iconName
    property string tone: "secondary"
    property bool small: false
    property string description
    property string joined
    property string toolTip
    property string shortcut

    readonly property color foreground: {
        if (!enabled)
            return Theme.colors.textDisabled
        switch (tone) {
        case "primary":
            return Theme.colors.textOnAccent
        case "danger":
            return Theme.colors.dangerText
        case "ghost":
            return hovered ? Theme.colors.textPrimary : Theme.colors.textSecondary
        default:
            return Theme.colors.textPrimary
        }
    }

    implicitHeight: small ? Theme.size.controlSmall : Theme.size.control
    implicitWidth: text === "" ? implicitHeight : implicitContentWidth + leftPadding + rightPadding
    horizontalPadding: text === "" ? 0 : small ? Theme.space.md : Theme.space.lg
    verticalPadding: 0
    spacing: small ? Theme.space.xs + Theme.space.xxs : Theme.space.sm
    hoverEnabled: true
    font.pixelSize: small ? Theme.type.caption.size : Theme.type.label.size
    font.weight: tone === "primary" ? Font.DemiBold : Font.Medium

    Accessible.name: text !== "" ? text : description !== "" ? description : toolTip

    Tooltip {
        objectName: "toolTip"
        text: control.toolTip
        shortcut: control.shortcut
        visible: control.toolTip !== "" && control.hovered && !control.down
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Row {
            id: row
            anchors.centerIn: parent
            spacing: control.spacing

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.iconName !== ""
                name: control.iconName
                size: control.text === "" && !control.small ? Theme.size.icon : Theme.size.iconSmall
                color: control.foreground
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.text !== ""
                textFormat: Text.PlainText
                text: control.text
                font: control.font
                color: control.foreground
            }
        }
    }

    background: Rectangle {
        id: face

        radius: Theme.radius.sm
        topLeftRadius: control.joined === "end" ? 0 : radius
        bottomLeftRadius: control.joined === "end" ? 0 : radius
        topRightRadius: control.joined === "start" ? 0 : radius
        bottomRightRadius: control.joined === "start" ? 0 : radius
        color: {
            if (control.tone === "ghost")
                return "transparent"
            if (!control.enabled)
                return Theme.colors.statePress
            switch (control.tone) {
            case "primary":
                return Theme.colors.accent
            case "danger":
                return Theme.colors.dangerSubtle
            default:
                return Theme.colors.bgElevated
            }
        }
        border.width: control.enabled && (control.tone === "secondary" || control.tone === "danger")
                      ? Theme.size.stroke : 0
        border.color: control.tone === "danger" ? Theme.colors.dangerLine : Theme.colors.strokeDefault

        Rectangle {
            visible: control.enabled && control.tone === "secondary"
            x: face.radius
            y: Theme.size.stroke
            width: face.width - 2 * face.radius
            height: Theme.size.stroke
            color: Theme.colors.highlight
        }

        StateLayer {
            anchors.fill: parent
            radius: face.radius
            topLeftRadius: face.topLeftRadius
            bottomLeftRadius: face.bottomLeftRadius
            topRightRadius: face.topRightRadius
            bottomRightRadius: face.bottomRightRadius
            hovered: control.hovered
            pressed: control.down
        }

        FocusRing {
            visible: control.visualFocus
            baseRadius: face.radius
        }
    }
}
