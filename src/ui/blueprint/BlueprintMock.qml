pragma ComponentBehavior: Bound

import QtQuick

import "Geometry.js" as Geometry

QtObject {
    id: mock

    property string page: ""
    property bool readOnly: false
    property var graphs: ({ "": mock.eventGraph(), "fn:combo": mock.functionGraph(), "perf": mock.largeGraph(300, 400),
                            "broken": mock.problemGraph(), "machine:12:3": mock.machineGraph(),
                            "control:fire": mock.controlGraph() })
    readonly property var graph: {
        const base = graphs[page] ?? mock.emptyGraph()
        return readOnly ? Object.assign({}, base, { readOnly: true }) : base
    }
    property var selection: []
    property bool debugging: false
    property string pausedNode: ""
    property var pinValues: ({})
    property var reached: []
    property string debugState: ""
    property string pausedTitle: ""
    property int othersPaused: 0
    property var timeline: ({ span: { start: 0, end: 0 }, playhead: 0, live: true, frames: [], steps: [], lanes: [] })
    property var moment: ({ title: "", subtitle: "", frame: "", boxes: [], result: "", variables: [], stack: [] })
    property var calls: []
    property int created: 0
    readonly property var pages: [
        { key: "", title: "主页", icon: "workflow", kind: "events" },
        { key: "control:fire", title: "开火 · F", icon: "pointer", kind: "control" },
        { key: "fn:combo", title: "一套连招", icon: "code-xml", kind: "function" }
    ]
    readonly property var crumbs: page === "" ? [{ key: "", title: "主页" }]
                                              : [{ key: "", title: "主页" }, { key: page, title: graph.title }]
    readonly property var sidebar: mock.sidebarSections()
    readonly property var details: mock.detailsOf(selection)
    readonly property var items: [
        { id: "tap", title: "点某处", subtitle: "touch.tap", group: "系统节点", tone: "action",
          icon: "mouse-pointer-click", keywords: "tap dianjichu", takes: ["exec", "point"], gives: ["exec"] },
        { id: "wait", title: "等待", subtitle: "time.wait", group: "系统节点", tone: "wait", icon: "timer",
          keywords: "wait dengdai", takes: ["exec", "duration"], gives: ["exec"], description: "等一段时间再往下走。" },
        { id: "find", title: "找图", subtitle: "vision.find", group: "系统节点", tone: "vision", icon: "search",
          keywords: "find zhaotu", takes: ["exec", "image"], gives: ["exec", "point"] },
        { id: "branch", title: "判断", subtitle: "if", group: "流程", tone: "flow", icon: "navigation",
          keywords: "if panduan", takes: ["exec", "bool"], gives: ["exec"] },
        { id: "repeat", title: "重复", subtitle: "for", group: "流程", tone: "flow", icon: "refresh-cw",
          keywords: "repeat chongfu", takes: ["exec", "number"], gives: ["exec", "number"] },
        { id: "aim.held", title: "开镜 · 按着中", subtitle: "controls.aim.held", group: "组件", tone: "custom",
          icon: "pointer", keywords: "aim held kaijing", takes: [], gives: ["bool"], control: "aim", controlTitle: "开镜" },
        { id: "fire.press", title: "开火 · 按下", subtitle: "controls.fire.press()", group: "组件", tone: "action",
          icon: "pointer", keywords: "fire press kaihuo", takes: ["exec"], gives: ["exec"], control: "fire",
          controlTitle: "开火 · F" },
        { id: "fire.release", title: "开火 · 松开", subtitle: "controls.fire.release()", group: "组件", tone: "action",
          icon: "pointer", keywords: "fire release kaihuo", takes: ["exec"], gives: ["exec"], control: "fire",
          controlTitle: "开火 · F" },
        { id: "log", title: "写日志", subtitle: "log.info", group: "系统节点", tone: "action", icon: "terminal",
          keywords: "log rizhi", takes: ["exec", "text"], gives: ["exec"] },
        { id: "code", title: "代码块", subtitle: "", group: "代码", tone: "code", icon: "code-xml",
          keywords: "code daimakuai", takes: ["exec"], gives: ["exec"] }
    ]

    function emptyGraph(): var {
        return { key: "", kind: "events", title: "", readOnly: false, nodes: [], wires: [], comments: [], problem: "",
                 problemLine: 0 }
    }

    function open(key: string) {
        if (key === "perf" && graphs[key] === undefined) {
            const next = Object.assign({}, graphs)
            next[key] = mock.largeGraph(300, 400)
            graphs = next
        }
        selection = []
        page = key
    }

    function exec(name: string, label: string): var {
        return { name: name, label: label, type: "exec", connected: false, optional: false, value: "", editor: "",
                 raw: null, choices: [], preview: "" }
    }

    function pin(name: string, label: string, type: string, extra: var): var {
        return Object.assign({ name: name, label: label, type: type, connected: false, optional: false, value: "",
                               editor: "", raw: null, choices: [], preview: "" }, extra ?? {})
    }

    function block(key: string, kind: string, tone: string, icon: string, title: string, x: real, y: real,
                   inputs: var, outputs: var, extra: var): var {
        return Object.assign({ key: key, kind: kind, tone: tone, icon: icon, title: title, badge: "", subtitle: "",
                               note: "", x: x, y: y, inputs: inputs, outputs: outputs, code: "", floating: false,
                               breakpoint: "", breakpointReason: "", problem: "", start: false, active: false,
                               activeSince: 0, visited: false }, extra ?? {})
    }

    function line(key: string, from: string, fromPin: string, to: string, toPin: string, type: string,
                  extra: var): var {
        return Object.assign({ key: key, from: from, fromPin: fromPin, to: to, toPin: toPin, type: type, label: "",
                               active: false, dashed: false }, extra ?? {})
    }

    function linked(graph: var): var {
        const used = {}
        for (const wire of graph.wires) {
            used[wire.from + "/out/" + wire.fromPin] = true
            used[wire.to + "/in/" + wire.toPin] = true
        }
        graph.nodes = graph.nodes.map(node => Object.assign({}, node, {
            inputs: node.inputs.map(entry => Object.assign({}, entry, {
                connected: used[node.key + "/in/" + entry.name] === true })),
            outputs: node.outputs.map(entry => Object.assign({}, entry, {
                connected: used[node.key + "/out/" + entry.name] === true }))
        }))
        return graph
    }

    function eventGraph(): var {
        const nodes = [
            block("fTap", "event", "event", "keyboard", "按键 F", 40, 60, [], [exec("exec", "")], { badge: "单击" }),
            block("pick", "action", "action", "mouse-pointer-click", "拾取 · 点一下", 290, 60,
                  [exec("exec", "")], [exec("exec", "")]),
            block("wait", "action", "wait", "timer", "等待", 540, 60,
                  [exec("exec", ""), pin("time", "多久", "duration", { editor: "duration", value: "0.3 秒", raw: 300,
                                                                     min: 0, max: 60000, step: 100 })],
                  [exec("exec", "")]),
            block("fHold", "event", "event", "keyboard", "按键 F", 40, 190, [], [exec("exec", "")],
                  { badge: "长按 0.5 秒", note: "长按半秒打开背包" }),
            block("bag", "action", "action", "mouse-pointer-click", "背包 · 点一下", 290, 190,
                  [exec("exec", "")], [exec("exec", "")],
                  { breakpoint: "invalid", breakpointReason: "只能停在 async 函数里" }),
            block("moveOff", "action", "action", "joystick-ring", "设置 移动 · 开着", 540, 190,
                  [exec("exec", ""), pin("on", "开着", "bool", { editor: "toggle", value: "否", raw: false })],
                  [exec("exec", "")]),
            block("lmbDown", "event", "event", "mouse", "按键 左键", 40, 340, [], [exec("exec", "")], { badge: "按下" }),
            block("aiming", "pure", "custom", "pointer", "开镜 · 按着中", 48, 432, [],
                  [pin("value", "", "bool")]),
            block("branch", "flow", "flow", "navigation", "判断", 290, 340,
                  [exec("exec", ""), pin("cond", "条件", "bool")],
                  [exec("yes", "是"), exec("no", "否")]),
            block("burstStart", "action", "action", "tap-echo", "连发 · 开始", 540, 320,
                  [exec("exec", "")], [exec("exec", "")], { problem: "找不到组件“连发”" }),
            block("fireDown", "action", "action", "pointer", "开火 · 按下", 540, 410,
                  [exec("exec", "")], [exec("exec", "")], { breakpoint: "off" }),
            block("lmbUp", "event", "event", "mouse", "按键 左键", 40, 520, [], [exec("exec", "")], { badge: "松开" }),
            block("burstStop", "action", "action", "tap-echo", "连发 · 停止", 290, 520,
                  [exec("exec", "")], [exec("exec", "")]),
            block("fireUp", "action", "action", "pointer", "开火 · 松开", 540, 520,
                  [exec("exec", "")], [exec("exec", "")]),
            block("pressKey", "action", "action", "keyboard", "按一下键", 790, 520,
                  [exec("exec", ""), pin("key", "按键", "key", { editor: "key", value: "空格", raw: "Space" })],
                  [exec("exec", "")]),
            block("every", "event", "event", "timer", "每隔", 40, 700, [], [exec("exec", "")], { badge: "0.5 秒" }),
            block("find", "action", "vision", "search", "找图", 290, 700,
                  [exec("exec", ""),
                   pin("image", "图片", "image", { editor: "image", value: "拾取", raw: "pickup",
                                                 choices: [{ value: "pickup", text: "拾取" },
                                                           { value: "start", text: "开始按钮" }] }),
                   pin("which", "取哪个", "text", { editor: "choice", value: "最像的", raw: "best",
                                                  choices: [{ value: "best", text: "最像的" },
                                                            { value: "first", text: "第一个" }] }),
                   pin("threshold", "相似度", "number", { optional: true, editor: "number", raw: 0.9, min: 0, max: 1,
                                                        step: 0.01 })],
                  [exec("found", "找到"), pin("pos", "位置", "point"), exec("missing", "没找到")],
                  { breakpoint: "on" }),
            block("reroute", "reroute", "flow", "", "", 560, 760, [pin("in", "", "point")], [pin("out", "", "point")]),
            block("click", "action", "action", "mouse-pointer-click", "点某处", 640, 700,
                  [exec("exec", ""), pin("pos", "位置", "point"),
                   pin("hold", "按多久", "duration", { optional: true, editor: "duration", raw: 0, min: 0, max: 10000,
                                                     step: 100 })],
                  [exec("exec", "")]),
            block("log", "code", "code", "code-xml", "代码块", 900, 700, [exec("exec", "")], [exec("exec", "")],
                  { code: "log.info(`点到了“拾取”`)\nhint.flash(\"拾取\")" }),
            block("flash", "action", "action", "sparkles", "在画面上闪一下", 1060, 60,
                  [exec("exec", ""), pin("at", "位置", "point", { editor: "point", value: "50%, 30%",
                                                               raw: { x: 0.5, y: 0.3 } }),
                   pin("label", "文字", "text", { editor: "text", value: "看这里", raw: "看这里" })],
                  [exec("exec", "")], { floating: true })
        ]
        const wires = [
            line("w1", "fTap", "exec", "pick", "exec", "exec"),
            line("w2", "pick", "exec", "wait", "exec", "exec"),
            line("w3", "fHold", "exec", "bag", "exec", "exec"),
            line("w4", "bag", "exec", "moveOff", "exec", "exec"),
            line("w5", "lmbDown", "exec", "branch", "exec", "exec"),
            line("w6", "aiming", "value", "branch", "cond", "bool"),
            line("w7", "branch", "yes", "burstStart", "exec", "exec"),
            line("w8", "branch", "no", "fireDown", "exec", "exec"),
            line("w9", "lmbUp", "exec", "burstStop", "exec", "exec"),
            line("w10", "burstStop", "exec", "fireUp", "exec", "exec"),
            line("w11", "fireUp", "exec", "pressKey", "exec", "exec"),
            line("w12", "every", "exec", "find", "exec", "exec"),
            line("w13", "find", "found", "click", "exec", "exec"),
            line("w14", "find", "pos", "reroute", "in", "point"),
            line("w15", "reroute", "out", "click", "pos", "point"),
            line("w16", "click", "exec", "log", "exec", "exec")
        ]
        const comments = [
            { key: "c1", x: 16, y: 16, width: 990, height: 290, title: "F：单击拾取，长按开背包", color: "neutral" },
            { key: "c2", x: 16, y: 316, width: 1020, height: 300, title: "开镜时左键连发", color: "blue" },
            { key: "c3", x: 16, y: 660, width: 1160, height: 260, title: "看到“拾取”就点", color: "green" }
        ]
        return linked({ key: "", kind: "events", title: "主页", readOnly: false, nodes: nodes, wires: wires,
                        comments: comments, problem: "", problemLine: 0 })
    }

    function functionGraph(): var {
        const nodes = [
            block("entry", "entry", "flow", "play", "一套连招", 40, 80, [],
                  [exec("exec", ""), pin("times", "次数", "number")]),
            block("skill1", "action", "action", "target", "技能 1 · 施放", 300, 60, [exec("exec", "")], [exec("exec", "")]),
            block("pause", "action", "wait", "timer", "等待", 540, 60,
                  [exec("exec", ""), pin("time", "多久", "duration", { editor: "duration", value: "0.2 秒", raw: 200 })],
                  [exec("exec", "")]),
            block("skill2", "action", "action", "target", "技能 2 · 施放", 780, 60, [exec("exec", "")],
                  [exec("exec", "")]),
            block("repeat", "flow", "flow", "refresh-cw", "重复", 300, 200,
                  [exec("exec", ""), pin("count", "次数", "number")], [exec("each", "每次"), exec("done", "完成")]),
            block("done", "return", "flow", "arrow-left", "返回", 1020, 80, [exec("exec", "")], [])
        ]
        const wires = [
            line("f1", "entry", "exec", "skill1", "exec", "exec"),
            line("f2", "skill1", "exec", "pause", "exec", "exec"),
            line("f3", "pause", "exec", "skill2", "exec", "exec"),
            line("f4", "skill2", "exec", "done", "exec", "exec"),
            line("f5", "entry", "times", "repeat", "count", "number")
        ]
        return linked({ key: "fn:combo", kind: "function", title: "一套连招", readOnly: false, nodes: nodes,
                        wires: wires, comments: [], problem: "", problemLine: 0 })
    }

    function controlGraph(): var {
        const nodes = [
            block("cfDown", "event", "event", "keyboard", "按下时", 40, 60, [], [exec("exec", "")], { badge: "按下" }),
            block("cfPress", "action", "action", "pointer", "开火 · 按下", 290, 60, [exec("exec", "")], [exec("exec", "")]),
            block("cfUp", "event", "event", "keyboard", "松开时", 40, 200, [], [exec("exec", "")], { badge: "松开" })
        ]
        const wires = [line("cf1", "cfDown", "exec", "cfPress", "exec", "exec")]
        return linked({ key: "control:fire", kind: "control", title: "开火 · F", readOnly: false, nodes: nodes,
                        wires: wires, comments: [], problem: "", problemLine: 0 })
    }

    function problemGraph(): var {
        return { key: "broken", kind: "events", title: "主页", readOnly: false, nodes: [], wires: [], comments: [],
                 problem: "第 12 行少了一个右括号“)”", problemLine: 12 }
    }

    function state(key: string, icon: string, title: string, subtitle: string, x: real, y: real,
                   extra: var): var {
        return block(key, "state", "flow", icon, title, x, y, [], [], Object.assign({ subtitle: subtitle }, extra ?? {}))
    }

    function transition(key: string, from: string, to: string, label: string, extra: var): var {
        return line(key, from, "", to, "", "transition", Object.assign({ label: label }, extra ?? {}))
    }

    function machineGraph(): var {
        const nodes = [
            state("lobby", "house", "大厅", "点“开始”，最多等 10 秒", 80, 96, { start: true }),
            state("matching", "timer", "匹配中", "等到场景“对局中”", 356, 96, { visited: true }),
            state("playing", "crosshair", "对局中", "运行“挂机一局”", 632, 96,
                  { active: true, activeSince: Date.now() - 192000, visited: true }),
            state("settle", "sparkles", "结算", "点“继续”，领取奖励", 632, 340),
            state("reconnect", "wifi", "断线重连", "点“重新连接”，最多 3 次", 356, 340)
        ]
        const wires = [
            transition("t1", "lobby", "matching", "点了开始"),
            transition("t2", "matching", "playing", "场景 = 对局中", { active: true }),
            transition("t3", "matching", "lobby", "超时 120 秒 · 重试", { dashed: true }),
            transition("t4", "playing", "settle", "场景 = 结算"),
            transition("t5", "settle", "lobby", "点了继续"),
            transition("t6", "playing", "reconnect", "场景 = 断线", { dashed: true }),
            transition("t7", "reconnect", "playing", "重连成功")
        ]
        return { key: "machine:12:3", kind: "machine", title: "挂机流程", readOnly: false, nodes: nodes, wires: wires,
                 comments: [], problem: "", problemLine: 0 }
    }

    function largeGraph(count: int, wiring: int): var {
        const columns = 20
        const nodes = []
        const wires = []
        for (let index = 0; index < count; ++index) {
            const column = index % columns
            const row = Math.floor(index / columns)
            const tones = ["action", "wait", "vision", "flow", "custom"]
            nodes.push(block("n" + index, "action", tones[index % tones.length], "mouse-pointer-click", "方块 " + index,
                             column * 240, row * 140,
                             [exec("exec", ""), pin("value", "数值", "number", { editor: "number", value: String(index),
                                                                              raw: index })],
                             [exec("exec", ""), pin("result", "结果", "number")]))
        }
        let made = 0
        for (let index = 0; index < count && made < wiring; ++index) {
            if (index % columns !== columns - 1 && index + 1 < count) {
                wires.push(line("e" + index, "n" + index, "exec", "n" + (index + 1), "exec", "exec"))
                ++made
            }
        }
        for (let index = 0; index + columns < count && made < wiring; ++index) {
            wires.push(line("d" + index, "n" + index, "result", "n" + (index + columns), "value", "number"))
            ++made
        }
        return linked({ key: "perf", kind: "events", title: "大图", readOnly: false, nodes: nodes, wires: wires,
                        comments: [], problem: "", problemLine: 0 })
    }

    function nodeOf(key: string): var {
        return (graph.nodes ?? []).find(node => node.key === key) ?? null
    }

    function pinOf(node: string, name: string, output: bool): var {
        const found = nodeOf(node)
        if (!found)
            return null
        return ((output ? found.outputs : found.inputs) ?? []).find(entry => entry.name === name) ?? null
    }

    function check(from: string, fromPin: string, to: string, toPin: string): string {
        if (from === to)
            return "不能接到自己身上"
        const source = pinOf(from, fromPin, true)
        const target = pinOf(to, toPin, false)
        if (!source || !target)
            return "这里没有引脚"
        if (source.type === target.type || source.type === "any" || target.type === "any")
            return ""
        if (source.type === "exec" || target.type === "exec")
            return "执行线只能接执行引脚"
        const convertible = { number: ["text", "duration", "bool"], duration: ["number", "text"], bool: ["text"],
                              key: ["text"], point: ["text"] }
        if ((convertible[source.type] ?? []).indexOf(target.type) >= 0)
            return ""
        return "“" + source.type + "”接不到“" + target.type + "”上"
    }

    function catalog(fromNode: string, fromPin: string, query: string): var {
        calls = calls.concat([{ fromNode: fromNode, fromPin: fromPin, query: query }])
        let output = true
        let source = pinOf(fromNode, fromPin, true)
        if (!source) {
            source = pinOf(fromNode, fromPin, false)
            output = false
        }
        const needle = query.trim().toLowerCase()
        return mock.items.filter(item => {
            if (source && (output ? item.takes : item.gives).indexOf(source.type) < 0)
                return false
            if (needle === "")
                return true
            return item.title.toLowerCase().indexOf(needle) >= 0 || item.subtitle.toLowerCase().indexOf(needle) >= 0
                   || item.keywords.indexOf(needle) >= 0
        }).map(item => ({ id: item.id, title: item.title, subtitle: item.subtitle, group: item.group, tone: item.tone,
                          icon: item.icon, keywords: item.keywords, control: item.control ?? "",
                          controlTitle: item.controlTitle ?? "", description: item.description ?? "" }))
    }

    function edit(change: var) {
        const next = Object.assign({}, graphs)
        next[page] = linked(change(JSON.parse(JSON.stringify(graphs[page] ?? mock.emptyGraph()))))
        graphs = next
    }

    function move(keys: var, dx: real, dy: real) {
        edit(current => {
            for (const node of current.nodes) {
                if (keys.indexOf(node.key) >= 0) {
                    node.x += dx
                    node.y += dy
                }
            }
            for (const comment of current.comments) {
                if (keys.indexOf(comment.key) >= 0) {
                    comment.x += dx
                    comment.y += dy
                }
            }
            return current
        })
    }

    function connectPins(from: string, fromPin: string, to: string, toPin: string) {
        if (check(from, fromPin, to, toPin) !== "")
            return
        const type = pinOf(from, fromPin, true)?.type ?? "any"
        edit(current => {
            if (type !== "exec")
                current.wires = current.wires.filter(wire => !(wire.to === to && wire.toPin === toPin))
            else if (!current.wires.some(wire => wire.from === from && wire.fromPin === fromPin))
                current.wires = current.wires.filter(wire => !(wire.to === to && wire.toPin === toPin))
            mock.created += 1
            current.wires.push(mock.line("new" + mock.created, from, fromPin, to, toPin, type))
            return current
        })
    }

    function disconnectPin(node: string, name: string) {
        edit(current => {
            current.wires = current.wires.filter(wire => !((wire.from === node && wire.fromPin === name)
                                                           || (wire.to === node && wire.toPin === name)))
            return current
        })
    }

    function removeWire(key: string) {
        edit(current => {
            current.wires = current.wires.filter(wire => wire.key !== key)
            return current
        })
    }

    function remove(keys: var) {
        edit(current => {
            current.nodes = current.nodes.filter(node => keys.indexOf(node.key) < 0)
            current.comments = current.comments.filter(comment => keys.indexOf(comment.key) < 0)
            current.wires = current.wires.filter(wire => keys.indexOf(wire.from) < 0 && keys.indexOf(wire.to) < 0)
            return current
        })
        selection = selection.filter(key => keys.indexOf(key) < 0)
    }

    function setValue(node: string, name: string, value: var) {
        edit(current => {
            const found = current.nodes.find(entry => entry.key === node)
            const target = found ? found.inputs.find(entry => entry.name === name) : null
            if (!target)
                return current
            target.raw = value
            if (target.editor === "duration")
                target.value = (value / 1000) + " 秒"
            else if (target.editor === "toggle")
                target.value = value ? "是" : "否"
            else if (target.editor === "choice" || target.editor === "component" || target.editor === "image")
                target.value = (target.choices.find(choice => choice.value === value) ?? {}).text ?? String(value)
            else if (target.editor === "key")
                target.value = String(value).replace(/^Key/, "").replace(/^Digit/, "")
            else
                target.value = String(value)
            return current
        })
    }

    function setCode(node: string, text: string) {
        edit(current => {
            const found = current.nodes.find(entry => entry.key === node)
            if (found)
                found.code = text
            return current
        })
    }

    function editComment(key: string, changes: var) {
        edit(current => {
            const found = current.comments.find(entry => entry.key === key)
            if (found)
                Object.assign(found, changes)
            return current
        })
    }

    function addComment(keys: var) {
        const rects = []
        for (const key of keys) {
            const found = nodeOf(key)
            if (found)
                rects.push({ x: found.x, y: found.y })
        }
        const left = rects.length > 0 ? Math.min(...rects.map(rect => rect.x)) - 24 : 0
        const top = rects.length > 0 ? Math.min(...rects.map(rect => rect.y)) - 48 : 0
        edit(current => {
            mock.created += 1
            current.comments.push({ key: "note" + mock.created, x: left, y: top, width: 420, height: 220,
                                    title: "新注释框", color: "neutral" })
            return current
        })
    }

    function addReroute(wireKey: string, x: real, y: real) {
        edit(current => {
            const wire = current.wires.find(entry => entry.key === wireKey)
            if (!wire)
                return current
            mock.created += 1
            const key = "reroute" + mock.created
            current.nodes.push(mock.block(key, "reroute", "flow", "", "", x, y, [mock.pin("in", "", wire.type)],
                                          [mock.pin("out", "", wire.type)]))
            current.wires = current.wires.filter(entry => entry.key !== wireKey)
            current.wires.push(mock.line(wireKey + "a", wire.from, wire.fromPin, key, "in", wire.type))
            current.wires.push(mock.line(wireKey + "b", key, "out", wire.to, wire.toPin, wire.type))
            return current
        })
    }

    function add(id: string, x: real, y: real, fromNode: string, fromPin: string) {
        const item = mock.items.find(entry => entry.id === id)
        if (!item)
            return
        mock.created += 1
        const key = "added" + mock.created
        const inputs = item.takes.map((type, index) => type === "exec" ? mock.exec("exec", "")
                                                                         : mock.pin("in" + index, "", type))
        const outputs = item.gives.map((type, index) => type === "exec" ? mock.exec("exec", "")
                                                                          : mock.pin("out" + index, "", type))
        const kind = item.tone === "code" ? "code" : item.takes.indexOf("exec") < 0 ? "pure" : "action"
        edit(current => {
            current.nodes.push(mock.block(key, kind, item.tone, item.icon, item.title, x, y, inputs, outputs))
            return current
        })
        if (fromNode !== "") {
            const outgoing = pinOf(fromNode, fromPin, true)
            if (outgoing) {
                const target = inputs.find(entry => entry.type === outgoing.type)
                if (target)
                    connectPins(fromNode, fromPin, key, target.name)
            } else {
                const incoming = pinOf(fromNode, fromPin, false)
                const source = incoming ? outputs.find(entry => entry.type === incoming.type) : null
                if (source)
                    connectPins(key, source.name, fromNode, fromPin)
            }
        }
        selection = [key]
    }

    function insertOnWire(wireKey: string, node: string) {
        const wire = (graph.wires ?? []).find(entry => entry.key === wireKey)
        const block = nodeOf(node)
        if (!wire || !block || block.floating !== true || wire.type !== "exec")
            return
        edit(current => {
            const target = current.wires.find(entry => entry.key === wireKey)
            const moved = current.nodes.find(entry => entry.key === node)
            const head = current.nodes.find(entry => entry.key === target?.from)
            if (!target || !moved || !head)
                return current
            mock.created += 1
            current.wires = current.wires.filter(entry => entry.key !== wireKey)
            current.wires.push(mock.line("ins" + mock.created + "a", target.from, target.fromPin, node, "exec", "exec"))
            current.wires.push(mock.line("ins" + mock.created + "b", node, "exec", target.to, target.toPin, "exec"))
            moved.floating = false
            moved.x = head.x + 240
            moved.y = head.y
            return current
        })
        selection = [node]
    }

    function duplicate(keys: var) {
        const copies = []
        edit(current => {
            for (const node of current.nodes.filter(entry => keys.indexOf(entry.key) >= 0)) {
                mock.created += 1
                const copy = JSON.parse(JSON.stringify(node))
                copy.key = node.key + "copy" + mock.created
                copy.x += 32
                copy.y += 32
                copies.push(copy.key)
                current.nodes.push(copy)
            }
            return current
        })
        selection = copies
    }

    function toggleBreakpoint(key: string) {
        edit(current => {
            const found = current.nodes.find(entry => entry.key === key)
            if (found)
                found.breakpoint = found.breakpoint === "" ? "on" : ""
            return current
        })
    }

    function enableBreakpoint(key: string, on: bool) {
        edit(current => {
            const found = current.nodes.find(entry => entry.key === key)
            if (found && found.breakpoint !== "")
                found.breakpoint = on ? "on" : "off"
            return current
        })
    }

    function sidebarSections(): var {
        return [
            { key: "controls", title: "组件", count: "13", addable: false, items: [
                { key: "control:fire", icon: "pointer", title: "开火", badge: "按住", dot: "", members: [
                    { key: "key", title: "按键", kind: "property", badge: "" },
                    { key: "press", title: "按下", kind: "function", badge: "" }] },
                { key: "control:aim", icon: "pointer", title: "开镜", badge: "按住", dot: "", members: [
                    { key: "key", title: "按键", kind: "property", badge: "" },
                    { key: "pos", title: "位置", kind: "property", badge: "" },
                    { key: "on", title: "开着", kind: "property", badge: "" },
                    { key: "held", title: "按着中", kind: "state", badge: "" },
                    { key: "heldFor", title: "按了多久", kind: "state", badge: "" },
                    { key: "onPress", title: "按下时", kind: "event", badge: "" },
                    { key: "onRelease", title: "松开时", kind: "event", badge: "" },
                    { key: "press", title: "按下", kind: "function", badge: "" },
                    { key: "release", title: "松开", kind: "function", badge: "" }] },
                { key: "control:burst", icon: "tap-echo", title: "连发", badge: "连发", dot: "", members: [] },
                { key: "control:walk", icon: "joystick-ring", title: "移动", badge: "WASD 轮盘", dot: "", members: [] }] },
            { key: "vars", title: "变量", count: "", addable: true, items: [
                { key: "variable:health", icon: "gauge", title: "血量", badge: "整数", dot: "variable",
                  category: "战斗", description: "英雄的血量", members: [] },
                { key: "variable:speed", icon: "gauge", title: "速度", badge: "小数", dot: "variable",
                  category: "战斗", description: "", members: [] },
                { key: "local:count", icon: "gauge", title: "次数", badge: "整数", dot: "local",
                  category: "", description: "", members: [] },
                { key: "inCar", icon: "sliders-horizontal", title: "在车上", badge: "开关", dot: "var", members: [] },
                { key: "afk", icon: "layers", title: "挂机中", badge: "全局", dot: "global", members: [] }] },
            { key: "events", title: "自定义事件", count: "", addable: true, items: [
                { key: "swapped", icon: "", title: "换枪了", badge: "", dot: "event", members: [] }] },
            { key: "interfaces", title: "接口", count: "", addable: true, items: [
                { key: "canFire", icon: "link", title: "能开火", badge: "开火、连发", dot: "", members: [] }] },
            { key: "functions", title: "函数", count: "", addable: true, items: [
                { key: "combo", icon: "code-xml", title: "一套连招", badge: "", dot: "", members: [] }] },
            { key: "pages", title: "页", count: "", addable: true, items: [
                { key: "home", icon: "workflow", title: "主页", badge: "", dot: "", members: [] },
                { key: "car", icon: "workflow", title: "载具", badge: "", dot: "", members: [] }] }
        ]
    }

    function detailsOf(keys: var): var {
        if (keys.length !== 1)
            return {}
        const found = nodeOf(keys[0])
        if (!found)
            return {}
        if (found.key === "fHold") {
            return {
                icon: "keyboard", tone: "event", title: "按键事件", subtitle: "主页 · F 长按 → 背包 · 点一下",
                groups: [
                    { title: "", fields: [
                        { name: "key", label: "按键", kind: "key", value: "F", caption: "", info: "", unit: "" },
                        { name: "when", label: "什么时候算", kind: "segmented", value: "long", choices: [
                            { value: "press", text: "按下" }, { value: "release", text: "松开" },
                            { value: "tap", text: "单击" }, { value: "long", text: "长按" },
                            { value: "double", text: "双击" }] },
                        { name: "hold", label: "长按多久", kind: "slider", value: 0.5, min: 0.1, max: 2, step: 0.1,
                          unit: "秒" }] },
                    { title: "", fields: [
                        { name: "again", label: "上一次还没做完又按了", kind: "choice", value: "ignore",
                          caption: "还可以选：停掉上一次从头来 · 排在后面 · 同时做", choices: [
                            { value: "ignore", text: "不理，等它做完" }, { value: "restart", text: "停掉上一次、从头再来" },
                            { value: "queue", text: "排在后面" }, { value: "parallel", text: "同时跑" }] },
                        { name: "enabled", label: "开着", kind: "toggle", value: true, info: "关掉时这个事件先不响应。" }] }
                ],
                code: "keys.F.onLongPress(500, async () => {\n  await controls.背包.tap()\n})", codeLine: 5
            }
        }
        if (found.kind === "state")
            return mock.stateDetails(found)
        return {
            icon: found.icon, tone: found.tone, title: found.title, subtitle: "主页 · " + found.title,
            groups: [{ title: "", fields: [{ name: "kind", label: "种类", kind: "readonly", value: found.kind },
                                           { name: "note", label: "注释", kind: "text", value: found.note }] }],
            code: found.code, codeLine: 0
        }
    }

    function stateDetails(found: var): var {
        const leaving = (graph.wires ?? []).filter(wire => wire.from === found.key).map(wire => ({
            icon: "", text: (wire.label ?? "") !== "" ? wire.label : "（没写原因）",
            badge: "→ " + (nodeOf(wire.to)?.title ?? wire.to), tone: "" }))
        const playing = found.key === "playing"
        const stayed = found.active === true ? " · 已停留 " + Geometry.clock(Date.now() - (found.activeSince ?? 0)) : ""
        const entering = playing ? [{ icon: "keyboard", text: "打开键位“自动拾取”", badge: "", tone: "" },
                                    { icon: "workflow", text: "运行“挂机一局”", badge: "循环", tone: "" }]
                                 : [{ icon: "mouse-pointer-click", text: found.subtitle ?? "", badge: "", tone: "" }]
        return {
            icon: found.icon, tone: "state", title: found.title, subtitle: "状态" + stayed,
            groups: [
                { title: "", fields: [{ name: "enter", label: "进入时", kind: "list", items: entering }] },
                { title: "", fields: [{ name: "leave", label: "什么时候离开", kind: "list", items: leaving.concat(playing ? [
                    { icon: "", text: "超过 25 分钟", badge: "→ 大厅 · 通知我", tone: "warn" }] : []) }] },
                { title: "", fields: [{ name: "log", label: "转移记录", kind: "log", entries: [
                    { time: "15:59:12", text: "匹配中 → 对局中 · 场景 0.97", tone: "" },
                    { time: "15:58:40", text: "大厅 → 匹配中 · 点了开始", tone: "" },
                    { time: "15:31:05", text: "匹配中 → 大厅 · 超时重试", tone: "warn" }] }] }
            ],
            code: "machine({\n  大厅: { … },\n  " + found.title + ": { … }\n})", codeLine: 12
        }
    }

    function addState(x: real, y: real) {
        mock.created += 1
        const key = "state" + mock.created
        edit(current => {
            current.nodes.push(mock.state(key, "", "新状态", "", x, y, {}))
            return current
        })
        selection = [key]
    }

    function addTransition(from: string, to: string) {
        edit(current => {
            mock.created += 1
            current.wires.push(mock.transition("move" + mock.created, from, to, ""))
            return current
        })
    }

    function setTransitionLabel(key: string, text: string) {
        edit(current => {
            const found = current.wires.find(entry => entry.key === key)
            if (found)
                found.label = text
            return current
        })
    }

    function renameState(key: string, name: string) {
        edit(current => {
            const found = current.nodes.find(entry => entry.key === key)
            if (found)
                found.title = name
            return current
        })
    }

    function setStartState(key: string) {
        edit(current => {
            for (const node of current.nodes)
                node.start = node.key === key
            return current
        })
    }

    function debugTimeline(): var {
        const frames = []
        for (let at = 0; at <= 42000; at += 3000)
            frames.push({ at: at, source: "" })
        const steps = [
            { lane: "daily", start: 0, end: 1800, text: "唤醒", tone: "action" },
            { lane: "daily", start: 2000, end: 5200, text: "找图 0.93", tone: "vision" },
            { lane: "daily", start: 5400, end: 6600, text: "点击", tone: "action" },
            { lane: "daily", start: 6800, end: 11600, text: "等到 对局中 6.8 s", tone: "wait" },
            { lane: "daily", start: 11800, end: 12400, text: "找图", tone: "vision" }
        ]
        for (let at = 1000; at < 42000; at += 6000)
            steps.push({ lane: "pickup", start: at, end: at + 2400, text: "找图 拾取", tone: "vision" })
        return { span: { start: 0, end: 42100 }, playhead: 12400, live: false, frames: frames, steps: steps,
                 lanes: [{ key: "daily", title: "每日任务" }, { key: "pickup", title: "自动拾取" }] }
    }

    function debugMoment(): var {
        return {
            title: "此刻 · 00:12.40", subtitle: "停在“找图” · 断点", frame: "",
            boxes: [{ x: 0.68, y: 0.72, w: 0.24, h: 0.17, text: "开始按钮 0.94", tone: "good" }],
            result: "找到 · 相似度 0.94 · 阈值 0.90 · 4.2 ms",
            variables: [{ name: "序号", value: "3" }, { name: "找到", value: "是" }, { name: "位置", value: "(1712, 880)" },
                        { name: "相似度", value: "0.94" }],
            stack: [{ title: "找图", place: "main.ts:8", current: true },
                    { title: "循环", place: "main.ts:7", current: false },
                    { title: "每日任务", place: "main.ts:5", current: false }]
        }
    }

    function seek(at: real) {
        timeline = Object.assign({}, timeline, { playhead: at, live: false })
        moment = Object.assign({}, moment, { title: "此刻 · " + Geometry.preciseClock(at - (timeline.span?.start ?? 0)) })
    }

    function goLive() {
        timeline = Object.assign({}, timeline, { playhead: timeline.span?.end ?? 0, live: true })
    }

    function chooseFrame(index: int) {
        moment = Object.assign({}, moment, { stack: (moment.stack ?? []).map((entry, at) => Object.assign({}, entry, {
            current: at === index })) })
    }

    function resume() {
        debugState = "running"
        pausedNode = ""
        pausedTitle = ""
        pinValues = {}
    }

    function startDebugging() {
        debugging = true
        reached = ["every", "find", "fTap", "pick"]
        pausedNode = "find"
        pinValues = { find: { found: "是", pos: "(1712, 880)", threshold: "0.94" }, pick: { exec: "✓" } }
        debugState = "paused"
        pausedTitle = "找图"
        othersPaused = 1
        timeline = mock.debugTimeline()
        moment = mock.debugMoment()
    }

    function stopDebugging() {
        debugging = false
        reached = []
        pausedNode = ""
        pinValues = {}
        debugState = ""
        pausedTitle = ""
        othersPaused = 0
        timeline = { span: { start: 0, end: 0 }, playhead: 0, live: true, frames: [], steps: [], lanes: [] }
        moment = { title: "", subtitle: "", frame: "", boxes: [], result: "", variables: [], stack: [] }
    }
}
