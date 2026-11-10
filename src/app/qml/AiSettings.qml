pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

SettingCard {
    id: card

    readonly property string service: AiAssistant.service

    function saveKey() {
        if (AiAssistant.saveKey(keyField.text))
            keyField.clear()
    }

    title: qsTr("AI assistant")
    iconName: "bot"
    tone: "azure"

    SegmentsSetting {
        id: serviceRow

        objectName: "aiService"
        path: "assistant.service"
        labels: ({ openai: qsTr("Compatible with OpenAI"), openaiResponses: qsTr("OpenAI Responses"),
                   anthropic: qsTr("Anthropic") })
    }

    TextRow {
        objectName: "aiAddress"
        path: "assistant." + card.service + ".address"
        placeholder: AiAssistant.defaultAddress
        wide: true
    }

    TextRow {
        objectName: "aiModel"
        path: "assistant." + card.service + ".model"
        placeholder: card.service === "anthropic" ? qsTr("For example claude-sonnet-4-5")
                                                   : qsTr("For example gpt-4.1 or deepseek-chat")
    }

    SettingsRow {
        objectName: "aiModels"
        width: parent ? parent.width : implicitWidth
        visible: serviceRow.matched
        divider: true
        title: qsTr("Models of this service")
        description: qsTr("Asks the service which models the key can use, so you can pick one instead of typing it.")
        note: AiAssistant.modelsState === "running" ? qsTr("Asking the service…") : AiAssistant.modelsProblem

        Button {
            id: modelsButton

            property bool asked: false

            objectName: "aiModelsButton"
            small: true
            iconName: "chevron-down"
            text: AiAssistant.models.length > 0 ? qsTr("Choose a model…") : qsTr("Get the list")
            enabled: AiAssistant.modelsState !== "running"
            onClicked: {
                if (AiAssistant.models.length > 0) {
                    modelsMenu.open()
                } else {
                    modelsButton.asked = true
                    AiAssistant.fetchModels()
                }
            }

            Connections {
                target: AiAssistant

                function onModelsChanged() {
                    if (modelsButton.asked && AiAssistant.modelsState !== "running") {
                        modelsButton.asked = false
                        if (AiAssistant.models.length > 0)
                            modelsMenu.open()
                    }
                }
            }

            Menu {
                id: modelsMenu

                objectName: "aiModelsMenu"
                y: modelsButton.height + Theme.space.xs

                Instantiator {
                    model: AiAssistant.models

                    delegate: MenuItem {
                        required property string modelData

                        text: modelData
                        onTriggered: AppSettings.setValue("assistant." + card.service + ".model", modelData)
                    }

                    onObjectAdded: (index, object) => modelsMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => modelsMenu.removeItem(object)
                }
            }
        }

        Button {
            objectName: "aiModelsRefresh"
            visible: AiAssistant.models.length > 0
            small: true
            tone: "ghost"
            iconName: "refresh-cw"
            toolTip: qsTr("Ask the service again")
            enabled: AiAssistant.modelsState !== "running"
            onClicked: AiAssistant.fetchModels()
        }
    }

    SettingsRow {
        id: keyRow

        objectName: "aiKey"
        width: parent ? parent.width : implicitWidth
        visible: serviceRow.matched
        divider: true
        title: qsTr("API key")
        description: qsTr("Kept in Windows Credential Manager under %1 and never shown again. A service on this "
                          + "computer, such as Ollama, may need none.").arg(AiAssistant.keyTarget)
        note: AiAssistant.keyProblem !== "" ? AiAssistant.keyProblem
              : AiAssistant.hasKey ? qsTr("A key is saved.") : qsTr("No key is saved.")

        TextField {
            id: keyField

            objectName: "aiKeyField"
            implicitWidth: Theme.size.fieldWidth
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhHiddenText
            placeholderText: AiAssistant.hasKey ? qsTr("Type a new key to replace it") : qsTr("Paste the key")
            Accessible.name: keyRow.title
            onAccepted: card.saveKey()
        }

        Button {
            objectName: "aiKeySave"
            small: true
            text: qsTr("Save")
            enabled: keyField.text.trim() !== ""
            onClicked: card.saveKey()
        }

        Button {
            objectName: "aiKeyRemove"
            visible: AiAssistant.hasKey
            small: true
            tone: "ghost"
            text: qsTr("Remove")
            Accessible.description: keyRow.title
            onClicked: AiAssistant.forgetKey()
        }
    }

    SettingsRow {
        objectName: "aiTest"
        width: parent ? parent.width : implicitWidth
        visible: serviceRow.matched
        divider: true
        title: qsTr("Test connection")
        description: qsTr("Sends one short message with these settings and shows whether the service answers.")
        note: AiAssistant.testState === "running" ? qsTr("Waiting for the service…") : AiAssistant.testMessage

        StatusPill {
            objectName: "aiTestResult"
            visible: AiAssistant.testState === "passed" || AiAssistant.testState === "failed"
            tone: AiAssistant.testState === "passed" ? "ok" : "bad"
            text: AiAssistant.testState === "passed" ? qsTr("Connected") : qsTr("Failed")
        }

        Button {
            objectName: "aiTestButton"
            small: true
            iconName: "zap"
            text: qsTr("Test connection")
            enabled: AiAssistant.testState !== "running"
            onClicked: AiAssistant.testConnection()
        }
    }

    SwitchSetting {
        objectName: "aiDeepThinking"
        path: "assistant.deepThinking"
    }

    SwitchSetting {
        objectName: "aiExplain"
        path: "assistant.explain"
    }

    SwitchSetting {
        id: scriptsRow

        objectName: "aiScripts"
        path: "assistant.scripts"
        fact: scriptsRow.value === true && !AiAssistant.ready
              ? qsTr("Logic that asks gets an error until the service above is set up.") : ""
    }

    SwitchSetting {
        objectName: "aiBubble"
        path: "assistant.bubble"
    }

    SwitchSetting {
        objectName: "aiMirrorBubble"
        path: "assistant.mirrorBubble"
    }

    SettingsRow {
        objectName: "aiHistory"
        width: parent ? parent.width : implicitWidth
        visible: serviceRow.matched
        divider: true
        title: qsTr("Saved conversations")
        description: qsTr("The newest %n conversation(s) with the assistant are kept on this computer.", "",
                          AiChats.mostKept)
        note: AiChats.recent.length === 0 ? qsTr("None yet") : qsTr("%n saved", "", AiChats.recent.length)

        Button {
            objectName: "aiHistoryClear"
            small: true
            tone: "ghost"
            iconName: "trash-2"
            text: qsTr("Delete all")
            enabled: AiChats.recent.length > 0
            onClicked: AiChats.clearAll()
        }
    }

    component TextRow: SettingRow {
        id: row

        property string placeholder
        property bool wide: false

        function shown(stored: var): string {
            return stored === undefined ? "" : String(stored)
        }

        function revert() {
            field.errorText = ""
            field.text = Qt.binding(() => row.shown(row.value))
        }

        function submit() {
            if (field.text === row.shown(row.value)) {
                row.revert()
                return
            }
            if (!row.commit(field.text.trim())) {
                field.errorText = qsTr("Could not save this text")
                return
            }
            row.revert()
        }

        onValueChanged: row.revert()
        onPathChanged: row.revert()

        TextField {
            id: field

            implicitWidth: row.wide ? Theme.size.captureField : Theme.size.fieldWidth
            maximumLength: row.entry && row.entry.maxLength > 0 ? row.entry.maxLength : 2048
            placeholderText: row.placeholder
            text: row.shown(row.value)
            Accessible.name: row.title
            onEditingFinished: row.submit()
            Keys.onEscapePressed: row.revert()
        }
    }
}
