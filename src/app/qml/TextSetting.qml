import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

SettingRow {
    id: row

    readonly property int maxLength: entry && entry.maxLength > 0 ? entry.maxLength : 4096

    onValueChanged: revert()
    onDeviceChanged: revert()

    function shown(stored: var): string {
        return stored === undefined ? "" : String(stored)
    }

    function revert() {
        field.errorText = ""
        field.text = Qt.binding(() => row.shown(row.value))
    }

    function submit() {
        if (field.text === shown(value)) {
            revert()
            return
        }
        if (!commit(field.text)) {
            field.errorText = qsTr("Could not save this text")
            return
        }
        revert()
    }

    TextField {
        id: field

        implicitWidth: Theme.size.fieldWidth
        maximumLength: row.maxLength
        text: row.shown(row.value)
        Accessible.name: row.title
        onEditingFinished: row.submit()
        Keys.onEscapePressed: row.revert()
    }
}
