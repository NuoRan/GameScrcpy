pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Controls
import GameScrcpy.App

SettingsSheet {
    id: sheet

    property string extension
    property string panel
    property string device
    property string deviceName
    property var shown: null
    readonly property var entry: ExtensionPanels.panels.find(each => each.where === "device"
                                                             && each.extension === sheet.extension
                                                             && each.name === sheet.panel) ?? null

    function show(extension: string, panel: string, device: string, deviceName: string) {
        sheet.extension = extension
        sheet.panel = panel
        sheet.device = device
        sheet.deviceName = deviceName
        sheet.open()
        sheet.announce()
    }

    function announce() {
        const wanted = sheet.visible ? sheet.entry : null
        const before = sheet.shown !== null ? sheet.shown.key + "\n" + sheet.shown.device : ""
        const after = wanted !== null ? wanted.key + "\n" + sheet.device : ""
        if (before === after)
            return
        if (sheet.shown !== null)
            ExtensionPanels.send(sheet.shown.logic, sheet.shown.name, "close", sheet.shown.device, null)
        sheet.shown = wanted !== null ? { key: wanted.key, logic: wanted.logic, name: wanted.name, device: sheet.device }
                                      : null
        if (sheet.shown !== null)
            ExtensionPanels.send(sheet.shown.logic, sheet.shown.name, "open", sheet.shown.device, null)
    }

    objectName: "devicePanelSheet"
    title: sheet.entry !== null ? sheet.entry.title : sheet.deviceName
    subtitle: sheet.entry !== null ? qsTr("%1 · %2").arg(sheet.entry.extensionName).arg(sheet.deviceName) : ""
    iconName: sheet.entry !== null && sheet.entry.icon !== "" ? sheet.entry.icon : "panel-top"
    tone: "azure"
    onEntryChanged: sheet.announce()
    onVisibleChanged: sheet.announce()
    Component.onDestruction: {
        if (sheet.shown !== null)
            ExtensionPanels.send(sheet.shown.logic, sheet.shown.name, "close", sheet.shown.device, null)
    }

    UiPanelView {
        id: view

        objectName: "devicePanelView"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(view.implicitHeight, sheet.bodyLimit)
        entry: sheet.entry
        waitingFor: sheet.entry !== null ? sheet.entry.extensionName : sheet.deviceName
    }
}
