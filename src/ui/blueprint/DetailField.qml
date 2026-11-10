pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

Column {
    id: field

    required property var spec
    property var keyName: null
    readonly property string kind: spec.kind ?? "readonly"
    readonly property string name: spec.name ?? ""
    readonly property bool inline: kind === "toggle"
    readonly property var choices: spec.choices ?? []
    readonly property bool unset: spec.isDefault === true

    signal edited(var value)

    function choiceIndex(): int {
        for (let index = 0; index < choices.length; ++index) {
            if (choices[index].value === spec.value)
                return index
        }
        return -1
    }

    function decimalsOf(step: real, fallback: int): int {
        if (!(step > 0))
            return fallback
        const text = String(step)
        const dot = text.indexOf(".")
        return dot < 0 ? 0 : text.length - dot - 1
    }

    function wholeStep(step: var): bool {
        return typeof step === "number" && step >= 1 && Number.isInteger(step)
    }

    function nameOf(capture: var): string {
        const naming = field.keyName
        return typeof naming === "function" ? naming(capture) : BlueprintLook.keyName(capture)
    }

    objectName: "bpField_" + name
    spacing: Theme.space.xs + Theme.space.xxs

    Item {
        width: field.width
        height: Math.max(labelRow.implicitHeight, field.inline ? toggle.implicitHeight : 0)
        visible: (field.spec.label ?? "") !== "" || field.inline

        Row {
            id: labelRow

            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.xs

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: field.spec.label ?? ""
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Theme.type.label.weight
            }

            InfoButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: (field.spec.info ?? "") !== ""
                title: field.spec.label ?? ""
                info: field.spec.info ?? ""
            }
        }

        Switch {
            id: toggle

            objectName: "bpFieldSwitch_" + field.name
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: field.inline
            checked: field.spec.value === true
            Accessible.name: field.spec.label ?? ""
            onToggled: field.edited(toggle.checked)
        }
    }

    Loader {
        width: field.width
        active: !field.inline
        sourceComponent: {
            switch (field.kind) {
            case "key":
                return keyEditor
            case "segmented":
                return segmentedEditor
            case "choice":
                return choiceEditor
            case "number":
            case "duration":
                return numberEditor
            case "slider":
                return sliderEditor
            case "text":
                return textEditor
            case "list":
                return listView
            case "log":
                return logView
            default:
                return readonlyView
            }
        }
    }

    Text {
        width: field.width
        visible: (field.spec.caption ?? "") !== ""
        textFormat: Text.PlainText
        text: field.spec.caption ?? ""
        wrapMode: Text.Wrap
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
        lineHeight: Theme.type.caption.lineHeight
        lineHeightMode: Text.FixedHeight
    }

    Component {
        id: keyEditor

        KeyCaptureField {
            objectName: "bpFieldKey_" + field.name
            keys: (field.spec.value ?? "") !== "" ? [{ text: String(field.spec.value) }] : []
            Accessible.name: field.spec.label ?? ""
            onKeyCaptured: (key, nativeScanCode, modifiers) => {
                const name = field.nameOf({ kind: "key", key: key, scanCode: nativeScanCode, modifiers: modifiers })
                if (name !== "")
                    field.edited(name)
            }
            onMouseCaptured: (button, modifiers) => {
                const name = field.nameOf({ kind: "mouse", button: button, modifiers: modifiers })
                if (name !== "")
                    field.edited(name)
            }
            onWheelCaptured: (dx, dy, modifiers) => {
                const name = field.nameOf({ kind: "wheel", dx: dx, dy: dy, modifiers: modifiers })
                if (name !== "")
                    field.edited(name)
            }
            onCleared: field.edited("")
        }
    }

    Component {
        id: segmentedEditor

        SegmentedControl {
            id: segments

            objectName: "bpFieldSegments_" + field.name
            model: field.choices.map(choice => choice.text ?? "")
            currentIndex: Math.max(0, field.choiceIndex())
            Accessible.name: field.spec.label ?? ""
            onActivated: index => {
                field.edited(field.choices[index].value)
                segments.currentIndex = Qt.binding(() => Math.max(0, field.choiceIndex()))
            }
        }
    }

    Component {
        id: choiceEditor

        Select {
            id: select

            objectName: "bpFieldSelect_" + field.name
            model: field.choices
            textRole: "text"
            currentIndex: field.choiceIndex()
            displayText: !field.unset ? select.currentText
                                      : select.currentIndex >= 0 ? qsTr("Default · %1").arg(select.currentText)
                                                                 : qsTr("Default")
            Accessible.name: field.spec.label ?? ""
            onActivated: index => {
                field.edited(field.choices[index].value)
                select.currentIndex = Qt.binding(() => field.choiceIndex())
            }
        }
    }

    Component {
        id: numberEditor

        NumberField {
            id: numberBox

            readonly property bool seconds: field.kind === "duration"
            readonly property real factor: seconds ? 1000 : 1

            objectName: "bpFieldNumber_" + field.name
            value: Number(field.spec.value ?? 0) / factor
            unit: (field.spec.unit ?? "") !== "" ? field.spec.unit : seconds ? qsTr("s") : ""
            automatic: field.unset
            word: !field.unset ? ""
                  : typeof field.spec.value === "number"
                    ? qsTr("Default %1").arg(numberBox.numbers.text(numberBox.value)
                                             + (numberBox.unit !== "" ? " " + numberBox.unit : ""))
                    : qsTr("Default")
            numbers: NumberScale {
                minimum: typeof field.spec.min === "number" ? field.spec.min / (field.kind === "duration" ? 1000 : 1)
                                                            : -Infinity
                maximum: typeof field.spec.max === "number" ? field.spec.max / (field.kind === "duration" ? 1000 : 1)
                                                            : Infinity
                step: typeof field.spec.step === "number" ? field.spec.step / (field.kind === "duration" ? 1000 : 1)
                                                          : (field.kind === "duration" ? 0.1 : 1)
                decimals: field.decimalsOf(step, field.kind === "duration" ? 1 : 0)
                exact: field.kind !== "duration" && !field.wholeStep(field.spec.step)
            }
            Accessible.name: field.spec.label ?? ""
            onSubmitted: number => field.edited(field.kind === "duration" ? Math.round(number * 1000) : number)
        }
    }

    Component {
        id: sliderEditor

        Item {
            implicitHeight: Theme.size.control

            Slider {
                id: slider

                objectName: "bpFieldSlider_" + field.name
                width: parent.width - amount.width - Theme.space.sm
                anchors.verticalCenter: parent.verticalCenter
                showValue: false
                from: typeof field.spec.min === "number" ? field.spec.min : 0
                to: typeof field.spec.max === "number" ? field.spec.max : 100
                stepSize: typeof field.spec.step === "number" ? field.spec.step : 1
                value: Number(field.spec.value ?? 0)
                Accessible.name: field.spec.label ?? ""
                onPressedChanged: {
                    if (!slider.pressed && slider.value !== Number(field.spec.value ?? 0))
                        field.edited(slider.value)
                }
                onMoved: {
                    if (!slider.pressed)
                        field.edited(slider.value)
                }
            }

            NumberField {
                id: amount

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                value: slider.value
                unit: field.spec.unit ?? ""
                numbers: NumberScale {
                    minimum: slider.from
                    maximum: slider.to
                    step: slider.stepSize
                    decimals: field.decimalsOf(slider.stepSize, 0)
                    exact: !field.wholeStep(field.spec.step)
                }
                Accessible.name: field.spec.label ?? ""
                onSubmitted: number => field.edited(number)
            }
        }
    }

    Component {
        id: textEditor

        TextField {
            id: textField

            objectName: "bpFieldText_" + field.name
            text: String(field.spec.value ?? "")
            placeholderText: field.unset ? qsTr("Default") : ""
            Accessible.name: field.spec.label ?? ""
            onEditingFinished: {
                if (textField.text !== String(field.spec.value ?? ""))
                    field.edited(textField.text)
            }
        }
    }

    Component {
        id: readonlyView

        Text {
            textFormat: Text.PlainText
            text: String(field.spec.value ?? "")
            wrapMode: Text.Wrap
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.label.size
        }
    }

    Component {
        id: listView

        Rectangle {
            implicitHeight: rows.implicitHeight
            radius: Theme.radius.lg
            color: Theme.colors.bgOverlay
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeSubtle
            clip: true

            Column {
                id: rows

                width: parent.width

                Repeater {
                    model: field.spec.items ?? []

                    delegate: Item {
                        id: line

                        required property var modelData
                        required property int index

                        width: rows.width
                        height: Theme.size.bpListRow

                        Rectangle {
                            visible: line.index > 0
                            width: parent.width
                            height: Theme.size.stroke
                            color: Theme.colors.strokeSubtle
                        }

                        Row {
                            x: Theme.space.md
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 2 * x - (badge.visible ? badge.width + Theme.space.sm : 0)
                            spacing: Theme.space.sm

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: (line.modelData.icon ?? "") !== ""
                                name: line.modelData.icon ?? ""
                                size: Theme.size.iconSmall
                                color: Theme.colors.textTertiary
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - (line.modelData.icon ? Theme.size.iconSmall + parent.spacing : 0)
                                textFormat: Text.PlainText
                                text: line.modelData.text ?? ""
                                elide: Text.ElideRight
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size
                            }
                        }

                        Rectangle {
                            id: badge

                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space.md
                            anchors.verticalCenter: parent.verticalCenter
                            visible: (line.modelData.badge ?? "") !== ""
                            width: badgeText.implicitWidth + 2 * Theme.space.xs + Theme.space.xxs
                            height: Theme.size.badge
                            radius: Theme.radius.xs
                            color: {
                                switch (line.modelData.tone ?? "") {
                                case "warn":
                                    return Theme.colors.warningSubtle
                                case "bad":
                                    return Theme.colors.dangerSubtle
                                case "good":
                                case "ok":
                                    return Theme.colors.successSubtle
                                case "info":
                                    return Theme.colors.infoSubtle
                                case "accent":
                                    return Theme.colors.accentSubtle
                                default:
                                    return Theme.colors.statePress
                                }
                            }

                            Text {
                                id: badgeText

                                anchors.centerIn: parent
                                textFormat: Text.PlainText
                                text: line.modelData.badge ?? ""
                                color: (line.modelData.tone ?? "") === "" ? Theme.colors.textSecondary
                                                                          : BlueprintLook.statusText(line.modelData.tone)
                                font.pixelSize: Theme.type.micro.size
                                font.weight: Theme.type.micro.weight
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: logView

        Rectangle {
            implicitHeight: entries.implicitHeight + 2 * Theme.space.xs
            radius: Theme.radius.md
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeDefault

            Column {
                id: entries

                y: Theme.space.xs
                width: parent.width

                Repeater {
                    model: field.spec.entries ?? []

                    delegate: Item {
                        id: entry

                        required property var modelData

                        width: entries.width
                        height: Theme.size.bpKvRow

                        Text {
                            x: Theme.space.sm + Theme.space.xxs
                            width: Theme.size.bpKvKey
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: entry.modelData.time ?? ""
                            elide: Text.ElideRight
                            color: Theme.colors.textTertiary
                            font.family: Theme.type.monoFamily
                            font.pixelSize: Theme.type.micro.size
                        }

                        Text {
                            x: Theme.space.sm + Theme.space.xxs + Theme.size.bpKvKey + Theme.space.md
                            width: parent.width - x - Theme.space.sm
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: entry.modelData.text ?? ""
                            elide: Text.ElideRight
                            color: (entry.modelData.tone ?? "") === "" ? Theme.colors.textPrimary
                                                                       : BlueprintLook.statusText(entry.modelData.tone)
                            font.pixelSize: Theme.type.caption.size
                        }
                    }
                }
            }
        }
    }
}
