pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls
import GameScrcpy.App

Item {
    id: row

    required property var fields
    required property bool first
    required property bool last
    required property bool separated
    property ProfileEditor editor
    property string keyId

    readonly property real topSpace: first ? Theme.space.lg : Theme.space.md + Theme.space.xxs
    readonly property real gap: Theme.space.sm

    function write(key: string, value: var): bool {
        return editor !== null && keyId !== "" && key !== "" && editor.setField(keyId, key, value)
    }

    implicitHeight: topSpace + cells.height + (last ? Theme.space.lg : 0)
    height: implicitHeight

    Rectangle {
        visible: row.separated
        width: parent.width
        height: Theme.size.stroke
        color: Theme.colors.strokeSubtle
    }

    Row {
        id: cells

        x: Theme.space.lg
        y: row.topSpace
        width: row.width - 2 * Theme.space.lg
        spacing: row.gap

        Repeater {
            model: row.fields.length

            delegate: Item {
                id: cell

                required property int index
                readonly property var field: index < row.fields.length ? row.fields[index] : ({})
                readonly property string widget: field.widget ?? ""
                readonly property bool inline: widget === "switch"
                readonly property bool bare: widget === "note"
                readonly property real headingGap: Theme.space.xs + Theme.space.xxs

                function bind(input: string) {
                    if (input !== "" && row.editor !== null && row.keyId !== "")
                        row.editor.setKey(row.keyId, field.bindPath ?? "", input)
                }

                objectName: "field_" + (field.key ?? "")
                width: row.fields.length > 1 ? (cells.width - cells.spacing) / 2 : cells.width
                height: inline ? Math.max(heading.height, body.height)
                               : bare ? body.height : heading.height + headingGap + body.height

                Item {
                    id: heading

                    visible: !cell.bare
                    y: cell.inline ? (cell.height - height) / 2 : 0
                    width: cell.inline ? cell.width - body.width - Theme.space.md : cell.width
                    height: cell.bare ? 0 : Math.max(titleRow.implicitHeight, hint.implicitHeight)

                    Row {
                        id: titleRow

                        width: parent.width - (hint.visible ? hint.implicitWidth + Theme.space.sm : 0)
                        spacing: Theme.space.xs

                        Text {
                            id: title

                            width: Math.min(implicitWidth, titleRow.width - (infoButton.visible ? infoButton.width
                                                                                                   + titleRow.spacing
                                                                                                 : 0))
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: cell.field.title ?? ""
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                            font.weight: Theme.type.label.weight
                        }

                        InfoButton {
                            id: infoButton

                            objectName: "fieldInfo"
                            anchors.verticalCenter: parent.verticalCenter
                            visible: (cell.field.info ?? "") !== ""
                            title: cell.field.title ?? ""
                            info: cell.field.info ?? ""
                        }
                    }

                    Text {
                        id: hint

                        anchors.right: parent.right
                        anchors.verticalCenter: titleRow.verticalCenter
                        visible: text !== ""
                        textFormat: Text.PlainText
                        text: cell.field.hint ?? ""
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }
                }

                Loader {
                    id: body

                    x: cell.inline ? cell.width - width : 0
                    y: cell.inline ? (cell.height - height) / 2 : cell.bare ? 0 : heading.height + cell.headingGap
                    width: cell.inline ? implicitWidth : cell.width
                    sourceComponent: {
                        switch (cell.widget) {
                        case "boxes":
                            return boxesWidget
                        case "slider":
                            return sliderWidget
                        case "number":
                            return numberWidget
                        case "segmented":
                            return segmentedWidget
                        case "select":
                            return selectWidget
                        case "switch":
                            return switchWidget
                        case "key":
                            return keyWidget
                        case "rhythm":
                            return rhythmWidget
                        case "note":
                            return noteWidget
                        case "text":
                            return textWidget
                        }
                        return null
                    }
                }

                Component {
                    id: textWidget

                    TextField {
                        objectName: "text"
                        text: cell.field.value ?? ""
                        maximumLength: 255
                        Accessible.name: cell.field.title ?? ""
                        onEditingFinished: row.write(cell.field.key ?? "", text)
                    }
                }

                Component {
                    id: noteWidget

                    Item {
                        objectName: "note"
                        implicitHeight: Math.max(noteIcon.height, noteText.implicitHeight)

                        Icon {
                            id: noteIcon

                            y: (noteText.implicitHeight / Math.max(1, noteText.lineCount) - height) / 2
                            name: "info"
                            size: Theme.size.iconSmall
                            color: Theme.colors.textTertiary
                        }

                        Text {
                            id: noteText

                            x: noteIcon.width + Theme.space.sm
                            width: parent.width - x
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            text: cell.field.value ?? ""
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                        }
                    }
                }

                Component {
                    id: boxesWidget

                    Grid {
                        id: grid

                        columns: 2
                        columnSpacing: row.gap
                        rowSpacing: row.gap

                        Repeater {
                            model: (cell.field.boxes ?? []).length

                            delegate: NumberField {
                                id: box

                                required property int index
                                readonly property var specs: cell.field.boxes ?? []
                                readonly property var spec: index < specs.length ? specs[index] : ({})

                                objectName: "box_" + (spec.key ?? "")
                                width: (grid.width - grid.columnSpacing) / 2
                                prefix: spec.prefix ?? ""
                                unit: spec.unit ?? ""
                                value: spec.value ?? 0
                                numbers: NumberScale {
                                    minimum: box.spec.minimum ?? 0
                                    maximum: box.spec.maximum ?? 0
                                    step: box.spec.step ?? 1
                                    decimals: box.spec.decimals ?? 0
                                }
                                Accessible.name: (cell.field.title ?? "") + " " + (spec.prefix ?? "")
                                onSubmitted: number => row.write(box.spec.key ?? "", number)
                            }
                        }
                    }
                }

                Component {
                    id: sliderWidget

                    Item {
                        id: sliding

                        readonly property NumberScale numbers: NumberScale {
                            minimum: cell.field.minimum ?? 0
                            maximum: cell.field.maximum ?? 0
                            from: cell.field.from ?? 0
                            to: cell.field.to ?? 0
                            step: cell.field.step ?? 1
                            decimals: cell.field.decimals ?? 0
                            logarithmic: cell.field.logarithmic ?? false
                        }
                        readonly property real shown: cell.field.value ?? 0
                        readonly property real dragged: numbers.valueAt(slider.value)
                        readonly property string unit: cell.field.unit ?? ""

                        function store(value: real) {
                            row.write(cell.field.key ?? "", value)
                        }

                        function rebind() {
                            slider.value = Qt.binding(() => sliding.numbers.position(sliding.shown))
                        }

                        implicitHeight: Theme.size.control

                        Slider {
                            id: slider

                            objectName: "slider"
                            width: parent.width - number.width - Theme.space.md
                            height: parent.height
                            from: 0
                            to: 1
                            showValue: false
                            value: sliding.numbers.position(sliding.shown)
                            valueText: sliding.unit === "" ? sliding.numbers.text(sliding.dragged)
                                                           : sliding.numbers.text(sliding.dragged) + " " + sliding.unit
                            Accessible.name: cell.field.title ?? ""
                            onMoved: {
                                if (pressed)
                                    sliding.store(sliding.dragged)
                            }
                            onPressedChanged: {
                                if (row.editor === null)
                                    return
                                if (pressed) {
                                    row.editor.beginGesture()
                                    return
                                }
                                sliding.store(sliding.dragged)
                                row.editor.endGesture()
                                sliding.rebind()
                            }
                            Keys.onPressed: event => {
                                const direction = event.key === Qt.Key_Right || event.key === Qt.Key_Up ? 1
                                                : event.key === Qt.Key_Left || event.key === Qt.Key_Down ? -1 : 0
                                if (direction === 0)
                                    return
                                event.accepted = true
                                sliding.store(sliding.numbers.stepped(sliding.shown, direction))
                                sliding.rebind()
                            }
                        }

                        NumberField {
                            id: number

                            objectName: "sliderNumber"
                            x: parent.width - width
                            numbers: sliding.numbers
                            unit: sliding.unit
                            value: slider.pressed ? sliding.dragged : sliding.shown
                            showsMessage: false
                            Accessible.name: cell.field.title ?? ""
                            onSubmitted: value => sliding.store(value)
                        }
                    }
                }

                Component {
                    id: numberWidget

                    Item {
                        implicitHeight: plain.implicitHeight

                        NumberField {
                            id: plain

                            objectName: "number"
                            width: cell.field.half === true ? parent.width : (parent.width - row.gap) / 2
                            unit: cell.field.unit ?? ""
                            value: cell.field.value ?? 0
                            word: cell.field.word ?? ""
                            automatic: word !== ""
                            numbers: NumberScale {
                                minimum: cell.field.minimum ?? 0
                                maximum: cell.field.maximum ?? 0
                                step: cell.field.step ?? 1
                                decimals: cell.field.decimals ?? 0
                            }
                            Accessible.name: cell.field.title ?? ""
                            onSubmitted: value => row.write(cell.field.key ?? "", value)
                        }
                    }
                }

                Component {
                    id: segmentedWidget

                    Item {
                        implicitHeight: segments.implicitHeight

                        SegmentedControl {
                            id: segments

                            readonly property var options: cell.field.options ?? []

                            function chosen(): int {
                                return segments.options.findIndex(option => option.value === cell.field.value)
                            }

                            objectName: "segments"
                            model: options.map(option => option.text)
                            currentIndex: chosen()
                            Accessible.name: cell.field.title ?? ""
                            onActivated: index => {
                                row.write(cell.field.key ?? "", segments.options[index].value)
                                segments.currentIndex = Qt.binding(() => segments.chosen())
                            }
                        }
                    }
                }

                Component {
                    id: selectWidget

                    Select {
                        id: choice

                        readonly property var options: cell.field.options ?? []

                        function chosen(): int {
                            return choice.options.findIndex(option => option.value === cell.field.value)
                        }

                        objectName: "select"
                        model: options.map(option => option.text)
                        currentIndex: chosen()
                        Accessible.name: cell.field.title ?? ""
                        onActivated: index => {
                            row.write(cell.field.key ?? "", choice.options[index].value)
                            choice.currentIndex = Qt.binding(() => choice.chosen())
                        }
                    }
                }

                Component {
                    id: switchWidget

                    Switch {
                        id: toggle

                        objectName: "switch"
                        checked: cell.field.value === true
                        enabled: cell.field.enabled !== false
                        Accessible.name: cell.field.title ?? ""
                        onToggled: {
                            row.write(cell.field.key ?? "", toggle.checked)
                            toggle.checked = Qt.binding(() => cell.field.value === true)
                        }
                    }
                }

                Component {
                    id: keyWidget

                    KeyCaptureField {
                        id: keyField

                        objectName: "keyField"
                        keys: cell.field.keys ?? []
                        conflictText: cell.field.conflict ?? ""
                        clearable: (cell.field.clearPath ?? "") !== ""
                        Accessible.name: cell.field.title ?? ""
                        onKeyCaptured: (key, nativeScanCode, modifiers) => cell.bind(row.editor.inputOfKey(nativeScanCode, modifiers))
                        onMouseCaptured: (button, modifiers) => cell.bind(row.editor.inputOfMouse(button, modifiers))
                        onWheelCaptured: (dx, dy, modifiers) => cell.bind(row.editor.inputOfWheel(dx, dy, modifiers))
                        onCleared: {
                            if (row.editor !== null && row.keyId !== "" && (cell.field.clearPath ?? "") !== "")
                                row.editor.clearKey(row.keyId, cell.field.clearPath)
                        }

                        Connections {
                            target: HardwareAccess
                            enabled: keyField.capturing

                            function onInputSeen(name: string) {
                                keyField.cancel()
                                cell.bind(name)
                            }
                        }
                    }
                }

                Component {
                    id: rhythmWidget

                    Rectangle {
                        id: rhythm

                        readonly property int count: Math.max(1, cell.field.value ?? 1)
                        readonly property real inset: Theme.space.sm + Theme.space.xxs

                        objectName: "rhythm"
                        implicitHeight: Theme.size.control
                        radius: Theme.radius.sm
                        color: Theme.colors.bgSunken
                        border.width: Theme.size.stroke
                        border.color: Theme.colors.strokeDefault
                        Accessible.name: cell.field.hint ?? ""

                        Repeater {
                            model: rhythm.count

                            delegate: Rectangle {
                                required property int index

                                x: rhythm.count > 1
                                   ? rhythm.inset + index * (rhythm.width - 2 * rhythm.inset - width) / (rhythm.count - 1)
                                   : (rhythm.width - width) / 2
                                y: (rhythm.height - height) / 2
                                width: 2 * Theme.size.stroke
                                height: Theme.space.md
                                radius: Theme.size.stroke
                                color: Theme.colors.textSecondary
                            }
                        }
                    }
                }
            }
        }
    }
}
