pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Dialog {
    id: dialog

    required property string serial
    property bool syncing: false
    property var candidates: []
    property var chosen: []

    signal notified(string message)

    function show() {
        candidates = DeviceManager.syncCandidates(serial)
        chosen = candidates.filter(candidate => candidate.checked).map(candidate => candidate.serial)
        open()
    }

    function choose(phone: string, on: bool) {
        const others = chosen.filter(each => each !== phone)
        chosen = on ? others.concat([phone]) : others
    }

    objectName: "syncDialog"
    width: parent ? Math.min(implicitWidth, parent.width - 2 * Theme.space.lg) : implicitWidth
    title: qsTr("Sync other phones with this one")
    acceptText: syncing ? qsTr("Apply") : qsTr("Start syncing")
    alternativeText: syncing ? qsTr("Stop syncing") : ""

    onAccepted: {
        const problem = DeviceManager.setSyncFollowers(dialog.serial, dialog.chosen)
        if (problem !== "")
            dialog.notified(problem)
    }
    onAlternativeChosen: DeviceManager.stopSync(dialog.serial)

    Column {
        width: parent.width
        spacing: Theme.space.md

        Text {
            width: parent.width
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("The phones you tick follow this one: the keys and taps you give it reach them too, and "
                       + "each acts by its own profile.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }

        Text {
            objectName: "syncNobody"
            width: parent.width
            visible: dialog.candidates.length === 0
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("No other phone is mirroring now.")
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.body.size
        }

        ListView {
            objectName: "syncCandidates"
            width: parent.width
            height: Math.min(contentHeight, Theme.size.paletteListMax)
            visible: count > 0
            clip: true
            spacing: Theme.space.sm
            boundsBehavior: Flickable.StopAtBounds
            model: dialog.candidates

            delegate: Column {
                id: entry

                required property var modelData

                width: ListView.view.width
                spacing: Theme.space.xxs

                Checkbox {
                    id: pick

                    objectName: "syncCandidate"
                    text: entry.modelData.name
                    enabled: entry.modelData.enabled
                    checked: dialog.chosen.includes(entry.modelData.serial)
                    onToggled: {
                        dialog.choose(entry.modelData.serial, pick.checked)
                        pick.checked = Qt.binding(() => dialog.chosen.includes(entry.modelData.serial))
                    }
                }

                Text {
                    visible: entry.modelData.note !== ""
                    leftPadding: Theme.size.checkbox + Theme.space.sm
                    textFormat: Text.PlainText
                    text: entry.modelData.note
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }
    }
}
