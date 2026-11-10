pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.App

UiPanelView {
    id: block

    property string target
    property bool shown: false
    property var announced: null

    function announce() {
        const wanted = block.shown && block.entry !== null ? block.entry.key + "\n" + block.target : ""
        const before = block.announced !== null ? block.announced.key + "\n" + block.announced.target : ""
        if (wanted === before)
            return
        if (block.announced !== null)
            ExtensionPanels.send(block.announced.logic, block.announced.name, "close", block.announced.target, null)
        block.announced = wanted !== "" ? { key: block.entry.key, logic: block.entry.logic, name: block.entry.name,
                                            target: block.target }
                                        : null
        if (block.announced !== null)
            ExtensionPanels.send(block.announced.logic, block.announced.name, "open", block.announced.target, null)
    }

    onShownChanged: block.announce()
    onEntryChanged: block.announce()
    onTargetChanged: block.announce()
    Component.onCompleted: block.announce()
    Component.onDestruction: {
        if (block.announced !== null)
            ExtensionPanels.send(block.announced.logic, block.announced.name, "close", block.announced.target, null)
    }
}
