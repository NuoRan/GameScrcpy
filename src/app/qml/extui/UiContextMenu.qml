pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Item {
    id: area

    property var groups: []

    function fill() {
        while (menu.count > 0) {
            const old = menu.takeItem(0)
            if (old !== null)
                old.destroy()
        }
        area.groups.forEach((group, index) => {
            if (index > 0)
                menu.addItem(separatorMaker.createObject(menu))
            menu.addItem(labelMaker.createObject(menu, { text: group.title !== "" ? group.title : group.extensionName }))
            for (const entry of group.items ?? [])
                menu.addItem(itemMaker.createObject(menu, { group: group, entry: entry }))
        })
    }

    function openAt(across: real, down: real) {
        area.fill()
        if (menu.count > 0)
            menu.popup(across, down)
    }

    objectName: "extensionMenuArea"

    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: (eventPoint, button) => area.openAt(eventPoint.position.x, eventPoint.position.y)
    }

    Menu {
        id: menu

        objectName: "extensionMenu"
    }

    Component {
        id: labelMaker

        Item {
            property alias text: label.text

            implicitWidth: label.implicitWidth + 2 * (Theme.space.sm + Theme.space.xxs)
            implicitHeight: Theme.size.pill + Theme.space.xs

            Text {
                id: label

                x: Theme.space.sm + Theme.space.xxs
                anchors.bottom: parent.bottom
                textFormat: Text.PlainText
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                font.weight: Font.DemiBold
            }
        }
    }

    Component {
        id: itemMaker

        MenuItem {
            id: pick

            property var group: ({})
            property var entry: ({})
            readonly property bool toggles: pick.entry.checkable === true

            objectName: "extensionMenuItem"
            text: pick.entry.title ?? ""
            iconName: pick.toggles ? "" : ExtensionPanels.iconOf(pick.entry.icon ?? "")
            choice: pick.toggles
            chosen: pick.entry.checked === true
            enabled: pick.entry.enabled !== false
            onTriggered: ExtensionPanels.send(pick.group.logic, "menu", "select", pick.entry.id ?? "",
                                              pick.toggles ? pick.entry.checked !== true : null)
        }
    }

    Component {
        id: separatorMaker

        MenuSeparator {}
    }
}
