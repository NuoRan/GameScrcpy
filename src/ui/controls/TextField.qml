import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme

T.TextField {
    id: control

    property string prefix
    property string suffix
    property string errorText
    property bool mono: false
    property bool dense: false
    property bool bare: false
    property bool showsMessage: true

    readonly property bool hasError: errorText !== ""
    readonly property real messageHeight: hasError && showsMessage ? message.implicitHeight + Theme.space.xs : 0
    readonly property int edge: dense ? Theme.space.sm : Theme.space.md

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.size.control + messageHeight
    leftPadding: edge + (prefix !== "" ? lead.implicitWidth + (dense ? Theme.space.xs : Theme.space.sm) : 0)
    rightPadding: edge + (suffix !== "" ? unit.implicitWidth + (dense ? Theme.space.xs : Theme.space.sm) : 0)
    topPadding: 0
    bottomPadding: messageHeight
    bottomInset: messageHeight
    verticalAlignment: TextInput.AlignVCenter
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity

    color: Theme.colors.textPrimary
    placeholderTextColor: Theme.colors.textTertiary
    selectionColor: Theme.colors.accentLine
    selectedTextColor: Theme.colors.textPrimary
    font.pixelSize: mono ? Theme.type.mono.size : Theme.type.label.size

    Accessible.description: errorText

    Binding {
        target: control
        property: "font.family"
        value: Theme.type.monoFamily
        when: control.mono
    }

    background: FieldFrame {
        implicitWidth: Theme.size.fieldWidth
        focused: control.activeFocus
        error: control.hasError
        hovered: control.hovered
        bare: control.bare
    }

    Text {
        x: control.leftPadding
        y: control.topPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height - control.topPadding - control.bottomPadding
        visible: control.length === 0 && control.preeditText === ""
        textFormat: Text.PlainText
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    Text {
        id: lead

        objectName: "prefix"
        visible: control.prefix !== ""
        x: control.edge
        y: (control.height - control.messageHeight - height) / 2
        textFormat: Text.PlainText
        text: control.prefix
        font.pixelSize: control.dense ? Theme.type.label.size : control.font.pixelSize
        color: Theme.colors.textTertiary

        Binding on font.family {
            when: !control.dense
            value: control.font.family
        }
    }

    Text {
        id: unit

        objectName: "unit"
        visible: control.suffix !== ""
        x: control.width - control.edge - width
        y: (control.height - control.messageHeight - height) / 2
        textFormat: Text.PlainText
        text: control.suffix
        font.pixelSize: control.dense ? Theme.type.label.size : control.font.pixelSize
        color: Theme.colors.textTertiary

        Binding on font.family {
            when: !control.dense
            value: control.font.family
        }
    }

    Text {
        id: message

        visible: control.hasError && control.showsMessage
        y: control.height - control.messageHeight + Theme.space.xs
        width: control.width
        textFormat: Text.PlainText
        text: control.errorText
        color: Theme.colors.dangerText
        font.pixelSize: Theme.type.caption.size
        wrapMode: Text.Wrap
    }
}
