import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons

Rectangle {
    id: keycap

    property string text
    property string iconName
    property bool large: false

    readonly property real inset: large ? Theme.space.sm : Theme.space.xs + Theme.space.xxs

    implicitWidth: Math.max(implicitHeight, content.implicitWidth + 2 * inset)
    implicitHeight: large ? Theme.size.keycapLarge : Theme.size.keycap
    radius: Theme.radius.xs
    color: Theme.colors.bgOverlay
    border.width: Theme.size.stroke
    border.color: Theme.colors.strokeDefault

    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: keycap.radius
        anchors.rightMargin: keycap.radius
        height: Theme.size.stroke
        color: Theme.colors.strokeStrong
    }

    Row {
        id: content

        anchors.centerIn: parent
        spacing: Theme.space.xs

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: keycap.iconName !== ""
            name: keycap.iconName
            size: keycap.large ? Theme.size.iconSmall : Theme.size.keyHintTagIcon
            color: keycap.large ? Theme.colors.textPrimary : Theme.colors.textSecondary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: keycap.text
            color: keycap.large ? Theme.colors.textPrimary : Theme.colors.textSecondary
            font.pixelSize: keycap.large ? Theme.type.caption.size : Theme.type.micro.size
            font.weight: keycap.large ? Font.Medium : Theme.type.micro.weight
        }
    }
}
