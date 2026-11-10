import QtQuick
import GameScrcpy.Theme

TextField {
    id: control

    property NumberScale numbers: NumberScale {}
    property real value
    property string unit
    property bool automatic: false
    property string word

    readonly property string shownText: word !== "" ? word : numbers.text(value)

    signal submitted(real value)

    function revert() {
        errorText = ""
        text = Qt.binding(() => control.shownText)
    }

    function rangeText(): string {
        const low = numbers.text(numbers.minimum)
        const high = numbers.text(numbers.maximum)
        if (low !== "" && high !== "")
            return unit === "" ? qsTr("Enter %1 to %2").arg(low).arg(high)
                               : qsTr("Enter %1 to %2 %3").arg(low).arg(high).arg(unit)
        if (low !== "")
            return qsTr("Enter %1 or more").arg(low)
        if (high !== "")
            return qsTr("Enter %1 or less").arg(high)
        return qsTr("Enter a number")
    }

    function submit() {
        if (text === shownText || text.trim() === "") {
            revert()
            return
        }
        const number = numbers.parse(text)
        if (Number.isNaN(number) || !numbers.accepts(number)) {
            errorText = rangeText()
            return
        }
        revert()
        if (number !== value || word !== "")
            submitted(number)
    }

    function nudge(direction: int) {
        const next = numbers.stepped(value, direction)
        revert()
        if (next !== value || word !== "")
            submitted(next)
    }

    implicitWidth: Theme.size.numberField
    dense: true
    mono: true
    suffix: word !== "" ? "" : unit
    horizontalAlignment: TextInput.AlignRight
    inputMethodHints: Qt.ImhFormattedNumbersOnly
    color: automatic && !activeFocus ? Theme.colors.textSecondary : Theme.colors.textPrimary
    text: shownText
    onEditingFinished: submit()
    onActiveFocusChanged: {
        if (activeFocus && word !== "")
            text = ""
        else if (!activeFocus && text.trim() === "")
            revert()
    }
    Keys.onEscapePressed: event => {
        event.accepted = text !== shownText || errorText !== ""
        revert()
    }
    Keys.onUpPressed: nudge(1)
    Keys.onDownPressed: nudge(-1)

    WheelHandler {
        enabled: control.activeFocus
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => control.nudge(event.angleDelta.y > 0 ? 1 : -1)
    }
}
