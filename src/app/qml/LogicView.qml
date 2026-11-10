pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.Blueprint
import GameScrcpy.App

Item {
    id: view

    required property ScriptWorkbench bench
    property bool blueprint: false
    property bool blueprintUsed: false
    property int tourBaseNodes: 0
    property int tourBaseWires: 0
    readonly property var graphData: JSON.parse(blueprintModel.graphText)
    readonly property var sidebarData: JSON.parse(blueprintModel.sidebarText)
    readonly property int tourNodes: (view.graphData.nodes ?? []).length
    readonly property int tourWires: (view.graphData.wires ?? []).length
    property ProfileEditor preview: null
    property var debugPause: null
    property var replayStates: []
    property int closing: -1
    readonly property string selectionText: {
        if (view.blueprint) {
            const details = blueprintModel.details ?? {}
            return [details.title ?? "", details.subtitle ?? ""].filter(part => part !== "").join(" — ")
        }
        return view.currentEditor !== null ? view.currentEditor.document.path : ""
    }
    readonly property ScriptWorkbenchEditor currentEditor: editors.count > 0 && view.bench.current >= 0
                                                           ? editors.itemAt(view.bench.current) as ScriptWorkbenchEditor
                                                           : null
    readonly property ScriptDocument blueprintDocument: view.currentEditor !== null && view.currentEditor.document.script
                                                        ? view.currentEditor.document : null
    readonly property var codeDiff: view.preview !== null && view.blueprintDocument !== null
                                    ? view.preview.codeDiff(view.blueprintDocument.path) : []
    readonly property bool canUndo: view.blueprint ? blueprintModel.canUndo
                                                   : view.currentEditor !== null && view.currentEditor.canUndo
    readonly property bool canRedo: view.blueprint ? blueprintModel.canRedo
                                                   : view.currentEditor !== null && view.currentEditor.canRedo
    readonly property DebugRecord record: view.bench.debugRecord
    readonly property var timelineData: view.record.timeline
    readonly property var momentData: view.record.moment
    readonly property bool recordShown: view.record.active && view.blueprint && view.blueprintDocument !== null
    readonly property bool lookingBack: view.recordShown && (!view.record.live || view.debugPause !== null)
    readonly property var missingBlocks: {
        if (!view.blueprint)
            return []
        return (view.graphData.nodes ?? []).filter(node => (node.fix ?? "") === "missing")
    }
    readonly property var codeColors: ({
        keyword: Theme.colors.accentText,
        string: Theme.colors.successText,
        number: Theme.colors.warningText,
        comment: Theme.colors.textTertiary,
        type: Theme.colors.infoText
    })

    function editorAt(index: int): ScriptWorkbenchEditor {
        return editors.itemAt(index) as ScriptWorkbenchEditor
    }

    function focusCurrent() {
        if (view.blueprint || !view.visible)
            return
        const editor = view.editorAt(view.bench.current)
        if (editor !== null)
            editor.focusText()
    }

    function jump(path: string, line: int, column: int) {
        view.blueprint = false
        const index = view.bench.open(path)
        if (index < 0)
            return
        Qt.callLater(() => {
            const editor = view.editorAt(index)
            if (editor !== null)
                editor.jumpTo(line, column)
        })
    }

    function jumpRecorded(path: string, line: int, column: int) {
        view.blueprint = false
        const index = view.bench.openRecorded(path)
        if (index < 0)
            return
        Qt.callLater(() => {
            const editor = view.editorAt(index)
            if (editor !== null)
                editor.jumpTo(line, column)
        })
    }

    function reference(id: string) {
        const index = view.bench.openControl(id)
        if (index >= 0)
            view.bench.current = index
        view.blueprintUsed = true
        view.blueprint = true
    }

    function closeTab(index: int) {
        const editor = view.editorAt(index)
        if (editor !== null && editor.document.dirty) {
            view.closing = index
            unsaved.open()
            return
        }
        view.bench.closeDocument(index)
    }

    function documentName(index: int): string {
        const editor = view.editorAt(index)
        return editor !== null ? editor.document.name : ""
    }

    function showBlueprint(on: bool) {
        view.blueprint = on
        if (!on) {
            Qt.callLater(view.focusCurrent)
            return
        }
        view.blueprintUsed = true
        if (view.blueprintDocument === null && view.bench.hasLogic) {
            const index = view.bench.openLogicEntry()
            if (index >= 0)
                view.bench.current = index
        }
    }

    function undo() {
        if (view.blueprint)
            blueprintModel.undo()
        else if (view.currentEditor !== null)
            view.currentEditor.undo()
    }

    function redo() {
        if (view.blueprint)
            blueprintModel.redo()
        else if (view.currentEditor !== null)
            view.currentEditor.redo()
    }

    function report(problem: string) {
        if (problem !== "")
            toast.show(problem, "warn")
    }

    function placed(result: var) {
        if (result.problem !== "")
            toast.show(result.problem, "warn")
        else if (result.key !== "")
            blueprintModel.selection = [result.key]
    }

    function jumpToNode(node: string) {
        const target = blueprintModel.jumpTarget(node)
        if (target.path === undefined || target.path === "")
            return
        if (view.blueprintDocument !== null && view.blueprintDocument.recorded)
            view.jumpRecorded(target.path, target.line, target.column)
        else
            view.jump(target.path, target.line, target.column)
    }

    function openNode(node: string) {
        const page = blueprintModel.open(node)
        if (page === "") {
            view.jumpToNode(node)
        } else if (page.startsWith("file:")) {
            const title = (view.graphData.nodes ?? []).find(item => item.key === node)?.title ?? ""
            view.openLibrary(title !== "" ? "fn:" + title : "")
        } else if (blueprintModel.page !== page) {
            blueprintModel.page = page
        }
    }

    function sendBreakpoints() {
        if (!view.bench.debugging || view.blueprintDocument === null)
            return
        const file = view.blueprintDocument.path
        view.bench.setBreakpoints(blueprintModel.breakpointPositions().filter(point => point.enabled)
                                  .map(point => file + "#" + point.position))
    }

    function step(mode: int) {
        if (view.debugPause !== null)
            view.bench.resume(view.debugPause.pause, mode)
    }

    function replay(file: string, position: string, values: var) {
        if (view.record.opened) {
            const index = view.bench.openRecorded(file)
            if (index >= 0)
                view.bench.current = index
        }
        if (view.blueprintDocument !== null && view.blueprintDocument.path === file)
            blueprintModel.pausedAt(position, values)
        else
            blueprintModel.resumed()
    }

    function applyReplayStates() {
        if (view.blueprintDocument === null || view.replayStates.length === 0)
            return
        blueprintModel.resetMachines()
        for (const state of view.replayStates) {
            if (state.file === view.blueprintDocument.path)
                blueprintModel.machineState(String(state.line), state.state, state.reason, 0, state.at)
        }
    }

    function chooseStack(index: int) {
        const entry = (view.momentData.stack ?? [])[index]
        if (entry === undefined)
            return
        const place = String(entry.place ?? "")
        const split = place.lastIndexOf(":")
        if (split <= 0)
            return
        if (view.record.opened)
            view.jumpRecorded(place.substring(0, split), Number(place.substring(split + 1)), 1)
        else
            view.jump(place.substring(0, split), Number(place.substring(split + 1)), 1)
    }

    function pick(node: string, pin: string, kind: string) {
        const found = (view.graphData.nodes ?? []).find(item => item.key === node)
        const input = found !== undefined ? (found.inputs ?? []).find(item => item.name === pin) : undefined
        const where = qsTr("%1 · %2").arg(found?.title ?? "").arg(input?.label ?? pin)
        const prompt = kind === "point" ? qsTr("Click the picture to fill in %1").arg(where)
                     : kind === "region" ? qsTr("Drag a box on the picture to fill in %1").arg(where)
                                         : qsTr("Drag a box around the picture to find, to fill in %1").arg(where)
        view.bench.pickOnPicture(node, pin, kind, prompt)
    }

    function activateItem(item: var) {
        const key = item.item ?? ""
        if (key.startsWith("control:"))
            view.reference(key.substring(8))
        else if (key.startsWith("fn:"))
            blueprintModel.page = key
        else if (key.startsWith("lib:"))
            view.openLibrary("fn:" + key.substring(4))
        else if (key.startsWith("file:")) {
            const index = view.bench.open(key.substring(5))
            if (index >= 0)
                view.bench.current = index
        } else if (key.startsWith("variable:") || key.startsWith("local:") || key.startsWith("field:"))
            blueprintModel.editVariable(key)
    }

    function openLibrary(page: string) {
        const index = view.bench.openLibrary()
        if (index < 0)
            return
        view.bench.current = index
        view.blueprintUsed = true
        view.blueprint = true
        if (page !== "")
            Qt.callLater(() => {
                if (blueprintModel.page !== page)
                    blueprintModel.page = page
            })
    }

    function addTo(section: string) {
        if (section === "vars") {
            blueprintModel.newVariable()
        } else if (section === "library") {
            view.openLibrary("")
        } else if (section === "functions") {
            const made = blueprintModel.addType("function", "")
            if (made.problem !== "")
                toast.show(made.problem, "warn")
            else if (made.page !== "")
                Qt.callLater(() => blueprintModel.page = made.page)
        }
    }

    onCodeColorsChanged: view.bench.setColors(view.codeColors)
    onBlueprintDocumentChanged: Qt.callLater(view.applyReplayStates)
    onVisibleChanged: {
        if (view.visible)
            Qt.callLater(view.focusCurrent)
    }

    Connections {
        target: view.bench

        function onNotify(message: string, tone: string) {
            toast.show(message, tone)
        }

        function onPicked(node: string, pin: string, kind: string, name: string) {
            view.report(blueprintModel.setValue(node, pin, name))
        }

        function onRevealRequested(path: string, line: int, column: int) {
            view.jump(path, line, column)
        }

        function onCurrentChanged() {
            Qt.callLater(view.focusCurrent)
        }

        function onMachineState(state: var) {
            view.replayStates = []
            if (view.blueprintDocument !== null && view.blueprintDocument.path === state.file)
                blueprintModel.machineState(String(state.line), state.state, state.reason, state.since)
        }

        function onReplayMachines(states: var) {
            view.replayStates = states
            view.applyReplayStates()
        }

        function onDebugChanged() {
            blueprintModel.debugging = view.bench.debugging || view.record.opened
            if (view.bench.debugging) {
                view.sendBreakpoints()
            } else {
                view.debugPause = null
                blueprintModel.resumed()
            }
        }

        function onDebugHits(hits: var) {
            if (view.blueprintDocument === null || !view.record.live)
                return
            const file = view.blueprintDocument.path
            for (const hit of hits) {
                if (hit.file === file)
                    blueprintModel.hit(hit.position)
            }
        }

        function onDebugPaused(pause: var) {
            view.debugPause = pause
            view.record.goLive()
            if (view.blueprintDocument === null || view.blueprintDocument.path !== pause.file) {
                const index = view.bench.open(pause.file)
                if (index >= 0)
                    view.bench.current = index
            }
            view.showBlueprint(true)
            Qt.callLater(() => blueprintModel.pausedAt(pause.position, pause.values))
        }

        function onDebugResumed(pause: var) {
            if (view.debugPause === null || view.debugPause.pause !== pause)
                return
            view.debugPause = null
            if (view.record.live)
                blueprintModel.resumed()
        }
    }

    Connections {
        target: view.record

        function onReplayed(file: string, position: string, values: var) {
            view.replay(file, position, values)
        }

        function onWentLive() {
            if (view.debugPause !== null)
                view.replay(view.debugPause.file, view.debugPause.position, view.debugPause.values)
            else
                blueprintModel.resumed()
        }

        function onChanged() {
            blueprintModel.debugging = view.bench.debugging || view.record.opened
        }
    }

    Connections {
        target: blueprintModel

        function onGraphChanged() {
            view.sendBreakpoints()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.colors.bgBase
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = true
    }

    Workbench {
        id: frame

        objectName: "logicFrame"
        anchors.fill: parent
        barShown: false
        leftOpen: true
        bottomOpen: true

        leftContent: [
            ScriptWorkbenchTree {
                anchors.fill: parent
                visible: !view.blueprint
                bench: view.bench
                onOpened: path => {
                    view.blueprint = false
                    view.bench.open(path)
                }
            },
            BlueprintSidebar {
                id: blueprintSidebar

                objectName: "blueprintSidebar"
                anchors.fill: parent
                visible: view.blueprint
                sections: {
                    const owned = view.sidebarData.some(section => section.key === "props" || section.key === "params")
                    return view.sidebarData.map(section => Object.assign({}, section, {
                        addable: section.key === "vars" || section.key === "structs" || section.key === "enums"
                                 || section.key === "classes" || section.key === "library"
                                 || (section.key === "functions" && !owned)
                    }))
                }
                model: blueprintModel
                onItemActivated: item => view.activateItem(item)
                onAddClicked: section => view.addTo(section)
            }
        ]

        sidebarContent: [
            ScriptWorkbenchSidebar {
                id: scriptSidebar

                anchors.fill: parent
                visible: !blueprintDetails.shown && !view.lookingBack
                bench: view.bench
                onRecordOpened: {
                    view.showBlueprint(true)
                    view.record.seek(view.timelineData.playhead ?? 0)
                }
            },
            BlueprintDetails {
                id: blueprintDetails

                readonly property bool shown: view.blueprint && !blueprintDetails.empty && !view.lookingBack

                objectName: "blueprintDetails"
                anchors.fill: parent
                visible: blueprintDetails.shown
                details: blueprintModel.details
                onFieldEdited: (name, value) => view.report(blueprintModel.editDetail(name, value))
                onJumpRequested: {
                    if (blueprintModel.selection.length > 0)
                        view.jumpToNode(blueprintModel.selection[0])
                }
                onCodeDocumentReady: textDocument => view.bench.highlight(textDocument)
            },
            Item {
                objectName: "logicMoment"
                anchors.fill: parent
                visible: view.lookingBack

                Item {
                    id: recordHead

                    objectName: "debugRecordHead"
                    width: parent.width
                    height: view.record.opened ? Theme.size.tab : 0
                    visible: view.record.opened

                    Icon {
                        id: recordIcon

                        x: Theme.space.lg
                        anchors.verticalCenter: parent.verticalCenter
                        name: "history"
                        size: Theme.size.iconSmall
                        color: Theme.colors.accentText
                    }

                    Text {
                        anchors.left: recordIcon.right
                        anchors.leftMargin: Theme.space.sm
                        anchors.right: closeRecord.left
                        anchors.rightMargin: Theme.space.sm
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: qsTr("Debug record · %1").arg(view.record.name)
                        elide: Text.ElideRight
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                        font.weight: Font.DemiBold
                    }

                    Button {
                        id: closeRecord

                        objectName: "closeDebugRecord"
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.space.sm
                        anchors.verticalCenter: parent.verticalCenter
                        small: true
                        tone: "ghost"
                        iconName: "x"
                        text: qsTr("Close")
                        onClicked: view.record.close()
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Theme.size.stroke
                        color: Theme.colors.strokeSubtle
                    }
                }

                DebugMoment {
                    anchors.fill: parent
                    anchors.topMargin: recordHead.height
                    title: view.momentData.title ?? ""
                    subtitle: view.momentData.subtitle ?? ""
                    frame: view.momentData.frame ?? ""
                    boxes: view.momentData.boxes ?? []
                    result: view.momentData.result ?? ""
                    variables: view.momentData.variables ?? []
                    stack: view.momentData.stack ?? []
                    onStackChosen: index => view.chooseStack(index)
                }
            }
        ]

        bottomContent: [
            ScriptWorkbenchPanel {
                id: consolePanel

                anchors.fill: parent
                bench: view.bench
                onJumped: (path, line, column) => view.jump(path, line, column)
            }
        ]
    }

    Item {
        id: canvas

        objectName: "codeCanvas"
        x: frame.canvasRect.x
        y: frame.canvasRect.y
        width: frame.canvasRect.width
        height: frame.canvasRect.height
        clip: true

        Rectangle {
            id: tabStrip

            objectName: "documentTabs"
            width: parent.width
            height: Theme.size.tab + (pagesStrip.visible && pagesStrip.nested ? Theme.size.bpCrumb : 0)
            color: Theme.colors.bgBase

            Flickable {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.right: viewSwitch.left
                anchors.rightMargin: Theme.space.sm
                height: Theme.size.tab
                visible: !view.blueprint
                clip: true
                contentWidth: tabRow.width
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick

                Row {
                    id: tabRow

                    height: parent.height

                    Repeater {
                        model: view.bench.documents

                        delegate: Rectangle {
                            id: tab

                            required property ScriptDocument document
                            required property int index
                            readonly property bool current: index === view.bench.current

                            objectName: "documentTab"
                            width: tabContent.implicitWidth + Theme.space.md + Theme.space.xs
                            height: tabRow.height
                            color: tab.current ? Theme.colors.bgRaised
                                               : tabHover.hovered ? Theme.colors.stateHover : "transparent"

                            HoverHandler {
                                id: tabHover
                            }

                            TapHandler {
                                onTapped: view.bench.current = tab.index
                            }

                            TapHandler {
                                acceptedButtons: Qt.MiddleButton
                                onTapped: view.closeTab(tab.index)
                            }

                            Row {
                                id: tabContent

                                x: Theme.space.md
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.space.xs

                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: tab.document.image ? "camera" : tab.document.recorded ? "history" : "code-xml"
                                    size: Theme.size.iconSmall
                                    color: tab.current ? Theme.colors.accentText : Theme.colors.textTertiary
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.PlainText
                                    text: tab.document.name
                                    color: tab.current ? Theme.colors.textPrimary : Theme.colors.textSecondary
                                    font.pixelSize: Theme.type.label.size
                                }

                                Rectangle {
                                    objectName: "unsavedMark"
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: tab.document.dirty
                                    width: Theme.size.pillDot
                                    height: width
                                    radius: width / 2
                                    color: Theme.colors.accent
                                }

                                Button {
                                    objectName: "closeTab"
                                    anchors.verticalCenter: parent.verticalCenter
                                    tone: "ghost"
                                    small: true
                                    iconName: "x"
                                    description: qsTr("Close %1").arg(tab.document.name)
                                    focusPolicy: Qt.NoFocus
                                    onClicked: view.closeTab(tab.index)
                                }
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                visible: tab.current
                                width: parent.width
                                height: Theme.size.tabIndicator
                                color: Theme.colors.accent
                            }
                        }
                    }
                }
            }

            BlueprintPages {
                id: pagesStrip

                objectName: "blueprintPages"
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.right: viewSwitch.left
                anchors.rightMargin: Theme.space.sm
                visible: view.blueprint && view.blueprintDocument !== null
                pages: view.blueprintDocument !== null && (view.blueprintDocument.path.startsWith("controls/")
                                                           || view.blueprintDocument.path === "library.ts")
                       ? [{ key: "file:logic", title: qsTr("Main page"), icon: "workflow", kind: "file" }]
                         .concat(blueprintModel.pages)
                       : blueprintModel.pages
                crumbs: blueprintModel.crumbs
                current: blueprintModel.page
                onPageChosen: key => {
                    if (key === "file:logic") {
                        const index = view.bench.openLogicEntry()
                        if (index >= 0)
                            view.bench.current = index
                    } else {
                        blueprintModel.page = key
                    }
                }
                onCrumbChosen: key => blueprintModel.page = key
            }

            SegmentedControl {
                id: viewSwitch

                objectName: "blueprintSwitch"
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.sm
                y: (Theme.size.tab - height) / 2
                model: [{ text: qsTr("Blueprint"), icon: "workflow" }, { text: qsTr("Code"), icon: "code-xml" }]
                currentIndex: view.blueprint ? 0 : 1
                Accessible.name: qsTr("Show the logic as a blueprint or as code")
                onActivated: index => {
                    view.showBlueprint(index === 0)
                    viewSwitch.currentIndex = Qt.binding(() => view.blueprint ? 0 : 1)
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.size.stroke
                color: Theme.colors.strokeSubtle
            }
        }

        Item {
            id: editorArea

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: tabStrip.bottom
            anchors.bottom: parent.bottom
            visible: !view.blueprint || view.blueprintDocument === null

            Repeater {
                id: editors

                model: view.bench.documents

                delegate: ScriptWorkbenchEditor {
                    required property int index

                    anchors.fill: parent
                    visible: index === view.bench.current && !view.blueprint
                }
            }

            Column {
                objectName: "addLogic"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space.xxl, Theme.size.dialogWidth)
                visible: view.bench.count === 0 && !view.bench.hasLogic
                spacing: Theme.space.lg

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "code-xml"
                    size: Theme.size.iconHero
                    color: Theme.colors.textTertiary
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: qsTr("This profile has no logic yet")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.subtitle.size
                    font.weight: Theme.type.subtitle.weight
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: qsTr("Logic is TypeScript that reacts to the keys and to the phone. It runs on every phone that uses this profile.")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.body.size
                }

                Button {
                    objectName: "addLogicButton"
                    anchors.horizontalCenter: parent.horizontalCenter
                    tone: "primary"
                    iconName: "plus"
                    text: qsTr("Add logic")
                    onClicked: view.bench.addLogic()
                }
            }

            Text {
                objectName: "chooseFile"
                anchors.centerIn: parent
                visible: view.bench.count === 0 && view.bench.hasLogic
                textFormat: Text.PlainText
                text: qsTr("Choose a file on the left")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
            }
        }

        Flickable {
            id: codeDraftOverlay

            objectName: "codeDraftDiff"
            visible: view.preview !== null && !view.blueprint && view.codeDiff.length > 0
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: tabStrip.bottom
            anchors.bottom: parent.bottom
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: codeDiffColumn.implicitHeight + 2 * Theme.space.md

            Rectangle {
                anchors.fill: parent
                color: Theme.colors.bgBase
            }

            Column {
                id: codeDiffColumn

                x: Theme.space.lg
                y: Theme.space.md
                width: parent.width - 2 * Theme.space.lg
                spacing: 1
                z: 1

                Repeater {
                    model: view.codeDiff

                    delegate: Rectangle {
                        required property var modelData

                        readonly property string kind: modelData.kind ?? "context"

                        width: parent ? parent.width : implicitWidth
                        height: diffLine.implicitHeight + 1
                        color: kind === "added" ? Theme.colors.successSubtle
                             : kind === "removed" ? Theme.colors.dangerSubtle
                             : kind === "header" ? Theme.colors.infoSubtle : "transparent"

                        Text {
                            id: diffLine

                            width: parent.width
                            textFormat: Text.PlainText
                            text: (kind === "added" ? "+ " : kind === "removed" ? "\u2212 " : "  ")
                                  + (modelData.text ?? "")
                            color: kind === "added" ? Theme.colors.successText
                                 : kind === "removed" ? Theme.colors.dangerText
                                 : kind === "header" ? Theme.colors.infoText
                                 : kind === "note" ? Theme.colors.textTertiary : Theme.colors.textSecondary
                            font.family: Theme.type.monoFamily
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }

        BlueprintCanvas {
            id: blueprintCanvas

            objectName: "logicBlueprint"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: missingNotice.visible ? missingNotice.bottom + Theme.space.sm : tabStrip.bottom
            anchors.bottom: timelineBar.top
            visible: view.blueprint && view.blueprintDocument !== null
            graph: view.graphData
            selection: blueprintModel.selection
            viewState: blueprintModel.viewState
            debugging: blueprintModel.debugging
            pausedNode: blueprintModel.pausedNode
            pinValues: blueprintModel.pinValues
            reached: blueprintModel.reached
            check: (from, fromPin, to, toPin) => blueprintModel.check(from, fromPin, to, toPin)
            catalog: (fromNode, fromPin, query) => blueprintModel.catalog(fromNode, fromPin, query)
            onMoved: (keys, dx, dy) => view.report(blueprintModel.move(keys, dx, dy))
            onConnectRequested: (from, fromPin, to, toPin) => view.report(blueprintModel.connectPins(from, fromPin, to, toPin))
            onRewireRequested: (node, pin, toNode, toPin) => view.report(blueprintModel.rewire(node, pin, toNode, toPin))
            onDisconnectRequested: (node, pin) => view.report(blueprintModel.disconnectPin(node, pin))
            onWireRemoved: wire => view.report(blueprintModel.removeWire(wire))
            onValueEdited: (node, pin, value) => view.report(blueprintModel.setValue(node, pin, value))
            onPickRequested: (node, pin, kind) => view.pick(node, pin, kind)
            onAddChosen: (id, x, y, fromNode, fromPin) => view.placed(blueprintModel.add(id, x, y, fromNode, fromPin))
            onDropped: (item, x, y, node, pin) => view.placed(blueprintModel.drop(item, x, y, node, pin))
            onInsertRequested: (wire, node) => view.placed(blueprintModel.insertOnWire(wire, node))
            onPromoteRequested: (node, pin) => view.report(blueprintModel.promoteToVariable(node, pin))
            onRemoveRequested: keys => view.report(blueprintModel.remove(keys))
            onDuplicateRequested: keys => view.report(blueprintModel.duplicate(keys))
            onCopyRequested: keys => view.bench.copyText(blueprintModel.copyText(keys))
            onPasteRequested: (x, y) => view.report(blueprintModel.paste(view.bench.clipboardText(), x, y))
            onCodeEdited: (node, text) => view.report(blueprintModel.setCode(node, text))
            onCodeDocumentReady: (node, textDocument) => view.bench.highlight(textDocument)
            onCollapseRequested: keys => view.report(blueprintModel.collapse(keys))
            onCommentRequested: keys => view.report(blueprintModel.addComment(keys))
            onCommentEdited: (key, changes) => view.report(blueprintModel.editComment(key, changes))
            onRerouteRequested: (wire, x, y) => view.report(blueprintModel.addReroute(wire, x, y))
            onArrangeRequested: view.report(blueprintModel.arrange())
            onOpened: node => view.openNode(node)
            onJumpRequested: node => {
                if (node === "")
                    view.jump(view.blueprintDocument.path, blueprintModel.problemLine, 1)
                else
                    view.jumpToNode(node)
            }
            onBreakpointToggled: node => view.report(blueprintModel.toggleBreakpoint(node))
            onBreakpointEnabled: (node, on) => view.report(blueprintModel.enableBreakpoint(node, on))
            onMeasured: sizes => blueprintModel.measured(sizes)
            onViewEdited: (x, y, zoom) => blueprintModel.setView(x, y, zoom)
            onSelectionEdited: keys => blueprintModel.selection = keys
            onStateAdded: (x, y) => view.report(blueprintModel.addState(x, y))
            onTransitionRequested: (from, to) => view.report(blueprintModel.connectStates(from, to))
            onTransitionLabelEdited: (wire, text) => view.report(blueprintModel.setTransitionLabel(wire, text))
            onStateRenamed: (node, name) => view.report(blueprintModel.renameState(node, name))
            onStartStateSet: node => view.report(blueprintModel.setStart(node))
            onFixReplaceRequested: (node, control) => {
                replaceDialog.node = node
                replaceDialog.control = control
                replaceDialog.open()
            }
            onFixDeleteRequested: node => view.report(blueprintModel.remove([node]))
        }

        InlineAlert {
            id: missingNotice

            objectName: "missingComponents"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: tabStrip.bottom
            anchors.leftMargin: Theme.space.md
            anchors.rightMargin: Theme.space.md
            anchors.topMargin: Theme.space.xs
            tone: "bad"
            visible: view.blueprint && view.blueprintDocument !== null && view.missingBlocks.length > 0
            title: view.missingBlocks.length === 1
                   ? qsTr("A block uses a component that is no longer in the profile")
                   : qsTr("%1 blocks use components that are no longer in the profile").arg(view.missingBlocks.length)
            detail: view.missingBlocks.length === 1
                    ? (view.missingBlocks[0].problem ?? "")
                    : qsTr("Pick “Replace with another component…” or “Delete block” on each block with a warning mark.")
        }

        Rectangle {
            objectName: "logicDraftRemoved"
            visible: view.preview !== null && view.blueprint && view.blueprintDocument !== null
                     && blueprintModel.diffRemoved.length > 0
            anchors.left: parent.left
            anchors.bottom: timelineBar.top
            anchors.margins: Theme.space.md
            width: Math.min(Theme.size.dialogWidth, parent.width - 2 * Theme.space.md)
            height: removedColumn.implicitHeight + 2 * Theme.space.sm
            radius: Theme.radius.md
            color: Theme.colors.dangerSubtle
            border.width: Theme.size.stroke
            border.color: Theme.colors.dangerLine

            Column {
                id: removedColumn

                x: Theme.space.sm
                y: Theme.space.sm
                width: parent.width - 2 * Theme.space.sm
                spacing: Theme.space.xxs

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: qsTr("Removed in this draft: %n block(s)", "", blueprintModel.diffRemoved.length)
                    color: Theme.colors.dangerText
                    font.pixelSize: Theme.type.caption.size
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: blueprintModel.diffRemoved

                    delegate: Text {
                        required property var modelData

                        width: parent.width
                        textFormat: Text.PlainText
                        text: (modelData.title ?? "") !== "" ? modelData.title : modelData.key
                        elide: Text.ElideRight
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                    }
                }
            }
        }

        DebugBar {
            objectName: "logicDebugBar"
            anchors.horizontalCenter: blueprintCanvas.horizontalCenter
            anchors.top: blueprintCanvas.top
            anchors.topMargin: Theme.space.sm
            visible: view.bench.debugging && blueprintCanvas.visible
            state: view.debugPause !== null ? "paused" : "running"
            pausedTitle: view.debugPause !== null
                         ? qsTr("%1, line %2").arg(view.debugPause.file).arg(String(view.debugPause.position).split(":")[0])
                         : ""
            shortcutsEnabled: view.visible && view.blueprint && view.debugPause !== null
            onContinueClicked: view.step(0)
            onStepOverClicked: view.step(1)
            onStepIntoClicked: view.step(2)
            onStepOutClicked: view.step(3)
            onStopClicked: view.bench.debug(false)
        }

        DebugTimeline {
            id: timelineBar

            objectName: "logicTimeline"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: view.recordShown
            height: visible ? implicitHeight : 0
            span: view.timelineData.span ?? ({ start: 0, end: 0 })
            playhead: view.timelineData.playhead ?? 0
            live: view.record.opened || (view.timelineData.live ?? true)
            frames: view.timelineData.frames ?? []
            steps: view.timelineData.steps ?? []
            lanes: view.timelineData.lanes ?? []
            onSeeked: at => view.record.seek(at)
            onLiveClicked: view.record.goLive()
            onSaveClicked: view.bench.saveDebugRecord()
        }
    }

    BlueprintModel {
        id: blueprintModel

        bench: view.bench
        document: view.blueprintUsed ? view.blueprintDocument : null
        diffBase: view.preview !== null && view.blueprintDocument !== null
                  ? view.preview.liveTextOf(view.blueprintDocument.path) : ""
        onPulse: (node, wire) => blueprintCanvas.pulse(node, wire)
        onJumpRequested: (path, line, column) => view.jump(path, line, column)
        onToast: text => toast.show(text, "warn")
    }

    Dialog {
        id: unsaved

        objectName: "unsavedFileDialog"
        title: qsTr("Save the changes to %1?").arg(view.documentName(view.closing))
        acceptText: qsTr("Save")
        alternativeText: qsTr("Don't save")
        rejectText: qsTr("Cancel")
        onAccepted: {
            const index = view.closing
            if (index >= 0 && view.bench.save(index))
                view.bench.closeDocument(index)
        }
        onAlternativeChosen: {
            const index = view.closing
            if (index >= 0)
                view.bench.closeDocument(index)
        }
        onClosed: view.closing = -1

        Text {
            width: parent.width
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("The changes are lost if you close the file without saving them.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }
    }

    Dialog {
        id: replaceDialog

        objectName: "replaceControlDialog"
        property string node
        property string control
        property var choices: []
        title: qsTr("Replace “%1” with another component").arg(replaceDialog.control)
        acceptText: qsTr("Replace")
        rejectText: qsTr("Cancel")
        onOpened: replaceDialog.choices = blueprintModel.controls()
        onAccepted: {
            if (replaceDialog.node !== "" && choice.currentIndex >= 0
                    && choice.currentIndex < replaceDialog.choices.length)
                view.report(blueprintModel.replaceControl(replaceDialog.node, replaceDialog.control,
                                                          replaceDialog.choices[choice.currentIndex].id))
        }
        onClosed: {
            replaceDialog.node = ""
            replaceDialog.control = ""
        }

        Column {
            width: parent.width
            spacing: Theme.space.md

            Text {
                width: parent.width
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: qsTr("Every block that used the old component will use the one you choose here.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
            }

            Select {
                id: choice

                width: parent.width
                Accessible.name: qsTr("Component")
                model: replaceDialog.choices.map(entry => entry.name + " · " + entry.type)
                currentIndex: replaceDialog.choices.length > 0 ? 0 : -1
            }
        }
    }

    Toast {
        id: toast
    }

    T.Popup {
        id: variablePopup

        objectName: "bpVariablePopup"
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        modal: true
        focus: true
        closePolicy: T.Popup.CloseOnEscape
        padding: Theme.space.lg
        background: PopupSurface {}

        contentItem: VariableEditor {
            id: variableEditor

            spec: blueprintModel.variableEditor
            onSaved: spec => {
                variableEditor.problem = blueprintModel.saveVariable(spec)
            }
            onRemoved: {
                variableEditor.problem = blueprintModel.removeVariable(blueprintModel.variableEditor.key ?? "")
            }
            onCancelled: blueprintModel.closeVariable()
        }

        onClosed: blueprintModel.closeVariable()
    }

    Connections {
        target: blueprintModel

        function onVariableEditorChanged() {
            const open = Object.keys(blueprintModel.variableEditor ?? ({})).length > 0
            variableEditor.problem = ""
            if (open && !variablePopup.opened)
                variablePopup.open()
            else if (!open && variablePopup.opened)
                variablePopup.close()
        }
    }

    Component.onCompleted: {
        view.bench.setColors(view.codeColors)
        view.showBlueprint(true)
    }

    TourOverlay {
        id: tour

        anchors.fill: parent
        objectName: "logicTour"
        tourKey: "logic"
        settings: AppSettings
        startWhen: view.visible && view.blueprint && !AppSettings.toursQuiet
        version: view.tourNodes + view.tourWires
        steps: [
            {
                centered: true,
                title: qsTr("The logic view"),
                text: qsTr("Logic lets keys do more: a combo on one press, a tap when a picture shows up, a "
                           + "different move depending on what is happening. The blueprint draws the logic as "
                           + "blocks; it is the code, and you can edit either one.")
            },
            {
                target: blueprintSidebar,
                title: qsTr("My blueprint"),
                text: qsTr("Components, variables, functions, the function library and the pages are here. Click a "
                           + "component to open its own file, where its key events go.")
            },
            {
                target: blueprintCanvas,
                title: qsTr("Place a block"),
                text: qsTr("Right-click an empty spot, or drag an item from the left, to place a block. Rest the "
                           + "mouse on a block in the list to see what it does. Try it: place one.")
            },
            {
                target: blueprintCanvas,
                title: qsTr("Connect the blocks"),
                text: qsTr("Drag from the white arrow on the right of a block to the left of the next one; the "
                           + "blocks run from left to right. The colored dots carry values and connect the same "
                           + "way.")
            },
            {
                target: pagesStrip,
                title: qsTr("Pages"),
                text: qsTr("The main page, the component files and every function have a page of their own; switch "
                           + "between them here.")
            },
            {
                target: viewSwitch,
                title: qsTr("Blueprint and code"),
                text: qsTr("Switch to the code to see the TypeScript behind the blueprint. Change the code and the "
                           + "blueprint follows.")
            },
            {
                target: scriptSidebar,
                optional: true,
                title: qsTr("Run and debug"),
                text: qsTr("Run starts the logic on the phone, Run on a recording tests it against a recorded video, "
                           + "and the live list shows what each component is doing. Set breakpoints on blocks to "
                           + "watch the values step by step.")
            },
            {
                target: consolePanel,
                optional: true,
                title: qsTr("Console, problems and references"),
                text: qsTr("Logs go to the console. Mistakes are listed under Problems; click one to go to it.")
            },
            {
                centered: true,
                title: qsTr("Start building"),
                text: qsTr("Not sure what a block does? Rest the mouse on it, or ask the AI assistant at the bottom "
                           + "right. Help can replay this tour.")
            }
        ]
        advance: {
            switch (tour.step) {
            case 2:
                return view.tourNodes > view.tourBaseNodes
            case 3:
                return view.tourWires > view.tourBaseWires
            }
            return false
        }
        onStarted: {
            view.tourBaseNodes = view.tourNodes
            view.tourBaseWires = view.tourWires
        }

        function startTour() {
            tour.restart()
        }
    }
}
