import QtQuick
import QtTest
import GameScrcpy.Theme
import GameScrcpy.Blueprint

Item {
    id: root

    width: 1280
    height: 800

    BlueprintMock {
        id: mock
    }

    BlueprintCanvas {
        id: canvas

        anchors.fill: parent
    }

    TestCase {
        name: "BlueprintMachine"
        when: windowShown

        function init() {
            failOnWarning(/.?/)
            canvas.graph = { key: "reset", kind: "events", nodes: [], wires: [], comments: [] }
            canvas.graph = mock.machineGraph()
            canvas.selection = []
            canvas.viewState = { x: 0, y: 0, zoom: 1 }
            canvas.forceActiveFocus()
            wait(0)
        }

        function cleanup() {
            for (const name of ["bpStateMenu", "bpMachineMenu", "bpWireMenu"]) {
                const menu = findChild(canvas, name)
                if (menu.visible)
                    menu.close()
                tryVerify(() => !menu.visible)
            }
        }

        function spy(name) {
            return createTemporaryObject(spyComponent, root, { signalName: name })
        }

        function card(key) {
            const found = findChild(canvas, "bpNode_" + key)
            verify(found !== null, "state " + key)
            return found
        }

        function middleOf(item) {
            return item.mapToItem(canvas, item.width / 2, item.height / 2)
        }

        function ringOf(key) {
            const found = card(key)
            return found.mapToItem(canvas, -Theme.size.bpRing / 2, found.height / 2)
        }

        function menuItem(menu, name) {
            return findChild(menu.contentItem, name) ?? findChild(canvas, name)
        }

        function drag(from, to) {
            mousePress(canvas, from.x, from.y)
            for (let step = 1; step <= 6; ++step)
                mouseMove(canvas, from.x + (to.x - from.x) * step / 6, from.y + (to.y - from.y) * step / 6, -1,
                          Qt.LeftButton)
            mouseRelease(canvas, to.x, to.y)
        }

        function test_statesAndTransitionsFollowTheGraph() {
            for (const key of ["lobby", "matching", "playing", "settle", "reconnect"])
                verify(card(key).visible, key)
            for (const key of ["t1", "t2", "t3", "t4", "t5", "t6", "t7"])
                verify(findChild(canvas, "bpWire_" + key).visible, key)
            verify(findChild(canvas, "bpTransitionLabel_t1").visible)
            verify(findChild(canvas, "bpAddState").visible)
            compare(findChild(canvas, "bpArrange").text, "Tidy up")
            verify(!findChild(canvas, "bpCommentBox").visible)
            compare(findChild(canvas, "bpStateClock_settle"), null)
            const clock = findChild(canvas, "bpStateClock_playing")
            verify(clock !== null && clock.visible)
            verify(clock.text.startsWith("03:1"), clock.text)
            compare(card("playing").Accessible.name, "State: 对局中")
        }

        function test_theActiveStateClockTicks() {
            const graph = mock.machineGraph()
            graph.nodes[2].activeSince = Date.now() - 5000
            canvas.graph = graph
            const clock = findChild(canvas, "bpStateClock_playing")
            tryVerify(() => clock.text === "00:05" || clock.text === "00:06")
            const before = clock.text
            tryVerify(() => clock.text !== before, 3000)
        }

        function test_twoTransitionsBetweenAPairCurveApart() {
            const there = findChild(canvas, "bpWire_t1")
            const back = findChild(canvas, "bpWire_t3")
            verify(there.lane !== 0)
            verify(Math.abs(there.route.labelY - back.route.labelY) > Theme.size.bpCurve)
            compare(findChild(canvas, "bpWire_t4").lane, 0)
        }

        function test_draggingFromTheRingAsksForATransition() {
            const asks = spy("transitionRequested")
            drag(ringOf("settle"), middleOf(card("reconnect")))
            compare(asks.count, 1)
            compare(asks.signalArguments[0][0], "settle")
            compare(asks.signalArguments[0][1], "reconnect")
            drag(ringOf("settle"), Qt.point(1100, 650))
            drag(ringOf("settle"), middleOf(card("settle")))
            compare(asks.count, 1)
            compare(canvas.selection, ["settle"])
        }

        function test_theRingDragHighlightsTheTarget() {
            const from = ringOf("settle")
            const to = middleOf(card("reconnect"))
            mousePress(canvas, from.x, from.y)
            for (let step = 1; step <= 6; ++step)
                mouseMove(canvas, from.x + (to.x - from.x) * step / 6, from.y + (to.y - from.y) * step / 6, -1,
                          Qt.LeftButton)
            verify(card("reconnect").target)
            verify(findChild(canvas, "bpDragWire").visible)
            mouseRelease(canvas, to.x, to.y)
            verify(!card("reconnect").target)
        }

        function test_doubleClickingALabelEditsTheReason() {
            const edits = spy("transitionLabelEdited")
            const label = findChild(canvas, "bpTransitionLabel_t1")
            mouseDoubleClickSequence(label, label.width / 2, label.height / 2)
            const field = findChild(canvas, "bpTransitionName_t1")
            verify(field !== null)
            tryVerify(() => field.activeFocus)
            compare(field.text, "点了开始")
            field.text = "点了开始按钮"
            keyClick(Qt.Key_Return)
            compare(edits.count, 1)
            compare(edits.signalArguments[0][0], "t1")
            compare(edits.signalArguments[0][1], "点了开始按钮")
            tryVerify(() => findChild(canvas, "bpTransitionName_t1") === null)
            const again = findChild(canvas, "bpTransitionLabel_t1")
            mouseDoubleClickSequence(again, again.width / 2, again.height / 2)
            const second = findChild(canvas, "bpTransitionName_t1")
            tryVerify(() => second.activeFocus)
            second.text = "别的"
            keyClick(Qt.Key_Escape)
            compare(edits.count, 1)
        }

        function test_doubleClickingAStateOpensIt() {
            const opens = spy("opened")
            const playing = card("playing")
            mouseDoubleClickSequence(playing, playing.width / 2, playing.height - Theme.space.sm)
            compare(opens.count, 1)
            compare(opens.signalArguments[0][0], "playing")
        }

        function test_theToolbarAddsAStateAtTheCentre() {
            const adds = spy("stateAdded")
            mouseClick(findChild(canvas, "bpAddState"))
            compare(adds.count, 1)
            fuzzyCompare(adds.signalArguments[0][0], canvas.width / 2 - Theme.size.bpStateWidth / 2, 1)
            fuzzyCompare(adds.signalArguments[0][1], canvas.height / 2 - Theme.size.bpHeader, 1)
        }

        function test_theStateMenuSetsTheStartAndRenames() {
            const starts = spy("startStateSet")
            const renames = spy("stateRenamed")
            const settle = middleOf(card("settle"))
            mouseClick(canvas, settle.x, settle.y, Qt.RightButton)
            const menu = findChild(canvas, "bpStateMenu")
            tryVerify(() => menu.opened)
            mouseClick(menuItem(menu, "bpStateStart"))
            compare(starts.count, 1)
            compare(starts.signalArguments[0][0], "settle")
            tryVerify(() => !menu.visible)
            mouseClick(canvas, settle.x, settle.y, Qt.RightButton)
            tryVerify(() => menu.opened)
            mouseClick(menuItem(menu, "bpStateRename"))
            const field = findChild(canvas, "bpStateName_settle")
            verify(field !== null)
            tryVerify(() => field.activeFocus)
            compare(field.text, "结算")
            field.text = "领奖"
            keyClick(Qt.Key_Return)
            compare(renames.count, 1)
            compare(renames.signalArguments[0][0], "settle")
            compare(renames.signalArguments[0][1], "领奖")
        }

        function test_theStartStateHasNoSetAsStart() {
            const lobby = middleOf(card("lobby"))
            mouseClick(canvas, lobby.x, lobby.y, Qt.RightButton)
            const menu = findChild(canvas, "bpStateMenu")
            tryVerify(() => menu.opened)
            verify(!menuItem(menu, "bpStateStart").visible)
            menu.close()
        }

        function test_theEmptyCanvasMenuAddsAStateThere() {
            const adds = spy("stateAdded")
            mouseClick(canvas, 1000, 600, Qt.RightButton)
            const menu = findChild(canvas, "bpMachineMenu")
            tryVerify(() => menu.opened)
            mouseClick(menuItem(menu, "bpMenuAddState"))
            compare(adds.count, 1)
            fuzzyCompare(adds.signalArguments[0][0], 1000 - Theme.size.bpStateWidth / 2, 1)
            fuzzyCompare(adds.signalArguments[0][1], 600 - Theme.size.bpHeader, 1)
            const picker = canvas.picker
            verify(!picker.opened)
        }

        function test_aSelectedTransitionIsRemovedWithDelete() {
            const removals = spy("wireRemoved")
            mouseClick(findChild(canvas, "bpTransitionLabel_t4"))
            compare(canvas.selectedWire, "t4")
            keyClick(Qt.Key_Delete)
            compare(removals.count, 1)
            compare(removals.signalArguments[0][0], "t4")
        }

        function test_readOnlyMachinesOnlySelect() {
            const graph = mock.machineGraph()
            graph.readOnly = true
            canvas.graph = graph
            const asks = spy("transitionRequested")
            drag(ringOf("settle"), middleOf(card("reconnect")))
            compare(asks.count, 0)
            verify(!findChild(canvas, "bpAddState").visible)
            const label = findChild(canvas, "bpTransitionLabel_t1")
            mouseDoubleClickSequence(label, label.width / 2, label.height / 2)
            compare(findChild(canvas, "bpTransitionName_t1"), null)
        }

        Component {
            id: spyComponent

            SignalSpy {
                target: canvas
            }
        }
    }
}
