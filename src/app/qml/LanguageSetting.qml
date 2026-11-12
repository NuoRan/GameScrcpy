pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

SettingRow {
    id: row

    property var labels: ({})
    readonly property var builtIn: entry ? entry.choices : []
    readonly property var packs: LanguagePacks.packs
    readonly property var missing: LanguagePacks.missing
    readonly property string current: value === undefined ? "" : String(value)
    readonly property var options: {
        const list = builtIn.concat(packs.map(pack => pack.locale))
        return current === "" || list.indexOf(current) >= 0 ? list : list.concat([current])
    }
    readonly property bool listed: options.length > builtIn.length

    function titleOf(option: string): string {
        const pack = packs.find(item => item.locale === option)
        if (pack !== undefined)
            return qsTr("%1 · %2").arg(pack.name).arg(pack.extensionName)
        if (labels[option] !== undefined)
            return labels[option]
        return qsTr("%1 (unavailable)").arg(option === (missing.locale ?? "") ? missing.name : option)
    }

    path: "general.language"
    globalText: titleOf(String(globalValue ?? ""))
    fact: (missing.locale ?? "") === "" ? ""
          : missing.installed
            ? qsTr("%1 comes from the extension %2, which is off, so the interface follows Windows until it is on "
                   + "again.").arg(missing.name).arg(missing.extensionName)
            : qsTr("The extension that added %1 is no longer installed, so the interface follows Windows.")
                  .arg(missing.name)

    SegmentedControl {
        id: segments

        objectName: "languageSegments"
        visible: !row.listed
        model: row.builtIn.map(option => row.titleOf(option))
        currentIndex: row.builtIn.indexOf(row.current)
        Accessible.name: row.title
        onActivated: index => {
            row.commit(row.builtIn[index])
            segments.currentIndex = Qt.binding(() => row.builtIn.indexOf(row.current))
        }
    }

    Select {
        id: select

        objectName: "languageSelect"
        visible: row.listed
        model: row.options.map(option => row.titleOf(option))
        currentIndex: row.options.indexOf(row.current)
        Accessible.name: row.title
        onActivated: index => {
            if (!row.commit(row.options[index]))
                select.currentIndex = Qt.binding(() => row.options.indexOf(row.current))
        }
    }
}
