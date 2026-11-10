pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

T.Popup {
    id: popup

    property var entry: null

    function drawn(): bool {
        const tree = popup.entry !== null ? ExtensionPanels.tree(popup.entry.key) : null
        return tree !== undefined && tree !== null
    }

    function openFor(wanted: var) {
        popup.entry = wanted
        if (popup.drawn())
            popup.open()
    }

    objectName: "extensionToolbarPopup"
    width: Theme.size.extensionPanel
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    height: Math.min(popup.implicitHeight, Theme.size.extensionPanelMax)
    padding: Theme.space.xs
    margins: Theme.space.sm

    background: PopupSurface {}

    contentItem: UiPanelBlock {
        objectName: "extensionToolbarPanel"
        entry: popup.entry
        shown: popup.visible
    }

    Connections {
        target: ExtensionPanels

        function onTreeChanged(key: string) {
            if (popup.visible && popup.entry !== null && key === popup.entry.key && !popup.drawn())
                popup.close()
        }

        function onCloseRequested(key: string) {
            if (popup.entry !== null && key === popup.entry.key)
                popup.close()
        }
    }
}
