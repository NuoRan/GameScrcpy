import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

SettingsRow {
    id: row

    required property string path
    readonly property bool extension: path.startsWith("ext:")
    readonly property var entry: SettingsCatalog.entries[path]
    readonly property string device: SettingsCatalog.device
    readonly property var deviceValues: device === "" ? undefined
                                        : extension ? ExtensionHost.overrides[device] : AppSettings.overrides[device]
    readonly property var globalValue: extension ? ExtensionHost.values[path] : AppSettings.values[path]
    property string globalText: globalValue === undefined ? "" : String(globalValue)
    readonly property bool ownValue: deviceValues !== undefined && deviceValues[path] !== undefined
    readonly property var value: ownValue ? deviceValues[path] : globalValue
    property var relatedPaths: []
    readonly property bool matched: (!SettingsCatalog.searching || SettingsCatalog.hits[path] === true
                                     || relatedPaths.some(other => SettingsCatalog.hits[other] === true))
                                    && (device === "" || (entry?.perDevice ?? false))
    readonly property bool changed: device === "" ? entry !== undefined && value !== undefined && value !== entry.defaultValue
                                                  : ownValue
    property string fact

    function commit(next: var): bool {
        if (next === value)
            return true
        if (extension)
            return device === "" ? ExtensionHost.setValue(path, next) : ExtensionHost.setDeviceValue(device, path, next)
        return device === "" ? AppSettings.setValue(path, next) : AppSettings.setDeviceValue(device, path, next)
    }

    function forget() {
        if (extension)
            ExtensionHost.resetDeviceValue(device, path)
        else
            AppSettings.resetDeviceValue(device, path)
    }

    function restore() {
        if (device === "")
            commit(entry.defaultValue)
        else
            forget()
    }

    width: parent ? parent.width : implicitWidth
    visible: matched
    divider: {
        const siblings = parent ? parent.visibleChildren : []
        for (let i = 0; i < siblings.length; ++i) {
            if ((siblings[i] as SettingRow) !== null)
                return siblings[i] !== row
        }
        return false
    }
    title: entry ? entry.title : path
    description: entry ? entry.description : ""
    note: [ownValue ? qsTr("Global default: %1").arg(globalText) : "", fact].filter(text => text !== "").join(" · ")
    overridden: ownValue
    highlight: SettingsCatalog.searching ? SettingsCatalog.query : ""
    onResetRequested: forget()
}
