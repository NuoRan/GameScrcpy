pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

ToolbarButton {
    id: button

    property var entry: null
    readonly property var look: button.entry !== null ? button.entry.toolbar ?? ({}) : ({})
    readonly property bool ownIcon: (button.look.icon ?? "") !== "" || (button.look.iconPath ?? "") !== ""

    objectName: "extensionToolbarButton"
    text: (button.look.title ?? "") !== "" ? button.look.title : button.entry !== null ? button.entry.title : ""
    iconName: button.ownIcon ? ExtensionPanels.iconOf(button.look.icon ?? "")
              : button.entry !== null && button.entry.icon !== "" ? button.entry.icon
              : button.entry !== null && button.entry.iconPath !== "" ? "" : "puzzle"
    iconPath: button.ownIcon ? button.look.iconPath ?? "" : button.entry !== null ? button.entry.iconPath : ""
    selected: button.look.checked === true || popup.visible
    enabled: button.look.enabled !== false
    Accessible.description: button.entry !== null ? button.entry.extensionName : ""
    onClicked: {
        if (button.entry === null)
            return
        ExtensionPanels.send(button.entry.logic, button.entry.name, "click", "", null)
        if (popup.visible)
            popup.close()
        else if (popup.drawn())
            popup.open()
    }

    UiToolbarPanel {
        id: popup

        x: (button.width - width) / 2
        y: button.height + Theme.space.xs
        entry: button.entry
    }
}
