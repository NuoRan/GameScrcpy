import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.MenuItem {
    id: control

    property string iconName
    property string shortcut
    property string description
    property bool danger: false
    property bool choice: false
    property bool chosen: false

    readonly property color foreground: {
        if (!enabled)
            return Theme.colors.textDisabled
        return danger ? Theme.colors.dangerText : Theme.colors.textPrimary
    }
    readonly property bool marked: checkable ? checked : choice && chosen
    readonly property string shownIcon: checkable || choice ? (marked ? "check" : "") : iconName

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: description === "" ? Theme.size.menuItem : Theme.size.menuItem + Theme.type.caption.lineHeight
    horizontalPadding: Theme.space.sm + Theme.space.xxs
    verticalPadding: 0
    spacing: Theme.space.sm + Theme.space.xxs
    hoverEnabled: true
    font.pixelSize: Theme.type.label.size

    contentItem: Item {
        implicitWidth: row.implicitWidth + (control.shortcut !== "" ? Theme.space.xxl + keys.implicitWidth : 0)
        implicitHeight: row.implicitHeight

        Row {
            id: row

            anchors.verticalCenter: parent.verticalCenter
            spacing: control.spacing

            Item {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.iconName !== "" || control.checkable || control.choice
                width: Theme.size.iconSmall
                height: Theme.size.iconSmall

                Icon {
                    anchors.fill: parent
                    name: control.shownIcon
                    size: Theme.size.iconSmall
                    color: control.danger ? Theme.colors.dangerText
                         : control.checkable || control.choice ? Theme.colors.accentText : Theme.colors.textSecondary
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    textFormat: Text.PlainText
                    text: control.text
                    font: control.font
                    color: control.foreground
                }

                Text {
                    objectName: "description"
                    visible: control.description !== ""
                    textFormat: Text.PlainText
                    text: control.description
                    color: control.enabled ? Theme.colors.textTertiary : Theme.colors.textDisabled
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }

        Text {
            id: keys

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: control.shortcut !== ""
            textFormat: Text.PlainText
            text: control.shortcut
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }
    }

    background: Rectangle {
        radius: Theme.radius.sm
        color: !control.highlighted ? "transparent"
             : control.visualFocus ? Theme.colors.accentSubtle : Theme.colors.statePress
    }
}
