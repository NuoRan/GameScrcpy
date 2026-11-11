pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Controls
import GameScrcpy.App

SettingCard {
    id: card

    readonly property var engines: SpeechEngines.engines
    readonly property var speakers: card.engines.filter(engine => engine.say)
    readonly property var listeners: card.engines.filter(engine => engine.listen)
    readonly property string speaker: card.chosen("speech.engine")
    readonly property string listener: card.chosen("speech.listenEngine")
    readonly property var listenEngine: card.listeners.find(engine => engine.id === card.listener) ?? null
    property var voices: []
    property string voiceProblem

    function chosen(path: string): string {
        const value = String(AppSettings.values[path] ?? "")
        return value === "" ? "system" : value
    }

    function titleOf(list: var, id: string): string {
        const found = list.find(engine => engine.id === id)
        return found ? found.title : id
    }

    function languageName(code: string): string {
        if (code === "")
            return qsTr("Same as the interface")
        const named = Qt.locale(code.replace("-", "_")).nativeLanguageName
        return named !== "" ? qsTr("%1 (%2)").arg(named).arg(code) : code
    }

    title: qsTr("Reading aloud and voice commands")
    iconName: "audio-lines"
    tone: "rose"
    note: qsTr("Used by speech.say and speech.listen in logic")

    Component.onCompleted: SpeechEngines.loadVoices(card.speaker)
    onSpeakerChanged: SpeechEngines.loadVoices(card.speaker)

    Connections {
        target: SpeechEngines

        function onVoicesLoaded(engine: string, voices: var, problem: string) {
            if (engine !== card.speaker)
                return
            card.voices = voices
            card.voiceProblem = problem
        }

        function onEnginesChanged() {
            SpeechEngines.loadVoices(card.speaker)
        }
    }

    SettingRow {
        id: engineRow

        objectName: "speechEngine"
        path: "speech.engine"
        globalText: card.titleOf(card.speakers, card.speaker)
        fact: card.speakers.length === 0 ? qsTr("Windows has no voice; add one under Time & language, Speech.") : ""

        Select {
            objectName: "speechEngineSelect"
            model: card.speakers.map(engine => engine.title)
            currentIndex: card.speakers.findIndex(engine => engine.id === card.speaker)
            enabled: card.speakers.length > 0
            Accessible.name: engineRow.title
            onActivated: index => {
                const next = card.speakers[index].id
                if (next !== card.speaker && engineRow.commit(next))
                    AppSettings.setValue("speech.voice", "")
            }
        }
    }

    SettingRow {
        id: voiceRow

        readonly property var options: [{ id: "", name: qsTr("The engine's default"), language: "" }].concat(card.voices)

        objectName: "speechVoice"
        path: "speech.voice"
        globalText: (voiceRow.options.find(voice => voice.id === String(voiceRow.globalValue ?? "")) ?? voiceRow.options[0]).name
        fact: card.voiceProblem

        Select {
            objectName: "speechVoiceSelect"
            model: voiceRow.options.map(voice => voice.language !== ""
                                        ? qsTr("%1 (%2)").arg(voice.name).arg(voice.language) : voice.name)
            currentIndex: Math.max(0, voiceRow.options.findIndex(voice => voice.id === String(voiceRow.value ?? "")))
            enabled: card.speakers.length > 0
            Accessible.name: voiceRow.title
            onActivated: index => voiceRow.commit(voiceRow.options[index].id)
        }
    }

    SliderSetting {
        objectName: "speechRate"
        path: "speech.rate"
        unit: "%"
        step: 10
        marks: [50, 100, 150, 200]
    }

    SliderSetting {
        objectName: "speechVolume"
        path: "speech.volume"
        unit: "%"
        step: 5
    }

    SettingsRow {
        objectName: "speechPreview"
        width: parent ? parent.width : implicitWidth
        visible: engineRow.matched
        divider: true
        title: qsTr("Try it")
        description: qsTr("Reads a short sentence with the engine, voice, rate and volume above.")

        Button {
            objectName: "speechPreviewButton"
            small: true
            iconName: "play"
            text: qsTr("Read aloud")
            enabled: card.speakers.length > 0
            onClicked: SpeechEngines.preview()
        }
    }

    SettingRow {
        id: listenRow

        objectName: "speechListenEngine"
        path: "speech.listenEngine"
        globalText: card.titleOf(card.listeners, card.listener)
        fact: card.listeners.length === 0
              ? qsTr("Windows has no speech recognizer; add a speech language under Time & language, Speech.") : ""

        Select {
            objectName: "speechListenSelect"
            model: card.listeners.map(engine => engine.title)
            currentIndex: card.listeners.findIndex(engine => engine.id === card.listener)
            enabled: card.listeners.length > 0
            Accessible.name: listenRow.title
            onActivated: index => {
                const next = card.listeners[index].id
                if (next !== card.listener && listenRow.commit(next))
                    AppSettings.setValue("speech.language", "")
            }
        }
    }

    SettingRow {
        id: languageRow

        readonly property var options: [""].concat(card.listenEngine ? card.listenEngine.languages : [])

        objectName: "speechLanguage"
        path: "speech.language"
        globalText: card.languageName(String(languageRow.globalValue ?? ""))

        Select {
            objectName: "speechLanguageSelect"
            model: languageRow.options.map(code => card.languageName(code))
            currentIndex: Math.max(0, languageRow.options.indexOf(String(languageRow.value ?? "")))
            enabled: languageRow.options.length > 1
            Accessible.name: languageRow.title
            onActivated: index => languageRow.commit(languageRow.options[index])
        }
    }
}
