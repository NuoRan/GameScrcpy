import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

SettingRow {
    id: row

    property string unit
    property real factor: 1
    property real step: 1
    property int decimals: 0
    property bool logarithmic: false
    property real sliderFrom: minimum / factor
    property real sliderTo: maximum / factor
    property var marks: []
    property var special
    property string info
    readonly property real minimum: entry ? entry.minimum : 0
    readonly property real maximum: entry ? entry.maximum : 0
    readonly property bool whole: entry?.kind !== "number"
    readonly property bool automatic: special !== undefined && value === special.value
    readonly property real shownValue: (value ?? minimum) / factor
    readonly property real draggedValue: numbers.valueAt(slider.value)
    readonly property NumberScale numbers: NumberScale {
        minimum: row.minimum / row.factor
        maximum: row.maximum / row.factor
        from: row.sliderFrom
        to: row.sliderTo
        step: row.step
        decimals: row.decimals
        logarithmic: row.logarithmic
    }

    globalText: globalValue === undefined ? "" : textOf(globalValue)
    onDeviceChanged: field.revert()

    function withUnit(text: string): string {
        return unit === "" ? text : text + " " + unit
    }

    function markValue(mark: var): real {
        return typeof mark === "number" ? mark : mark.value
    }

    function markText(mark: var): string {
        return typeof mark === "number" ? String(mark) : mark.text
    }

    function textOf(stored: var): string {
        if (special !== undefined && stored === special.value)
            return special.text
        return withUnit(numbers.text(stored / factor))
    }

    function store(shown: real) {
        const stored = whole ? Math.round(shown * factor) : shown * factor
        commit(stored)
        slider.value = Qt.binding(() => row.automatic ? 1 : row.numbers.position(row.shownValue))
    }

    function keyed(key: int): var {
        const current = automatic ? numbers.to : shownValue
        switch (key) {
        case Qt.Key_Right:
        case Qt.Key_Up:
            return numbers.stepped(current, 1)
        case Qt.Key_Left:
        case Qt.Key_Down:
            return numbers.stepped(current, -1)
        case Qt.Key_PageUp:
            return numbers.valueAt(numbers.position(current) + 0.1)
        case Qt.Key_PageDown:
            return numbers.valueAt(numbers.position(current) - 0.1)
        case Qt.Key_Home:
            return numbers.from
        case Qt.Key_End:
            return numbers.to
        }
        return undefined
    }

    NumberField {
        id: field

        numbers: row.numbers
        unit: row.unit
        value: slider.pressed ? row.draggedValue : row.automatic ? row.numbers.to : row.shownValue
        automatic: row.automatic && !slider.pressed
        word: automatic ? row.special.word ?? qsTr("Auto") : ""
        showsMessage: false
        Accessible.name: row.title
        onSubmitted: value => row.store(value)
    }

    below: [
        Slider {
            id: slider

            objectName: "slider"
            width: parent ? parent.width : implicitWidth
            from: 0
            to: 1
            showValue: false
            automatic: row.automatic && !pressed
            value: row.automatic ? 1 : row.numbers.position(row.shownValue)
            valueText: row.withUnit(row.numbers.text(row.draggedValue))
            ticks: row.marks.map(mark => ({
                position: row.numbers.position(row.markValue(mark)),
                text: row.markText(mark),
                current: !row.automatic && row.markValue(mark) === row.shownValue
            }))
            Accessible.name: row.title
            onPressedChanged: {
                if (!pressed)
                    row.store(row.draggedValue)
            }
            onTickActivated: index => row.store(row.markValue(row.marks[index]))
            Keys.onPressed: event => {
                const next = row.keyed(event.key)
                if (next !== undefined) {
                    row.store(next)
                    event.accepted = true
                }
            }
            Keys.onReleased: event => event.accepted = true
        },
        Item {
            objectName: "meta"
            width: parent ? parent.width : implicitWidth
            height: Math.max(row.special !== undefined ? choice.implicitHeight : 0, side.implicitHeight)
            visible: row.special !== undefined || side.text !== ""

            Checkbox {
                id: choice

                anchors.verticalCenter: parent.verticalCenter
                visible: row.special !== undefined
                text: row.special?.text ?? ""
                checked: row.automatic
                onToggled: {
                    if (checked)
                        row.commit(row.special.value)
                    else
                        row.store(row.special.fallback / row.factor)
                    checked = Qt.binding(() => row.automatic)
                }
            }

            Text {
                id: side

                objectName: "side"
                anchors.left: row.special !== undefined ? choice.right : parent.left
                anchors.leftMargin: row.special !== undefined ? Theme.space.lg : 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: field.errorText !== ""
                      ? qsTr("%1 · Esc restores %2").arg(field.errorText).arg(row.textOf(row.value))
                      : row.automatic ? (row.special?.note ?? "") : row.info
                color: field.errorText !== "" ? Theme.colors.dangerText : Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }
        }
    ]
}
