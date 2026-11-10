pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: sidebar

    required property ScriptWorkbench bench
    property bool stripShown: true
    property int page: 0
    readonly property real contentTop: sidebar.stripShown ? tabsLine.y + tabsLine.height : 0
    readonly property var entries: sidebar.bench.api
    readonly property var shownEntries: {
        const query = apiSearch.text.trim().toLowerCase()
        if (query === "")
            return sidebar.entries
        return sidebar.entries.filter(entry => (entry.label + " " + entry.detail + " " + entry.documentation)
                                      .toLowerCase().includes(query))
    }
    readonly property string phoneName: (sidebar.bench.phones.find(phone => phone.serial === sidebar.bench.phone) ?? {}).name
                                        ?? ""
    readonly property bool componentMode: sidebar.bench.component !== ""
    readonly property bool watching: sidebar.visible && sidebar.page === 0 && !sidebar.componentMode

    signal recordOpened()

    function stateText(item: var): string {
        if (!item.enabled)
            return qsTr("Turned off")
        if (item.aiming)
            return qsTr("Aiming")
        if (item.walking)
            return qsTr("Walking")
        if (item.moving)
            return qsTr("Pushed to %1°").arg(item.direction)
        if (item.cell !== undefined)
            return qsTr("On cell %1").arg(item.cell + 1)
        if (item.touching)
            return item.type === "tempCursor" ? qsTr("Cursor out") : item.type === "actionWheel" ? qsTr("Open")
                                                                                                  : qsTr("Touching")
        return qsTr("Idle")
    }

    function valueText(value: var): string {
        if (typeof value === "boolean")
            return value ? qsTr("On") : qsTr("Off")
        if (typeof value === "number")
            return String(Math.round(value * 1000) / 1000)
        return value === "" ? qsTr("(empty)") : String(value)
    }

    onWatchingChanged: sidebar.bench.setWatching(sidebar.watching)
    Component.onCompleted: sidebar.bench.setWatching(sidebar.watching)

    FolderDialog {
        id: recordingPicker

        title: qsTr("Choose a recording")
        currentFolder: sidebar.bench.recordingsFolder()
        onAccepted: sidebar.bench.runOnRecording(recordingPicker.selectedFolder)
    }

    FolderDialog {
        id: debugRecordPicker

        title: qsTr("Choose a debug record")
        currentFolder: sidebar.bench.debugRecordsFolder
        onAccepted: {
            if (sidebar.bench.openDebugRecord(debugRecordPicker.selectedFolder))
                sidebar.recordOpened()
        }
    }

    Tabs {
        id: tabs

        objectName: "sideTabs"
        x: Theme.space.lg
        y: Theme.space.xs
        visible: sidebar.stripShown
        model: sidebar.componentMode ? [qsTr("Component"), qsTr("API")] : [qsTr("Run"), qsTr("API")]
        currentIndex: sidebar.page
        onActivated: index => {
            sidebar.page = index
            tabs.currentIndex = Qt.binding(() => sidebar.page)
        }
    }

    Rectangle {
        id: tabsLine

        visible: sidebar.stripShown
        y: tabs.y + tabs.height
        width: parent.width
        height: Theme.size.stroke
        color: Theme.colors.strokeSubtle
    }

    ComponentSettings {
        anchors.fill: parent
        anchors.topMargin: sidebar.contentTop
        visible: sidebar.page === 0 && sidebar.componentMode
        ref: sidebar.bench.component
        locked: sidebar.bench.locked
    }

    Flickable {
        id: runView

        objectName: "runTab"
        anchors.fill: parent
        anchors.topMargin: sidebar.contentTop
        visible: sidebar.page === 0 && !sidebar.componentMode
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: runColumn.implicitHeight + 2 * Theme.space.lg

        Column {
            id: runColumn

            x: Theme.space.lg
            y: Theme.space.lg
            width: runView.width - 2 * Theme.space.lg
            spacing: Theme.space.md

            Rectangle {
                objectName: "phonePicture"
                width: parent.width
                height: Math.round(width * Theme.size.previewShort / Theme.size.previewLong)
                radius: Theme.radius.md
                color: Theme.colors.bgSunken
                border.width: Theme.size.stroke
                border.color: Theme.colors.strokeSubtle

                ThumbnailView {
                    anchors.fill: parent
                    anchors.margins: Theme.size.stroke
                    visible: sidebar.bench.hasThumbnail
                    radius: Theme.radius.md
                    image: sidebar.bench.thumbnail
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.space.lg
                    visible: !sidebar.bench.hasThumbnail
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: sidebar.bench.phone === "" ? qsTr("No phone is mirroring") : qsTr("No picture yet")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }
            }

            Row {
                width: parent.width
                spacing: Theme.space.sm

                StatusPill {
                    id: stateLabel

                    objectName: "logicState"
                    anchors.verticalCenter: parent.verticalCenter
                    tone: sidebar.bench.logicState === "running" ? "live"
                          : sidebar.bench.logicState === "failed" ? "bad" : "idle"
                    text: sidebar.bench.logicState === "running" ? qsTr("Running")
                          : sidebar.bench.logicState === "failed" ? qsTr("Failed") : qsTr("Stopped")
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, parent.width - stateLabel.width - parent.spacing)
                    textFormat: Text.PlainText
                    text: sidebar.phoneName
                    elide: Text.ElideRight
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.label.size
                }
            }

            Text {
                objectName: "lastError"
                width: parent.width
                visible: sidebar.bench.lastError !== ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: sidebar.bench.lastError
                color: Theme.colors.dangerText
                font.pixelSize: Theme.type.caption.size
            }

            Text {
                objectName: "runNoteText"
                width: parent.width
                visible: !sidebar.bench.canRun && sidebar.bench.runNote !== ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: sidebar.bench.runNote
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            }

            Button {
                objectName: "runAgain"
                tone: "primary"
                iconName: "play"
                text: qsTr("Run")
                enabled: sidebar.bench.canRun
                toolTip: sidebar.bench.runNote
                onClicked: sidebar.bench.run()
            }

            Button {
                objectName: "runOnRecording"
                iconName: "flask-conical"
                text: qsTr("Run on a recording…")
                enabled: sidebar.bench.hasLogic && sidebar.bench.task === "" && !sidebar.bench.replaying
                toolTip: qsTr("Plays a recording's picture, keys and answers through this logic, without the phone")
                onClicked: recordingPicker.open()
            }

            Button {
                objectName: "runTests"
                visible: sidebar.bench.hasTests
                iconName: "circle-check"
                text: qsTr("Run tests")
                enabled: sidebar.bench.hasLogic && !sidebar.bench.tests.running
                toolTip: qsTr("Plays the recording of each test in the tests folder through this logic and compares "
                              + "what it does with what it did when the test was saved")
                onClicked: sidebar.bench.runTests()
            }

            Button {
                objectName: "openDebugRecord"
                iconName: "history"
                text: qsTr("Open a debug record…")
                enabled: sidebar.bench.hasLogic && !sidebar.bench.debugging
                toolTip: sidebar.bench.debugging ? qsTr("Stop debugging first")
                                                 : qsTr("Look back at a part kept while debugging: the picture, each step and its values")
                onClicked: debugRecordPicker.open()
            }

            Text {
                objectName: "replayNote"
                width: parent.width
                visible: sidebar.bench.replayNote !== ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: sidebar.bench.replayNote
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            }

            Text {
                objectName: "testNote"
                width: parent.width
                visible: sidebar.bench.hasTests && sidebar.bench.tests.note !== ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: sidebar.bench.tests.note
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
            }

            Repeater {
                model: sidebar.bench.hasTests ? sidebar.bench.tests.results : []

                delegate: Row {
                    id: testRow

                    required property var modelData
                    readonly property string result: testRow.modelData.result

                    objectName: "testResult"
                    width: runColumn.width
                    spacing: Theme.space.sm

                    Icon {
                        y: Math.round((testName.height - height) / 2)
                        name: testRow.result === "pass" ? "circle-check"
                              : testRow.result === "fail" ? "circle-x" : "triangle-alert"
                        size: Theme.size.iconSmall
                        color: testRow.result === "pass" ? Theme.colors.successText
                               : testRow.result === "fail" ? Theme.colors.dangerText : Theme.colors.warningText
                    }

                    Column {
                        width: Math.max(0, testRow.width - Theme.size.iconSmall - testRow.spacing)
                        spacing: Theme.space.xxs

                        Text {
                            id: testName

                            width: parent.width
                            textFormat: Text.PlainText
                            text: testRow.modelData.name
                            elide: Text.ElideRight
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                        }

                        Text {
                            objectName: "testDetail"
                            width: parent.width
                            visible: text !== ""
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAnywhere
                            text: testRow.modelData.text
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                        }
                    }
                }
            }

            Text {
                objectName: "liveNote"
                width: parent.width
                visible: sidebar.bench.liveComponents.length === 0 && sidebar.bench.liveVars.length === 0
                topPadding: Theme.space.sm
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: qsTr("While the logic runs on this phone, its components and variables show here as they change.")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            Item {
                width: parent.width
                height: componentsTitle.implicitHeight
                visible: sidebar.bench.liveComponents.length > 0

                Text {
                    id: componentsTitle

                    topPadding: Theme.space.sm
                    textFormat: Text.PlainText
                    text: qsTr("Components")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.label.size
                    font.weight: Font.Medium
                }

                Text {
                    anchors.right: parent.right
                    anchors.baseline: componentsTitle.baseline
                    textFormat: Text.PlainText
                    text: qsTr("Live")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }
            }

            Repeater {
                model: sidebar.bench.liveComponents

                delegate: Item {
                    id: componentRow

                    required property var modelData
                    readonly property bool busy: modelData.touching || modelData.moving || modelData.aiming
                                                 || modelData.walking

                    objectName: "liveComponent"
                    width: runColumn.width
                    height: Theme.size.controlSmall

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width / 2
                        textFormat: Text.PlainText
                        text: componentRow.modelData.name
                        elide: Text.ElideRight
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.label.size
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width / 2
                        horizontalAlignment: Text.AlignRight
                        textFormat: Text.PlainText
                        text: sidebar.stateText(componentRow.modelData)
                        elide: Text.ElideRight
                        color: componentRow.busy ? Theme.colors.accentText : Theme.colors.textTertiary
                        font.pixelSize: Theme.type.label.size
                    }
                }
            }

            Text {
                visible: sidebar.bench.liveVars.length > 0
                topPadding: Theme.space.sm
                textFormat: Text.PlainText
                text: qsTr("Variables")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.label.size
                font.weight: Font.Medium
            }

            Repeater {
                model: sidebar.bench.liveVars

                delegate: Item {
                    id: varRow

                    required property var modelData

                    objectName: "liveVar"
                    width: runColumn.width
                    height: Theme.size.controlSmall

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width / 2
                        textFormat: Text.PlainText
                        text: varRow.modelData.name
                        elide: Text.ElideRight
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.label.size
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width / 2
                        horizontalAlignment: Text.AlignRight
                        textFormat: Text.PlainText
                        text: sidebar.valueText(varRow.modelData.value)
                        elide: Text.ElideRight
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                    }
                }
            }
        }
    }

    Item {
        objectName: "apiTab"
        anchors.fill: parent
        anchors.topMargin: sidebar.contentTop
        visible: sidebar.page === 1

        SearchField {
            id: apiSearch

            objectName: "apiSearch"
            x: Theme.space.lg
            y: Theme.space.md
            width: parent.width - 2 * Theme.space.lg
            placeholderText: qsTr("Search the API")
        }

        ListView {
            id: apiList

            anchors.fill: parent
            anchors.topMargin: apiSearch.y + apiSearch.height + Theme.space.sm
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: sidebar.shownEntries

            delegate: Column {
                id: apiEntry

                required property var modelData
                readonly property real inner: width - leftPadding - rightPadding

                objectName: "apiEntry"
                width: apiList.width
                leftPadding: Theme.space.lg
                rightPadding: Theme.space.lg
                spacing: Theme.space.xxs
                bottomPadding: Theme.space.md

                Text {
                    width: apiEntry.inner
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    text: apiEntry.modelData.label
                    color: Theme.colors.textPrimary
                    font.family: Theme.type.monoFamily
                    font.pixelSize: Theme.type.mono.size
                }

                Text {
                    width: apiEntry.inner
                    visible: text !== ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    text: apiEntry.modelData.detail
                    color: Theme.colors.textTertiary
                    font.family: Theme.type.monoFamily
                    font.pixelSize: Theme.type.caption.size
                }

                Text {
                    width: apiEntry.inner
                    visible: text !== ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: apiEntry.modelData.documentation
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }

                Text {
                    width: apiEntry.inner
                    visible: text !== ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    text: apiEntry.modelData.example
                    color: Theme.colors.accentText
                    font.family: Theme.type.monoFamily
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }

        Text {
            objectName: "apiEmpty"
            anchors.centerIn: apiList
            width: apiList.width - 2 * Theme.space.lg
            visible: apiList.count === 0
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("Nothing in the API matches “%1”").arg(apiSearch.text.trim())
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.label.size
        }
    }
}
