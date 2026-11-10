pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import GameScrcpy.App

FileDialog {
    id: dialog

    property string kind
    property string source
    property string sourceName

    signal finished(bool ok, string message)

    function start(exportKind: string, exportSource: string, exportName: string) {
        dialog.kind = exportKind
        dialog.source = exportSource
        dialog.sourceName = exportName
        dialog.selectedFile = exportName.replace(/[\\/:*?"<>|]/g, "-").trim() + ".gsext"
        dialog.open()
    }

    title: qsTr("Export %1 as an extension package").arg(dialog.sourceName)
    fileMode: FileDialog.SaveFile
    defaultSuffix: "gsext"
    nameFilters: [qsTr("GameScrcpy extension (*.gsext)")]
    onAccepted: {
        const target = dialog.selectedFile.toString()
        let exported = false
        if (dialog.kind === "component")
            exported = ComponentLibrary.exportPackage(dialog.source, target)
        else if (dialog.kind === "task")
            exported = TaskLibrary.exportPackage(dialog.source, target)
        else if (dialog.kind === "profile")
            exported = ProfileLibrary.exportPackage(dialog.source, target)
        dialog.finished(exported, exported ? qsTr("Exported %1 as an extension package").arg(dialog.sourceName)
                                           : qsTr("Could not export %1 as an extension package").arg(dialog.sourceName))
    }
}
