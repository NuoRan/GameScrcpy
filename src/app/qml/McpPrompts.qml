pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: prompts

    signal attentionWanted(string question)

    function question(): string {
        if (McpServer.approval !== "")
            return qsTr("Let %1 use GameScrcpy?").arg(McpServer.approval)
        const asked = McpServer.confirmation
        const client = asked.client ?? ""
        switch (asked.kind ?? "") {
        case "apply":
            return qsTr("Let %1 apply its changes to %2?").arg(client).arg(asked.name ?? "")
        case "record":
            return qsTr("Let %1 record %2?").arg(client).arg(asked.phone ?? "")
        case "tool":
            return qsTr("Let %1 use the tool %2 of the extension %3?").arg(client).arg(asked.title ?? "")
                .arg(asked.extension ?? "")
        }
        return qsTr("Let %1 run a script on %2?").arg(client).arg(asked.phone ?? "")
    }

    function sync() {
        if (approval.visible || confirm.visible)
            return
        if (McpServer.approval !== "") {
            approval.client = McpServer.approval
            approval.answered = false
            remember.checked = false
            approval.open()
            return
        }
        const asked = McpServer.confirmation
        if (asked.id !== undefined) {
            confirm.shown = asked
            confirm.answered = false
            quiet.checked = false
            confirm.open()
        }
    }

    Connections {
        target: McpServer

        function onApprovalChanged() {
            if (approval.visible && !approval.answered && McpServer.approval !== approval.client)
                approval.close()
            Qt.callLater(prompts.sync)
        }

        function onConfirmationChanged() {
            if (confirm.visible && !confirm.answered && McpServer.confirmation.id !== confirm.shown.id)
                confirm.close()
            Qt.callLater(prompts.sync)
        }

        function onAttentionWanted() {
            prompts.attentionWanted(prompts.question())
        }
    }

    Component.onCompleted: sync()

    Dialog {
        id: approval

        property string client
        property bool answered: false

        objectName: "mcpApproval"
        title: qsTr("Let %1 use GameScrcpy?").arg(approval.client)
        acceptText: qsTr("Allow")
        rejectText: qsTr("Deny")
        onAccepted: {
            approval.answered = true
            McpServer.approve(approval.client, true, remember.checked)
        }
        onRejected: {
            approval.answered = true
            McpServer.approve(approval.client, false, false)
        }
        onClosed: {
            if (!approval.answered && McpServer.approval === approval.client)
                McpServer.approve(approval.client, false, false)
            Qt.callLater(prompts.sync)
        }

        Column {
            width: approval.availableWidth
            spacing: Theme.space.md

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: qsTr("%1 asks to connect through MCP. Once you allow it, it can:").arg(approval.client)
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.body.size
                wrapMode: Text.Wrap
            }

            Column {
                width: parent.width
                spacing: Theme.space.sm

                Repeater {
                    model: [
                        { icon: "code-xml", text: qsTr("Look up the script API and the profile format") },
                        { icon: "history", text: qsTr("Try logic on recordings, step by step, without touching a phone") },
                        { icon: "image", text: qsTr("See a phone's current picture and find pictures in it") },
                        { icon: "terminal", text: qsTr("Read GameScrcpy's log") },
                        { icon: "pencil", text: qsTr("Change profiles, components, tasks and blueprints in a draft; you confirm before a draft is used") },
                        { icon: "smartphone", text: qsTr("Run scripts on a phone; you confirm every run") },
                        { icon: "monitor-play", text: qsTr("Record a phone; you confirm every recording") },
                        { icon: "keyboard", text: qsTr("Suggest controls in the keymap editor; you accept each one") },
                        { icon: "puzzle", text: qsTr("Make extensions; you install them yourself") }
                    ]

                    delegate: Row {
                        id: ability

                        required property var modelData

                        width: parent ? parent.width : implicitWidth
                        spacing: Theme.space.sm

                        Icon {
                            name: ability.modelData.icon
                            size: Theme.size.iconSmall
                            color: Theme.colors.textSecondary
                        }

                        Text {
                            width: ability.width - Theme.size.iconSmall - ability.spacing
                            textFormat: Text.PlainText
                            text: ability.modelData.text
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.body.size
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            Checkbox {
                id: remember

                objectName: "mcpRemember"
                text: qsTr("Remember this client")
            }

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: qsTr("Remembered clients are listed in Settings, under AI connection, where you can remove them.")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }
        }
    }

    Dialog {
        id: confirm

        property var shown: ({})
        property bool answered: false
        readonly property string kind: confirm.shown.kind ?? ""
        readonly property bool applying: confirm.kind === "apply"
        readonly property bool running: confirm.kind === "run"
        readonly property bool recording: confirm.kind === "record"
        readonly property bool tooling: confirm.kind === "tool"
        readonly property var files: confirm.shown.files ?? []
        readonly property int conflicts: confirm.shown.conflicts ?? 0
        readonly property real bodyLimit: (confirm.parent ? confirm.parent.height : 0) - 2 * Theme.space.x6l
                                          - confirm.implicitHeaderHeight - confirm.implicitFooterHeight
                                          - confirm.topPadding - confirm.bottomPadding

        objectName: "mcpConfirm"
        implicitWidth: confirm.running || confirm.tooling ? Theme.size.workbenchCode : Theme.size.dialogWidth
        title: confirm.applying ? qsTr("Let %1 apply its changes to %2?").arg(confirm.shown.client ?? "").arg(confirm.shown.name ?? "")
               : confirm.recording ? qsTr("Let %1 record %2?").arg(confirm.shown.client ?? "").arg(confirm.shown.phone ?? "")
               : confirm.tooling ? qsTr("Let %1 use the tool %2 of the extension %3?").arg(confirm.shown.client ?? "")
                                       .arg(confirm.shown.title ?? "").arg(confirm.shown.extension ?? "")
               : qsTr("Let %1 run a script on %2?").arg(confirm.shown.client ?? "").arg(confirm.shown.phone ?? "")
        acceptText: confirm.applying ? qsTr("Apply")
                    : confirm.recording ? qsTr("Start recording")
                    : qsTr("Run")
        rejectText: confirm.applying ? qsTr("Not now") : qsTr("Refuse")
        onAccepted: {
            confirm.answered = true
            McpServer.answer(confirm.shown.id, true, (confirm.running || confirm.tooling) && quiet.checked)
        }
        onRejected: {
            confirm.answered = true
            McpServer.answer(confirm.shown.id, false)
        }
        onClosed: {
            if (!confirm.answered && McpServer.confirmation.id === confirm.shown.id)
                McpServer.answer(confirm.shown.id, false)
            Qt.callLater(prompts.sync)
        }

        Flickable {
            id: scroller

            width: confirm.availableWidth
            height: implicitHeight
            implicitHeight: Math.max(0, Math.min(body.implicitHeight, confirm.bodyLimit))
            contentWidth: width
            contentHeight: body.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Column {
                id: body

                width: scroller.width
                spacing: Theme.space.md

                Text {
                    width: parent.width
                    visible: confirm.applying
                    textFormat: Text.PlainText
                    text: qsTr("These files of %1 are changed:").arg(confirm.shown.name ?? "")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    wrapMode: Text.Wrap
                }

                Text {
                    objectName: "mcpConflicts"
                    width: parent.width
                    visible: confirm.applying && confirm.conflicts > 0
                    textFormat: Text.PlainText
                    text: qsTr("%n file(s) changed in GameScrcpy after the AI made its draft; applying replaces them with the draft.", "",
                               confirm.conflicts)
                    color: Theme.colors.warningText
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Column {
                    objectName: "mcpFiles"
                    width: parent.width
                    visible: confirm.applying
                    spacing: Theme.space.xs

                    Repeater {
                        model: confirm.files

                        delegate: Column {
                            id: changed

                            required property var modelData

                            readonly property string diffText: changed.modelData.diff ?? ""
                            property bool open: false

                            width: parent ? parent.width : implicitWidth
                            spacing: Theme.space.xxs

                            Rectangle {
                                objectName: "mcpFileRow"
                                width: parent.width
                                height: Theme.size.listItem
                                radius: Theme.radius.md
                                color: fileHover.hovered ? Theme.colors.stateHover : "transparent"

                                HoverHandler {
                                    id: fileHover
                                }

                                TapHandler {
                                    onTapped: changed.open = !changed.open
                                }

                                Row {
                                    anchors.fill: parent
                                    spacing: Theme.space.sm

                                    Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: changed.modelData.conflict === true
                                        name: "triangle-alert"
                                        size: Theme.size.iconSmall
                                        color: Theme.colors.warningText
                                    }

                                    Icon {
                                        objectName: "mcpFileChevron"
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: changed.open ? "chevron-down" : "chevron-right"
                                        size: Theme.size.iconSmall
                                        color: Theme.colors.textTertiary
                                    }

                                    Text {
                                        width: Math.min(implicitWidth, parent.width - counts.width - parent.spacing * 3
                                                        - (changed.modelData.conflict === true
                                                           ? Theme.size.iconSmall + parent.spacing : 0))
                                        anchors.verticalCenter: parent.verticalCenter
                                        textFormat: Text.PlainText
                                        text: changed.modelData.path
                                        color: Theme.colors.textPrimary
                                        font.family: Theme.type.monoFamily
                                        font.pixelSize: Theme.type.mono.size
                                        elide: Text.ElideMiddle
                                    }

                                    Text {
                                        id: counts

                                        anchors.verticalCenter: parent.verticalCenter
                                        textFormat: Text.PlainText
                                        text: changed.modelData.state === "removed" ? qsTr("removed")
                                              : changed.modelData.created === true
                                                ? qsTr("new, %n line(s)", "", changed.modelData.added)
                                                : "+" + changed.modelData.added + " \u2212" + changed.modelData.removed
                                        color: Theme.colors.textSecondary
                                        font.pixelSize: Theme.type.caption.size
                                    }
                                }
                            }

                            Rectangle {
                                objectName: "mcpFileDiff"
                                visible: changed.open && changed.diffText !== ""
                                width: parent.width
                                height: visible ? diffLines.implicitHeight + 2 * Theme.space.sm : 0
                                radius: Theme.radius.md
                                color: Theme.colors.bgCode
                                border.width: Theme.size.stroke
                                border.color: Theme.colors.strokeSubtle
                                clip: true

                                Column {
                                    id: diffLines

                                    x: Theme.space.sm
                                    y: Theme.space.sm
                                    width: parent.width - 2 * Theme.space.sm

                                    Repeater {
                                        model: changed.open ? changed.diffText.split("\n") : []

                                        delegate: Rectangle {
                                            id: diffRow

                                            required property var modelData

                                            readonly property bool addedLine: modelData.startsWith("+")
                                            readonly property bool removedLine: modelData.startsWith("-")
                                            readonly property bool headerLine: modelData.startsWith("@@")

                                            width: parent ? parent.width : implicitWidth
                                            height: lineText.implicitHeight
                                            color: addedLine ? Theme.colors.successSubtle
                                                   : removedLine ? Theme.colors.dangerSubtle
                                                   : headerLine ? Theme.colors.infoSubtle : "transparent"

                                            Text {
                                                id: lineText

                                                width: parent.width
                                                textFormat: Text.PlainText
                                                text: diffRow.modelData
                                                color: diffRow.addedLine ? Theme.colors.successText
                                                       : diffRow.removedLine ? Theme.colors.dangerText
                                                       : diffRow.headerLine ? Theme.colors.infoText
                                                       : Theme.colors.textSecondary
                                                font.family: Theme.type.monoFamily
                                                font.pixelSize: Theme.type.caption.size
                                                wrapMode: Text.WrapAnywhere
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Row {
                    objectName: "mcpPreviewRow"
                    width: parent.width
                    visible: confirm.applying && confirm.shown.previewable === true
                    spacing: Theme.space.sm

                    Button {
                        id: previewButton

                        objectName: "mcpOpenPreview"
                        small: true
                        iconName: "eye"
                        text: qsTr("Open preview")
                        onClicked: {
                            const target = confirm.shown.target ?? ""
                            confirm.answered = true
                            McpServer.answer(confirm.shown.id, false)
                            DeviceManager.openDraftPreview(target)
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, parent.width - parent.spacing - previewButton.width)
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        text: qsTr("Shows the draft in the key mapping editor, marked up, where you can apply it too. The draft is kept.")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                    }
                }

                Text {
                    width: parent.width
                    visible: confirm.applying && confirm.shown.folder === true
                    textFormat: Text.PlainText
                    text: qsTr("The profile becomes a folder so that it can hold these files.")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: confirm.applying
                    textFormat: Text.PlainText
                    text: qsTr("If you do not apply it now, the draft is kept and nothing you use changes.")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: confirm.recording
                    textFormat: Text.PlainText
                    text: qsTr("It records like the record button of the mirror window. %1 can stop it and try its logic on the recording.")
                          .arg(confirm.shown.client ?? "")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: confirm.running
                    textFormat: Text.PlainText
                    text: (confirm.shown.functionName ?? "") !== ""
                          ? qsTr("Runs the function %1 from the logic of %2 (%3):").arg(confirm.shown.functionName)
                                .arg(confirm.shown.profile ?? "").arg(confirm.shown.file ?? "")
                          : qsTr("The script:")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    wrapMode: Text.Wrap
                }

                Text {
                    objectName: "mcpToolDescription"
                    width: parent.width
                    visible: confirm.tooling
                    textFormat: Text.PlainText
                    text: qsTr("The extension says the tool does this: %1").arg(confirm.shown.description ?? "")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: confirm.tooling
                    textFormat: Text.PlainText
                    text: qsTr("Its logic runs in the extension's sandbox and may act on your phones. %1 gives it:")
                          .arg(confirm.shown.client ?? "")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.body.size
                    wrapMode: Text.Wrap
                }

                Rectangle {
                    width: parent.width
                    height: code.implicitHeight + 2 * Theme.space.md
                    visible: confirm.running || confirm.tooling
                    radius: Theme.radius.md
                    color: Theme.colors.bgCode
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeSubtle

                    Text {
                        id: code

                        objectName: "mcpCode"
                        x: Theme.space.md
                        y: Theme.space.md
                        width: parent.width - 2 * Theme.space.md
                        textFormat: Text.PlainText
                        text: confirm.tooling ? (confirm.shown.arguments ?? "") : (confirm.shown.code ?? "")
                        color: Theme.colors.textPrimary
                        font.family: Theme.type.monoFamily
                        font.pixelSize: Theme.type.code.size
                        wrapMode: Text.WrapAnywhere
                    }
                }

                Text {
                    width: parent.width
                    visible: confirm.running
                    textFormat: Text.PlainText
                    text: qsTr("It stops after %n second(s) at the latest, and its fingers are lifted when it ends.", "",
                               confirm.shown.seconds ?? 60)
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    visible: confirm.tooling
                    textFormat: Text.PlainText
                    text: qsTr("If it gives no answer within %n second(s), it is stopped.", "", confirm.shown.seconds ?? 60)
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Checkbox {
                    id: quiet

                    objectName: "mcpQuiet"
                    visible: (confirm.running || confirm.tooling) && confirm.shown.quiet === true
                    text: confirm.tooling ? qsTr("Don't ask again for this tool until GameScrcpy closes")
                                          : qsTr("Don't ask again for this phone until GameScrcpy closes")
                }

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: qsTr("If you do not answer within two minutes, it is refused.")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
