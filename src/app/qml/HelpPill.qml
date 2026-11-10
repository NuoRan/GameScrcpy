import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons

Rectangle {
    id: pill

    property string iconName
    property string text
    property bool hot: false

    implicitWidth: row.implicitWidth + 2 * Theme.space.sm
    implicitHeight: Theme.size.badge + Theme.space.xs
    radius: height / 2
    color: pill.hot ? Theme.colors.accentSubtle : Theme.colors.bgOverlay
    border.width: Theme.size.stroke
    border.color: pill.hot ? Theme.colors.accentLine : Theme.colors.strokeDefault

    Row {
        id: row

        anchors.centerIn: parent
        spacing: Theme.space.xs

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.iconName !== ""
            name: pill.iconName
            size: Theme.size.infoIcon
            color: pill.hot ? Theme.colors.accentText : Theme.colors.textSecondary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.text !== ""
            textFormat: Text.PlainText
            text: pill.text
            color: pill.hot ? Theme.colors.accentText : Theme.colors.textSecondary
            font.pixelSize: Theme.type.micro.size
            font.weight: Font.Medium
        }
    }
}
