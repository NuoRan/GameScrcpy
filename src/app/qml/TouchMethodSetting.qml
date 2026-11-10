pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

SettingRow {
    id: row

    property var labels: ({})
    property var confirmFirst: []
    readonly property string extensionPath: "touch.extension"
    readonly property string extensionGlobal: String(AppSettings.values[extensionPath] ?? "")
    readonly property bool extensionOwn: deviceValues !== undefined && deviceValues[extensionPath] !== undefined
    readonly property string extensionValue: extensionOwn ? String(deviceValues[extensionPath]) : extensionGlobal
    readonly property var methods: ExtensionHost.touchMethods
    readonly property string current: extensionValue !== "" ? "ext:" + extensionValue : String(value)
    readonly property var method: methods.find(item => "ext:" + item.key === current) ?? null
    readonly property bool methodMissing: current.startsWith("ext:") && method === null
    readonly property var options: {
        const builtIn = entry ? entry.choices : []
        const list = builtIn.filter(option => option !== "none").concat(methods.map(item => "ext:" + item.key))
                            .concat(builtIn.filter(option => option === "none"))
        return list.indexOf(current) >= 0 ? list : list.concat([current])
    }
    readonly property var choiceStates: DeviceManager.linkRevision >= 0 && device !== ""
                                        ? (DeviceManager.pictureAndTouch(device).touchChoices ?? []) : []
    readonly property string blockedText: options.filter(option => !available(option))
                                                 .map(option => qsTr("%1: %2").arg(titleOf(option))
                                                                              .arg(stateOf(option)?.reason ?? ""))
                                                 .join("\n")

    signal confirmationWanted(var option)

    function titleOf(option: string): string {
        if (option.startsWith("ext:")) {
            const found = methods.find(item => "ext:" + item.key === option)
            return found ? found.title : option.substring(4)
        }
        return labels[option] ?? option
    }

    function stateOf(option: string): var {
        const id = option.startsWith("ext:") ? option.substring(4) : option
        return choiceStates.find(choice => choice.id === id) ?? null
    }

    function available(option: string): bool {
        return stateOf(option)?.enabled !== false
    }

    function chooseExtension(key: string): bool {
        if (key === extensionValue)
            return true
        return device === "" ? AppSettings.setValue(extensionPath, key)
                             : AppSettings.setDeviceValue(device, extensionPath, key)
    }

    function choose(option: var): bool {
        const text = String(option)
        if (text.startsWith("ext:"))
            return chooseExtension(text.substring(4))
        const done = commit(text)
        return chooseExtension("") && done
    }

    relatedPaths: [extensionPath, "touch.device"]
    globalText: extensionGlobal !== "" ? titleOf("ext:" + extensionGlobal) : (labels[globalValue] ?? String(globalValue))
    overridden: ownValue || extensionOwn
    onResetRequested: AppSettings.resetDeviceValue(device, extensionPath)

    SegmentedControl {
        id: segments

        objectName: "touchMethodSegments"
        model: row.options.map(option => ({ text: row.titleOf(option), enabled: row.available(option) }))
        currentIndex: row.options.indexOf(row.current)
        Accessible.name: row.title
        onActivated: index => {
            const option = row.options[index]
            if (option !== row.current && row.available(option)) {
                if (row.confirmFirst.indexOf(option) >= 0)
                    row.confirmationWanted(option)
                else
                    row.choose(option)
            }
            segments.currentIndex = Qt.binding(() => row.options.indexOf(row.current))
        }
    }
}
