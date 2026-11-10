pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Icons
import GameScrcpy.Controls

import "Geometry.js" as Geometry

FocusScope {
    id: canvas

    property var graph: ({})
    property var selection: []
    property var check: null
    property var catalog: null
    property bool debugging: false
    property string pausedNode: ""
    property var pinValues: ({})
    property var reached: []
    property var viewState: ({ x: 0, y: 0, zoom: 1 })
    property bool snap: false
    property bool minimap: false
    property var keyName: null

    readonly property real minimumZoom: 0.25
    readonly property real maximumZoom: 2
    readonly property bool readOnly: graph.readOnly === true
    readonly property bool machine: internal.machinePage
    readonly property string problem: graph.problem ?? ""
    readonly property real zoom: view.zoom
    readonly property string selectedWire: internal.selectedWire
    readonly property bool animating: viewMotion.running || internal.flowing()
    readonly property alias picker: picker

    signal moved(var keys, real dx, real dy)
    signal connectRequested(string from, string fromPin, string to, string toPin)
    signal rewireRequested(string node, string pin, string toNode, string toPin)
    signal disconnectRequested(string node, string pin)
    signal wireRemoved(string wire)
    signal valueEdited(string node, string pin, var value)
    signal pickRequested(string node, string pin, string kind)
    signal addChosen(string id, real x, real y, string fromNode, string fromPin)
    signal dropped(var item, real x, real y, string node, string pin)
    signal insertRequested(string wire, string node)
    signal promoteRequested(string node, string pin)
    signal removeRequested(var keys)
    signal duplicateRequested(var keys)
    signal copyRequested(var keys)
    signal pasteRequested(real x, real y)
    signal codeEdited(string node, string text)
    signal codeDocumentReady(string node, var textDocument)
    signal collapseRequested(var keys)
    signal commentRequested(var keys)
    signal commentEdited(string key, var changes)
    signal rerouteRequested(string wire, real x, real y)
    signal arrangeRequested()
    signal opened(string node)
    signal jumpRequested(string node)
    signal breakpointToggled(string node)
    signal breakpointEnabled(string node, bool on)
    signal measured(var sizes)
    signal viewEdited(real x, real y, real zoom)
    signal selectionEdited(var keys)
    signal stateAdded(real x, real y)
    signal transitionRequested(string from, string to)
    signal transitionLabelEdited(string wire, string text)
    signal stateRenamed(string node, string name)
    signal startStateSet(string node)
    signal fixReplaceRequested(string node, string control)
    signal fixDeleteRequested(string node)

    function pulse(node: string, wire: string) {
        const item = internal.itemOf(node, internal.generation)
        if (item)
            item.glow()
        if (wire === "" || Theme.reducedMotion)
            return
        const link = internal.wireItems[wire]
        if (!link || !link.visible)
            return
        const flow = pulses.itemAt(internal.nextFlow) as WirePulse
        internal.nextFlow = (internal.nextFlow + 1) % pulses.count
        if (flow)
            flow.play(link.curve)
    }

    function frame(keys: var) {
        const rects = internal.rectsOf(keys ?? [])
        if (rects.length === 0) {
            fit()
            return
        }
        internal.animateTo(Geometry.union(rects))
    }

    function fit() {
        internal.animateTo(Geometry.union(internal.rectsOf(Object.keys(internal.nodeItems)
                                                            .concat(Object.keys(internal.commentItems)))))
    }

    onGraphChanged: internal.sync()
    onViewStateChanged: internal.adoptView()
    Component.onCompleted: {
        internal.adoptView()
        internal.sync()
    }

    activeFocusOnTab: true
    onActiveFocusChanged: {
        if (!activeFocus)
            internal.space = false
    }
    Keys.onShortcutOverride: event => event.accepted = internal.owns(event)
    Keys.onPressed: event => internal.press(event)
    Keys.onReleased: event => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            internal.space = false
            event.accepted = true
        }
    }

    Accessible.role: Accessible.Canvas
    Accessible.name: graph.title ?? ""
    objectName: "blueprintCanvas"

    QtObject {
        id: view

        property real x: 0
        property real y: 0
        property real zoom: 1
    }

    ListModel {
        id: nodeRows
    }

    ListModel {
        id: wireRows
    }

    ListModel {
        id: commentRows
    }

    QtObject {
        id: internal

        property string page: "\u0001"
        property bool machinePage: false
        property var nodeData: ({})
        property var wireData: ({})
        property var commentData: ({})
        property var nodeItems: ({})
        property var wireItems: ({})
        property var commentItems: ({})
        property int generation: 0
        property string gesture: ""
        property point start: Qt.point(0, 0)
        property point startView: Qt.point(0, 0)
        property bool travelled: false
        property var moveKeys: []
        property var movingSet: ({})
        property real moveX: 0
        property real moveY: 0
        property string movePrimary: ""
        property point moveOrigin: Qt.point(0, 0)
        property var rightTarget: null
        property bool marqueeAdds: false
        property rect marquee: Qt.rect(0, 0, 0, 0)
        property var link: null
        property var targets: null
        property var hover: null
        property string reason: ""
        property string selectedWire: ""
        property bool space: false
        property var pendingSizes: ({})
        property var reportedSizes: ({})
        property bool echo: false
        property int nextFlow: 0
        property point menuAt: Qt.point(0, 0)
        property var hint: null
        property var insertHint: null
        property string insertNode: ""
        property string wireTarget: ""
        property real now: 0
        property bool ticking: false
        property var lanes: ({})
        property string stateTarget: ""
        readonly property var selectedSet: {
            const set = {}
            for (const key of canvas.selection ?? [])
                set[key] = true
            return set
        }
        readonly property var reachedSet: {
            const set = {}
            for (const key of canvas.reached ?? [])
                set[key] = true
            return set
        }
        readonly property var linkCurve: {
            if (link === null)
                return Geometry.wire(0, 0, 0, 0, Theme.size.bpWireBend)
            if (link.straight === true)
                return Geometry.straight(link.x, link.y, link.endX, link.endY)
            if (link.output)
                return Geometry.wire(link.x, link.y, link.endX, link.endY, Theme.size.bpWireBend)
            return Geometry.wire(link.endX, link.endY, link.x, link.y, Theme.size.bpWireBend)
        }

        function flowing(): bool {
            for (let index = 0; index < pulses.count; ++index) {
                const flow = pulses.itemAt(index) as WirePulse
                if (flow && flow.running)
                    return true
            }
            return false
        }

        function itemOf(key: string, generation: int): var {
            return generation >= 0 ? (nodeItems[key] ?? null) : null
        }

        function toCanvas(point: point): point {
            return Qt.point(view.x + point.x / view.zoom, view.y + point.y / view.zoom)
        }

        function fromScene(scene: point): point {
            return viewport.mapFromItem(null, scene.x, scene.y)
        }

        function mapOf(list: var): var {
            const map = {}
            for (const entry of list ?? [])
                map[entry.key] = entry
            return map
        }

        function laneMap(list: var): var {
            const groups = {}
            for (const wire of list ?? []) {
                const from = wire.from ?? ""
                const to = wire.to ?? ""
                if (from === to)
                    continue
                const pair = from < to ? from + "\u0001" + to : to + "\u0001" + from
                if (groups[pair] === undefined)
                    groups[pair] = []
                groups[pair].push(wire)
            }
            const result = {}
            for (const pair of Object.keys(groups)) {
                const group = groups[pair]
                if (group.length < 2)
                    continue
                for (let index = 0; index < group.length; ++index) {
                    const wire = group[index]
                    const offset = 2 * index - (group.length - 1)
                    result[wire.key] = (wire.from ?? "") < (wire.to ?? "") ? offset : -offset
                }
            }
            return result
        }

        function reconcile(rows: ListModel, info: var, items: var, order: var) {
            for (let index = rows.count - 1; index >= 0; --index) {
                if (info[rows.get(index).key] === undefined)
                    rows.remove(index)
            }
            const present = {}
            for (let index = 0; index < rows.count; ++index)
                present[rows.get(index).key] = true
            for (const entry of order ?? []) {
                if (present[entry.key] === true) {
                    const item = items[entry.key]
                    if (item)
                        item.apply(entry)
                } else {
                    present[entry.key] = true
                    rows.append({ key: entry.key })
                }
            }
        }

        function sync() {
            const page = (canvas.graph.key ?? "") + "\u0001" + (canvas.graph.kind ?? "")
            if (page !== internal.page) {
                internal.page = page
                cancel()
                selectedWire = ""
                hint = null
                picker.close()
                choiceMenu.close()
                nodeRows.clear()
                wireRows.clear()
                commentRows.clear()
                reportedSizes = {}
                machinePage = (canvas.graph.kind ?? "") === "machine"
            }
            nodeData = mapOf(canvas.graph.nodes)
            wireData = mapOf(canvas.graph.wires)
            commentData = mapOf(canvas.graph.comments)
            if (machinePage) {
                lanes = laneMap(canvas.graph.wires)
                ticking = (canvas.graph.nodes ?? []).some(node => node.active === true && (node.activeSince ?? 0) > 0)
            } else {
                lanes = {}
                ticking = false
            }
            reconcile(nodeRows, nodeData, nodeItems, canvas.graph.nodes)
            reconcile(wireRows, wireData, wireItems, canvas.graph.wires)
            reconcile(commentRows, commentData, commentItems, canvas.graph.comments)
            if (selectedWire !== "" && wireData[selectedWire] === undefined)
                selectedWire = ""
            generation++
        }

        function register(items: var, item: var, info: var) {
            items[item.key] = item
            const entry = info[item.key]
            if (entry)
                item.apply(entry)
            Qt.callLater(internal.bump)
        }

        function unregister(items: var, item: var) {
            if (items[item.key] === item)
                delete items[item.key]
            Qt.callLater(internal.bump)
        }

        function bump() {
            generation++
        }

        function noteSize(key: string, width: real, height: real) {
            pendingSizes[key] = [Math.round(width), Math.round(height)]
            Qt.callLater(internal.flushSizes)
        }

        function flushSizes() {
            const changed = {}
            let any = false
            for (const key of Object.keys(pendingSizes)) {
                const size = pendingSizes[key]
                const last = reportedSizes[key]
                if (nodeItems[key] && (!last || last[0] !== size[0] || last[1] !== size[1])) {
                    changed[key] = size
                    reportedSizes[key] = size
                    any = true
                }
            }
            pendingSizes = {}
            if (any)
                canvas.measured(changed)
        }

        function adoptView() {
            if (echo)
                return
            const state = canvas.viewState ?? {}
            viewMotion.stop()
            view.zoom = Math.max(canvas.minimumZoom, Math.min(canvas.maximumZoom, state.zoom ?? 1))
            view.x = state.x ?? 0
            view.y = state.y ?? 0
        }

        function commitView() {
            echo = true
            canvas.viewState = { x: view.x, y: view.y, zoom: view.zoom }
            echo = false
            canvas.viewEdited(view.x, view.y, view.zoom)
        }

        function zoomAt(zoom: real, point: point) {
            const target = Math.max(canvas.minimumZoom, Math.min(canvas.maximumZoom, zoom))
            if (target === view.zoom)
                return
            viewMotion.stop()
            const at = toCanvas(point)
            view.zoom = target
            view.x = at.x - point.x / target
            view.y = at.y - point.y / target
            Qt.callLater(internal.commitView)
        }

        function center(): point {
            return Qt.point(viewport.width / 2, viewport.height / 2)
        }

        function panBy(dx: real, dy: real) {
            viewMotion.stop()
            view.x -= dx / view.zoom
            view.y -= dy / view.zoom
            Qt.callLater(internal.commitView)
        }

        function wheel(event: var) {
            const point = wheelHandler.point.position
            const pixel = event.pixelDelta
            const angle = event.angleDelta
            if (event.modifiers & Qt.ControlModifier) {
                const amount = angle.y !== 0 ? angle.y : pixel.y
                zoomAt(view.zoom * Math.pow(1.1, amount / 120), point)
            } else if (pixel.x !== 0 || pixel.y !== 0) {
                panBy(pixel.x, pixel.y)
            } else if (angle.x !== 0 || (event.device && event.device.type === PointerDevice.TouchPad)) {
                panBy(angle.x / 4, angle.y / 4)
            } else if (angle.y !== 0) {
                zoomAt(view.zoom * Math.pow(1.1, angle.y / 120), point)
            }
        }

        function animateTo(rect: var) {
            if (viewport.width <= 0 || viewport.height <= 0)
                return
            const margin = Theme.space.x5l
            let zoom = 1
            let x = -margin
            let y = -margin
            if (rect) {
                const width = Math.max(1, rect.width)
                const height = Math.max(1, rect.height)
                zoom = Math.max(canvas.minimumZoom, Math.min(1, (viewport.width - 2 * margin) / width,
                                                             (viewport.height - 2 * margin) / height))
                x = rect.x + rect.width / 2 - viewport.width / (2 * zoom)
                y = rect.y + rect.height / 2 - viewport.height / (2 * zoom)
            }
            viewMotion.stop()
            glideX.to = x
            glideY.to = y
            glideZoom.to = zoom
            viewMotion.start()
        }

        function rectsOf(keys: var): var {
            const rects = []
            for (const key of keys) {
                const item = nodeItems[key] ?? commentItems[key]
                if (item && item.visible)
                    rects.push({ x: item.x, y: item.y, width: item.width, height: item.height })
            }
            return rects
        }

        function setSelection(keys: var) {
            const current = canvas.selection ?? []
            if (keys.length === current.length && keys.every((key, index) => current[index] === key))
                return
            canvas.selection = keys
            canvas.selectionEdited(keys)
        }

        function selectOnly(key: string) {
            selectedWire = ""
            setSelection([key])
        }

        function toggle(key: string) {
            selectedWire = ""
            const current = canvas.selection ?? []
            setSelection(current.indexOf(key) >= 0 ? current.filter(entry => entry !== key) : current.concat([key]))
        }

        function selectWire(key: string) {
            selectedWire = key
            setSelection([])
        }

        function selectedNodes(): var {
            return (canvas.selection ?? []).filter(key => nodeData[key] !== undefined)
        }

        function nodeAt(at: point, skip: string): string {
            let found = ""
            for (const key of Object.keys(nodeItems)) {
                if (key === skip)
                    continue
                const item = nodeItems[key]
                if (!item.visible)
                    continue
                const margin = item.reroute ? Theme.space.xs : 0
                if (at.x >= item.x - margin && at.x <= item.x + item.width + margin && at.y >= item.y - margin
                        && at.y <= item.y + item.height + margin) {
                    found = key
                    if (item.selected)
                        return key
                }
            }
            return found
        }

        function wireAt(at: point): string {
            const tolerance = Theme.space.sm / view.zoom
            let best = ""
            let nearest = tolerance
            for (const key of Object.keys(wireItems)) {
                const item = wireItems[key]
                if (!item.visible)
                    continue
                const distance = Geometry.distanceToCubic(at.x, at.y, item.curve, 24)
                if (distance <= nearest) {
                    nearest = distance
                    best = key
                }
            }
            return best
        }

        function insertWire(at: point): string {
            if (insertNode === "")
                return ""
            const key = wireAt(at)
            if (key === "")
                return ""
            const wire = wireData[key]
            if (!wire || wire.type !== "exec")
                return ""
            if (wire.from === insertNode || wire.to === insertNode)
                return ""
            return key
        }

        function viewportOf(at: point): point {
            return Qt.point((at.x - view.x) * view.zoom, (at.y - view.y) * view.zoom)
        }

        function cancel() {
            if (gesture === "move") {
                movingSet = {}
                moveX = 0
                moveY = 0
            }
            gesture = ""
            link = null
            targets = null
            hover = null
            reason = ""
            stateTarget = ""
            wireTarget = ""
            insertNode = ""
            insertHint = null
        }

        function beginPan(point: point) {
            gesture = "pan"
            start = point
            startView = Qt.point(view.x, view.y)
            viewMotion.stop()
        }

        function beginRight(point: point, target: var) {
            gesture = "right"
            start = point
            startView = Qt.point(view.x, view.y)
            travelled = false
            rightTarget = target
            viewMotion.stop()
        }

        function beginMarquee(point: point, modifiers: int) {
            gesture = "marquee"
            start = point
            travelled = false
            marqueeAdds = (modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0
            marquee = Qt.rect(point.x, point.y, 0, 0)
        }

        function beginMove(point: point, primary: string) {
            insertNode = ""
            wireTarget = ""
            if (canvas.readOnly) {
                gesture = "idle"
                return
            }
            const keys = []
            const set = {}
            for (const key of canvas.selection ?? []) {
                if (nodeItems[key] || commentItems[key]) {
                    keys.push(key)
                    set[key] = true
                }
            }
            for (const key of keys) {
                const box = commentItems[key]
                if (!box)
                    continue
                const area = { x: box.x, y: box.y, width: box.width, height: box.height }
                for (const nodeKey of Object.keys(nodeItems)) {
                    const item = nodeItems[nodeKey]
                    if (set[nodeKey] !== true && Geometry.encloses(area, { x: item.x, y: item.y, width: item.width,
                                                                          height: item.height })) {
                        keys.push(nodeKey)
                        set[nodeKey] = true
                    }
                }
            }
            gesture = "move"
            start = point
            travelled = false
            moveKeys = keys
            movingSet = set
            moveX = 0
            moveY = 0
            movePrimary = primary
            moveOrigin = toCanvas(point)
            if (keys.length === 1 && !machinePage) {
                const info = nodeData[keys[0]]
                if (info && info.floating === true && (info.kind ?? "") !== "pure"
                        && (info.inputs ?? []).every(entry => entry.connected !== true))
                    insertNode = keys[0]
            }
        }

        function pinOf(node: string, pin: string, output: bool): var {
            const info = nodeData[node]
            if (!info)
                return null
            return ((output ? info.outputs : info.inputs) ?? []).find(entry => entry.name === pin) ?? null
        }

        function anchorOf(node: string, pin: string, output: bool): point {
            const item = nodeItems[node]
            if (!item)
                return Qt.point(0, 0)
            return Qt.point(item.x + (output ? item.outX : item.inX), item.y + item.anchorY(output ? "out" : "in", pin))
        }

        function verdict(from: string, fromPin: string, to: string, toPin: string): string {
            const ask = canvas.check
            const answer = typeof ask === "function" ? ask(from, fromPin, to, toPin) : ""
            return answer === undefined || answer === null || answer === "" ? "ok" : String(answer)
        }

        function judge(node: string, pin: string): string {
            if (link === null)
                return ""
            if (link.rewire) {
                if (node === link.node && pin === link.pin)
                    return ""
                for (const wire of Object.values(wireData)) {
                    if (link.output && wire.from === link.node && wire.fromPin === link.pin) {
                        const answer = verdict(node, pin, wire.to, wire.toPin)
                        if (answer !== "ok")
                            return answer
                    } else if (!link.output && wire.to === link.node && wire.toPin === link.pin) {
                        const answer = verdict(wire.from, wire.fromPin, node, pin)
                        if (answer !== "ok")
                            return answer
                    }
                }
                return "ok"
            }
            return link.output ? verdict(link.node, link.pin, node, pin) : verdict(node, pin, link.node, link.pin)
        }

        function targetsFor(node: string): var {
            const item = nodeItems[node]
            const map = {}
            if (!item || (node === link.node && !link.rewire))
                return map
            const side = link.rewire ? (link.output ? "out" : "in") : (link.output ? "in" : "out")
            const list = side === "in" ? item.info.inputs : item.info.outputs
            for (const pin of list ?? [])
                map[side + ":" + pin.name] = judge(node, pin.name)
            return map
        }

        function beginLink(node: string, pin: string, output: bool, rewire: bool, point: point) {
            insertNode = ""
            wireTarget = ""
            const info = pinOf(node, pin, output)
            const anchor = anchorOf(node, pin, output)
            const at = toCanvas(point)
            const label = (info?.label ?? "") !== "" ? info.label : (nodeData[node]?.title ?? "")
            gesture = rewire ? "rewire" : "connect"
            start = point
            travelled = false
            link = { node: node, pin: pin, output: output, rewire: rewire, type: info?.type ?? "", label: label,
                     connected: info?.connected === true, x: anchor.x, y: anchor.y, endX: at.x, endY: at.y }
            const visible = { x: view.x, y: view.y, width: viewport.width / view.zoom,
                              height: viewport.height / view.zoom }
            const all = {}
            for (const key of Object.keys(nodeItems)) {
                const item = nodeItems[key]
                if (Geometry.intersects(visible, { x: item.x, y: item.y, width: item.width, height: item.height }))
                    all[key] = targetsFor(key)
            }
            targets = all
            hover = null
            reason = ""
        }

        function targetAt(at: point): var {
            const key = nodeAt(at, link !== null && !link.rewire ? link.node : "")
            if (key === "")
                return null
            const item = nodeItems[key]
            const side = link.rewire ? (link.output ? "out" : "in") : (link.output ? "in" : "out")
            return { node: key, pin: item.pinAt(at.y - item.y, side), side: side }
        }

        function verdictAt(target: var): string {
            if (target === null || target.pin === "")
                return ""
            if (targets[target.node] === undefined) {
                const next = Object.assign({}, targets)
                next[target.node] = targetsFor(target.node)
                targets = next
            }
            return targets[target.node][target.side + ":" + target.pin] ?? ""
        }

        function drag(point: point) {
            if (gesture === "" || gesture === "idle")
                return
            const distance = Math.abs(point.x - start.x) + Math.abs(point.y - start.y)
            if (!travelled && distance >= Application.styleHints.startDragDistance)
                travelled = true
            switch (gesture) {
            case "pan":
                view.x = startView.x - (point.x - start.x) / view.zoom
                view.y = startView.y - (point.y - start.y) / view.zoom
                break
            case "right":
                if (travelled) {
                    view.x = startView.x - (point.x - start.x) / view.zoom
                    view.y = startView.y - (point.y - start.y) / view.zoom
                }
                break
            case "marquee":
                marquee = Qt.rect(Math.min(start.x, point.x), Math.min(start.y, point.y), Math.abs(point.x - start.x),
                                  Math.abs(point.y - start.y))
                break
            case "move": {
                if (!travelled)
                    return
                const at = toCanvas(point)
                let dx = at.x - moveOrigin.x
                let dy = at.y - moveOrigin.y
                const primary = nodeItems[movePrimary] ?? commentItems[movePrimary]
                if (canvas.snap && primary) {
                    const grid = Theme.size.bpSnap
                    const baseX = primary.x - moveX
                    const baseY = primary.y - moveY
                    dx = Math.round((baseX + dx) / grid) * grid - baseX
                    dy = Math.round((baseY + dy) / grid) * grid - baseY
                }
                moveX = dx
                moveY = dy
                wireTarget = internal.insertWire(at)
                break
            }
            case "connect":
            case "rewire": {
                const at = toCanvas(point)
                link = Object.assign({}, link, { endX: at.x, endY: at.y })
                const target = targetAt(at)
                const same = (hover === null && target === null) || (hover !== null && target !== null
                                                                      && hover.node === target.node
                                                                      && hover.pin === target.pin)
                if (!same) {
                    hover = target
                    const answer = verdictAt(target)
                    reason = answer !== "" && answer !== "ok" ? answer : ""
                }
                insertHint = travelled && target === null && link.output === true && link.rewire !== true
                             && link.type === "exec" && link.connected === true ? { at: point } : null
                break
            }
            case "transition": {
                const at = toCanvas(point)
                const source = nodeItems[link.node]
                const edge = source ? Geometry.borderPoint({ x: source.x, y: source.y, width: source.width,
                                                             height: source.height }, at.x, at.y)
                                    : { x: link.x, y: link.y }
                link = Object.assign({}, link, { x: edge.x, y: edge.y, endX: at.x, endY: at.y })
                stateTarget = travelled ? nodeAt(at, link.node) : ""
                break
            }
            }
        }

        function release(point: point) {
            const kind = gesture
            gesture = ""
            insertHint = null
            switch (kind) {
            case "pan":
                commitView()
                break
            case "right":
                if (travelled)
                    commitView()
                else
                    openMenu(point, rightTarget)
                break
            case "marquee":
                finishMarquee()
                break
            case "move": {
                const dx = moveX
                const dy = moveY
                const keys = moveKeys
                const target = wireTarget
                const node = insertNode
                movingSet = {}
                moveX = 0
                moveY = 0
                insertNode = ""
                wireTarget = ""
                if (node !== "" && target !== "") {
                    canvas.insertRequested(target, node)
                    break
                }
                if (travelled && (dx !== 0 || dy !== 0))
                    canvas.moved(keys, dx, dy)
                break
            }
            case "connect":
            case "rewire":
                finishLink(point, kind === "rewire")
                break
            case "transition":
                finishTransition(point)
                break
            }
        }

        function beginTransition(key: string, point: point) {
            const item = nodeItems[key]
            if (!item || canvas.readOnly) {
                gesture = "idle"
                return
            }
            const at = toCanvas(point)
            gesture = "transition"
            start = point
            travelled = false
            link = { node: key, pin: "", output: true, rewire: false, type: "transition", label: "", straight: true,
                     x: item.x + item.width / 2, y: item.y + item.height / 2, endX: at.x, endY: at.y }
            stateTarget = ""
        }

        function finishTransition(point: point) {
            const current = link
            const target = travelled && current !== null ? nodeAt(toCanvas(point), current.node) : ""
            link = null
            stateTarget = ""
            if (current !== null && target !== "")
                canvas.transitionRequested(current.node, target)
        }

        function ringPressed(key: string, button: int, scene: point) {
            if (button !== Qt.LeftButton) {
                nodePressed(key, button, Qt.NoModifier, scene)
                return
            }
            takeFocus(Qt.MouseFocusReason)
            if (space) {
                beginPan(fromScene(scene))
                return
            }
            if (selectedSet[key] !== true)
                selectOnly(key)
            beginTransition(key, fromScene(scene))
        }

        function transitionPressed(key: string, button: int, scene: point) {
            takeFocus(Qt.MouseFocusReason)
            const point = fromScene(scene)
            if (button === Qt.RightButton) {
                selectWire(key)
                beginRight(point, { kind: "wire", key: key })
                return
            }
            if (button === Qt.LeftButton) {
                selectWire(key)
                gesture = "idle"
            }
        }

        function addStateAt(point: point) {
            if (canvas.readOnly || !machinePage)
                return
            const at = toCanvas(point)
            canvas.stateAdded(Math.round(at.x - Theme.size.bpStateWidth / 2), Math.round(at.y - Theme.size.bpHeader))
        }

        function finishMarquee() {
            if (!travelled) {
                selectedWire = ""
                if (!marqueeAdds)
                    setSelection([])
                return
            }
            const corner = toCanvas(Qt.point(marquee.x, marquee.y))
            const area = { x: corner.x, y: corner.y, width: marquee.width / view.zoom, height: marquee.height / view.zoom }
            const keys = marqueeAdds ? (canvas.selection ?? []).slice() : []
            for (const key of Object.keys(nodeItems)) {
                const item = nodeItems[key]
                if (item.visible && keys.indexOf(key) < 0
                        && Geometry.intersects(area, { x: item.x, y: item.y, width: item.width, height: item.height }))
                    keys.push(key)
            }
            for (const key of Object.keys(commentItems)) {
                const item = commentItems[key]
                if (keys.indexOf(key) < 0
                        && Geometry.encloses(area, { x: item.x, y: item.y, width: item.width, height: item.height }))
                    keys.push(key)
            }
            selectedWire = ""
            setSelection(keys)
            marquee = Qt.rect(0, 0, 0, 0)
        }

        function finishLink(point: point, rewire: bool) {
            const current = link
            const at = toCanvas(point)
            const target = travelled ? targetAt(at) : null
            const answer = verdictAt(target)
            link = null
            targets = null
            hover = null
            reason = ""
            if (current === null || !travelled)
                return
            if (target !== null && target.pin !== "") {
                if (answer !== "ok")
                    return
                if (rewire)
                    canvas.rewireRequested(current.node, current.pin, target.node, target.pin)
                else if (current.output)
                    canvas.connectRequested(current.node, current.pin, target.node, target.pin)
                else
                    canvas.connectRequested(target.node, target.pin, current.node, current.pin)
                return
            }
            if (target === null && !rewire)
                openPicker(point, current.node, current.pin, current.label)
        }

        function openChoices(node: string, pin: string, scene: point) {
            const info = pinOf(node, pin, false)
            if (info === null || canvas.readOnly)
                return
            let widest = 0
            for (const option of info.choices ?? []) {
                choiceMeasure.text = option.text ?? ""
                widest = Math.max(widest, choiceMeasure.advanceWidth)
            }
            const at = canvas.mapFromItem(null, scene.x, scene.y)
            choiceMenu.node = node
            choiceMenu.pin = pin
            choiceMenu.raw = info.raw
            choiceMenu.choices = info.choices ?? []
            choiceMenu.width = Math.max(Theme.size.menuMinWidth, widest + Theme.size.iconSmall + 4 * Theme.space.sm
                                        + choiceMenu.leftPadding + choiceMenu.rightPadding)
            choiceMenu.x = at.x
            choiceMenu.y = at.y + Theme.space.xxs
            choiceMenu.open()
        }

        function openPicker(point: point, node: string, pin: string, label: string) {
            if (canvas.readOnly || machinePage)
                return
            const at = toCanvas(point)
            const spot = viewport.mapToItem(canvas, point.x, point.y)
            picker.show(spot.x, spot.y, at.x, at.y, node, pin, label)
        }

        function insertHere(wireKey: string): void {
            const wire = wireData[wireKey]
            if (!wire || canvas.readOnly || machinePage)
                return
            const info = nodeData[wire.from ?? ""]
            const pin = (info?.outputs ?? []).find(entry => entry.name === (wire.fromPin ?? ""))
            const label = (pin?.label ?? "") !== "" ? pin.label : (info?.title ?? "")
            openPicker(viewportOf(menuAt), wire.from ?? "", wire.fromPin ?? "", label)
        }

        function openMenu(point: point, target: var) {
            const spot = viewport.mapToItem(canvas, point.x, point.y)
            menuAt = toCanvas(point)
            if (target === null || target.kind === "canvas") {
                if (!machinePage) {
                    openPicker(point, "", "", "")
                } else if (!canvas.readOnly) {
                    machineMenu.at = point
                    machineMenu.popup(spot.x, spot.y)
                }
            } else if (target.kind === "node") {
                if (machinePage) {
                    stateMenu.target = target.key
                    stateMenu.popup(spot.x, spot.y)
                } else {
                    nodeMenu.target = target.key
                    nodeMenu.popup(spot.x, spot.y)
                }
            } else if (target.kind === "wire") {
                wireMenu.target = target.key
                wireMenu.popup(spot.x, spot.y)
            } else if (target.kind === "comment") {
                commentMenu.target = target.key
                commentMenu.popup(spot.x, spot.y)
            }
        }

        function nodePressed(key: string, button: int, modifiers: int, scene: point) {
            takeFocus(Qt.MouseFocusReason)
            const point = fromScene(scene)
            if (button === Qt.MiddleButton || (button === Qt.LeftButton && space)) {
                beginPan(point)
                return
            }
            if (button === Qt.RightButton) {
                if (selectedSet[key] !== true)
                    selectOnly(key)
                beginRight(point, { kind: "node", key: key })
                return
            }
            if (button !== Qt.LeftButton)
                return
            if (modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) {
                toggle(key)
                gesture = "idle"
                return
            }
            if (selectedSet[key] !== true)
                selectOnly(key)
            else
                selectedWire = ""
            beginMove(point, key)
        }

        function commentPressed(key: string, button: int, modifiers: int, scene: point) {
            nodePressed(key, button, modifiers, scene)
            if (gesture === "right")
                rightTarget = { kind: "comment", key: key }
        }

        function pinPressed(node: string, pin: string, output: bool, button: int, modifiers: int, scene: point) {
            takeFocus(Qt.MouseFocusReason)
            if (button === Qt.RightButton) {
                if (!output && !canvas.readOnly) {
                    const at = canvas.mapFromItem(null, scene.x, scene.y)
                    pinMenu.node = node
                    pinMenu.pin = pin
                    pinMenu.popup(at.x, at.y)
                }
                gesture = "idle"
                return
            }
            if (button !== Qt.LeftButton)
                return
            if (modifiers & Qt.AltModifier) {
                if (!canvas.readOnly)
                    canvas.disconnectRequested(node, pin)
                gesture = "idle"
                return
            }
            if (canvas.readOnly) {
                gesture = "idle"
                return
            }
            const info = pinOf(node, pin, output)
            beginLink(node, pin, output, (modifiers & Qt.ControlModifier) !== 0 && info !== null
                      && info.connected === true, fromScene(scene))
        }

        function backgroundPressed(point: point, button: int, modifiers: int) {
            takeFocus(Qt.MouseFocusReason)
            if (button === Qt.MiddleButton || (button === Qt.LeftButton && space)) {
                beginPan(point)
                return
            }
            const at = toCanvas(point)
            const wire = wireAt(at)
            if (button === Qt.RightButton) {
                if (wire !== "")
                    selectWire(wire)
                beginRight(point, wire !== "" ? { kind: "wire", key: wire } : { kind: "canvas", key: "" })
                return
            }
            if (button !== Qt.LeftButton)
                return
            if (wire !== "") {
                selectWire(wire)
                gesture = "idle"
                return
            }
            beginMarquee(point, modifiers)
        }

        function backgroundDoubleClicked(point: point, button: int) {
            if (button !== Qt.LeftButton || canvas.readOnly)
                return
            const at = toCanvas(point)
            const wire = wireAt(at)
            if (wire !== "") {
                if (!machinePage) {
                    canvas.rerouteRequested(wire, at.x - Theme.size.bpReroute / 2, at.y - Theme.size.bpReroute / 2)
                } else {
                    const arrow = wireItems[wire]
                    if (arrow)
                        arrow.rename()
                }
                return
            }
            if (machinePage)
                addStateAt(point)
            else
                openPicker(point, "", "", "")
        }

        function pasteSpot(): point {
            const point = hoverHandler.hovered ? hoverHandler.point.position : center()
            return toCanvas(point)
        }

        function typing(): bool {
            const focus = canvas.Window.activeFocusItem
            return focus instanceof TextInput || focus instanceof TextEdit
        }

        function takeFocus(reason: int) {
            const chain = []
            let item = canvas.Window.activeFocusItem
            while (item !== null && item !== canvas) {
                chain.push(item)
                item = item.parent
            }
            if (item === canvas) {
                for (const entry of chain)
                    entry.focus = false
            }
            canvas.forceActiveFocus(reason)
        }

        function editingInside(): bool {
            let item = canvas.Window.activeFocusItem
            if (!(item instanceof TextInput || item instanceof TextEdit))
                return false
            while (item !== null && item !== canvas)
                item = item.parent
            return item === canvas
        }

        function restoreFocus() {
            if (!editingInside())
                takeFocus(Qt.PopupFocusReason)
        }

        function command(event: var): string {
            const control = (event.modifiers & Qt.ControlModifier) !== 0
            const plain = (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
            switch (event.key) {
            case Qt.Key_Delete:
            case Qt.Key_Backspace:
                return plain ? "remove" : ""
            case Qt.Key_C:
                return control ? "copy" : plain && (event.modifiers & Qt.ShiftModifier) === 0 ? "comment" : ""
            case Qt.Key_V:
                return control ? "paste" : ""
            case Qt.Key_D:
                return control ? "duplicate" : ""
            case Qt.Key_A:
                return control ? "all" : ""
            case Qt.Key_F9:
                return plain ? "breakpoint" : ""
            case Qt.Key_F:
                return plain ? "frame" : ""
            case Qt.Key_Home:
                return plain ? "fit" : ""
            case Qt.Key_Escape:
                return gesture !== "" || selectedWire !== "" || (canvas.selection ?? []).length > 0 ? "escape" : ""
            case Qt.Key_Space:
                return plain ? "space" : ""
            default:
                return ""
            }
        }

        function owns(event: var): bool {
            return !typing() && command(event) !== ""
        }

        function press(event: var) {
            if (typing()) {
                if (event.key === Qt.Key_Escape) {
                    takeFocus(Qt.OtherFocusReason)
                    event.accepted = true
                }
                return
            }
            const action = command(event)
            if (action === "")
                return
            event.accepted = true
            const keys = (canvas.selection ?? []).slice()
            switch (action) {
            case "space":
                if (!event.isAutoRepeat)
                    space = true
                break
            case "remove":
                if (canvas.readOnly)
                    break
                if (selectedWire !== "") {
                    const wire = selectedWire
                    selectedWire = ""
                    canvas.wireRemoved(wire)
                } else if (keys.length > 0) {
                    canvas.removeRequested(keys)
                }
                break
            case "copy":
                if (keys.length > 0)
                    canvas.copyRequested(keys)
                break
            case "paste":
                if (!canvas.readOnly) {
                    const spot = pasteSpot()
                    canvas.pasteRequested(spot.x, spot.y)
                }
                break
            case "duplicate":
                if (!canvas.readOnly && keys.length > 0)
                    canvas.duplicateRequested(keys)
                break
            case "all":
                selectedWire = ""
                setSelection(Object.keys(nodeItems).concat(Object.keys(commentItems)))
                break
            case "breakpoint":
                if (!canvas.readOnly && !machinePage) {
                    for (const key of selectedNodes()) {
                        const kind = nodeData[key].kind ?? ""
                        if (kind !== "reroute" && kind !== "pure")
                            canvas.breakpointToggled(key)
                    }
                }
                break
            case "frame":
                canvas.frame(keys)
                break
            case "fit":
                canvas.fit()
                break
            case "comment":
                if (!canvas.readOnly && !machinePage)
                    canvas.commentRequested(keys)
                break
            case "escape":
                if (gesture !== "") {
                    cancel()
                } else {
                    selectedWire = ""
                    setSelection([])
                }
                break
            }
        }
    }

    Timer {
        interval: Theme.motion.tick
        repeat: true
        running: internal.ticking
        triggeredOnStart: true
        onTriggered: internal.now = Date.now()
    }

    ParallelAnimation {
        id: viewMotion

        onFinished: internal.commitView()

        NumberAnimation {
            id: glideX

            target: view
            property: "x"
            duration: Theme.motion.medium
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.emphasized
        }
        NumberAnimation {
            id: glideY

            target: view
            property: "y"
            duration: Theme.motion.medium
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.emphasized
        }
        NumberAnimation {
            id: glideZoom

            target: view
            property: "zoom"
            duration: Theme.motion.medium
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.emphasized
        }
    }

    Item {
        id: viewport

        anchors.fill: parent
        clip: true

        Rectangle {
            anchors.fill: parent
            color: Theme.colors.bpCanvas
        }

        GridDots {
            anchors.fill: parent
            viewX: view.x
            viewY: view.y
            zoom: view.zoom
        }

        HoverHandler {
            id: hoverHandler
        }

        WheelHandler {
            id: wheelHandler

            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: event => internal.wheel(event)
        }

        MouseArea {
            id: background

            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            preventStealing: true
            cursorShape: internal.gesture === "pan" || (internal.gesture === "right" && internal.travelled)
                         ? Qt.ClosedHandCursor : internal.space ? Qt.OpenHandCursor : Qt.ArrowCursor
            onPressed: mouse => internal.backgroundPressed(Qt.point(mouse.x, mouse.y), mouse.button, mouse.modifiers)
            onPositionChanged: mouse => internal.drag(Qt.point(mouse.x, mouse.y))
            onReleased: mouse => internal.release(Qt.point(mouse.x, mouse.y))
            onDoubleClicked: mouse => internal.backgroundDoubleClicked(Qt.point(mouse.x, mouse.y), mouse.button)
            onCanceled: internal.cancel()
        }

        Item {
            id: content

            objectName: "bpContent"
            x: -view.x * view.zoom
            y: -view.y * view.zoom
            scale: view.zoom
            transformOrigin: Item.TopLeft
            visible: canvas.problem === ""

            Repeater {
                model: commentRows

                delegate: CommentBox {
                    id: note

                    readOnly: canvas.readOnly
                    selected: internal.selectedSet[note.key] === true
                    moveX: internal.movingSet[note.key] === true ? internal.moveX : 0
                    moveY: internal.movingSet[note.key] === true ? internal.moveY : 0
                    Component.onCompleted: internal.register(internal.commentItems, note, internal.commentData)
                    Component.onDestruction: internal.unregister(internal.commentItems, note)
                    onTitlePressed: (button, modifiers, scene) => internal.commentPressed(note.key, button, modifiers,
                                                                                          scene)
                    onTitleMoved: scene => internal.drag(internal.fromScene(scene))
                    onTitleReleased: scene => internal.release(internal.fromScene(scene))
                    onRenamed: title => canvas.commentEdited(note.key, { title: title })
                    onResized: (width, height) => canvas.commentEdited(note.key, { width: width, height: height })
                    onRenameFinished: internal.takeFocus(Qt.OtherFocusReason)
                }
            }

            Repeater {
                model: internal.machinePage ? null : wireRows

                delegate: BlueprintWire {
                    id: line

                    readonly property var from: internal.itemOf(line.info.from ?? "", internal.generation)
                    readonly property var to: internal.itemOf(line.info.to ?? "", internal.generation)

                    visible: line.from !== null && line.to !== null
                    start: line.from !== null ? Qt.point(line.from.x + line.from.outX,
                                                         line.from.y + line.from.anchorY("out", line.info.fromPin ?? ""))
                                              : Qt.point(0, 0)
                    end: line.to !== null ? Qt.point(line.to.x + line.to.inX,
                                                     line.to.y + line.to.anchorY("in", line.info.toPin ?? ""))
                                          : Qt.point(0, 0)
                    selected: internal.selectedWire === line.key
                    faded: canvas.debugging && !(internal.reachedSet[line.info.from ?? ""] === true
                                                 && internal.reachedSet[line.info.to ?? ""] === true)
                    Component.onCompleted: internal.register(internal.wireItems, line, internal.wireData)
                    Component.onDestruction: internal.unregister(internal.wireItems, line)
                }
            }

            Shape {
                id: insertWireHint

                readonly property var wireItem: internal.wireTarget !== "" ? internal.wireItems[internal.wireTarget] ?? null
                                                                           : null
                readonly property var curve: insertWireHint.wireItem !== null ? insertWireHint.wireItem.curve : null

                objectName: "bpInsertWire"
                visible: insertWireHint.curve !== null
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: Theme.colors.accent
                    strokeWidth: Theme.size.bpWireSelected + Theme.size.stroke
                    strokeStyle: ShapePath.SolidLine
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    startX: insertWireHint.curve !== null ? insertWireHint.curve.x0 : 0
                    startY: insertWireHint.curve !== null ? insertWireHint.curve.y0 : 0

                    PathCubic {
                        control1X: insertWireHint.curve !== null ? insertWireHint.curve.c0x : 0
                        control1Y: insertWireHint.curve !== null ? insertWireHint.curve.c0y : 0
                        control2X: insertWireHint.curve !== null ? insertWireHint.curve.c1x : 0
                        control2Y: insertWireHint.curve !== null ? insertWireHint.curve.c1y : 0
                        x: insertWireHint.curve !== null ? insertWireHint.curve.x1 : 0
                        y: insertWireHint.curve !== null ? insertWireHint.curve.y1 : 0
                    }
                }
            }

            Repeater {
                model: internal.machinePage ? wireRows : null

                delegate: TransitionWire {
                    id: arrow

                    readonly property var from: internal.itemOf(arrow.info.from ?? "", internal.generation)
                    readonly property var to: internal.itemOf(arrow.info.to ?? "", internal.generation)

                    visible: arrow.from !== null && arrow.to !== null
                    fromRect: arrow.from !== null ? Qt.rect(arrow.from.x, arrow.from.y, arrow.from.width,
                                                            arrow.from.height)
                                                  : Qt.rect(0, 0, 0, 0)
                    toRect: arrow.to !== null ? Qt.rect(arrow.to.x, arrow.to.y, arrow.to.width, arrow.to.height)
                                              : Qt.rect(0, 0, 0, 0)
                    lane: internal.lanes[arrow.key] ?? 0
                    selected: internal.selectedWire === arrow.key
                    readOnly: canvas.readOnly
                    Component.onCompleted: internal.register(internal.wireItems, arrow, internal.wireData)
                    Component.onDestruction: internal.unregister(internal.wireItems, arrow)
                    onLabelPressed: (button, modifiers, scene) => internal.transitionPressed(arrow.key, button, scene)
                    onLabelMoved: scene => internal.drag(internal.fromScene(scene))
                    onLabelReleased: scene => internal.release(internal.fromScene(scene))
                    onRenamed: text => canvas.transitionLabelEdited(arrow.key, text)
                    onRenameFinished: internal.takeFocus(Qt.OtherFocusReason)
                }
            }

            Repeater {
                id: pulses

                model: 12

                delegate: WirePulse {}
            }

            Repeater {
                model: internal.machinePage ? null : nodeRows

                delegate: BlueprintNode {
                    id: block

                    readOnly: canvas.readOnly
                    selected: internal.selectedSet[block.key] === true
                    moveX: internal.movingSet[block.key] === true ? internal.moveX : 0
                    moveY: internal.movingSet[block.key] === true ? internal.moveY : 0
                    faded: canvas.debugging && internal.reachedSet[block.key] !== true
                    paused: canvas.pausedNode !== "" && canvas.pausedNode === block.key
                    values: canvas.pinValues ? canvas.pinValues[block.key] ?? null : null
                    targets: internal.targets === null ? null : internal.targets[block.key] ?? ({})
                    keyName: canvas.keyName
                    Component.onCompleted: {
                        internal.register(internal.nodeItems, block, internal.nodeData)
                        internal.noteSize(block.key, block.width, block.height)
                    }
                    Component.onDestruction: internal.unregister(internal.nodeItems, block)
                    onWidthChanged: internal.noteSize(block.key, block.width, block.height)
                    onHeightChanged: internal.noteSize(block.key, block.width, block.height)
                    onBodyPressed: (button, modifiers, scene) => internal.nodePressed(block.key, button, modifiers,
                                                                                      scene)
                    onBodyMoved: scene => internal.drag(internal.fromScene(scene))
                    onBodyReleased: scene => internal.release(internal.fromScene(scene))
                    onBodyDoubleClicked: button => {
                        if (button === Qt.LeftButton && !block.reroute)
                            canvas.opened(block.key)
                    }
                    onPinPressed: (pin, output, button, modifiers, scene) => internal.pinPressed(block.key, pin, output,
                                                                                                 button, modifiers, scene)
                    onPinMoved: scene => internal.drag(internal.fromScene(scene))
                    onPinReleased: scene => internal.release(internal.fromScene(scene))
                    onValueEdited: (pin, value) => canvas.valueEdited(block.key, pin, value)
                    onPickRequested: (pin, kind) => canvas.pickRequested(block.key, pin, kind)
                    onEditFinished: internal.takeFocus(Qt.OtherFocusReason)
                    onCodeEdited: text => canvas.codeEdited(block.key, text)
                    onCodeDocumentReady: document => canvas.codeDocumentReady(block.key, document)
                    onHintShown: (text, scene) => internal.hint = { text: text, at: internal.fromScene(scene) }
                    onHintHidden: internal.hint = null
                    onChoicesRequested: (pin, scene) => internal.openChoices(block.key, pin, scene)
                    onFixReplaceRequested: canvas.fixReplaceRequested(block.key, block.info.fixControl ?? "")
                    onFixDeleteRequested: canvas.fixDeleteRequested(block.key)
                }
            }

            Repeater {
                model: internal.machinePage ? nodeRows : null

                delegate: StateNode {
                    id: card

                    readOnly: canvas.readOnly
                    selected: internal.selectedSet[card.key] === true
                    target: internal.stateTarget === card.key
                    moveX: internal.movingSet[card.key] === true ? internal.moveX : 0
                    moveY: internal.movingSet[card.key] === true ? internal.moveY : 0
                    now: internal.now
                    Component.onCompleted: {
                        internal.register(internal.nodeItems, card, internal.nodeData)
                        internal.noteSize(card.key, card.width, card.height)
                    }
                    Component.onDestruction: internal.unregister(internal.nodeItems, card)
                    onWidthChanged: internal.noteSize(card.key, card.width, card.height)
                    onHeightChanged: internal.noteSize(card.key, card.width, card.height)
                    onBodyPressed: (button, modifiers, scene) => internal.nodePressed(card.key, button, modifiers,
                                                                                     scene)
                    onBodyMoved: scene => internal.drag(internal.fromScene(scene))
                    onBodyReleased: scene => internal.release(internal.fromScene(scene))
                    onBodyDoubleClicked: button => {
                        if (button === Qt.LeftButton)
                            canvas.opened(card.key)
                    }
                    onRingPressed: (button, scene) => internal.ringPressed(card.key, button, scene)
                    onRingMoved: scene => internal.drag(internal.fromScene(scene))
                    onRingReleased: scene => internal.release(internal.fromScene(scene))
                    onRenamed: name => canvas.stateRenamed(card.key, name)
                    onRenameFinished: internal.takeFocus(Qt.OtherFocusReason)
                }
            }

            Shape {
                objectName: "bpDragWire"
                visible: internal.link !== null && internal.travelled
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: BlueprintLook.pin(internal.link !== null ? internal.link.type : "")
                    strokeWidth: Theme.size.bpWire
                    strokeStyle: internal.link !== null && internal.link.rewire ? ShapePath.DashLine
                                                                                : ShapePath.SolidLine
                    dashPattern: [Theme.space.xxs, Theme.space.xxs]
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    startX: internal.linkCurve.x0
                    startY: internal.linkCurve.y0

                    PathCubic {
                        control1X: internal.linkCurve.c0x
                        control1Y: internal.linkCurve.c0y
                        control2X: internal.linkCurve.c1x
                        control2Y: internal.linkCurve.c1y
                        x: internal.linkCurve.x1
                        y: internal.linkCurve.y1
                    }
                }
            }

            Rectangle {
                id: bubble

                readonly property var item: canvas.pausedNode !== "" ? internal.itemOf(canvas.pausedNode,
                                                                                        internal.generation) : null
                readonly property var lines: {
                    const values = canvas.pinValues ? canvas.pinValues[canvas.pausedNode] ?? null : null
                    const info = internal.nodeData[canvas.pausedNode]
                    if (!values || !info)
                        return []
                    const pins = (info.inputs ?? []).concat(info.outputs ?? [])
                    return Object.keys(values).map(name => {
                        const pin = pins.find(entry => entry.name === name)
                        const label = pin && (pin.label ?? "") !== "" ? pin.label : name
                        return qsTr("%1 = %2").arg(label).arg(values[name])
                    })
                }

                objectName: "bpBubble"
                visible: item !== null && lines.length > 0
                x: item !== null ? item.x + Theme.space.x5l : 0
                y: item !== null ? item.y + item.height + Theme.space.sm : 0
                z: 2
                width: bubbleText.implicitWidth + 2 * (Theme.space.sm + Theme.space.xxs / 2)
                height: bubbleText.implicitHeight + 2 * Theme.space.xs + Theme.space.xxs
                radius: Theme.radius.md
                color: Theme.colors.bpBubble
                border.width: Theme.size.stroke
                border.color: Theme.colors.bpBubbleStroke

                Text {
                    id: bubbleText

                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: bubble.lines.join("\n")
                    color: Theme.colors.textPrimary
                    font.family: Theme.type.monoFamily
                    font.pixelSize: Theme.type.caption.size
                    lineHeight: Theme.type.mono.lineHeight - Theme.space.xxs / 2
                    lineHeightMode: Text.FixedHeight
                }
            }
        }

        Rectangle {
            objectName: "bpMarquee"
            visible: internal.gesture === "marquee" && internal.travelled
            x: internal.marquee.x
            y: internal.marquee.y
            width: internal.marquee.width
            height: internal.marquee.height
            color: Theme.colors.accentSubtle
            border.width: Theme.size.stroke
            border.color: Theme.colors.accent
        }

        Rectangle {
            objectName: "bpReason"
            visible: internal.reason !== "" && internal.link !== null
            x: Math.min(viewport.width - width - Theme.space.sm,
                        (internal.link !== null ? (internal.link.endX - view.x) * view.zoom : 0) + Theme.space.lg)
            y: Math.max(Theme.space.sm, (internal.link !== null ? (internal.link.endY - view.y) * view.zoom : 0)
                        - height - Theme.space.sm)
            width: reasonText.implicitWidth + 2 * (Theme.space.sm + Theme.space.xxs)
            height: Theme.size.tooltip
            radius: Theme.radius.sm
            color: Theme.colors.bgElevated
            border.width: Theme.size.stroke
            border.color: Theme.colors.dangerLine

            Row {
                anchors.centerIn: parent
                spacing: Theme.space.xs + Theme.space.xxs

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "circle-x"
                    size: Theme.size.iconSmall - Theme.space.xxs
                    color: Theme.colors.dangerText
                }

                Text {
                    id: reasonText

                    objectName: "bpReasonText"
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: internal.reason
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }

        Rectangle {
            objectName: "bpHint"
            visible: internal.hint !== null
            x: internal.hint !== null ? Math.max(Theme.space.sm, Math.min(viewport.width - width - Theme.space.sm,
                                                                          internal.hint.at.x - width / 2)) : 0
            y: internal.hint !== null ? Math.max(Theme.space.sm, internal.hint.at.y - height - Theme.space.xs) : 0
            width: Math.min(Theme.size.infoBubble, hintText.implicitWidth) + 2 * (Theme.space.sm + Theme.space.xxs)
            height: hintText.height + 2 * Theme.space.xs + Theme.space.xxs
            radius: Theme.radius.sm
            color: Theme.colors.bgElevated
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeStrong

            Text {
                id: hintText

                objectName: "bpHintText"
                anchors.centerIn: parent
                width: Math.min(Theme.size.infoBubble, implicitWidth)
                textFormat: Text.PlainText
                text: internal.hint !== null ? internal.hint.text : ""
                wrapMode: Text.Wrap
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.caption.size
            }
        }

        Rectangle {
            objectName: "bpInsertHint"
            visible: internal.insertHint !== null
            x: internal.insertHint !== null ? Math.max(Theme.space.sm, Math.min(viewport.width - width - Theme.space.sm,
                                                                          internal.insertHint.at.x - width / 2)) : 0
            y: internal.insertHint !== null ? Math.max(Theme.space.sm,
                                                       internal.insertHint.at.y + Theme.space.md) : 0
            width: insertHintText.implicitWidth + 2 * (Theme.space.sm + Theme.space.xxs)
            height: insertHintText.height + 2 * Theme.space.xs + Theme.space.xxs
            radius: Theme.radius.sm
            color: Theme.colors.bgElevated
            border.width: Theme.size.stroke
            border.color: Theme.colors.accent

            Text {
                id: insertHintText

                objectName: "bpInsertHintText"
                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: qsTr("Insert between")
                color: Theme.colors.textPrimary
                font.pixelSize: Theme.type.caption.size
            }
        }

        DropArea {
            anchors.fill: parent
            keys: ["application/x-gamescrcpy-blueprint"]
            onDropped: drop => {
                if (canvas.readOnly)
                    return
                const source = drop.source
                const payload = source ? source["payload"] : null
                const at = internal.toCanvas(Qt.point(drop.x, drop.y))
                const key = internal.nodeAt(at, "")
                let pin = ""
                if (key !== "") {
                    const item = internal.nodeItems[key]
                    pin = item.pinAt(at.y - item.y, at.x - item.x < item.width / 2 ? "in" : "out")
                }
                drop.accept()
                const itemKey = payload ? (payload.item ?? "") : ""
                const isVariable = itemKey.startsWith("variable:") || itemKey.startsWith("local:")
                if (isVariable && (payload.mode ?? "") === "") {
                    variableMenu.payload = payload
                    variableMenu.dropX = at.x
                    variableMenu.dropY = at.y
                    variableMenu.nodeKey = key
                    variableMenu.pinName = pin
                    const spot = viewport.mapToItem(canvas, drop.x, drop.y)
                    variableMenu.popup(spot.x, spot.y)
                    return
                }
                canvas.dropped(payload, at.x, at.y, key, pin)
            }
        }

        InlineAlert {
            objectName: "bpProblem"
            anchors.centerIn: parent
            width: Math.min(parent.width - 2 * Theme.space.xl, Theme.size.dialogWidth + Theme.size.sideNav)
            visible: canvas.problem !== ""
            tone: "bad"
            title: canvas.problem
            detail: (canvas.graph.problemLine ?? 0) > 0 ? qsTr("Line %1").arg(canvas.graph.problemLine) : ""
            actionText: qsTr("Jump to the code")
            onActionTriggered: canvas.jumpRequested("")
        }

        Loader {
            anchors.right: parent.right
            anchors.bottom: tools.top
            anchors.margins: Theme.space.md
            anchors.bottomMargin: Theme.space.sm
            active: canvas.minimap && canvas.problem === ""
            sourceComponent: BlueprintMinimap {
                model: nodeRows
                itemOf: internal.itemOf
                generation: internal.generation
                viewRect: Qt.rect(view.x, view.y, viewport.width / view.zoom, viewport.height / view.zoom)
                onPanTo: (x, y) => {
                    viewMotion.stop()
                    view.x = x - viewport.width / (2 * view.zoom)
                    view.y = y - viewport.height / (2 * view.zoom)
                }
                onPanDone: internal.commitView()
            }
        }

        FloatingToolbar {
            id: tools

            objectName: "bpToolbar"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.space.md
            pinned: true
            visible: canvas.problem === "" && opacity > 0
            Component.onCompleted: reveal()

            ToolbarButton {
                objectName: "bpAddState"
                visible: !canvas.readOnly && internal.machinePage
                iconName: "plus"
                text: qsTr("Add a state")
                onClicked: internal.addStateAt(internal.center())
            }
            ToolbarButton {
                objectName: "bpArrange"
                visible: !canvas.readOnly
                iconName: "layout-grid"
                text: internal.machinePage ? qsTr("Tidy up") : qsTr("Arrange")
                onClicked: canvas.arrangeRequested()
            }
            ToolbarButton {
                objectName: "bpCommentBox"
                visible: !canvas.readOnly && !internal.machinePage
                iconName: "panel-top"
                text: qsTr("Comment box")
                shortcut: "C"
                onClicked: canvas.commentRequested((canvas.selection ?? []).slice())
            }
            ToolbarSeparator {
                visible: !canvas.readOnly
            }
            ToolbarButton {
                objectName: "bpZoomOut"
                iconName: "minus"
                text: qsTr("Zoom out")
                enabled: view.zoom > canvas.minimumZoom
                onClicked: internal.zoomAt(view.zoom / 1.25, internal.center())
            }
            Button {
                id: zoomLevel

                objectName: "bpZoomReset"
                anchors.verticalCenter: parent.verticalCenter
                width: zoomMetrics.advanceWidth + leftPadding + rightPadding
                small: true
                tone: "ghost"
                text: qsTr("%1%").arg(Math.round(view.zoom * 100))
                toolTip: qsTr("Zoom to 100%")
                Accessible.name: qsTr("Zoom %1%, set to 100%").arg(Math.round(view.zoom * 100))
                onClicked: internal.zoomAt(1, internal.center())

                TextMetrics {
                    id: zoomMetrics

                    font: zoomLevel.font
                    text: qsTr("%1%").arg(200)
                }
            }
            ToolbarButton {
                objectName: "bpZoomIn"
                iconName: "plus"
                text: qsTr("Zoom in")
                enabled: view.zoom < canvas.maximumZoom
                onClicked: internal.zoomAt(view.zoom * 1.25, internal.center())
            }
            ToolbarSeparator {}
            ToolbarButton {
                objectName: "bpSnap"
                iconName: "magnet"
                text: qsTr("Snap to the grid")
                selected: canvas.snap
                onClicked: canvas.snap = !canvas.snap
            }
            ToolbarButton {
                objectName: "bpMinimap"
                iconName: "picture-in-picture-2"
                text: qsTr("Minimap")
                selected: canvas.minimap
                onClicked: canvas.minimap = !canvas.minimap
            }
        }
    }

    BlueprintPicker {
        id: picker

        catalog: canvas.catalog
        onChosen: (id, x, y, node, pin) => canvas.addChosen(id, x, y, node, pin)
        onClosed: internal.restoreFocus()
    }

    TextMetrics {
        id: choiceMeasure

        font.pixelSize: Theme.type.label.size
    }

    T.Popup {
        id: choiceMenu

        property string node: ""
        property string pin: ""
        property var raw: null
        property var choices: []

        objectName: "bpChoices"
        width: Theme.size.menuMinWidth
        padding: Theme.space.xs + Theme.space.xxs
        margins: Theme.space.sm
        implicitHeight: choiceList.implicitHeight + topPadding + bottomPadding
        onClosed: internal.restoreFocus()

        background: PopupSurface {}

        contentItem: Column {
            id: choiceList

            Repeater {
                model: choiceMenu.choices

                delegate: T.ItemDelegate {
                    id: option

                    required property var modelData

                    objectName: "pinChoice_" + (modelData.value ?? "")
                    width: choiceMenu.availableWidth
                    implicitHeight: Theme.size.menuItem
                    hoverEnabled: true
                    horizontalPadding: Theme.space.sm
                    Accessible.name: modelData.text ?? ""
                    onClicked: {
                        const value = option.modelData.value
                        const node = choiceMenu.node
                        const pin = choiceMenu.pin
                        const before = choiceMenu.raw
                        choiceMenu.close()
                        if (value !== before)
                            canvas.valueEdited(node, pin, value)
                    }

                    contentItem: Item {
                        Text {
                            anchors.left: parent.left
                            anchors.right: mark.left
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: option.modelData.text ?? ""
                            elide: Text.ElideRight
                            color: Theme.colors.textPrimary
                            font.pixelSize: Theme.type.label.size
                        }

                        Icon {
                            id: mark

                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: option.modelData.value === choiceMenu.raw
                            name: "check"
                            size: Theme.size.iconSmall
                            color: Theme.colors.accentText
                        }
                    }

                    background: Rectangle {
                        radius: Theme.radius.sm
                        color: option.hovered ? Theme.colors.statePress : "transparent"
                    }
                }
            }
        }
    }

    Menu {
        id: nodeMenu

        property string target: ""
        readonly property var info: internal.nodeData[target] ?? ({})
        readonly property string breakpoint: info.breakpoint ?? ""
        readonly property string kind: info.kind ?? ""
        readonly property bool several: internal.selectedNodes().length > 1

        objectName: "bpNodeMenu"
        onClosed: internal.restoreFocus()

        MenuItem {
            objectName: "bpMenuDelete"
            visible: !canvas.readOnly
            height: visible ? implicitHeight : 0
            iconName: "trash-2"
            text: qsTr("Delete")
            shortcut: "Del"
            danger: true
            onTriggered: canvas.removeRequested((canvas.selection ?? []).slice())
        }
        MenuItem {
            objectName: "bpMenuDuplicate"
            visible: !canvas.readOnly
            height: visible ? implicitHeight : 0
            iconName: "copy"
            text: qsTr("Duplicate")
            shortcut: "Ctrl+D"
            onTriggered: canvas.duplicateRequested((canvas.selection ?? []).slice())
        }
        MenuItem {
            objectName: "bpMenuCopy"
            text: qsTr("Copy as text")
            shortcut: "Ctrl+C"
            onTriggered: canvas.copyRequested((canvas.selection ?? []).slice())
        }
        MenuItem {
            objectName: "bpMenuCollapse"
            visible: !canvas.readOnly && nodeMenu.several
            height: visible ? implicitHeight : 0
            text: qsTr("Collapse into a function")
            onTriggered: canvas.collapseRequested(internal.selectedNodes())
        }
        MenuSeparator {
            visible: !canvas.readOnly && nodeMenu.kind !== "reroute" && nodeMenu.kind !== "pure"
            height: visible ? implicitHeight : 0
        }
        MenuItem {
            objectName: "bpMenuBreakpoint"
            visible: !canvas.readOnly && nodeMenu.kind !== "reroute" && nodeMenu.kind !== "pure"
            height: visible ? implicitHeight : 0
            text: nodeMenu.breakpoint === "" ? qsTr("Set a breakpoint") : qsTr("Remove the breakpoint")
            shortcut: "F9"
            onTriggered: canvas.breakpointToggled(nodeMenu.target)
        }
        MenuItem {
            objectName: "bpMenuBreakpointEnable"
            visible: !canvas.readOnly && (nodeMenu.breakpoint === "on" || nodeMenu.breakpoint === "off")
            height: visible ? implicitHeight : 0
            text: nodeMenu.breakpoint === "off" ? qsTr("Enable the breakpoint") : qsTr("Disable the breakpoint")
            onTriggered: canvas.breakpointEnabled(nodeMenu.target, nodeMenu.breakpoint === "off")
        }
        MenuSeparator {}
        MenuItem {
            objectName: "bpMenuJump"
            visible: nodeMenu.kind !== "reroute"
            height: visible ? implicitHeight : 0
            iconName: "code-xml"
            text: qsTr("Jump to the code")
            onTriggered: canvas.jumpRequested(nodeMenu.target)
        }
        MenuItem {
            objectName: "bpMenuOpen"
            visible: nodeMenu.kind === "machine" || nodeMenu.kind === "state" || (nodeMenu.info.tone ?? "") === "custom"
            height: visible ? implicitHeight : 0
            text: qsTr("Open")
            onTriggered: canvas.opened(nodeMenu.target)
        }
        MenuItem {
            objectName: "bpMenuComment"
            visible: !canvas.readOnly && !internal.machinePage
            height: visible ? implicitHeight : 0
            iconName: "panel-top"
            text: qsTr("Add a comment box")
            shortcut: "C"
            onTriggered: canvas.commentRequested((canvas.selection ?? []).slice())
        }
    }

    Menu {
        id: wireMenu

        property string target: ""

        objectName: "bpWireMenu"
        onClosed: internal.restoreFocus()

        MenuItem {
            objectName: "bpMenuInsert"
            visible: !internal.machinePage && (internal.wireData[wireMenu.target]?.type ?? "") === "exec"
            height: visible ? implicitHeight : 0
            enabled: !canvas.readOnly
            iconName: "plus"
            text: qsTr("Insert a block here…")
            onTriggered: Qt.callLater(() => internal.insertHere(wireMenu.target))
        }
        MenuItem {
            objectName: "bpMenuDisconnect"
            enabled: !canvas.readOnly
            iconName: internal.machinePage ? "trash-2" : "unlink"
            text: internal.machinePage ? qsTr("Remove the transition") : qsTr("Disconnect")
            danger: internal.machinePage
            onTriggered: canvas.wireRemoved(wireMenu.target)
        }
        MenuItem {
            objectName: "bpMenuTransitionLabel"
            visible: internal.machinePage
            height: visible ? implicitHeight : 0
            enabled: !canvas.readOnly
            iconName: "pencil"
            text: qsTr("Edit the reason")
            onTriggered: {
                const arrow = internal.wireItems[wireMenu.target]
                if (arrow)
                    arrow.rename()
            }
        }
        MenuItem {
            objectName: "bpMenuReroute"
            visible: !internal.machinePage
            height: visible ? implicitHeight : 0
            enabled: !canvas.readOnly
            text: qsTr("Add a reroute point")
            onTriggered: canvas.rerouteRequested(wireMenu.target, internal.menuAt.x - Theme.size.bpReroute / 2,
                                                 internal.menuAt.y - Theme.size.bpReroute / 2)
        }
    }

    Menu {
        id: stateMenu

        property string target: ""
        readonly property var info: internal.nodeData[target] ?? ({})

        objectName: "bpStateMenu"
        onClosed: internal.restoreFocus()

        MenuItem {
            objectName: "bpStateOpen"
            text: qsTr("Open")
            onTriggered: canvas.opened(stateMenu.target)
        }
        MenuItem {
            objectName: "bpStateRename"
            visible: !canvas.readOnly
            height: visible ? implicitHeight : 0
            iconName: "pencil"
            text: qsTr("Rename")
            onTriggered: {
                const card = internal.nodeItems[stateMenu.target]
                if (card)
                    card.rename()
            }
        }
        MenuItem {
            objectName: "bpStateStart"
            visible: !canvas.readOnly && stateMenu.info.start !== true
            height: visible ? implicitHeight : 0
            iconName: "play"
            text: qsTr("Set as the start state")
            onTriggered: canvas.startStateSet(stateMenu.target)
        }
        MenuItem {
            objectName: "bpStateCopy"
            iconName: "copy"
            text: qsTr("Copy as text")
            shortcut: "Ctrl+C"
            onTriggered: canvas.copyRequested((canvas.selection ?? []).slice())
        }
        MenuSeparator {}
        MenuItem {
            objectName: "bpStateJump"
            iconName: "code-xml"
            text: qsTr("Jump to the code")
            onTriggered: canvas.jumpRequested(stateMenu.target)
        }
        MenuItem {
            objectName: "bpStateDelete"
            visible: !canvas.readOnly
            height: visible ? implicitHeight : 0
            iconName: "trash-2"
            text: qsTr("Delete")
            shortcut: "Del"
            danger: true
            onTriggered: canvas.removeRequested((canvas.selection ?? []).slice())
        }
    }

    Menu {
        id: machineMenu

        property point at: Qt.point(0, 0)

        objectName: "bpMachineMenu"
        onClosed: internal.restoreFocus()

        MenuItem {
            objectName: "bpMenuAddState"
            iconName: "plus"
            text: qsTr("Add a state here")
            onTriggered: internal.addStateAt(machineMenu.at)
        }
        MenuItem {
            objectName: "bpMenuTidy"
            iconName: "layout-grid"
            text: qsTr("Tidy up")
            onTriggered: canvas.arrangeRequested()
        }
    }

    Menu {
        id: commentMenu

        property string target: ""
        readonly property string tint: internal.commentData[target]?.color ?? "neutral"

        objectName: "bpCommentMenu"
        onClosed: internal.restoreFocus()

        MenuItem {
            enabled: !canvas.readOnly
            iconName: "pencil"
            text: qsTr("Rename")
            onTriggered: {
                const box = internal.commentItems[commentMenu.target]
                if (box)
                    box.rename()
            }
        }
        MenuSeparator {}
        MenuItem {
            enabled: !canvas.readOnly
            choice: true
            chosen: commentMenu.tint === "neutral"
            text: qsTr("Gray")
            onTriggered: canvas.commentEdited(commentMenu.target, { color: "neutral" })
        }
        MenuItem {
            enabled: !canvas.readOnly
            choice: true
            chosen: commentMenu.tint === "blue"
            text: qsTr("Blue")
            onTriggered: canvas.commentEdited(commentMenu.target, { color: "blue" })
        }
        MenuItem {
            enabled: !canvas.readOnly
            choice: true
            chosen: commentMenu.tint === "green"
            text: qsTr("Green")
            onTriggered: canvas.commentEdited(commentMenu.target, { color: "green" })
        }
        MenuItem {
            enabled: !canvas.readOnly
            choice: true
            chosen: commentMenu.tint === "amber"
            text: qsTr("Amber")
            onTriggered: canvas.commentEdited(commentMenu.target, { color: "amber" })
        }
        MenuItem {
            enabled: !canvas.readOnly
            choice: true
            chosen: commentMenu.tint === "red"
            text: qsTr("Red")
            onTriggered: canvas.commentEdited(commentMenu.target, { color: "red" })
        }
        MenuItem {
            enabled: !canvas.readOnly
            choice: true
            chosen: commentMenu.tint === "purple"
            text: qsTr("Purple")
            onTriggered: canvas.commentEdited(commentMenu.target, { color: "purple" })
        }
        MenuSeparator {}
        MenuItem {
            enabled: !canvas.readOnly
            iconName: "trash-2"
            text: qsTr("Delete")
            danger: true
            onTriggered: canvas.removeRequested([commentMenu.target])
        }
    }

    Menu {
        id: variableMenu

        objectName: "bpVarMenu"
        property var payload: null
        property real dropX: 0
        property real dropY: 0
        property string nodeKey: ""
        property string pinName: ""

        MenuItem {
            objectName: "bpVarGet"
            iconName: "arrow-left"
            text: qsTr("Get")
            onTriggered: {
                const chosen = Object.assign({}, variableMenu.payload, { mode: "get" })
                canvas.dropped(chosen, variableMenu.dropX, variableMenu.dropY, variableMenu.nodeKey, variableMenu.pinName)
            }
        }

        MenuItem {
            objectName: "bpVarSet"
            iconName: "pencil"
            text: qsTr("Set")
            onTriggered: {
                const chosen = Object.assign({}, variableMenu.payload, { mode: "set" })
                canvas.dropped(chosen, variableMenu.dropX, variableMenu.dropY, variableMenu.nodeKey, variableMenu.pinName)
            }
        }
    }

    Menu {
        id: pinMenu

        objectName: "bpPinMenu"
        property string node: ""
        property string pin: ""

        MenuItem {
            objectName: "bpPinPromote"
            iconName: "sliders-horizontal"
            text: qsTr("Promote to variable")
            onTriggered: canvas.promoteRequested(pinMenu.node, pinMenu.pin)
        }
    }
}
