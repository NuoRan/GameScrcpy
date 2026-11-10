import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.AbstractButton {
    id: control

    property string iconName
    property string iconPath
    property bool current: false
    property bool focusShown: false

    implicitWidth: Theme.size.navItemWidth
    implicitHeight: Theme.size.navItemHeight
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    font.pixelSize: Theme.type.micro.size
    font.weight: Font.Medium

    Accessible.role: Accessible.PageTab
    Accessible.selected: current

    contentItem: Item {
        Column {
            anchors.centerIn: parent
            spacing: Theme.space.xs

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: control.iconName
                customPath: control.iconPath
                size: Theme.size.iconLarge
                color: control.current ? Theme.colors.accentText
                     : control.hovered ? Theme.colors.textPrimary : Theme.colors.textSecondary
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                textFormat: Text.PlainText
                text: control.text
                font: control.font
                color: control.current || control.hovered ? Theme.colors.textPrimary : Theme.colors.textSecondary
            }
        }
    }

    background: Rectangle {
        id: face

        radius: Theme.radius.lg
        color: control.current ? Theme.colors.stateHover : "transparent"

        StateLayer {
            anchors.fill: parent
            radius: face.radius
            hovered: control.hovered && !control.current
            pressed: control.down
        }

        FocusRing {
            visible: control.focusShown
            baseRadius: face.radius
        }
    }
}
