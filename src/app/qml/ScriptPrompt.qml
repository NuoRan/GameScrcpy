pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls

Item {
    id: card

    property string request
    property string kind
    property string question
    property var choices: []
    property string initial
    property string okText
    property string cancelText
    property string profileName
    property var changes: []
    property string extensionName
    readonly property real widest: parent ? Math.min(Theme.size.toastMaxWidth, parent.width - 2 * Theme.space.sm)
                                          : Theme.size.toastMaxWidth

    signal answered(string request, string json)

    function open(shown: string, prompt: var) {
        card.request = shown
        card.kind = prompt.kind ?? "confirm"
        card.question = prompt.question ?? ""
        card.choices = prompt.choices ?? []
        card.initial = prompt.initial ?? ""
        card.okText = prompt.ok ?? ""
        card.cancelText = prompt.cancel ?? ""
        card.profileName = prompt.profile ?? ""
        card.changes = prompt.changes ?? []
        card.extensionName = prompt.extension ?? ""
        answerField.text = card.initial
        card.visible = true
        if (card.kind === "ask")
            answerField.forceActiveFocus()
    }

    function finish(json: string) {
        const shown = card.request
        card.visible = false
        card.request = ""
        card.answered(shown, json)
    }

    function dismiss() {
        card.finish(card.kind === "confirm" || card.kind === "save" ? "false" : "null")
    }

    function propLabel(prop: string): string {
        switch (prop) {
        case "position":
            return qsTr("Place")
        case "rate":
            return qsTr("Presses a second")
        case "duration":
            return qsTr("Duration (ms)")
        case "radius":
            return qsTr("Radius")
        case "sensitivity":
            return qsTr("Sensitivity")
        default:
            return prop
        }
    }

    visible: false
    width: widest
    height: body.implicitHeight + 2 * Theme.space.md

    Accessible.role: Accessible.Dialog
    Accessible.name: card.kind === "save" ? saveTitle.text : card.question

    PopupSurface {
        anchors.fill: parent
        radius: Theme.radius.xl
    }

    Column {
        id: body

        x: Theme.space.md
        y: Theme.space.md
        width: card.width - 2 * Theme.space.md
        spacing: Theme.space.sm

        Text {
            objectName: "scriptPromptSource"
            width: parent.width
            visible: card.extensionName !== "" && card.kind !== "save"
            textFormat: Text.PlainText
            text: qsTr("From %1").arg(card.extensionName)
            elide: Text.ElideRight
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
        }

        Text {
            width: parent.width
            visible: card.kind !== "save"
            textFormat: Text.PlainText
            text: card.question
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.body.size
            wrapMode: Text.Wrap
        }

        Text {
            id: saveTitle

            width: parent.width
            visible: card.kind === "save"
            textFormat: Text.PlainText
            text: card.extensionName !== ""
                  ? qsTr("Save the changes of %1 to the profile %2?").arg(card.extensionName).arg(card.profileName)
                  : qsTr("Save the changes the logic made to %1?").arg(card.profileName)
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.body.size
            font.weight: Theme.type.label.weight
            wrapMode: Text.Wrap
        }

        Column {
            objectName: "scriptPromptChanges"
            width: parent.width
            spacing: Theme.space.xxs
            visible: card.kind === "save"

            Repeater {
                model: card.kind === "save" ? card.changes : []

                Item {
                    id: change

                    required property var modelData

                    width: parent.width
                    height: Math.max(what.implicitHeight, values.implicitHeight)

                    Text {
                        id: what

                        width: parent.width - values.implicitWidth - Theme.space.md
                        textFormat: Text.PlainText
                        text: qsTr("%1 · %2").arg(change.modelData.name ?? "").arg(card.propLabel(change.modelData.prop ?? ""))
                        elide: Text.ElideRight
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                    }

                    Text {
                        id: values

                        anchors.right: parent.right
                        textFormat: Text.PlainText
                        text: qsTr("%1 → %2").arg(change.modelData.before ?? "").arg(change.modelData.after ?? "")
                        color: Theme.colors.textPrimary
                        font.family: Theme.type.monoFamily
                        font.pixelSize: Theme.type.caption.size
                    }
                }
            }
        }

        TextField {
            id: answerField

            objectName: "scriptPromptField"
            width: parent.width
            visible: card.kind === "ask"
            maximumLength: 200
            onAccepted: card.finish(JSON.stringify(answerField.text))
        }

        Flow {
            width: parent.width
            spacing: Theme.space.xs
            visible: card.kind === "choose"

            Repeater {
                model: card.kind === "choose" ? card.choices : []

                Button {
                    required property string modelData
                    required property int index

                    text: modelData
                    onClicked: card.finish(String(index))
                }
            }
        }

        Row {
            anchors.right: parent.right
            spacing: Theme.space.xs

            Button {
                objectName: "scriptPromptCancel"
                text: card.cancelText !== "" ? card.cancelText
                      : card.kind === "save" ? qsTr("Don't save")
                      : card.kind === "confirm" ? qsTr("No") : qsTr("Cancel")
                onClicked: card.dismiss()
            }

            Button {
                objectName: "scriptPromptOk"
                visible: card.kind !== "choose"
                tone: "primary"
                text: card.okText !== "" ? card.okText
                      : card.kind === "save" ? qsTr("Save")
                      : card.kind === "confirm" ? qsTr("Yes") : qsTr("OK")
                onClicked: card.finish(card.kind === "confirm" || card.kind === "save" ? "true"
                                                                                       : JSON.stringify(answerField.text))
            }
        }
    }
}
