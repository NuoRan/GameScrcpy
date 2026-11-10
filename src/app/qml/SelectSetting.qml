import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

SettingRow {
    id: row

    property var labels: ({})
    readonly property var choices: entry ? entry.choices : []

    globalText: labels[globalValue] ?? String(globalValue)

    Select {
        id: select

        model: row.choices.map(name => row.labels[name] ?? name)
        currentIndex: row.choices.indexOf(row.value)
        Accessible.name: row.title
        onActivated: index => {
            if (!row.commit(row.choices[index]))
                select.currentIndex = Qt.binding(() => row.choices.indexOf(row.value))
        }
    }
}
