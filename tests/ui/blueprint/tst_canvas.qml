import QtQuick
import QtTest
import GameScrcpy.Theme
import GameScrcpy.Blueprint

Item {
    id: root

    width: 1280
    height: 980

    BlueprintMock {
        id: mock
    }

    BlueprintCanvas {
        id: canvas

        anchors.fill: parent
        check: mock.check
        catalog: mock.catalog
        onInsertRequested: (wire, node) => mock.insertOnWire(wire, node)
    }

    SignalSpy {
        id: inserts

        target: canvas
        signalName: "insertRequested"
    }

    SignalSpy {
        id: connects

        target: canvas
        signalName: "connectRequested"
    }

    SignalSpy {
        id: rewires

        target: canvas
        signalName: "rewireRequested"
    }

    SignalSpy {
        id: disconnects

        target: canvas
        signalName: "disconnectRequested"
    }

    SignalSpy {
        id: additions

        target: canvas
        signalName: "addChosen"
    }

    SignalSpy {
        id: removals

        target: canvas
        signalName: "removeRequested"
    }

    SignalSpy {
        id: moves

        target: canvas
        signalName: "moved"
    }

    SignalSpy {
        id: values

        target: canvas
        signalName: "valueEdited"
    }

    SignalSpy {
        id: codes

        target: canvas
        signalName: "codeEdited"
    }

    SignalSpy {
        id: selections

        target: canvas
        signalName: "selectionEdited"
    }

    SignalSpy {
        id: views

        target: canvas
        signalName: "viewEdited"
    }

    SignalSpy {
        id: sizes

        target: canvas
        signalName: "measured"
    }

    TestCase {
        name: "BlueprintCanvas"
        when: windowShown

        function init() {
            failOnWarning(/.?/)
            canvas.graph = { key: "reset", kind: "events", nodes: [], wires: [], comments: [] }
            canvas.graph = mock.eventGraph()
            canvas.selection = []
            canvas.snap = false
            canvas.viewState = { x: 0, y: 0, zoom: 1 }
            mock.calls = []
            for (const spy of [connects, rewires, disconnects, additions, removals, moves, values, codes, selections,
                               views, sizes, inserts])
                spy.clear()
            canvas.forceActiveFocus()
            wait(0)
        }

        function cleanup() {
            if (canvas.picker.opened)
                canvas.picker.close()
            const menu = findChild(canvas, "bpWireMenu")
            if (menu !== null && menu.visible)
                menu.close()
        }

        function pinItem(node, pin, output) {
            const block = findChild(canvas, "bpNode_" + node)
            verify(block !== null, "node " + node)
            const name = "bpPin_" + node + "_" + pin
            const stack = [block]
            while (stack.length > 0) {
                const item = stack.pop()
                if (item.objectName === name && item.output === output && item.visible)
                    return item
                for (const child of item.children)
                    stack.push(child)
            }
            fail("no pin " + name)
            return null
        }

        function centerOf(item) {
            return item.mapToItem(canvas, item.width / 2, item.height / 2)
        }

        function argsOf(spy, index) {
            return Array.prototype.slice.call(spy.signalArguments[index])
        }

        function chipOf(node, pin) {
            return findChild(pinItem(node, pin, false).parent, "pinChip")
        }

        function press(point, modifiers) {
            mousePress(canvas, point.x, point.y, Qt.LeftButton, modifiers ?? Qt.NoModifier)
        }

        function glide(from, to) {
            for (let step = 1; step <= 6; ++step)
                mouseMove(canvas, from.x + (to.x - from.x) * step / 6, from.y + (to.y - from.y) * step / 6, -1,
                          Qt.LeftButton)
        }

        function dragBetween(from, to, modifiers) {
            press(from, modifiers)
            glide(from, to)
            mouseRelease(canvas, to.x, to.y, Qt.LeftButton, modifiers ?? Qt.NoModifier)
        }

        function test_nodesWiresAndCommentsFollowTheGraph() {
            for (const key of ["fTap", "find", "reroute", "log", "flash"])
                verify(findChild(canvas, "bpNode_" + key) !== null, key)
            for (const key of ["w1", "w6", "w14", "w15"])
                verify(findChild(canvas, "bpWire_" + key) !== null, key)
            for (const key of ["c1", "c2", "c3"])
                verify(findChild(canvas, "bpComment_" + key) !== null, key)
            const fTap = findChild(canvas, "bpNode_fTap")
            compare(fTap.x, 40)
            compare(fTap.y, 60)
            verify(fTap.width >= Theme.size.bpNodeWidth)
            compare(fTap.Accessible.name, "Event: 按键 F · 单击")
            const pin = pinItem("find", "pos", true)
            compare(pin.Accessible.name, "Output: 位置")
            verify(findChild(canvas, "bpFloating_flash").visible)
            tryVerify(() => sizes.count > 0)
            const reported = sizes.signalArguments[sizes.count - 1][0]
            verify(Object.keys(reported).length > 0)
        }

        function test_dragFromAnOutputToAnInputConnects() {
            dragBetween(centerOf(pinItem("pressKey", "exec", true)), centerOf(pinItem("log", "exec", false)))
            compare(connects.count, 1)
            compare(argsOf(connects, 0), ["pressKey", "exec", "log", "exec"])
        }

        function test_dragFromAnInputPutsTheOutputFirst() {
            dragBetween(centerOf(pinItem("fireUp", "exec", false)), centerOf(pinItem("moveOff", "exec", true)))
            compare(connects.count, 1)
            compare(argsOf(connects, 0), ["moveOff", "exec", "fireUp", "exec"])
        }

        function test_dragFromAConnectedOutputToAnotherInputAsksForASecondWire() {
            dragBetween(centerOf(pinItem("pick", "exec", true)), centerOf(pinItem("flash", "exec", false)))
            compare(connects.count, 1)
            compare(argsOf(connects, 0), ["pick", "exec", "flash", "exec"])
            compare(rewires.count, 0)
            verify(findChild(canvas, "bpWire_w2") !== null)
        }

        function test_severalWiresFromOneOutputAreAllDrawn() {
            mock.connectPins("pick", "exec", "flash", "exec")
            canvas.graph = mock.graph
            wait(0)
            const wires = (canvas.graph.wires ?? []).filter(wire => wire.from === "pick" && wire.fromPin === "exec")
            compare(wires.length, 2)
            for (const wire of wires)
                verify(findChild(canvas, "bpWire_" + wire.key) !== null, wire.key)
            compare(mock.pinOf("pick", "exec", true).connected, true)
            compare(findChild(canvas, "bpNode_pick").opacity, 1)
        }

        function test_hoveringAnIncompatiblePinShowsTheReason() {
            const from = centerOf(pinItem("fTap", "exec", true))
            const to = centerOf(pinItem("branch", "cond", false))
            press(from)
            glide(from, to)
            const reason = findChild(canvas, "bpReason")
            tryVerify(() => reason.visible)
            compare(findChild(canvas, "bpReasonText").text, mock.check("fTap", "exec", "branch", "cond"))
            mouseRelease(canvas, to.x, to.y)
            compare(connects.count, 0)
            verify(!reason.visible)
        }

        function test_dropOnEmptyCanvasOpensTheFilteredPicker() {
            const from = centerOf(pinItem("pressKey", "exec", true))
            const empty = Qt.point(1120, 600)
            dragBetween(from, empty)
            compare(connects.count, 0)
            const picker = canvas.picker
            tryVerify(() => picker.opened)
            compare(picker.fromNode, "pressKey")
            compare(picker.fromPin, "exec")
            verify(findChild(picker.contentItem, "bpPickerHeader").visible)
            compare(mock.calls[mock.calls.length - 1].fromNode, "pressKey")
            const search = findChild(picker.contentItem, "bpPickerSearch")
            tryVerify(() => search.activeFocus)
            for (const letter of "wait")
                keyClick(letter)
            tryVerify(() => mock.calls[mock.calls.length - 1].query === "wait")
            compare(picker.items.length, 1)
            const row = findChild(findChild(picker.contentItem, "bpPickerList"), "bpPickerItem_wait")
            verify(row !== null)
            compare(row.height, Theme.size.menuItem)
            const tip = findChild(picker.contentItem, "bpPickerTip")
            verify(tip !== null)
            verify(!tip.visible)
            waitForItemPolished(findChild(picker.contentItem, "bpPickerList"))
            waitForItemPolished(picker.contentItem.parent)
            mouseMove(row, row.width / 2, row.height / 2)
            tryVerify(() => tip.visible)
            compare(tip.text, "等待")
            compare(tip.detail, "等一段时间再往下走。")
            keyClick(Qt.Key_Return)
            compare(additions.count, 1)
            const added = additions.signalArguments[0]
            compare(added[0], "wait")
            fuzzyCompare(added[1], empty.x, 1)
            fuzzyCompare(added[2], empty.y, 1)
            compare(added[3], "pressKey")
            compare(added[4], "exec")
            tryVerify(() => !picker.opened)
        }

        function test_componentBlocksFoldIntoRowsWithASecondLevel() {
            const picker = canvas.picker
            picker.show(10, 10, 100, 100, "", "", "")
            tryVerify(() => picker.opened)
            const list = findChild(picker.contentItem, "bpPickerList")
            tryVerify(() => findChild(list, "bpPickerFolder_fire") !== null)
            verify(findChild(list, "bpPickerFolder_aim") !== null)
            compare(findChild(list, "bpPickerItem_fire.press"), null)
            const fire = findChild(list, "bpPickerFolder_fire")
            compare(fire.title, "开火 · F")
            compare(findChild(list, "bpPickerFolder_aim").title, "开镜")
            mouseClick(fire)
            tryVerify(() => findChild(list, "bpPickerItem_fire.release") !== null)
            verify(findChild(list, "bpPickerBack") !== null)
            compare(findChild(list, "bpPickerFolder_fire"), null)
            keyClick(Qt.Key_Left)
            tryVerify(() => findChild(list, "bpPickerFolder_fire") !== null)
            const search = findChild(picker.contentItem, "bpPickerSearch")
            for (const letter of "kaihuo")
                keyClick(letter)
            tryVerify(() => findChild(list, "bpPickerItem_fire.press") !== null)
            compare(findChild(list, "bpPickerFolder_fire"), null)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !picker.opened)
        }

        function test_altClickDisconnects() {
            const pin = pinItem("branch", "cond", false)
            mouseClick(pin, pin.width / 2, pin.height / 2, Qt.LeftButton, Qt.AltModifier)
            compare(disconnects.count, 1)
            compare(argsOf(disconnects, 0), ["branch", "cond"])
            compare(connects.count, 0)
        }

        function test_controlDragMovesTheWires() {
            dragBetween(centerOf(pinItem("fireUp", "exec", true)), centerOf(pinItem("moveOff", "exec", true)),
                        Qt.ControlModifier)
            compare(rewires.count, 1)
            compare(argsOf(rewires, 0), ["fireUp", "exec", "moveOff", "exec"])
            compare(connects.count, 0)
        }

        function test_marqueeSelectsAndDeleteRemoves() {
            dragBetween(Qt.point(30, 150), Qt.point(480, 280))
            const keys = canvas.selection.slice().sort()
            compare(keys, ["bag", "fHold"])
            verify(selections.count > 0)
            keyClick(Qt.Key_Delete)
            compare(removals.count, 1)
            compare(removals.signalArguments[0][0].slice().sort(), ["bag", "fHold"])
        }

        function test_clickSelectsAndModifiersToggle() {
            const fTap = findChild(canvas, "bpNode_fTap")
            mouseClick(fTap, fTap.width / 2, Theme.size.bpHeader / 2)
            compare(canvas.selection, ["fTap"])
            const pick = findChild(canvas, "bpNode_pick")
            mouseClick(pick, pick.width / 2, Theme.size.bpHeader / 2, Qt.LeftButton, Qt.ControlModifier)
            compare(canvas.selection, ["fTap", "pick"])
            mouseClick(fTap, fTap.width / 2, Theme.size.bpHeader / 2, Qt.LeftButton, Qt.ShiftModifier)
            compare(canvas.selection, ["pick"])
            keyClick(Qt.Key_A, Qt.ControlModifier)
            verify(canvas.selection.length > 20)
            keyClick(Qt.Key_Escape)
            compare(canvas.selection, [])
        }

        function test_draggingANodeEmitsMovedWithSnap() {
            canvas.snap = true
            const fTap = findChild(canvas, "bpNode_fTap")
            const from = fTap.mapToItem(canvas, fTap.width / 2, Theme.size.bpHeader / 2)
            dragBetween(from, Qt.point(from.x + 37, from.y + 21))
            compare(moves.count, 1)
            compare(moves.signalArguments[0][0], ["fTap"])
            compare(moves.signalArguments[0][1], 40)
            compare(moves.signalArguments[0][2], 20)
        }

        function test_draggingACommentMovesTheNodesInside() {
            const box = findChild(canvas, "bpComment_c3")
            const from = box.mapToItem(canvas, Theme.space.x5l, Theme.size.bpCommentTitle / 2)
            dragBetween(from, Qt.point(from.x + 40, from.y + 30))
            compare(moves.count, 1)
            const keys = moves.signalArguments[0][0]
            for (const key of ["c3", "every", "find", "reroute", "click", "log"])
                verify(keys.indexOf(key) >= 0, key)
            verify(keys.indexOf("fTap") < 0)
            fuzzyCompare(moves.signalArguments[0][1], 40, 0.5)
            fuzzyCompare(moves.signalArguments[0][2], 30, 0.5)
        }

        function test_inlineEditorsEmitTheRawValue() {
            const wait = findChild(canvas, "bpNode_wait")
            mouseClick(chipOf("wait", "time"))
            const input = findChild(wait, "pinInput")
            tryVerify(() => input.activeFocus)
            compare(input.text, "0.3")
            keyClick("1")
            keyClick(Qt.Key_Period)
            keyClick("5")
            keyClick(Qt.Key_Return)
            compare(values.count, 1)
            compare(argsOf(values, 0), ["wait", "time", 1500])
            mouseClick(chipOf("moveOff", "on"))
            compare(values.count, 2)
            compare(argsOf(values, 1), ["moveOff", "on", true])
            mouseClick(chipOf("find", "which"))
            const choices = findChild(canvas, "bpChoices")
            tryVerify(() => choices.opened)
            const option = findChild(choices.contentItem, "pinChoice_first")
            verify(option !== null && option.visible)
            mouseClick(option)
            compare(values.count, 3)
            compare(argsOf(values, 2), ["find", "which", "first"])
        }

        function test_keyEditorCapturesAName() {
            mouseClick(chipOf("pressKey", "key"))
            keyClick(Qt.Key_G)
            compare(values.count, 1)
            compare(argsOf(values, 0), ["pressKey", "key", "KeyG"])
        }

        function test_optionalPinsHideBehindMore() {
            const find = findChild(canvas, "bpNode_find")
            const more = findChild(find, "bpMore_find")
            verify(more.visible)
            const name = "bpPin_find_threshold"
            const hidden = () => {
                const stack = [find]
                while (stack.length > 0) {
                    const item = stack.pop()
                    if (item.objectName === name && item.visible)
                        return false
                    for (const child of item.children)
                        stack.push(child)
                }
                return true
            }
            verify(hidden())
            const before = find.height
            mouseClick(more)
            verify(!hidden())
            verify(find.height > before)
        }

        function test_codeBlockCommitsOnControlEnterAndRevertsOnEscape() {
            const code = findChild(canvas, "bpCode_log")
            const original = code.text
            mouseClick(code, Theme.space.xs, Theme.space.xs)
            tryVerify(() => code.activeFocus)
            code.cursorPosition = 0
            keyClick("x")
            keyClick(Qt.Key_Return, Qt.ControlModifier)
            compare(codes.count, 1)
            compare(codes.signalArguments[0][0], "log")
            compare(codes.signalArguments[0][1], "x" + original)
            tryVerify(() => !code.activeFocus)
            compare(codes.count, 1)
            mouseClick(code, Theme.space.xs, Theme.space.xs)
            tryVerify(() => code.activeFocus)
            keyClick("y")
            keyClick(Qt.Key_Escape)
            compare(code.text, original)
            tryVerify(() => !code.activeFocus)
            compare(codes.count, 1)
        }

        function test_zoomStaysWithinItsRange() {
            for (let step = 0; step < 30; ++step)
                mouseWheel(canvas, 1120, 600, 0, 120)
            compare(canvas.zoom, canvas.maximumZoom)
            for (let step = 0; step < 60; ++step)
                mouseWheel(canvas, 1120, 600, 0, -120)
            compare(canvas.zoom, canvas.minimumZoom)
            tryVerify(() => views.count > 0)
            compare(canvas.viewState.zoom, canvas.minimumZoom)
            const zoomIn = findChild(canvas, "bpZoomIn")
            for (let step = 0; step < 12; ++step)
                mouseClick(zoomIn)
            compare(canvas.zoom, canvas.maximumZoom)
            verify(!zoomIn.enabled)
            mouseClick(findChild(canvas, "bpZoomReset"))
            compare(canvas.zoom, 1)
        }

        function test_wheelKeepsThePointUnderTheCursor() {
            const at = Qt.point(1120, 600)
            const before = Qt.point(canvas.viewState.x + at.x / canvas.zoom, canvas.viewState.y + at.y / canvas.zoom)
            mouseWheel(canvas, at.x, at.y, 0, 120)
            tryVerify(() => views.count > 0)
            const state = canvas.viewState
            fuzzyCompare(state.x + at.x / state.zoom, before.x, 0.01)
            fuzzyCompare(state.y + at.y / state.zoom, before.y, 0.01)
        }

        function test_fitAndFrameChangeTheView() {
            canvas.viewState = { x: 600, y: 500, zoom: 1.5 }
            canvas.fit()
            tryVerify(() => views.count > 0 && !canvas.animating)
            verify(canvas.viewState.zoom < 1)
            verify(canvas.viewState.x < 600)
            views.clear()
            canvas.frame(["find"])
            tryVerify(() => views.count > 0 && !canvas.animating)
            compare(canvas.viewState.zoom, 1)
            const find = findChild(canvas, "bpNode_find")
            fuzzyCompare(canvas.viewState.x + canvas.width / 2, find.x + find.width / 2, 1)
            fuzzyCompare(canvas.viewState.y + canvas.height / 2, find.y + find.height / 2, 1)
        }

        function test_doubleClickOnAWireAsksForAReroute() {
            const spy = createTemporaryObject(spyComponent, root, { signalName: "rerouteRequested" })
            const wire = findChild(canvas, "bpWire_w1")
            const point = Qt.point((wire.curve.x0 + wire.curve.x1) / 2, (wire.curve.y0 + wire.curve.y1) / 2)
            mouseDoubleClickSequence(canvas, point.x, point.y)
            compare(spy.count, 1)
            compare(spy.signalArguments[0][0], "w1")
        }

        function test_readOnlyGraphsOnlySelectAndCopy() {
            const graph = mock.eventGraph()
            graph.readOnly = true
            canvas.graph = graph
            const copies = createTemporaryObject(spyComponent, root, { signalName: "copyRequested" })
            dragBetween(centerOf(pinItem("pressKey", "exec", true)), centerOf(pinItem("log", "exec", false)))
            compare(connects.count, 0)
            const fTap = findChild(canvas, "bpNode_fTap")
            const from = fTap.mapToItem(canvas, fTap.width / 2, Theme.size.bpHeader / 2)
            dragBetween(from, Qt.point(from.x + 40, from.y + 40))
            compare(moves.count, 0)
            compare(canvas.selection, ["fTap"])
            keyClick(Qt.Key_Delete)
            compare(removals.count, 0)
            keyClick(Qt.Key_C, Qt.ControlModifier)
            compare(copies.count, 1)
            verify(!findChild(canvas, "bpArrange").visible)
        }

        function test_problemShowsTheBanner() {
            const jumps = createTemporaryObject(spyComponent, root, { signalName: "jumpRequested" })
            canvas.graph = mock.problemGraph()
            const banner = findChild(canvas, "bpProblem")
            verify(banner.visible)
            compare(banner.title, mock.problemGraph().problem)
            mouseClick(findChild(banner, "action"))
            compare(jumps.count, 1)
            compare(jumps.signalArguments[0][0], "")
        }

        function test_debuggingFadesWhatWasNotReachedAndShowsValues() {
            canvas.reached = ["every", "find"]
            canvas.debugging = true
            canvas.pausedNode = "find"
            canvas.pinValues = { find: { found: "是", pos: "(1712, 880)" } }
            compare(findChild(canvas, "bpNode_find").opacity, 1)
            compare(findChild(canvas, "bpNode_fTap").opacity, Theme.fadedOpacity)
            verify(findChild(canvas, "bpPaused_find").visible)
            const bubble = findChild(canvas, "bpBubble")
            verify(bubble.visible)
            verify(findChild(canvas, "bpValue_find_pos").visible)
            canvas.pulse("find", "w12")
            verify(canvas.animating || Theme.reducedMotion)
            tryVerify(() => !canvas.animating, 5000)
            canvas.debugging = false
            canvas.pausedNode = ""
            canvas.pinValues = {}
            canvas.reached = []
            compare(findChild(canvas, "bpNode_fTap").opacity, 1)
        }

        function test_keysAskForTheirCommands() {
            const fTap = findChild(canvas, "bpNode_fTap")
            mouseClick(fTap, fTap.width / 2, Theme.size.bpHeader / 2)
            const duplicates = createTemporaryObject(spyComponent, root, { signalName: "duplicateRequested" })
            const comments = createTemporaryObject(spyComponent, root, { signalName: "commentRequested" })
            const breakpoints = createTemporaryObject(spyComponent, root, { signalName: "breakpointToggled" })
            const pastes = createTemporaryObject(spyComponent, root, { signalName: "pasteRequested" })
            keyClick(Qt.Key_D, Qt.ControlModifier)
            keyClick(Qt.Key_C)
            keyClick(Qt.Key_F9)
            keyClick(Qt.Key_V, Qt.ControlModifier)
            compare(duplicates.count, 1)
            compare(duplicates.signalArguments[0][0], ["fTap"])
            compare(comments.count, 1)
            compare(breakpoints.count, 1)
            compare(breakpoints.signalArguments[0][0], "fTap")
            compare(pastes.count, 1)
        }

        function test_eachPageBringsBackItsOwnView() {
            canvas.graph = mock.eventGraph()
            canvas.viewState = { x: 100, y: 50, zoom: 1.5 }
            compare(canvas.zoom, 1.5)
            let tap = findChild(canvas, "bpNode_fTap")
            let at = tap.mapToItem(canvas, 0, 0)
            fuzzyCompare(at.x, -90, 1)
            fuzzyCompare(at.y, 15, 1)

            canvas.graph = mock.controlGraph()
            canvas.viewState = { x: 0, y: 0, zoom: 1 }
            compare(canvas.zoom, 1)
            tryVerify(() => findChild(canvas, "bpNode_cfDown") !== null)
            const down = findChild(canvas, "bpNode_cfDown")
            verify(down !== null)
            compare(down.x, 40)
            compare(down.y, 60)

            canvas.graph = mock.eventGraph()
            canvas.viewState = { x: 100, y: 50, zoom: 1.5 }
            compare(canvas.zoom, 1.5)
            tryVerify(() => findChild(canvas, "bpNode_fTap") !== null)
            tap = findChild(canvas, "bpNode_fTap")
            at = tap.mapToItem(canvas, 0, 0)
            fuzzyCompare(at.x, -90, 1)
            fuzzyCompare(at.y, 15, 1)
        }

        function test_draggingAFloatingNodeOntoAWireInsertsIt() {
            const wire = findChild(canvas, "bpWire_w1")
            const point = Qt.point((wire.curve.x0 + wire.curve.x1) / 2, (wire.curve.y0 + wire.curve.y1) / 2)
            const flash = findChild(canvas, "bpNode_flash")
            const from = flash.mapToItem(canvas, flash.width / 2, Theme.size.bpHeader / 2)
            press(from)
            glide(from, point)
            const highlight = findChild(canvas, "bpInsertWire")
            tryVerify(() => highlight.visible)
            mouseRelease(canvas, point.x, point.y, Qt.LeftButton, Qt.NoModifier)
            compare(inserts.count, 1)
            compare(argsOf(inserts, 0), ["w1", "flash"])
            compare(moves.count, 0)
            verify(!highlight.visible)
            tryVerify(() => mock.nodeOf("flash").floating === false)
            verify(mock.graph.wires.some(entry => entry.from === "flash" && entry.to === "pick"))
        }

        function test_theWireMenuInsertsABlock() {
            const wire = findChild(canvas, "bpWire_w1")
            const point = Qt.point((wire.curve.x0 + wire.curve.x1) / 2, (wire.curve.y0 + wire.curve.y1) / 2)
            mouseClick(canvas, point.x, point.y, Qt.RightButton)
            const menu = findChild(canvas, "bpWireMenu")
            tryVerify(() => menu.opened)
            const item = findChild(menu.contentItem, "bpMenuInsert") ?? findChild(canvas, "bpMenuInsert")
            verify(item !== null && item.visible)
            mouseClick(item)
            tryVerify(() => canvas.picker.opened)
            compare(canvas.picker.fromNode, "fTap")
            compare(canvas.picker.fromPin, "exec")
            const row = findChild(canvas.picker.contentItem, "bpPickerItem_wait")
            verify(row !== null)
            mouseClick(row)
            compare(additions.count, 1)
            compare(additions.signalArguments[0][0], "wait")
            compare(additions.signalArguments[0][3], "fTap")
            compare(additions.signalArguments[0][4], "exec")
        }

        function test_aBlockWhoseComponentWasDeletedOffersReplaceAndDelete() {
            const warned = mock.block("gone", "action", "action", "pointer", "按住 · 按住", 40, 60,
                                      [mock.exec("exec", "")], [mock.exec("exec", "")],
                                      { problem: "Cannot find the component “Hold 6”: it was deleted",
                                        fix: "missing", fixControl: "hold6" })
            canvas.graph = { key: "", kind: "events", title: "", readOnly: false, nodes: [warned], wires: [],
                             comments: [], problem: "", problemLine: 0 }
            const replaces = createTemporaryObject(spyComponent, root, { signalName: "fixReplaceRequested" })
            const deletes = createTemporaryObject(spyComponent, root, { signalName: "fixDeleteRequested" })
            const replace = findChild(canvas, "bpFixReplace_gone")
            const remove = findChild(canvas, "bpFixDelete_gone")
            verify(replace !== null, "replace button")
            verify(remove !== null, "delete button")
            verify(replace.visible)
            verify(remove.visible)
            compare(findChild(canvas, "bpNode_gone").Accessible.description,
                    "Cannot find the component “Hold 6”: it was deleted")
            mouseClick(replace)
            compare(replaces.count, 1)
            compare(replaces.signalArguments[0][0], "gone")
            compare(replaces.signalArguments[0][1], "hold6")
            mouseClick(remove)
            compare(deletes.count, 1)
            compare(deletes.signalArguments[0][0], "gone")
        }

        Component {
            id: spyComponent

            SignalSpy {
                target: canvas
            }
        }
    }
}
