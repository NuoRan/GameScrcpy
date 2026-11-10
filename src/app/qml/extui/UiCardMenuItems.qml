pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

Instantiator {
    id: items

    required property Menu menu
    required property string kind
    required property string target
    required property string targetName
    property Item before: null
    readonly property var picks: {
        const found = []
        for (const group of ExtensionPanels.menus) {
            if (group.where !== "contextMenu" || (group.on ?? []).indexOf(items.kind) < 0)
                continue
            for (const entry of group.items ?? [])
                found.push({ group: group, entry: entry })
        }
        return found
    }

    model: items.picks.length

    delegate: MenuItem {
        id: pick

        required property int index
        readonly property var chosenPick: items.picks[pick.index] ?? null
        readonly property bool toggles: pick.chosenPick !== null && pick.chosenPick.entry.checkable === true

        objectName: "extensionCardItem"
        text: pick.chosenPick !== null ? pick.chosenPick.entry.title : ""
        shortcut: pick.chosenPick !== null ? pick.chosenPick.group.extensionName : ""
        iconName: pick.chosenPick === null || pick.toggles ? "" : ExtensionPanels.iconOf(pick.chosenPick.entry.icon ?? "")
        choice: pick.toggles
        chosen: pick.chosenPick !== null && pick.chosenPick.entry.checked === true
        enabled: pick.chosenPick !== null && pick.chosenPick.entry.enabled !== false
        onTriggered: {
            const picked = pick.chosenPick
            if (picked === null)
                return
            ExtensionPanels.choose(picked.group.logic, picked.group.name, picked.entry.id,
                                   pick.toggles ? picked.entry.checked !== true : null,
                                   { kind: items.kind, id: items.target, name: items.targetName })
        }
    }

    onObjectAdded: (index, object) => {
        for (let at = 0; items.before !== null && at < items.menu.count; ++at) {
            if (items.menu.itemAt(at) === items.before) {
                items.menu.insertItem(at, object)
                return
            }
        }
        items.menu.addItem(object)
    }
    onObjectRemoved: (index, object) => items.menu.removeItem(object)
}
