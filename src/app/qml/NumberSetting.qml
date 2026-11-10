import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

SettingRow {
    id: row

    property string unit
    property real factor: 1
    property int decimals: 0
    readonly property bool whole: entry?.kind !== "number"
    readonly property NumberScale numbers: NumberScale {
        minimum: (row.entry?.minimum ?? -Infinity) / row.factor
        maximum: (row.entry?.maximum ?? Infinity) / row.factor
        step: Math.pow(10, -row.decimals)
        decimals: row.decimals
    }

    globalText: globalValue === undefined ? "" : unit === "" ? numbers.text(globalValue / factor)
                                                             : numbers.text(globalValue / factor) + " " + unit
    onValueChanged: field.revert()
    onDeviceChanged: field.revert()

    NumberField {
        id: field

        numbers: row.numbers
        unit: row.unit
        value: (row.value ?? 0) / row.factor
        Accessible.name: row.title
        onSubmitted: value => {
            const stored = row.whole ? Math.round(value * row.factor) : value * row.factor
            if (!row.commit(stored))
                field.errorText = field.rangeText()
        }
    }
}
