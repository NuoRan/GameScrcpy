import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

SettingRow {
    id: row

    property var labels: ({})
    property var presets: []
    property var confirmFirst: []
    readonly property var options: {
        if (presets.length === 0)
            return entry ? entry.choices : []
        return value === undefined || presets.indexOf(value) >= 0 ? presets : presets.concat([value])
    }

    signal confirmationWanted(var option)

    globalText: labels[globalValue] ?? String(globalValue)

    SegmentedControl {
        id: segments

        model: row.options.map(option => row.labels[option] ?? String(option))
        currentIndex: row.options.indexOf(row.value)
        Accessible.name: row.title
        onActivated: index => {
            const option = row.options[index]
            if (option !== row.value && row.confirmFirst.indexOf(option) >= 0)
                row.confirmationWanted(option)
            else
                row.commit(option)
            segments.currentIndex = Qt.binding(() => row.options.indexOf(row.value))
        }
    }
}
