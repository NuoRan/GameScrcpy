pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Column {
    id: form

    property var params: []
    property ProfileEditor editor

    signal edited(string key, string json)

    spacing: Theme.space.md

    Repeater {
        model: form.params.length

        delegate: Column {
            id: entry

            required property int index
            readonly property var modelData: index < form.params.length ? form.params[index] : ({})
            readonly property string type: modelData.type ?? ""
            readonly property var value: modelData.value
            readonly property bool inline: type === "toggle"

            function send(next: var) {
                form.edited(entry.modelData.key ?? "", JSON.stringify(next))
            }

            function part(at: int): real {
                return Array.isArray(entry.value) && at < entry.value.length ? Number(entry.value[at]) : 0
            }

            function withPart(at: int, number: real): var {
                const next = Array.isArray(entry.value) ? entry.value.slice() : []
                next[at] = number
                return next
            }

            objectName: "declaredParam_" + (modelData.key ?? "")
            width: form.width
            spacing: Theme.space.xs

            Item {
                width: parent.width
                height: Math.max(paramTitle.implicitHeight, toggle.visible ? toggle.implicitHeight : 0)

                Text {
                    id: paramTitle

                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - (toggle.visible ? toggle.width + Theme.space.md : 0)
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: entry.modelData.title ?? ""
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Theme.type.label.weight
                }

                Switch {
                    id: toggle

                    objectName: "paramSwitch"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: entry.inline
                    checked: entry.value === true
                    Accessible.name: entry.modelData.title ?? ""
                    onToggled: {
                        entry.send(checked)
                        toggle.checked = Qt.binding(() => entry.value === true)
                    }
                }
            }

            Text {
                width: parent.width
                visible: text !== ""
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: entry.modelData.description ?? ""
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Loader {
                width: parent.width
                active: !entry.inline
                visible: active
                sourceComponent: {
                    switch (entry.type) {
                    case "number":
                    case "integer":
                        return numberEditor
                    case "choice":
                        return choiceEditor
                    case "key":
                        return keyEditor
                    case "point":
                    case "region":
                        return placeEditor
                    case "curve":
                        return curveEditor
                    case "color":
                        return colorEditor
                    }
                    return null
                }
            }

            Component {
                id: numberEditor

                Item {
                    id: sliding

                    readonly property NumberScale numbers: NumberScale {
                        minimum: entry.modelData.minimum ?? 0
                        maximum: entry.modelData.maximum ?? 0
                        step: entry.modelData.step ?? 1
                        decimals: entry.modelData.decimals ?? 0
                        logarithmic: entry.modelData.logarithmic ?? false
                    }
                    readonly property bool bounded: entry.modelData.bounded === true
                    readonly property real shown: Number(entry.value ?? 0)

                    implicitHeight: Theme.size.control

                    Slider {
                        id: slider

                        objectName: "paramSlider"
                        visible: sliding.bounded
                        width: parent.width - number.width - Theme.space.md
                        height: parent.height
                        from: 0
                        to: 1
                        showValue: false
                        value: sliding.numbers.position(sliding.shown)
                        Accessible.name: entry.modelData.title ?? ""
                        onMoved: {
                            if (pressed)
                                entry.send(sliding.numbers.valueAt(slider.value))
                        }
                        onPressedChanged: {
                            if (form.editor === null)
                                return
                            if (pressed) {
                                form.editor.beginGesture()
                                return
                            }
                            entry.send(sliding.numbers.valueAt(slider.value))
                            form.editor.endGesture()
                            slider.value = Qt.binding(() => sliding.numbers.position(sliding.shown))
                        }
                    }

                    NumberField {
                        id: number

                        objectName: "paramNumber"
                        x: sliding.bounded ? parent.width - width : 0
                        width: sliding.bounded ? Theme.size.numberField : parent.width
                        numbers: sliding.numbers
                        unit: entry.modelData.unit ?? ""
                        value: slider.pressed ? sliding.numbers.valueAt(slider.value) : sliding.shown
                        showsMessage: false
                        Accessible.name: entry.modelData.title ?? ""
                        onSubmitted: value => entry.send(value)
                    }
                }
            }

            Component {
                id: choiceEditor

                Select {
                    id: choice

                    readonly property var options: entry.modelData.options ?? []

                    function chosen(): int {
                        return choice.options.findIndex(option => option.value === entry.value)
                    }

                    objectName: "paramChoice"
                    model: options.map(option => option.text)
                    currentIndex: chosen()
                    Accessible.name: entry.modelData.title ?? ""
                    onActivated: index => {
                        entry.send(choice.options[index].value)
                        choice.currentIndex = Qt.binding(() => choice.chosen())
                    }
                }
            }

            Component {
                id: keyEditor

                KeyCaptureField {
                    function bind(name: string) {
                        if (name !== "")
                            entry.send(name)
                    }

                    objectName: "paramKey"
                    keys: entry.modelData.keys ?? []
                    clearable: true
                    Accessible.name: entry.modelData.title ?? ""
                    onKeyCaptured: (key, nativeScanCode, modifiers) => {
                        if (form.editor !== null)
                            bind(form.editor.inputOfKey(nativeScanCode, modifiers))
                    }
                    onMouseCaptured: (button, modifiers) => {
                        if (form.editor !== null)
                            bind(form.editor.inputOfMouse(button, modifiers))
                    }
                    onWheelCaptured: (dx, dy, modifiers) => {
                        if (form.editor !== null)
                            bind(form.editor.inputOfWheel(dx, dy, modifiers))
                    }
                    onCleared: entry.send("")
                }
            }

            Component {
                id: placeEditor

                Column {
                    id: place

                    readonly property bool region: entry.type === "region"
                    readonly property bool circle: (entry.modelData.shape ?? "circle") === "circle"
                    readonly property var labels: !region ? [qsTr("X"), qsTr("Y")]
                                                          : circle ? [qsTr("X"), qsTr("Y"), qsTr("Radius")]
                                                                   : [qsTr("X"), qsTr("Y"), qsTr("Width"), qsTr("Height")]

                    spacing: Theme.space.xs

                    Grid {
                        id: boxes

                        columns: 2
                        columnSpacing: Theme.space.sm
                        rowSpacing: Theme.space.sm

                        Repeater {
                            model: place.labels.length

                            delegate: NumberField {
                                id: box

                                required property int index

                                objectName: "paramPart" + index
                                width: (place.width - boxes.columnSpacing) / 2
                                prefix: place.labels[index]
                                value: entry.part(index)
                                numbers: NumberScale {
                                    minimum: box.index < 2 ? -2 : 0
                                    maximum: box.index < 2 ? 2 : 4
                                    step: 0.01
                                    decimals: 2
                                }
                                Accessible.name: (entry.modelData.title ?? "") + " " + place.labels[index]
                                onSubmitted: number => entry.send(entry.withPart(box.index, number))
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        text: qsTr("Drag its dot on the picture, or type an offset from the component in units of the picture's short side.")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }
                }
            }

            Component {
                id: curveEditor

                CurveBox {
                    objectName: "paramCurve"
                    points: Array.isArray(entry.value) ? entry.value : [[0, 0], [1, 1]]
                    Accessible.name: entry.modelData.title ?? ""
                    onEdited: points => entry.send(points)
                }
            }

            Component {
                id: colorEditor

                Item {
                    implicitHeight: Theme.size.control

                    Rectangle {
                        id: swatch

                        objectName: "paramSwatch"
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.size.swatch
                        height: Theme.size.swatch
                        radius: Theme.radius.sm
                        color: typeof entry.value === "string" ? entry.value : Theme.colors.bgSunken
                        border.width: Theme.size.stroke
                        border.color: Theme.colors.strokeStrong
                    }

                    TextField {
                        id: colorText

                        readonly property string shown: typeof entry.value === "string" ? entry.value : ""

                        objectName: "paramColor"
                        x: swatch.width + Theme.space.sm
                        width: parent.width - x
                        dense: true
                        mono: true
                        showsMessage: false
                        maximumLength: 7
                        text: shown
                        Accessible.name: entry.modelData.title ?? ""
                        onEditingFinished: {
                            const typed = text.trim()
                            if (/^#[0-9A-Fa-f]{6}$/.test(typed) && typed.toUpperCase() !== shown.toUpperCase())
                                entry.send(typed.toUpperCase())
                            text = Qt.binding(() => colorText.shown)
                        }
                    }
                }
            }
        }
    }
}
