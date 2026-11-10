import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons

T.TextField {
    id: control

    property string shortcut

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.size.search
    leftPadding: Theme.space.md + Theme.size.iconSmall + Theme.space.sm
    rightPadding: Theme.space.xs + Math.max(clear.visible ? clear.width : 0,
                                            hint.visible ? hint.width + Theme.space.xs : 0) + Theme.space.xs
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity

    color: Theme.colors.textPrimary
    placeholderTextColor: Theme.colors.textTertiary
    selectionColor: Theme.colors.accentLine
    selectedTextColor: Theme.colors.textPrimary
    font.pixelSize: Theme.type.label.size

    Accessible.role: Accessible.EditableText
    Accessible.searchEdit: true

    background: FieldFrame {
        implicitWidth: Theme.size.fieldWidth
        radius: Theme.radius.md
        focused: control.activeFocus
        hovered: control.hovered
    }

    Icon {
        x: Theme.space.md
        anchors.verticalCenter: parent.verticalCenter
        name: "search"
        size: Theme.size.iconSmall
        color: Theme.colors.textTertiary
    }

    Text {
        x: control.leftPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height
        visible: control.length === 0 && control.preeditText === ""
        textFormat: Text.PlainText
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    Keycap {
        id: hint

        visible: control.shortcut !== "" && control.length === 0 && !control.activeFocus
        anchors.right: parent.right
        anchors.rightMargin: Theme.space.sm
        anchors.verticalCenter: parent.verticalCenter
        text: control.shortcut
    }

    Button {
        id: clear

        objectName: "clear"
        visible: control.length > 0
        anchors.right: parent.right
        anchors.rightMargin: Theme.space.xs
        anchors.verticalCenter: parent.verticalCenter
        tone: "ghost"
        small: true
        iconName: "x"
        description: qsTr("Clear")
        focusPolicy: Qt.NoFocus
        onClicked: {
            control.clear()
            control.forceActiveFocus(Qt.MouseFocusReason)
        }
    }
}
