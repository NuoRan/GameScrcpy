import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.AbstractButton {
    id: control

    property string iconName
    property string iconPath
    property string shortcut
    property bool selected: checked
    property bool compact: false
    property bool alert: false

    readonly property color foreground: {
        if (selected)
            return alert ? Theme.colors.dangerText : Theme.colors.accentText
        return hovered ? Theme.colors.textPrimary : Theme.colors.textSecondary
    }

    implicitWidth: compact ? Theme.size.toolbarChevron : Theme.size.toolbarButton
    implicitHeight: Theme.size.toolbarButton
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1 : Theme.disabledOpacity

    Accessible.name: text
    Accessible.description: shortcut
    Accessible.checkable: checkable || selected
    Accessible.checked: selected

    Tooltip {
        objectName: "toolTip"
        y: control.height + Theme.space.xs
        text: control.shortcut !== "" ? qsTr("%1 (%2)").arg(control.text).arg(control.shortcut) : control.text
        visible: control.text !== "" && control.hovered && !control.down
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            customPath: control.iconPath
            size: control.compact ? Theme.size.iconSmall : Theme.size.icon
            color: control.foreground
        }
    }

    background: Rectangle {
        id: face

        radius: Math.min(width, height) / 2
        color: control.selected ? (control.alert ? Theme.colors.dangerSubtle : Theme.colors.accentSubtle) : "transparent"

        StateLayer {
            anchors.fill: parent
            radius: face.radius
            hovered: control.hovered
            pressed: control.down
        }

        FocusRing {
            visible: control.visualFocus
            baseRadius: face.radius
        }
    }
}
