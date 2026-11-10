pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme

FocusScope {
    id: control

    property int length: 6
    property alias text: input.text
    property string description
    readonly property bool complete: input.length === length

    signal completed(string code)

    readonly property real groupGap: Theme.space.sm + Theme.space.xs + Theme.space.xxs
    readonly property int half: Math.ceil(length / 2)

    implicitWidth: length * Theme.size.codeCellWidth + (length - 1) * Theme.space.sm + groupGap
    implicitHeight: Theme.size.codeCellHeight
    activeFocusOnTab: true
    opacity: enabled ? 1 : Theme.disabledOpacity

    TextInput {
        id: input

        anchors.fill: parent
        opacity: 0
        focus: true
        maximumLength: control.length
        inputMethodHints: Qt.ImhDigitsOnly
        Accessible.role: Accessible.EditableText
        Accessible.name: control.description

        onTextEdited: {
            const digits = input.text.replace(/[^0-9]/g, "").slice(0, control.length)
            if (digits !== input.text)
                input.text = digits
            if (input.length === control.length)
                control.completed(input.text)
        }
        onCursorPositionChanged: input.cursorPosition = input.length
    }

    Repeater {
        model: control.length

        delegate: Rectangle {
            id: cell

            required property int index
            readonly property bool current: input.activeFocus
                                            && index === Math.min(input.length, control.length - 1)

            x: index * (Theme.size.codeCellWidth + Theme.space.sm) + (index >= control.half ? control.groupGap : 0)
            width: Theme.size.codeCellWidth
            height: Theme.size.codeCellHeight
            radius: Theme.radius.sm
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: current ? Theme.colors.accent : Theme.colors.strokeDefault

            Rectangle {
                anchors.fill: parent
                anchors.margins: -Theme.size.fieldGlow
                radius: cell.radius + Theme.size.fieldGlow
                visible: cell.current
                color: "transparent"
                border.width: Theme.size.fieldGlow
                border.color: Theme.colors.accentSubtle
            }

            Text {
                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: input.text.charAt(cell.index)
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.title.size
                font.weight: Theme.type.title.weight
                font.features: ({ "tnum": 1 })
            }
        }
    }
}
