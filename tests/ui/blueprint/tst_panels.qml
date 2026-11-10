import QtQuick
import QtTest
import GameScrcpy.Theme
import GameScrcpy.Blueprint

Item {
    id: root

    width: 1200
    height: 1500

    BlueprintMock {
        id: mock
    }

    BlueprintSidebar {
        id: sidebar

        width: Theme.size.workbenchLeft
        height: 600
        sections: mock.sidebar
    }

    BlueprintPages {
        id: pages

        x: sidebar.width
        width: 600
        pages: mock.pages
        crumbs: [{ key: "", title: "刷副本" }, { key: "fn:run", title: "run" }, { key: "machine:3:5", title: "状态机" }]
        current: "machine:3:5"
    }

    BlueprintPages {
        id: componentTabs

        objectName: "componentTabs"
        x: 0
        y: 690
        width: 700
        pages: mock.pages
        current: mock.page
        onPageChosen: key => mock.open(key)
        onCrumbChosen: key => mock.open(key)
    }

    Connections {
        target: sidebar

        function onItemActivated(item: var) {
            const key = item.item ?? ""
            if (key.startsWith("control:"))
                mock.open(key)
        }
    }

    BlueprintCanvas {
        id: canvas

        x: sidebar.width
        y: pages.height
        width: 600
        height: 600
        graph: mock.eventGraph()
    }

    BlueprintDetails {
        id: details

        x: canvas.x + canvas.width
        width: Theme.size.workbenchSidebar
        height: 900
    }

    VariableEditor {
        id: variableEditor

        y: 900
        spec: ({})
    }

    SignalSpy {
        id: activations

        target: sidebar
        signalName: "itemActivated"
    }

    SignalSpy {
        id: adds

        target: sidebar
        signalName: "addClicked"
    }

    SignalSpy {
        id: drops

        target: canvas
        signalName: "dropped"
    }

    SignalSpy {
        id: chosenPages

        target: pages
        signalName: "pageChosen"
    }

    SignalSpy {
        id: chosenCrumbs

        target: pages
        signalName: "crumbChosen"
    }

    SignalSpy {
        id: edits

        target: details
        signalName: "fieldEdited"
    }

    SignalSpy {
        id: jumps

        target: details
        signalName: "jumpRequested"
    }

    SignalSpy {
        id: savedVariables

        target: variableEditor
        signalName: "saved"
    }

    TestCase {
        name: "BlueprintPanels"
        when: windowShown

        function init() {
            failOnWarning(/.?/)
            for (const spy of [activations, adds, drops, chosenPages, chosenCrumbs, edits, jumps, savedVariables])
                spy.clear()
            sidebar.collapsed = {}
            sidebar.expanded = {}
            mock.selection = ["fHold"]
            details.details = mock.details
            waitForPolish(sidebar)
        }

        function argsOf(spy, index) {
            return Array.prototype.slice.call(spy.signalArguments[index])
        }

        function test_sectionsCollapseAndAdd() {
            verify(findChild(sidebar, "bpItem_controls_control:fire") !== null)
            mouseClick(findChild(sidebar, "bpSection_controls"), Theme.space.x3l, Theme.size.bpSection / 2)
            tryVerify(() => findChild(sidebar, "bpItem_controls_control:fire") === null)
            mouseClick(findChild(sidebar, "bpSection_controls"), Theme.space.x3l, Theme.size.bpSection / 2)
            tryVerify(() => findChild(sidebar, "bpItem_controls_control:fire") !== null)
            mouseClick(findChild(sidebar, "bpAdd_vars"))
            compare(adds.count, 1)
            compare(adds.signalArguments[0][0], "vars")
        }

        function test_membersShowAndActivate() {
            verify(findChild(sidebar, "bpMember_controls_control:aim_held") === null)
            sidebar.expanded = { "controls/control:aim": true }
            const member = findChild(sidebar, "bpMember_controls_control:aim_held")
            verify(member !== null)
            wait(0)
            waitForPolish(member.parent)
            mouseClick(member, member.width / 2, member.height / 2)
            compare(activations.count, 1)
            compare(activations.signalArguments[0][0].section, "controls")
            compare(activations.signalArguments[0][0].item, "control:aim")
            compare(activations.signalArguments[0][0].member, "held")
        }

        function test_draggingAnItemOntoTheCanvasDrops() {
            const item = findChild(sidebar, "bpItem_controls_control:fire")
            const from = item.mapToItem(root, item.width / 2, item.height / 2)
            const to = canvas.mapToItem(root, 500, 560)
            mousePress(root, from.x, from.y)
            for (let step = 1; step <= 8; ++step)
                mouseMove(root, from.x + (to.x - from.x) * step / 8, from.y + (to.y - from.y) * step / 8, -1,
                          Qt.LeftButton)
            mouseRelease(root, to.x, to.y)
            compare(drops.count, 1)
            compare(drops.signalArguments[0][0].section, "controls")
            compare(drops.signalArguments[0][0].item, "control:fire")
            compare(drops.signalArguments[0][0].member, "")
            fuzzyCompare(drops.signalArguments[0][1], 500, 1)
            fuzzyCompare(drops.signalArguments[0][2], 560, 1)
        }

        function test_pagesAndCrumbsAsk() {
            mouseClick(findChild(pages, "bpPage_fn:combo"))
            compare(chosenPages.count, 1)
            compare(chosenPages.signalArguments[0][0], "fn:combo")
            verify(findChild(pages, "bpCrumbs").visible)
            mouseClick(findChild(pages, "bpCrumb_1"))
            compare(chosenCrumbs.count, 1)
            compare(chosenCrumbs.signalArguments[0][0], "fn:run")
            mouseClick(findChild(pages, "bpCrumb_2"))
            compare(chosenCrumbs.count, 1)
        }

        function test_detailsEditFields() {
            compare(findChild(details, "bpDetailsTitle").text, "按键事件")
            const segments = findChild(details, "bpFieldSegments_when")
            verify(segments !== null)
            compare(segments.currentIndex, 3)
            waitForPolish(segments.contentItem.children[1])
            mouseClick(segments, Theme.space.md, segments.height / 2)
            compare(edits.count, 1)
            compare(argsOf(edits, 0), ["when", "press"])
            const toggle = findChild(details, "bpFieldSwitch_enabled")
            mouseClick(toggle)
            compare(edits.count, 2)
            compare(argsOf(edits, 1), ["enabled", false])
            const code = findChild(details, "bpDetailsCode")
            verify(code.text.indexOf("onLongPress") >= 0)
            mouseClick(findChild(details, "bpJumpToCode"))
            compare(jumps.count, 1)
        }

        function test_unsetOptionsShowTheirDefault() {
            const field = (name, label, kind, value, unset, extra) => Object.assign({
                name: name, label: label, kind: kind, value: value, caption: "", info: "", unit: "",
                choices: [], items: [], entries: []
            }, unset ? { isDefault: true } : {}, extra ?? {})
            details.details = {
                icon: "", tone: "", title: "vision.waitFor", subtitle: "", code: "", codeLine: 0,
                groups: [{ title: "", fields: [
                    field("options.timeoutMs", "最多等", "duration", 10000, true, { unit: "秒" }),
                    field("options.intervalMs", "间隔", "duration", 3000, false, { unit: "秒" }),
                    field("options.threshold", "相似度", "number", "", true),
                    field("options.match", "比较", "choice", "grey", true,
                          { choices: [{ value: "color", text: "颜色" }, { value: "grey", text: "灰度" }] })
                ] }]
            }
            const timeout = findChild(details, "bpFieldNumber_options.timeoutMs")
            verify(timeout !== null)
            verify(timeout.automatic)
            verify(timeout.word.indexOf("10.0") >= 0)
            verify(timeout.word.indexOf("秒") >= 0)
            const interval = findChild(details, "bpFieldNumber_options.intervalMs")
            verify(interval !== null)
            verify(!interval.automatic)
            compare(interval.word, "")
            compare(interval.text, "3.0")
            const threshold = findChild(details, "bpFieldNumber_options.threshold")
            verify(threshold !== null)
            verify(threshold.automatic)
            verify(threshold.word !== "")
            verify(threshold.word.indexOf("0") < 0)
            const match = findChild(details, "bpFieldSelect_options.match")
            verify(match !== null)
            verify(match.displayText.indexOf("灰度") >= 0)
            verify(match.displayText !== "灰度")
        }

        function test_aComponentOpensItsOwnPageFromTheSidebar() {
            mock.open("")
            const item = findChild(sidebar, "bpItem_controls_control:fire")
            verify(item !== null)
            waitForPolish(item)
            mouseClick(item, item.width / 2, item.height / 2)
            compare(mock.page, "control:fire")
            const tab = findChild(componentTabs, "bpPage_control:fire")
            verify(tab !== null)
            compare(tab.Accessible.name, "开火 · F")
            compare(componentTabs.current, "control:fire")
            mouseClick(tab)
            compare(mock.page, "control:fire")
            const home = findChild(componentTabs, "bpPage_")
            verify(home !== null)
            mouseClick(home)
            compare(mock.page, "")
        }

        function test_emptyDetailsSayWhatToDo() {
            details.details = {}
            verify(!findChild(details, "bpDetailsTitle").visible || findChild(details, "bpDetailsTitle").text === "")
        }

        function test_variablesShowTheirCategory() {
            const head = findChild(sidebar, "bpCategory_vars_variable:health")
            verify(head !== null)
            verify(head.visible)
            compare(head.text, "战斗")
            const second = findChild(sidebar, "bpCategory_vars_variable:speed")
            verify(second !== null)
            verify(!second.visible)
        }

        function test_aVariableDraggedWithControlComesOutAsAGet() {
            const item = findChild(sidebar, "bpItem_vars_variable:health")
            verify(item !== null)
            const from = item.mapToItem(root, item.width / 2, item.height / 2)
            const to = canvas.mapToItem(root, 500, 560)
            mousePress(root, from.x, from.y, Qt.LeftButton, Qt.ControlModifier)
            for (let step = 1; step <= 8; ++step)
                mouseMove(root, from.x + (to.x - from.x) * step / 8, from.y + (to.y - from.y) * step / 8, -1,
                          Qt.LeftButton)
            mouseRelease(root, to.x, to.y)
            compare(drops.count, 1)
            compare(drops.signalArguments[0][0].item, "variable:health")
            compare(drops.signalArguments[0][0].mode, "get")
        }

        function test_aPlainVariableDropAsksGetOrSet() {
            const item = findChild(sidebar, "bpItem_vars_variable:speed")
            verify(item !== null)
            const from = item.mapToItem(root, item.width / 2, item.height / 2)
            const to = canvas.mapToItem(root, 500, 560)
            mousePress(root, from.x, from.y)
            for (let step = 1; step <= 8; ++step)
                mouseMove(root, from.x + (to.x - from.x) * step / 8, from.y + (to.y - from.y) * step / 8, -1,
                          Qt.LeftButton)
            mouseRelease(root, to.x, to.y)
            compare(drops.count, 0)
            const menu = findChild(canvas, "bpVarMenu")
            verify(menu !== null)
            verify(menu.visible)
            menu.close()
        }

        function test_variableEditorShowsTheSpecAndSavesIt() {
            variableEditor.spec = {
                key: "", name: "目标", kind: "point", container: "list", keyKind: "text", value: "[]",
                category: "战斗", description: "要去的地方", keep: false, editable: false, constant: false,
                scope: "file", pageTitle: "主页", canBeEditable: false,
                types: [{ value: "integer", text: "整数" }, { value: "point", text: "位置" }],
                containers: [{ value: "single", text: "一个" }, { value: "list", text: "列表" }],
                keyKinds: [{ value: "text", text: "文字" }, { value: "number", text: "数字" }]
            }
            compare(findChild(variableEditor, "bpVarName").text, "目标")
            compare(findChild(variableEditor, "bpVarValue").text, "[]")
            compare(findChild(variableEditor, "bpVarCategory").text, "战斗")
            const nameField = findChild(variableEditor, "bpVarName")
            nameField.text = "路标"
            nameField.textEdited()
            mouseClick(findChild(variableEditor, "bpVarSave"))
            compare(savedVariables.count, 1)
            const spec = savedVariables.signalArguments[0][0]
            compare(spec.name, "路标")
            compare(spec.kind, "point")
            compare(spec.container, "list")
            compare(spec.category, "战斗")
            compare(spec.description, "要去的地方")
        }
    }
}
