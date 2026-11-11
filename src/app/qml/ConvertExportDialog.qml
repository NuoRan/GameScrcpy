pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import GameScrcpy.App

FileDialog {
    id: dialog

    property string converter
    property string converterTitle
    property string source
    property string sourceName
    property int ticket: 0
    readonly property Connections results: Connections {
        target: Converters

        function onExported(ticket: int, ok: bool, message: string) {
            if (ticket !== dialog.ticket)
                return
            dialog.ticket = 0
            dialog.finished(ok, message)
        }
    }

    signal finished(bool ok, string message)

    function start(entry: var, exportSource: string, exportName: string) {
        dialog.converter = entry.key
        dialog.converterTitle = entry.title
        dialog.source = exportSource
        dialog.sourceName = exportName
        dialog.defaultSuffix = entry.to
        dialog.nameFilters = [entry.title + " (*." + entry.to + ")"]
        dialog.selectedFile = exportName.replace(/[\\/:*?"<>|]/g, "-").trim() + "." + entry.to
        dialog.open()
    }

    title: qsTr("Export %1 as %2").arg(dialog.sourceName).arg(dialog.converterTitle)
    fileMode: FileDialog.SaveFile
    onAccepted: dialog.ticket = Converters.exportTo(dialog.converter, dialog.source, dialog.selectedFile.toString())
}
