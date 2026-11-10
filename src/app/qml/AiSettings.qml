pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls
import GameScrcpy.App

SettingCard {
    id: card

    readonly property string service: AiAssistant.service
    readonly property string modelName: String(AppSettings.values["assistant." + service + ".model"] ?? "").trim()
    readonly property var traits: AiAssistant.service !== "" ? AiAssistant.traits(card.modelName) : ({})
    readonly property var efforts: card.traits.thinkingEffort === true
                                   && AppSettings.values["assistant." + service + ".thinking"] !== true
                                   ? [] : card.traits.efforts ?? []

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

    ModelRow {
        id: modelRow

        objectName: "aiModel"
        path: "assistant." + card.service + ".model"
    }

    SwitchSetting {
        objectName: "aiThinking"
        path: "assistant." + card.service + ".thinking"
        nested: true
        divider: false
        visible: matched && card.traits.think === true
    }

    SettingsRow {
        objectName: "aiAlwaysThinks"
        width: parent ? parent.width : implicitWidth
        visible: modelRow.matched && card.traits.alwaysThinks === true
        nested: true
        title: qsTr("Thinks before every answer")
        description: qsTr("This model always thinks first and cannot be told not to; the assistant shows the "
                          + "thinking above the answer.")
    }

    EffortRow {
        objectName: "aiEffort"
        path: "assistant." + card.service + ".effort"
        nested: true
        divider: false
        visible: matched && card.efforts.length > 0
    }

    SwitchSetting {
        objectName: "aiLongContext"
        path: "assistant." + card.service + ".longContext"
        nested: true
        divider: false
        visible: matched && card.traits.longContext === true
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
        description: qsTr("Every conversation with the assistant is kept on this computer. The newest %n are in "
                          + "the Recent menu of the assistant; all of them are in its own window.", "",
                          AiChats.menuShown)
        note: AiChats.recent.length === 0 ? qsTr("None yet") : qsTr("%n saved", "", AiChats.recent.length)

        Button {
            objectName: "aiHistoryOpen"
            small: true
            tone: "ghost"
            iconName: "history"
            text: qsTr("Show all")
            enabled: AiChats.recent.length > 0
            onClicked: AiChats.openWindow()
        }

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

    component ModelRow: SettingRow {
        id: chooser

        property bool asked: false

        function shown(stored: var): string {
            return stored === undefined ? "" : String(stored)
        }

        function revert() {
            modelField.errorText = ""
            modelField.text = Qt.binding(() => chooser.shown(chooser.value))
        }

        function submit() {
            if (modelField.text === chooser.shown(chooser.value)) {
                chooser.revert()
                return
            }
            if (!chooser.commit(modelField.text.trim())) {
                modelField.errorText = qsTr("Could not save this text")
                return
            }
            chooser.revert()
        }

        function fetch() {
            chooser.asked = true
            AiAssistant.fetchModels()
        }

        function showModels() {
            if (AiAssistant.models.length > 0)
                modelsMenu.open()
            else
                chooser.fetch()
        }

        note: AiAssistant.modelsState === "running" ? qsTr("Asking the service…") : AiAssistant.modelsProblem
        onValueChanged: chooser.revert()
        onPathChanged: chooser.revert()

        Connections {
            target: AiAssistant

            function onModelsChanged() {
                if (chooser.asked && AiAssistant.modelsState !== "running") {
                    chooser.asked = false
                    if (AiAssistant.models.length > 0)
                        modelsMenu.open()
                }
            }
        }

        TextField {
            id: modelField

            objectName: "aiModelField"
            implicitWidth: Theme.size.captureField
            rightPadding: modelField.edge + Theme.size.iconSmall + Theme.space.sm
            maximumLength: chooser.entry && chooser.entry.maxLength > 0 ? chooser.entry.maxLength : 256
            placeholderText: card.service === "anthropic" ? qsTr("For example claude-sonnet-4-5")
                                                           : qsTr("For example gpt-4.1 or deepseek-chat")
            text: chooser.shown(chooser.value)
            Accessible.name: chooser.title
            onEditingFinished: chooser.submit()
            Keys.onEscapePressed: chooser.revert()
            Keys.onDownPressed: chooser.showModels()

            T.AbstractButton {
                id: dropButton

                objectName: "aiModelsDrop"
                x: modelField.width - width - Theme.space.xs
                y: (modelField.height - modelField.messageHeight - height) / 2
                width: Theme.size.segment
                height: Theme.size.segment
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                enabled: AiAssistant.modelsState !== "running"
                Accessible.name: qsTr("Show the models of this service")
                onClicked: chooser.showModels()

                contentItem: Icon {
                    name: "chevron-down"
                    size: Theme.size.iconSmall
                    color: dropButton.hovered ? Theme.colors.textPrimary : Theme.colors.textSecondary
                    rotation: modelsMenu.visible ? 180 : 0

                    Behavior on rotation {
                        NumberAnimation {
                            duration: Theme.motion.fast
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: Theme.motion.standard
                        }
                    }
                }

                background: StateLayer {
                    radius: Theme.radius.sm
                    hovered: dropButton.hovered
                    pressed: dropButton.down
                }
            }

            Menu {
                id: modelsMenu

                objectName: "aiModelsMenu"
                y: modelField.height + Theme.space.xs
                width: Math.max(modelField.width, implicitWidth)

                Instantiator {
                    model: AiAssistant.models

                    delegate: MenuItem {
                        required property string modelData

                        text: modelData
                        choice: true
                        chosen: modelData === card.modelName
                        onTriggered: chooser.commit(modelData)
                    }

                    onObjectAdded: (index, object) => modelsMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => modelsMenu.removeItem(object)
                }
            }
        }

        Button {
            objectName: "aiModelsFetch"
            small: true
            iconName: "refresh-cw"
            text: qsTr("Get")
            toolTip: qsTr("Asks the service which models the key can use")
            enabled: AiAssistant.modelsState !== "running"
            onClicked: chooser.fetch()
        }
    }

    component EffortRow: SettingRow {
        id: effortRow

        readonly property var options: ["auto"].concat(card.efforts)
        readonly property var labels: ({ auto: qsTr("Automatic"), minimal: qsTr("Minimal"), low: qsTr("Low"),
                                         medium: qsTr("Medium"), high: qsTr("High") })

        globalText: effortRow.labels[globalValue] ?? String(globalValue)

        SegmentedControl {
            id: effortSegments

            model: effortRow.options.map(option => effortRow.labels[option] ?? String(option))
            currentIndex: effortRow.options.indexOf(effortRow.value)
            Accessible.name: effortRow.title
            onActivated: index => {
                effortRow.commit(effortRow.options[index])
                effortSegments.currentIndex = Qt.binding(() => effortRow.options.indexOf(effortRow.value))
            }
        }
    }
}
