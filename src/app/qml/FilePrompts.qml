pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import GameScrcpy.App

Item {
    id: prompts

    property string ticket

    signal attentionWanted(string question)

    function question(): string {
        const asked = FileAccess.prompt
        const own = asked.own === true
        const requester = asked.requester ?? ""
        if (asked.kind === "save")
            return own ? qsTr("Your logic wants to save a file") : qsTr("%1 wants to save a file").arg(requester)
        if (asked.kind === "folder")
            return own ? qsTr("Choose the folder %1 for your logic").arg(asked.folder ?? "")
                       : qsTr("Choose the folder %1 for %2").arg(asked.folder ?? "").arg(requester)
        return own ? qsTr("Your logic wants to open a file") : qsTr("%1 wants to open a file").arg(requester)
    }

    function sync() {
        const asked = FileAccess.prompt
        if (prompts.ticket !== "") {
            if (asked.ticket === prompts.ticket)
                return
            prompts.ticket = ""
            fileDialog.close()
            folderDialog.close()
        }
        if (asked.open !== true)
            return
        prompts.ticket = asked.ticket
        if (asked.kind === "folder") {
            const purpose = asked.purpose ?? ""
            folderDialog.title = purpose !== "" ? prompts.question() + " · " + purpose : prompts.question()
            folderDialog.currentFolder = asked.startUrl
            folderDialog.open()
            return
        }
        fileDialog.title = prompts.question()
        fileDialog.fileMode = asked.kind === "save" ? FileDialog.SaveFile : FileDialog.OpenFile
        fileDialog.nameFilters = asked.filters ?? []
        fileDialog.currentFolder = asked.startUrl
        fileDialog.selectedFile = asked.suggestedUrl ?? ""
        fileDialog.open()
    }

    function settle(chosen: url) {
        const ticket = prompts.ticket
        prompts.ticket = ""
        FileAccess.choose(ticket, chosen.toString())
    }

    function decline() {
        const ticket = prompts.ticket
        prompts.ticket = ""
        FileAccess.cancelPrompt(ticket)
    }

    Connections {
        target: FileAccess

        function onPromptChanged() {
            Qt.callLater(prompts.sync)
        }

        function onAttentionWanted() {
            prompts.attentionWanted(prompts.question())
        }
    }

    Component.onCompleted: sync()

    FileDialog {
        id: fileDialog

        objectName: "logicFileDialog"
        onAccepted: prompts.settle(fileDialog.selectedFile)
        onRejected: prompts.decline()
    }

    FolderDialog {
        id: folderDialog

        objectName: "logicFolderDialog"
        onAccepted: prompts.settle(folderDialog.selectedFolder)
        onRejected: prompts.decline()
    }
}
