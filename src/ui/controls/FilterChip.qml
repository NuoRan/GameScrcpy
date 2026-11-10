import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.AbstractButton {
    id: control

    property int count: -1
    property string iconName
    property bool selected: false

    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Theme.size.chip
    leftPadding: Theme.space.md
    rightPadding: Theme.space.md
    spacing: Theme.space.xs + Theme.space.xxs
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity

    Accessible.role: Accessible.RadioButton
    Accessible.name: count >= 0 ? text + " " + count : text
    Accessible.checked: selected

    contentItem: Row {
        spacing: control.spacing

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.iconName !== ""
            name: control.iconName
            size: Theme.size.infoIcon
            color: control.selected ? Theme.colors.accentText : Theme.colors.textSecondary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: control.text
            color: control.selected ? Theme.colors.accentText : Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
            font.weight: control.selected ? Font.DemiBold : Font.Normal
        }

        Text {
            objectName: "count"
            anchors.verticalCenter: parent.verticalCenter
            visible: control.count >= 0
            textFormat: Text.PlainText
            text: control.count
            color: control.selected ? Theme.colors.accentText : Theme.colors.textTertiary
            font.pixelSize: Theme.type.micro.size
            font.features: ({ "tnum": 1 })
        }
    }

    background: Rectangle {
        id: face

        radius: height / 2
        color: control.selected ? Theme.colors.accentSubtle : "transparent"
        border.width: control.selected ? 0 : Theme.size.stroke
        border.color: Theme.colors.strokeDefault

        StateLayer {
            anchors.fill: parent
            radius: face.radius
            visible: !control.selected
            hovered: control.hovered
            pressed: control.down
        }

        FocusRing {
            visible: control.visualFocus
            baseRadius: face.radius
        }
    }
}
