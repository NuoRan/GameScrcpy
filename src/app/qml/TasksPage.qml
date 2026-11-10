pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: page

    property string chosen
    property double now: Date.now()
    readonly property var tasks: TaskLibrary.tasks
    readonly property var runs: DeviceManager.taskRuns
    readonly property var history: DeviceManager.taskHistory
    readonly property var mirrors: DeviceManager.mirrors
    readonly property var current: tasks.find(task => task.id === chosen) ?? (tasks.length > 0 ? tasks[0] : null)
    readonly property int panelWidth: Theme.size.workbenchSidebar + Theme.space.xl
    readonly property var dayNames: [qsTr("Mon"), qsTr("Tue"), qsTr("Wed"), qsTr("Thu"), qsTr("Fri"), qsTr("Sat"),
                                     qsTr("Sun")]

    signal notify(string message, string tone)

    function runsOf(id: string): var {
        return page.runs.filter(run => run.task === id)
    }

    function elapsed(startedMs: double): string {
        return page.clock(page.now - startedMs)
    }

    function clock(ms: double): string {
        const seconds = Math.max(0, Math.floor(ms / 1000))
        const hours = Math.floor(seconds / 3600)
        const minutes = Math.floor(seconds / 60) % 60
        const rest = String(seconds % 60).padStart(2, "0")
        return hours > 0 ? hours + ":" + String(minutes).padStart(2, "0") + ":" + rest : minutes + ":" + rest
    }

    function scheduleText(task: var): string {
        if (task.schedule === "daily")
            return qsTr("Every day at %1").arg(task.time)
        if (task.schedule === "weekly") {
            const days = (task.days ?? []).map(day => page.dayNames[day - 1] ?? "")
            return days.length === 7 ? qsTr("Every day at %1").arg(task.time)
                                     : qsTr("%1 at %2").arg(days.join(qsTr(", "))).arg(task.time)
        }
        if (task.schedule === "once")
            return qsTr("Once at %1").arg(task.time)
        return qsTr("Started by hand")
    }

    function phoneName(serial: string): string {
        return (page.mirrors.find(mirror => mirror.serial === serial) ?? {}).name ?? serial
    }

    function mirroring(serial: string): bool {
        return page.mirrors.some(mirror => mirror.serial === serial)
    }

    function start(task: var) {
        let phones = task.phones.filter(serial => page.mirroring(serial))
        if (phones.length === 0 && page.mirrors.length === 1)
            phones = [page.mirrors[0].serial]
        if (phones.length === 0) {
            page.notify(page.mirrors.length === 0 ? qsTr("Mirror a phone first, then start the task on it")
                                                  : qsTr("Tick the phones to run %1 on").arg(task.name), "warn")
            page.chosen = task.id
            return
        }
        for (const serial of phones) {
            const problem = DeviceManager.startTask(task.id, serial)
            if (problem !== "")
                page.notify(problem, "warn")
        }
    }

    function create() {
        const id = TaskLibrary.add("")
        if (id === "") {
            page.notify(qsTr("The task could not be created"), "bad")
            return
        }
        page.chosen = id
    }

    function resultText(entry: var): string {
        switch (entry.result) {
        case "done":
            return qsTr("Done")
        case "failed":
            return entry.message !== "" ? qsTr("Failed: %1").arg(entry.message) : qsTr("Failed")
        case "stopped":
            return qsTr("Stopped")
        case "disconnected":
            return qsTr("The phone disconnected")
        case "offline":
            return qsTr("Not mirroring at the set time")
        case "missed":
            return qsTr("Missed while the computer was asleep")
        case "unreachable":
            return entry.message !== "" ? qsTr("Could not connect: %1").arg(entry.message) : qsTr("Could not connect")
        }
        return entry.result
    }

    function warns(entry: var): bool {
        return ["failed", "offline", "missed", "unreachable"].includes(entry.result)
    }

    function whenText(ms: double): string {
        const when = new Date(ms)
        const today = new Date(page.now)
        const time = String(when.getHours()).padStart(2, "0") + ":" + String(when.getMinutes()).padStart(2, "0")
        return when.toDateString() === today.toDateString() ? qsTr("Today %1").arg(time)
                                                          : (when.getMonth() + 1) + "/" + when.getDate() + " " + time
    }

    Timer {
        interval: 1000
        repeat: true
        running: page.visible && page.runs.length > 0
        onTriggered: page.now = Date.now()
    }

    Row {
        id: header

        x: Theme.size.panelPaddingH
        y: Theme.size.panelPaddingV
        spacing: Theme.space.lg

        Text {
            id: title

            textFormat: Text.PlainText
            text: qsTr("Tasks")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.display.size
            font.weight: Theme.type.display.weight
        }

        Text {
            anchors.baseline: title.baseline
            textFormat: Text.PlainText
            text: qsTr("Logic that runs on its own · it keeps going when you change games")
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.body.size
        }
    }

    component NewTaskButton: Button {
        id: newButton

        tone: "primary"
        iconName: "plus"
        text: qsTr("New task")
        onClicked: newMenu.open()

        Menu {
            id: newMenu

            objectName: "newTaskMenu"
            x: newButton.width - width
            y: newButton.height + Theme.space.xs

            MenuItem {
                objectName: "newBlankTask"
                iconName: "plus"
                text: qsTr("Blank task")
                description: qsTr("A small task to write your own logic in")
                onTriggered: page.create()
            }

            MenuItem {
                objectName: "newTaskFromExample"
                iconName: "sparkles"
                text: qsTr("From an example…")
                description: qsTr("Copies a ready-made task that you can change")
                enabled: TaskLibrary.examples.length > 0
                onTriggered: exampleDialog.start()
            }
        }
    }

    NewTaskButton {
        objectName: "newTask"
        anchors.right: panel.left
        anchors.rightMargin: Theme.space.xl
        anchors.verticalCenter: header.verticalCenter
        visible: page.tasks.length > 0
    }

    Column {
        objectName: "noTasks"
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.size.panelPaddingH, Theme.size.dialogWidth)
        visible: page.tasks.length === 0
        spacing: Theme.space.lg

        Tile {
            anchors.horizontalCenter: parent.horizontalCenter
            large: true
            tone: "teal"
            iconName: "bot"
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("No tasks yet")
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.subtitle.size
            font.weight: Theme.type.subtitle.weight
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("A task is logic that runs on a phone by itself, such as daily check-ins or farming. Your keys keep working while it runs, and it can start at a set time.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }

        NewTaskButton {
            objectName: "newFirstTask"
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    Flickable {
        id: list

        objectName: "taskList"
        anchors.left: parent.left
        anchors.right: panel.left
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.size.panelPaddingH
        anchors.rightMargin: Theme.space.xl
        anchors.topMargin: Theme.space.xl
        visible: page.tasks.length > 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: listColumn.implicitHeight + Theme.size.panelPaddingV

        Column {
            id: listColumn

            width: list.width
            spacing: Theme.space.md

            Repeater {
                model: page.tasks

                delegate: Card {
                    id: card

                    required property var modelData
                    readonly property var running: page.runsOf(card.modelData.id)

                    objectName: "taskCard"
                    width: listColumn.width
                    active: page.current !== null && page.current.id === card.modelData.id

                    TapHandler {
                        onTapped: page.chosen = card.modelData.id
                    }

                    Item {
                        implicitWidth: card.availableWidth
                        implicitHeight: Math.max(cardTile.height, cardText.implicitHeight, cardSide.height)

                        Tile {
                            id: cardTile

                            tone: card.running.length > 0 ? "teal" : ""
                            iconName: "bot"
                        }

                        Column {
                            id: cardText

                            x: cardTile.width + Theme.space.md
                            width: cardSide.x - x - Theme.space.md
                            spacing: Theme.space.xs

                            Text {
                                width: parent.width
                                textFormat: Text.PlainText
                                text: card.modelData.name
                                elide: Text.ElideRight
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.body.size
                                font.weight: Font.DemiBold
                            }

                            Text {
                                width: parent.width
                                visible: card.running.length === 0
                                textFormat: Text.PlainText
                                text: qsTr("Not running")
                                color: Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                            }

                            Flow {
                                width: parent.width
                                visible: card.running.length > 0
                                spacing: Theme.space.xs

                                Repeater {
                                    model: card.running

                                    delegate: StatusPill {
                                        required property var modelData

                                        objectName: "taskRun"
                                        tone: "live"
                                        text: [modelData.phone, modelData.progress, page.elapsed(modelData.startedMs)]
                                              .filter(part => part !== "").join(" · ")
                                    }
                                }
                            }
                        }

                        Row {
                            id: cardSide

                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.space.md

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: page.scheduleText(card.modelData)
                                color: Theme.colors.textSecondary
                                font.pixelSize: Theme.type.caption.size
                            }

                            Button {
                                objectName: "startStop"
                                anchors.verticalCenter: parent.verticalCenter
                                small: true
                                tone: "secondary"
                                iconName: card.running.length > 0 ? "square" : "play"
                                text: card.running.length > 0 ? qsTr("Stop") : qsTr("Start")
                                onClicked: {
                                    if (card.running.length > 0)
                                        DeviceManager.stopTask(card.modelData.id, "")
                                    else
                                        page.start(card.modelData)
                                }
                            }
                        }
                    }
                }
            }

            Row {
                visible: page.history.length > 0
                topPadding: Theme.space.lg
                spacing: Theme.space.sm

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "history"
                    size: Theme.size.iconSmall
                    color: Theme.colors.textSecondary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: qsTr("Recent runs")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    font.weight: Font.DemiBold
                }
            }

            Repeater {
                model: page.history

                delegate: Item {
                    id: line

                    required property var modelData

                    objectName: "historyRow"
                    width: listColumn.width
                    height: Theme.size.listItem

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Theme.size.stroke
                        color: Theme.colors.strokeSubtle
                    }

                    Text {
                        id: whenLabel

                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width * 0.16
                        textFormat: Text.PlainText
                        text: page.whenText(line.modelData.whenMs)
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                    }

                    Text {
                        id: taskText

                        anchors.verticalCenter: parent.verticalCenter
                        x: whenLabel.width
                        width: parent.width * 0.24
                        textFormat: Text.PlainText
                        text: line.modelData.name
                        elide: Text.ElideRight
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                    }

                    Text {
                        id: phoneText

                        anchors.verticalCenter: parent.verticalCenter
                        x: taskText.x + taskText.width
                        width: parent.width * 0.18
                        textFormat: Text.PlainText
                        text: line.modelData.phone
                        elide: Text.ElideRight
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.label.size
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: phoneText.x + phoneText.width
                        width: parent.width - x - durationText.width - Theme.space.md
                        textFormat: Text.PlainText
                        text: page.resultText(line.modelData)
                        elide: Text.ElideRight
                        color: page.warns(line.modelData) ? Theme.colors.warningText : Theme.colors.textSecondary
                        font.pixelSize: Theme.type.label.size
                    }

                    Text {
                        id: durationText

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: line.modelData.durationMs > 0 ? page.clock(line.modelData.durationMs) : "—"
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }
                }
            }
        }
    }

    Rectangle {
        id: panel

        objectName: "taskPanel"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: page.current !== null ? page.panelWidth : 0
        visible: page.current !== null
        color: Theme.colors.bgRaised

        Rectangle {
            width: Theme.size.stroke
            height: parent.height
            color: Theme.colors.strokeSubtle
        }

        TaskSettings {
            anchors.fill: parent
            anchors.leftMargin: Theme.size.stroke
            task: page.current
            runs: page.current !== null ? page.runsOf(page.current.id) : []
            mirrors: page.mirrors
            dayNames: page.dayNames
            onNotify: (message, tone) => page.notify(message, tone)
        }
    }

    TaskExampleDialog {
        id: exampleDialog

        onCopied: (id, name) => {
            page.chosen = id
            page.notify(qsTr("Added %1 to your tasks").arg(name), "ok")
        }
    }

    TourOverlay {
        id: tour

        anchors.fill: parent
        objectName: "tasksTour"
        tourKey: "tasks"
        settings: AppSettings
        startWhen: page.visible && !AppSettings.toursQuiet
        version: page.tasks.length + page.history.length
        steps: [
            {
                centered: true,
                title: qsTr("Tasks"),
                text: qsTr("A task is logic that runs by itself: check in every day, farm a level while you are "
                           + "away, remind you of something. It can run on the computer alone or work a phone that "
                           + "is mirroring.")
            },
            {
                target: function() { return tour.findItem(page, page.tasks.length > 0 ? "newTask" : "newFirstTask") },
                title: qsTr("New task"),
                text: qsTr("Start from a blank task and write your own logic, or copy an example and change it.")
            },
            {
                target: function() { return tour.findItem(page, "taskCard") },
                optional: true,
                title: qsTr("A task card"),
                text: qsTr("The card shows the task's name, when it runs and whether it is running now.")
            },
            {
                target: function() { return tour.findItem(page, "startStop") },
                optional: true,
                title: qsTr("Run and stop"),
                text: qsTr("Run a task by hand or stop it at any time.")
            },
            {
                target: function() { return tour.findItem(page, "taskPanel") },
                optional: true,
                title: qsTr("Task settings"),
                text: qsTr("Choose when the task runs, such as every day or on some days of the week, which phone "
                           + "it works, and open its logic to edit it.")
            },
            {
                target: function() { return tour.findItem(page, "historyRow") },
                optional: true,
                title: qsTr("Recent runs"),
                text: qsTr("Every run is kept here: when it started, on which phone, how long it took and how it "
                           + "ended.")
            },
            {
                centered: true,
                title: qsTr("That's tasks"),
                text: qsTr("With the setting for it on, closing the window leaves GameScrcpy in the notification area "
                           + "so that tasks with a schedule still run on time. Help can replay this tour.")
            }
        ]
    }

    function startTour() {
        tour.restart()
    }
}
