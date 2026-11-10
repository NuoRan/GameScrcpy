pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Item {
    id: page

    property string entryId: ""
    property var known: null
    property var opened: null
    readonly property var entry: ExtensionPanels.panels.find(each => each.where === "page"
                                                                     && each.id === page.entryId) ?? null
    readonly property bool knownFits: page.known !== null && page.known.id === page.entryId
    readonly property var shownEntry: page.entry ?? (page.knownFits ? page.known : null)

    function announce() {
        const wanted = page.visible ? page.entry : null
        const before = page.opened !== null ? page.opened.key : ""
        const after = wanted !== null ? wanted.key : ""
        if (before === after)
            return
        if (page.opened !== null)
            ExtensionPanels.send(page.opened.logic, page.opened.name, "close", "", null)
        page.opened = wanted
        if (wanted !== null)
            ExtensionPanels.send(wanted.logic, wanted.name, "open", "", null)
    }

    objectName: "extensionPage"
    onEntryChanged: {
        if (page.entry !== null)
            page.known = page.entry
        page.announce()
    }
    onVisibleChanged: page.announce()
    Component.onDestruction: {
        if (page.opened !== null)
            ExtensionPanels.send(page.opened.logic, page.opened.name, "close", "", null)
    }

    Row {
        id: header

        x: Theme.size.panelPaddingH
        y: Theme.size.panelPaddingV
        width: page.width - 2 * Theme.size.panelPaddingH
        spacing: Theme.space.md

        Tile {
            anchors.verticalCenter: parent.verticalCenter
            large: true
            iconName: page.shownEntry !== null && page.shownEntry.icon !== "" ? page.shownEntry.icon : "puzzle"
            tone: "azure"
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, header.width - Theme.size.tileLarge - header.spacing)

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: page.shownEntry !== null ? page.shownEntry.title : ""
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.display.size
                font.weight: Theme.type.display.weight
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: text !== ""
                textFormat: Text.PlainText
                text: page.shownEntry !== null ? page.shownEntry.extensionName : ""
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.body.size
                elide: Text.ElideRight
            }
        }
    }

    UiPanelView {
        objectName: "extensionPageView"
        x: Theme.size.panelPaddingH - Theme.space.lg
        y: header.y + header.height + Theme.space.lg
        width: Math.min(page.width - 2 * x, Theme.size.readableWidth)
        height: Math.max(0, page.height - y - Theme.size.panelPaddingV)
        entry: page.entry
        waitingFor: page.shownEntry !== null ? page.shownEntry.extensionName : ""
        onReleased: page.forceActiveFocus()
    }
}
