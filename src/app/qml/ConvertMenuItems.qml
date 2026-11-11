pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

Instantiator {
    id: items

    required property Menu menu
    required property string format
    property Item before: null
    property bool shown: true
    property bool usable: true
    readonly property var choices: Converters.converters.filter(entry => entry.from === items.format)

    signal picked(var converter)

    model: items.choices.length

    delegate: MenuItem {
        id: choice

        required property int index
        readonly property var converter: items.choices[choice.index] ?? null

        objectName: "convertExport"
        visible: items.shown
        enabled: items.usable
        height: visible ? implicitHeight : 0
        iconName: "share"
        text: choice.converter !== null ? qsTr("Export as %1…").arg(choice.converter.title) : ""
        shortcut: choice.converter !== null ? choice.converter.extensionName : ""
        onTriggered: {
            if (choice.converter !== null)
                items.picked(choice.converter)
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
