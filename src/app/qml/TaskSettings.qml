pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

Item {
    id: settings

    property var task: null
    property var runs: []
    property var mirrors: []
    property var dayNames: []
    readonly property bool readOnly: settings.task !== null && settings.task.readOnly === true
    readonly property var modes: ["manual", "daily", "weekly", "once"]
    readonly property var phones: {
        if (settings.task === null)
            return []
        const list = settings.mirrors.map(mirror => ({ serial: mirror.serial, name: mirror.name, online: true }))
        for (const serial of settings.task.phones) {
            if (!list.some(phone => phone.serial === serial))
                list.push({ serial: serial, name: serial, online: false })
        }
        return list
    }

    signal notify(string message, string tone)

    function togglePhone(serial: string, on: bool) {
        const chosen = settings.task.phones.filter(each => each !== serial)
        if (on)
            chosen.push(serial)
        TaskLibrary.setPhones(settings.task.id, chosen)
    }

    function runOn(serial: string): var {
        return settings.runs.find(run => run.serial === serial) ?? null
    }

    function toggleDay(day: int, on: bool) {
        const chosen = (settings.task.days ?? []).filter(each => each !== day)
        if (on)
            chosen.push(day)
        if (!TaskLibrary.setDays(settings.task.id, chosen))
            settings.notify(qsTr("A weekly task runs on at least one day"), "warn")
    }

    component SectionTitle: Text {
        width: parent.width
        topPadding: Theme.space.sm
        textFormat: Text.PlainText
        color: Theme.colors.textPrimary
        font.pixelSize: Theme.type.label.size
        font.weight: Font.Medium
    }

    component Note: Text {
        width: parent.width
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
    }

    component SwitchRow: Item {
        id: switchRow

        property string label
        property bool on: false

        signal toggled(bool on)

        width: parent.width
        height: Math.max(switchLabel.implicitHeight, toggle.implicitHeight)

        Text {
            id: switchLabel

            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - toggle.width - Theme.space.md
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: switchRow.label
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
        }

        Switch {
            id: toggle

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            checked: switchRow.on
            Accessible.name: switchRow.label
            onToggled: switchRow.toggled(toggle.checked)
        }
    }

    Flickable {
        id: scroll

        anchors.fill: parent
        anchors.bottomMargin: footer.height
        visible: settings.task !== null
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: body.implicitHeight + 2 * Theme.space.lg

        Column {
            id: body

            x: Theme.space.lg
            y: Theme.space.lg
            width: scroll.width - 2 * Theme.space.lg
            spacing: Theme.space.md

            Row {
                spacing: Theme.space.md

                Tile {
                    id: headTile

                    large: true
                    tone: settings.runs.length > 0 ? "teal" : ""
                    iconName: "bot"
                }

                Column {
                    width: body.width - headTile.width - Theme.space.md
                    spacing: Theme.space.xxs

                    TextField {
                        objectName: "taskName"
                        width: parent.width
                        enabled: !settings.readOnly
                        bare: true
                        dense: true
                        showsMessage: false
                        text: settings.task !== null ? settings.task.name : ""
                        Accessible.name: qsTr("Task name")
                        onEditingFinished: {
                            if (settings.task !== null && text.trim() !== "")
                                TaskLibrary.rename(settings.task.id, text)
                        }
                    }

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
                        text: settings.runs.length > 0 ? qsTr("Running on %n phone(s)", "", settings.runs.length)
                                                       : qsTr("Not running")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }
                }
            }

            Note {
                objectName: "readOnlyTaskNote"
                visible: settings.readOnly
                text: qsTr("This task comes with the extension %1. You can run it, and it runs in the extension's sandbox; copy it to your tasks to change it.")
                      .arg(settings.task !== null ? (settings.task.extension ?? "") : "")
            }

            Note {
                objectName: "taskExampleNote"
                visible: text !== ""
                text: settings.task !== null ? settings.task.exampleNote ?? "" : ""
            }

            SectionTitle {
                text: qsTr("Phones to run on")
            }

            Note {
                text: qsTr("Each ticked phone runs its own copy and looks at its own picture.")
            }

            Repeater {
                model: settings.phones

                delegate: Item {
                    id: phoneRow

                    required property var modelData
                    readonly property var run: settings.runOn(modelData.serial)

                    objectName: "taskPhone"
                    width: body.width
                    height: Theme.size.listItem

                    Checkbox {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - phoneState.width - Theme.space.md
                        enabled: !settings.readOnly
                        text: phoneRow.modelData.name
                        checked: settings.task !== null && settings.task.phones.includes(phoneRow.modelData.serial)
                        onToggled: settings.togglePhone(phoneRow.modelData.serial, checked)
                    }

                    StatusPill {
                        id: phoneState

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        tone: phoneRow.run !== null ? "live" : phoneRow.modelData.online ? "idle" : "warn"
                        text: phoneRow.run !== null ? qsTr("Running") : phoneRow.modelData.online ? qsTr("Idle")
                                                                                                  : qsTr("Not mirroring")
                    }
                }
            }

            Note {
                visible: settings.phones.length === 0
                text: qsTr("Mirror a phone, then tick it here.")
            }

            SectionTitle {
                text: qsTr("How it starts")
            }

            SegmentedControl {
                objectName: "scheduleMode"
                width: body.width
                enabled: !settings.readOnly
                model: [qsTr("By hand"), qsTr("Every day"), qsTr("Every week"), qsTr("Once")]
                currentIndex: settings.task !== null ? Math.max(0, settings.modes.indexOf(settings.task.schedule)) : 0
                Accessible.name: qsTr("How the task starts")
                onActivated: index => TaskLibrary.setSchedule(settings.task.id, settings.modes[index], settings.task.time)
            }

            Flow {
                objectName: "scheduleDays"
                width: body.width
                visible: settings.task !== null && settings.task.schedule === "weekly"
                spacing: Theme.space.xs

                Repeater {
                    model: 7

                    delegate: FilterChip {
                        id: dayChip

                        required property int index
                        readonly property int day: dayChip.index + 1

                        objectName: "scheduleDay" + dayChip.day
                        enabled: !settings.readOnly
                        text: settings.dayNames[dayChip.index] ?? String(dayChip.day)
                        selected: settings.task !== null && (settings.task.days ?? []).includes(dayChip.day)
                        onClicked: settings.toggleDay(dayChip.day, !dayChip.selected)
                    }
                }
            }

            Item {
                width: body.width
                height: timeField.implicitHeight
                visible: settings.task !== null && settings.task.schedule !== "manual"

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: qsTr("Time")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                }

                TextField {
                    id: timeField

                    objectName: "scheduleTime"
                    anchors.right: parent.right
                    width: Theme.size.fieldWidth / 2
                    enabled: !settings.readOnly
                    dense: true
                    mono: true
                    showsMessage: false
                    text: settings.task !== null ? settings.task.time : ""
                    validator: RegularExpressionValidator {
                        regularExpression: /^([01][0-9]|2[0-3]):[0-5][0-9]$/
                    }
                    Accessible.name: qsTr("Start time")
                    onEditingFinished: {
                        if (acceptableInput && settings.task !== null)
                            TaskLibrary.setSchedule(settings.task.id, settings.task.schedule, text)
                    }
                }
            }

            Note {
                visible: settings.task !== null && settings.task.schedule !== "manual"
                text: qsTr("At that time GameScrcpy connects the ticked phones, turns their screens on and starts "
                           + "the task, and tells you when it is done or fails. GameScrcpy has to be running, in the "
                           + "tray is enough; a start missed while the computer sleeps is not made up.")
            }

            SectionTitle {
                text: qsTr("Settings")
            }

            Repeater {
                model: settings.task !== null ? settings.task.params : []

                delegate: Column {
                    id: param

                    required property var modelData

                    objectName: "taskParam"
                    width: body.width
                    enabled: !settings.readOnly
                    spacing: Theme.space.xs

                    Text {
                        visible: param.modelData.kind !== "toggle"
                        width: parent.width
                        textFormat: Text.PlainText
                        text: param.modelData.label
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                    }

                    NumberField {
                        id: numberBox

                        visible: param.modelData.kind === "number"
                        width: parent.width
                        value: param.modelData.kind === "number" ? param.modelData.value : 0
                        numbers: NumberScale {
                            minimum: param.modelData.min ?? -999999
                            maximum: param.modelData.max ?? 999999
                            step: 1
                            decimals: Number.isInteger(numberBox.value) ? 0 : 2
                        }
                        Accessible.name: param.modelData.label
                        onSubmitted: number => TaskLibrary.setValue(settings.task.id, param.modelData.name, number)
                    }

                    TextField {
                        visible: param.modelData.kind === "text"
                        width: parent.width
                        showsMessage: false
                        text: param.modelData.kind === "text" ? param.modelData.value : ""
                        Accessible.name: param.modelData.label
                        onEditingFinished: TaskLibrary.setValue(settings.task.id, param.modelData.name, text)
                    }

                    SwitchRow {
                        visible: param.modelData.kind === "toggle"
                        label: param.modelData.label
                        on: param.modelData.kind === "toggle" && param.modelData.value === true
                        onToggled: on => TaskLibrary.setValue(settings.task.id, param.modelData.name, on)
                    }
                }
            }

            Note {
                visible: settings.task !== null && settings.task.params.length === 0
                text: qsTr("Settings the task declares in static params of task.ts show up here.")
            }

            SectionTitle {
                text: qsTr("When it fails")
            }

            SegmentedControl {
                objectName: "onError"
                width: body.width
                enabled: !settings.readOnly
                model: [qsTr("Stop"), qsTr("Retry once")]
                currentIndex: settings.task !== null && settings.task.onError === "retry" ? 1 : 0
                Accessible.name: qsTr("When the task fails")
                onActivated: index => TaskLibrary.setOption(settings.task.id, "onError", index === 1 ? "retry" : "stop")
            }

            SwitchRow {
                objectName: "wake"
                enabled: !settings.readOnly
                label: qsTr("Turn the phone's screen on before it starts")
                on: settings.task !== null && settings.task.wake
                onToggled: on => TaskLibrary.setOption(settings.task.id, "wake", on)
            }

            SwitchRow {
                objectName: "sleepAfter"
                enabled: !settings.readOnly
                label: qsTr("Turn the phone's screen off when it is done")
                on: settings.task !== null && settings.task.sleepAfter
                onToggled: on => TaskLibrary.setOption(settings.task.id, "sleepAfter", on)
            }
        }
    }

    Rectangle {
        id: footer

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: footerRow.implicitHeight + 2 * Theme.space.md
        visible: settings.task !== null
        color: Theme.colors.bgRaised

        Rectangle {
            width: parent.width
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }

        Row {
            id: footerRow

            anchors.left: parent.left
            anchors.leftMargin: Theme.space.lg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space.sm

            Button {
                objectName: "editTaskLogic"
                small: true
                enabled: !settings.readOnly
                iconName: "code-xml"
                text: qsTr("Edit logic")
                onClicked: {
                    const problem = DeviceManager.openTaskLogic(settings.task.id)
                    if (problem !== "")
                        settings.notify(problem, "warn")
                }
            }

            Button {
                objectName: "stopAll"
                small: true
                tone: "secondary"
                iconName: "square"
                text: qsTr("Stop all")
                enabled: settings.runs.length > 0
                onClicked: DeviceManager.stopTask(settings.task.id, "")
            }
        }

        ToolbarButton {
            id: moreButton

            anchors.right: parent.right
            anchors.rightMargin: Theme.space.md
            anchors.verticalCenter: parent.verticalCenter
            iconName: "ellipsis"
            text: qsTr("More")
            onClicked: moreMenu.open()

            Menu {
                id: moreMenu

                x: moreButton.width - width
                y: -height - Theme.space.xs

                MenuItem {
                    iconName: "folder-open"
                    text: qsTr("Open folder")
                    onTriggered: TaskLibrary.reveal(settings.task.id)
                }

                MenuItem {
                    objectName: "copyTask"
                    visible: settings.task !== null && settings.task.readOnly === true
                    height: visible ? implicitHeight : 0
                    iconName: "copy"
                    text: qsTr("Copy to my tasks")
                    onTriggered: copyToMine.ask("task", settings.task.id, settings.task.name)
                }

                MenuItem {
                    objectName: "exportTaskPackage"
                    visible: settings.task !== null && settings.task.readOnly !== true
                    height: visible ? implicitHeight : 0
                    iconName: "puzzle"
                    text: qsTr("Export as extension package…")
                    onTriggered: exportPackage.start("task", settings.task.id, settings.task.name)
                }

                ConvertMenuItems {
                    menu: moreMenu
                    format: "task"
                    before: packageSeparator
                    shown: settings.task !== null && settings.task.readOnly !== true
                    onPicked: converter => convertExport.start(converter, settings.task.id, settings.task.name)
                }

                MenuSeparator {
                    id: packageSeparator

                    visible: settings.task !== null && settings.task.readOnly !== true
                    height: visible ? implicitHeight : 0
                }

                MenuItem {
                    objectName: "deleteTask"
                    visible: settings.task !== null && settings.task.readOnly !== true
                    height: visible ? implicitHeight : 0
                    iconName: "trash-2"
                    text: qsTr("Delete…")
                    onTriggered: removal.open()
                }
            }
        }
    }

    Dialog {
        id: removal

        title: qsTr("Delete %1?").arg(settings.task !== null ? settings.task.name : "")
        acceptText: qsTr("Delete")
        danger: true
        onAccepted: {
            const id = settings.task.id
            DeviceManager.stopTask(id, "")
            if (!TaskLibrary.remove(id))
                settings.notify(qsTr("The task could not be deleted"), "bad")
        }

        Text {
            width: parent.width
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("Its folder goes to the Recycle Bin, with its code and settings.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }
    }

    ExportPackageDialog {
        id: exportPackage

        onFinished: (ok, message) => settings.notify(message, ok ? "ok" : "bad")
    }

    ConvertExportDialog {
        id: convertExport

        onFinished: (ok, message) => settings.notify(message, ok ? "ok" : "bad")
    }

    CopyToMineDialog {
        id: copyToMine

        onConfirmed: (kind, source, sourceName) => {
            if (TaskLibrary.copyToMine(source) !== "")
                settings.notify(qsTr("Copied %1 to your tasks").arg(sourceName), "ok")
            else
                settings.notify(qsTr("The task could not be copied"), "bad")
        }
    }
}
