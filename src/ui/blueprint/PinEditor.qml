pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

Item {
    id: editor

    required property var pin
    property bool readOnly: false
    property var keyName: null
    property bool typing: false
    property bool capturing: false
    property int pending: 0
    readonly property string kind: pin.editor ?? ""
    readonly property bool editing: typing || capturing
    readonly property bool picks: kind === "point" || kind === "region" || kind === "image"
    readonly property bool typed: kind === "number" || kind === "duration" || kind === "text" || kind === "code"
    readonly property string shown: {
        if (capturing)
            return qsTr("Press a key…")
        if ((pin.value ?? "") !== "")
            return pin.value
        if (kind === "toggle")
            return pin.raw === true ? qsTr("Yes") : qsTr("No")
        return ""
    }

    signal committed(var value)
    signal pickRequested(string kind)
    signal choicesRequested(point scene)
    signal hintShown(string text, point scene)
    signal hintHidden()
    signal finished()

    function decimalsOf(step: real): int {
        if (!(step > 0))
            return 3
        const text = String(step)
        const dot = text.indexOf(".")
        return dot < 0 ? 0 : text.length - dot - 1
    }

    function wholeStep(step: var): bool {
        return typeof step === "number" && step >= 1 && Number.isInteger(step)
    }

    function startTyping() {
        if (readOnly)
            return
        let start = ""
        if (kind === "number")
            start = typeof pin.raw === "number" ? String(pin.raw) : ""
        else if (kind === "duration")
            start = typeof pin.raw === "number" ? String(pin.raw / 1000) : ""
        else
            start = typeof pin.raw === "string" ? pin.raw : (pin.value ?? "")
        input.text = start
        typing = true
        input.selectAll()
        input.forceActiveFocus(Qt.MouseFocusReason)
    }

    function stopTyping(keep: bool) {
        if (!typing)
            return
        typing = false
        if (keep) {
            const text = input.text
            if (kind === "number" || kind === "duration") {
                const number = numbers.parse(text)
                if (!Number.isNaN(number)) {
                    const value = kind === "duration" ? Math.round(number * 1000) : number
                    const low = typeof pin.min === "number" ? pin.min : -Infinity
                    const high = typeof pin.max === "number" ? pin.max : Infinity
                    if (value >= low && value <= high && value !== pin.raw)
                        committed(value)
                }
            } else if (text !== (pin.raw ?? "")) {
                committed(text)
            }
        }
        finished()
    }

    function activate() {
        if (readOnly)
            return
        switch (kind) {
        case "toggle":
            committed(pin.raw !== true)
            finished()
            break
        case "choice":
        case "component":
            choicesRequested(chip.mapToItem(null, 0, chip.height))
            break
        case "image":
            if ((pin.choices ?? []).length > 0)
                choicesRequested(thumb.mapToItem(null, 0, thumb.height))
            else
                pickRequested(kind)
            break
        case "key":
            capturing = true
            pending = 0
            chip.forceActiveFocus(Qt.MouseFocusReason)
            break
        case "point":
        case "region":
            if ((pin.choices ?? []).length > 0)
                choicesRequested(chip.mapToItem(null, 0, chip.height))
            else
                pickRequested(kind)
            break
        default:
            if (typed)
                startTyping()
        }
    }

    function capture(found: var) {
        capturing = false
        pending = 0
        const naming = editor.keyName
        const name = typeof naming === "function" ? naming(found) : BlueprintLook.keyName(found)
        if (name !== "" && name !== pin.raw)
            committed(name)
        finished()
    }

    implicitWidth: row.implicitWidth
    implicitHeight: kind === "image" ? Theme.size.bpThumbHeight : Theme.size.bpChip

    NumberScale {
        id: numbers

        decimals: editor.kind === "duration" ? 3 : editor.decimalsOf(editor.pin.step ?? 0)
        exact: editor.kind === "number" && !editor.wholeStep(editor.pin.step)
    }

    Row {
        id: row

        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space.xxs

        Rectangle {
            id: thumb

            objectName: "pinThumb"
            visible: editor.kind === "image"
            width: Theme.size.bpThumbWidth
            height: Theme.size.bpThumbHeight
            radius: Theme.radius.xs
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: thumbArea.containsMouse && !editor.readOnly ? Theme.colors.textTertiary
                                                                      : Theme.colors.strokeStrong

            Image {
                anchors.fill: parent
                anchors.margins: Theme.size.stroke
                visible: (editor.pin.preview ?? "") !== ""
                source: editor.pin.preview ?? ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - Theme.space.sm
                visible: (editor.pin.preview ?? "") === ""
                textFormat: Text.PlainText
                text: editor.shown
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.micro.size
                font.weight: Theme.type.micro.weight
            }

            MouseArea {
                id: thumbArea

                anchors.fill: parent
                enabled: !editor.readOnly
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: editor.activate()
            }
        }

        Rectangle {
            id: chip

            objectName: "pinChip"
            anchors.verticalCenter: parent.verticalCenter
            visible: editor.kind !== "image"
            width: Math.min(Theme.size.bpChipMax,
                            Math.max(Theme.size.bpChip, (editor.typing ? input.contentWidth : label.implicitWidth)
                                     + 2 * label.x))
            height: Theme.size.bpChip
            radius: Theme.radius.xs
            color: Theme.colors.bpChip
            border.width: Theme.size.stroke
            border.color: editor.editing ? Theme.colors.accent
                                         : chipArea.containsMouse && !editor.readOnly ? Theme.colors.strokeStrong
                                                                                      : Theme.colors.strokeDefault

            Keys.onShortcutOverride: event => event.accepted = editor.capturing
            Keys.onPressed: event => {
                if (!editor.capturing)
                    return
                event.accepted = true
                if (event.isAutoRepeat)
                    return
                if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift || event.key === Qt.Key_Alt
                        || event.key === Qt.Key_Meta) {
                    editor.pending = event.key
                    return
                }
                editor.capture({ kind: "key", key: event.key, scanCode: event.nativeScanCode,
                                 modifiers: event.modifiers })
            }
            Keys.onReleased: event => {
                if (!editor.capturing || event.isAutoRepeat || editor.pending === 0 || event.key !== editor.pending)
                    return
                event.accepted = true
                editor.capture({ kind: "key", key: editor.pending, scanCode: event.nativeScanCode,
                                 modifiers: Qt.NoModifier })
            }
            onActiveFocusChanged: {
                if (!activeFocus && editor.capturing) {
                    editor.capturing = false
                    editor.pending = 0
                }
            }

            Text {
                id: label

                x: Theme.space.xs + Theme.space.xxs
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter
                visible: !editor.typing
                textFormat: Text.PlainText
                text: editor.shown
                elide: Text.ElideRight
                color: editor.capturing ? Theme.colors.accentText : Theme.colors.textPrimary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.micro.size
            }

            TextInput {
                id: input

                objectName: "pinInput"
                x: label.x
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter
                visible: editor.typing
                clip: true
                selectByMouse: true
                color: Theme.colors.textPrimary
                selectionColor: Theme.colors.accentLine
                selectedTextColor: Theme.colors.textPrimary
                font.family: Theme.type.monoFamily
                font.pixelSize: Theme.type.micro.size
                inputMethodHints: editor.kind === "number" || editor.kind === "duration"
                                  ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
                onAccepted: editor.stopTyping(true)
                onActiveFocusChanged: {
                    if (!activeFocus)
                        editor.stopTyping(true)
                }
                Keys.onEscapePressed: editor.stopTyping(false)
            }

            MouseArea {
                id: chipArea

                anchors.fill: parent
                enabled: !editor.typing
                hoverEnabled: true
                acceptedButtons: editor.capturing ? Qt.AllButtons : Qt.LeftButton
                cursorShape: editor.readOnly ? Qt.ArrowCursor : Qt.PointingHandCursor
                onPressed: mouse => {
                    if (editor.capturing) {
                        editor.capture({ kind: "mouse", button: mouse.button, modifiers: mouse.modifiers })
                        return
                    }
                    if (editor.readOnly)
                        mouse.accepted = false
                }
                onClicked: {
                    if (!editor.capturing)
                        editor.activate()
                }
                onWheel: wheel => {
                    if (!editor.capturing) {
                        wheel.accepted = false
                        return
                    }
                    editor.capture({ kind: "wheel", dx: wheel.angleDelta.x, dy: wheel.angleDelta.y,
                                     modifiers: wheel.modifiers })
                }
            }
        }

        T.AbstractButton {
            id: pick

            readonly property string tip: editor.kind === "point" ? qsTr("Pick on the picture")
                                        : editor.kind === "region" ? qsTr("Frame on the picture")
                                                                   : qsTr("Frame the picture on the screen")

            objectName: "pinPick"
            anchors.verticalCenter: parent.verticalCenter
            visible: editor.picks && !editor.readOnly
            width: Theme.size.bpChip
            height: Theme.size.bpChip
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            Accessible.name: pick.tip
            onClicked: editor.pickRequested(editor.kind)
            onHoveredChanged: {
                if (pick.hovered)
                    editor.hintShown(pick.tip, pick.mapToItem(null, pick.width / 2, 0))
                else
                    editor.hintHidden()
            }

            contentItem: Item {
                Icon {
                    anchors.centerIn: parent
                    name: editor.kind === "point" ? "crosshair" : "expand"
                    size: Theme.size.iconSmall - Theme.space.xxs
                    color: pick.hovered ? Theme.colors.textPrimary : Theme.colors.textSecondary
                }
            }

            background: StateLayer {
                radius: Theme.radius.xs
                hovered: pick.hovered
                pressed: pick.down
            }
        }
    }
}
