import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

SettingRow {
    id: row

    globalText: globalValue === true ? qsTr("On") : qsTr("Off")

    Switch {
        id: toggle

        checked: row.value === true
        Accessible.name: row.title
        onToggled: {
            if (!row.commit(checked))
                toggle.checked = Qt.binding(() => row.value === true)
        }
    }
}
