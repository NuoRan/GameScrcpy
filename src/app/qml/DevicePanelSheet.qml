pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Controls
import GameScrcpy.App

SettingsSheet {
    id: sheet

    property string extension
    property string panel
    property string serial
    property string device
    property string deviceName
    readonly property var entry: {
        const available = ExtensionPanels.panels
        if (available.length === 0)
            return null
        return ExtensionPanels.devicePanel(sheet.extension, sheet.panel, sheet.serial) ?? null
    }

    function show(extension: string, panel: string, serial: string, device: string, deviceName: string) {
        sheet.extension = extension
        sheet.panel = panel
        sheet.serial = serial
        sheet.device = device
        sheet.deviceName = deviceName
        sheet.open()
    }

    objectName: "devicePanelSheet"
    title: sheet.entry !== null ? sheet.entry.title : sheet.deviceName
    subtitle: sheet.entry !== null ? qsTr("%1 · %2").arg(sheet.entry.extensionName).arg(sheet.deviceName) : ""
    iconName: sheet.entry !== null && (sheet.entry.icon !== "" || sheet.entry.iconPath !== "") ? sheet.entry.icon
                                                                                              : "panel-top"
    iconPath: sheet.entry !== null ? sheet.entry.iconPath : ""
    tone: "azure"

    UiPanelBlock {
        id: view

        objectName: "devicePanelView"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(view.implicitHeight, sheet.bodyLimit)
        entry: sheet.entry
        target: sheet.device
        shown: sheet.visible
        waitingFor: sheet.entry !== null ? sheet.entry.extensionName : sheet.deviceName
    }
}
