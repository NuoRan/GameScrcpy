pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

SettingRow {
    id: row

    property var labels: ({})
    readonly property var facts: DeviceManager.linkRevision >= 0 && device !== ""
                                 ? DeviceManager.pictureAndTouch(device) : ({})
    readonly property var choiceStates: facts.pictureChoices ?? []
    readonly property var captureCard: facts.capture ?? ({})
    readonly property string current: value === undefined ? "" : String(value)
    readonly property var options: {
        const list = entry ? entry.choices : []
        return current === "" || list.indexOf(current) >= 0 ? list : list.concat([current])
    }
    readonly property string captureSerial: current === "capture" ? captureCard.serial ?? "" : ""
    readonly property bool captureOpenable: DeviceManager.linkRevision >= 0 && captureSerial !== ""
                                            && Commands.isEnabled("device.settings", captureSerial)
    readonly property string captureLine: current === "capture" && (captureCard.name ?? "") !== ""
                                          ? qsTr("Capture card · %1").arg(captureCard.name) : ""
    readonly property string blockedText: options.filter(option => !available(option))
                                                 .map(option => qsTr("%1: %2").arg(titleOf(option))
                                                                              .arg(stateOf(option)?.reason ?? ""))
                                                 .join("\n")

    function titleOf(option: string): string {
        return labels[option] ?? option
    }

    function stateOf(option: string): var {
        return choiceStates.find(choice => choice.id === option) ?? null
    }

    function available(option: string): bool {
        return stateOf(option)?.enabled !== false
    }

    globalText: titleOf(String(globalValue ?? ""))
    fact: [captureLine, facts.note ?? "", blockedText].filter(text => text !== "").join("\n")

    SegmentedControl {
        id: segments

        objectName: "pictureSourceSegments"
        model: row.options.map(option => ({ text: row.titleOf(option), enabled: row.available(option) }))
        currentIndex: row.options.indexOf(row.current)
        Accessible.name: row.title
        onActivated: index => {
            const option = row.options[index]
            if (option !== row.current && row.available(option))
                row.commit(option)
            segments.currentIndex = Qt.binding(() => row.options.indexOf(row.current))
        }
    }
}
