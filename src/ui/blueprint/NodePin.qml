pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme

Item {
    id: side

    required property var pin
    required property bool output
    required property string nodeKey
    property bool readOnly: false
    property string target: ""
    property string valueTag: ""
    property var keyName: null
    readonly property bool exec: (pin.type ?? "") === "exec"
    readonly property bool connected: pin.connected === true
    readonly property bool hasEditor: !output && !connected && (pin.editor ?? "") !== ""
    readonly property bool hasValue: !output && !connected && (pin.editor ?? "") === "" && (pin.value ?? "") !== ""
    readonly property color tone: BlueprintLook.pin(pin.type ?? "")
    readonly property string title: {
        if ((pin.label ?? "") !== "")
            return pin.label
        if (exec)
            return output ? qsTr("Then") : qsTr("Run")
        return pin.name ?? ""
    }
    readonly property real edge: Theme.space.sm + Theme.space.xxs
    readonly property real gap: Theme.space.xs + Theme.space.xxs

    signal handlePressed(int button, int modifiers, point scene)
    signal handleMoved(point scene)
    signal handleReleased(point scene)
    signal committed(var value)
    signal pickRequested(string kind)
    signal choicesRequested(point scene)
    signal hintShown(string text, point scene)
    signal hintHidden()
    signal finished()

    implicitWidth: edge + handle.width + (editorLoader.active ? gap + editorLoader.width : 0)
                   + (valueLoader.active ? gap + valueLoader.width : 0)
    implicitHeight: Theme.size.bpPinRow

    Component {
        id: execGlyph

        ExecPin {
            filled: side.connected
            color: side.target === "ok" ? Theme.colors.accent : side.tone
        }
    }

    Component {
        id: dataGlyph

        Rectangle {
            width: Theme.size.bpPin
            height: width
            radius: width / 2
            color: side.connected ? border.color : "transparent"
            border.width: Theme.size.focusRing
            border.color: side.target === "ok" ? Theme.colors.accent : side.tone
        }
    }

    Item {
        id: handle

        readonly property bool output: side.output

        objectName: "bpPin_" + side.nodeKey + "_" + (side.pin.name ?? "")
        x: side.output ? side.width - side.edge - width : side.edge
        anchors.verticalCenter: parent.verticalCenter
        width: glyphBox.width + (labelLoader.active ? side.gap + labelLoader.width : 0)
        height: Theme.size.bpPinRow

        Accessible.role: Accessible.Button
        Accessible.name: (side.output ? qsTr("Output: %1") : qsTr("Input: %1")).arg(side.title)

        Item {
            id: glyphBox

            x: side.output ? handle.width - width : 0
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.size.bpPin
            height: Theme.size.bpExecHeight
            opacity: side.target !== "" && side.target !== "ok" ? Theme.disabledOpacity : 1

            Rectangle {
                anchors.centerIn: parent
                visible: side.target === "ok"
                width: Theme.size.bpPin + 2 * Theme.size.fieldGlow
                height: width
                radius: width / 2
                color: Theme.colors.accentRing
            }

            Loader {
                anchors.centerIn: parent
                sourceComponent: side.exec ? execGlyph : dataGlyph
            }
        }

        Loader {
            id: labelLoader

            x: side.output ? 0 : glyphBox.width + side.gap
            anchors.verticalCenter: parent.verticalCenter
            active: (side.pin.label ?? "") !== ""

            sourceComponent: Text {
                textFormat: Text.PlainText
                text: side.pin.label ?? ""
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            }
        }

        MouseArea {
            id: handleArea

            anchors.fill: parent
            anchors.leftMargin: side.output ? 0 : -side.edge
            anchors.rightMargin: side.output ? -side.edge : 0
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            preventStealing: true
            cursorShape: side.readOnly ? Qt.ArrowCursor : Qt.CrossCursor
            onPressed: mouse => side.handlePressed(mouse.button, mouse.modifiers,
                                                   handleArea.mapToItem(null, mouse.x, mouse.y))
            onPositionChanged: mouse => side.handleMoved(handleArea.mapToItem(null, mouse.x, mouse.y))
            onReleased: mouse => side.handleReleased(handleArea.mapToItem(null, mouse.x, mouse.y))
        }
    }

    Loader {
        id: editorLoader

        x: handle.x + handle.width + side.gap
        anchors.verticalCenter: parent.verticalCenter
        active: side.hasEditor
        sourceComponent: PinEditor {
            pin: side.pin
            readOnly: side.readOnly
            keyName: side.keyName
            onCommitted: value => side.committed(value)
            onPickRequested: kind => side.pickRequested(kind)
            onChoicesRequested: scene => side.choicesRequested(scene)
            onHintShown: (text, scene) => side.hintShown(text, scene)
            onHintHidden: side.hintHidden()
            onFinished: side.finished()
        }
    }

    Loader {
        id: valueLoader

        x: handle.x + handle.width + side.gap
        anchors.verticalCenter: parent.verticalCenter
        active: side.hasValue

        sourceComponent: Text {
            textFormat: Text.PlainText
            text: side.pin.value ?? ""
            color: Theme.colors.textPrimary
            font.family: Theme.type.monoFamily
            font.pixelSize: Theme.type.micro.size
        }
    }

    Loader {
        x: side.output ? side.width + Theme.space.xs : -width - Theme.space.xs
        anchors.verticalCenter: parent.verticalCenter
        active: side.valueTag !== ""

        sourceComponent: Rectangle {
            objectName: "bpValue_" + side.nodeKey + "_" + (side.pin.name ?? "")
            width: tagText.implicitWidth + 2 * Theme.space.xs + Theme.space.xxs
            height: Theme.size.badge
            radius: Theme.radius.xs
            color: Theme.colors.bpBubble
            border.width: Theme.size.stroke
            border.color: Theme.colors.accentLine

            Text {
                id: tagText

                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: side.valueTag
                color: Theme.colors.textPrimary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.micro.size
            }
        }
    }
}
