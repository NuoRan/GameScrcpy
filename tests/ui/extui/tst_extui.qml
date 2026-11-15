import QtQuick
import QtTest
import GameScrcpy.Theme

Item {
    id: root

    width: 480
    height: 800

    property var nodeSource: null

    Component {
        id: hostMaker

        QtObject {
            id: panelHost

            property var tree: null

            function nodeAt(path) {
                let found = panelHost.tree
                for (let at = 0; found !== null && at < path.length; ++at)
                    found = (found.children ?? [])[path[at]] ?? null
                return found
            }

            function send(kind, id, value) {
            }

            function release() {
            }
        }
    }

    TestCase {
        name: "ExtensionUi"
        when: windowShown

        function initTestCase() {
            root.nodeSource = Qt.createComponent(Qt.resolvedUrl("../../../src/app/qml/extui/UiNode.qml"))
            verify(root.nodeSource !== null)
            compare(root.nodeSource.status, Component.Ready, root.nodeSource.errorString())
        }

        function init() {
            failOnWarning(/.?/)
        }

        function make(tree) {
            const host = hostMaker.createObject(root)
            host.tree = tree
            const node = root.nodeSource.createObject(root, { host: host, width: 320 })
            verify(node !== null)
            wait(0)
            return node
        }

        function settle(node) {
            tryVerify(() => node.implicitHeight > 0, 2000)
            waitForPolish(node)
            wait(0)
            return node
        }

        function drop(node) {
            const host = node.host
            node.destroy()
            wait(0)
            host.destroy()
        }

        function cells(node) {
            for (let at = 0; at < node.children.length; ++at) {
                const stack = node.children[at]
                if (stack.item === undefined || stack.item === null)
                    continue
                const found = []
                for (let inside = 0; inside < stack.item.children.length; ++inside) {
                    const each = stack.item.children[inside]
                    if (each.item !== undefined && each.width > 0 && each.height > 0)
                        found.push(each)
                }
                return found
            }
            return []
        }

        function gapDown(node, first, second) {
            const all = cells(node)
            verify(all.length > second)
            return all[second].y - (all[first].y + all[first].height)
        }

        function gapAcross(node, first, second) {
            const all = cells(node)
            verify(all.length > second)
            return all[second].x - (all[first].x + all[first].width)
        }

        function spanDown(node) {
            const all = cells(node)
            verify(all.length > 1)
            return all[all.length - 1].y - (all[0].y + all[0].height)
        }

        function spanAcross(node) {
            const all = cells(node)
            verify(all.length > 1)
            return all[all.length - 1].x - (all[0].x + all[0].width)
        }

        function test_r9_emptyTextBetweenTwoTextsKeepsTheGap() {
            const pair = settle(make({ type: "column", children: [{ type: "text", text: "Above" },
                                                                  { type: "text", text: "Below" }] }))
            const blank = make({ type: "text", text: "" })
            const filled = settle(make({ type: "column", children: [{ type: "text", text: "Above" },
                                                                    { type: "text", text: "" },
                                                                    { type: "text", text: "Below" }] }))
            compare(blank.visible, false)
            compare(blank.implicitHeight, 0)
            compare(pair.visible, true)
            compare(gapDown(pair, 0, 1), Theme.space.md)
            compare(spanDown(filled), spanDown(pair))
            compare(gapDown(filled, 0, 1), Theme.space.md)
            compare(filled.implicitHeight, pair.implicitHeight)
            drop(blank)
            drop(filled)
            drop(pair)
        }

        function test_r9_groupOfEmptyChildrenIsNotShown() {
            const hollow = make({ type: "group", children: [{ type: "text", text: "" },
                                                            { type: "column", children: [{ type: "text", text: "" }] },
                                                            { type: "row", children: [] }] })
            compare(hollow.visible, false)
            compare(hollow.implicitHeight, 0)
            const titled = settle(make({ type: "group", title: "Settings", children: [{ type: "text", text: "" }] }))
            compare(titled.visible, true)
            verify(titled.implicitHeight > 0)
            drop(hollow)
            drop(titled)
        }

        function test_r9_nodesThatDrawNothingTakeNoRoom() {
            const hollow = [{ type: "list", id: "rounds", items: [] },
                            { type: "steps", steps: [] },
                            { type: "keyValue", rows: [] },
                            { type: "image" },
                            { type: "canvas", width: 40, height: 40 },
                            { type: "column", children: [] },
                            { type: "row", children: [{ type: "text", text: "" }] }]
            for (const tree of hollow) {
                const node = make(tree)
                compare(node.visible, false)
                compare(node.implicitHeight, 0)
                drop(node)
            }
            const pair = settle(make({ type: "column", children: [{ type: "text", text: "Above" },
                                                                  { type: "text", text: "Below" }] }))
            const spaced = settle(make({ type: "column", children: [{ type: "text", text: "Above" }].concat(
                hollow, [{ type: "text", text: "Below" }]) }))
            compare(gapDown(spaced, 0, 1), Theme.space.md)
            compare(spanDown(spaced), spanDown(pair))
            compare(spaced.implicitHeight, pair.implicitHeight)
            drop(spaced)
            drop(pair)
        }

        function test_r9_rowSkipsTheCardThatShowsNothing() {
            const pair = settle(make({ type: "row", children: [{ type: "text", text: "One" },
                                                               { type: "text", text: "Two" }] }))
            const hollow = settle(make({ type: "row", children: [{ type: "text", text: "One" },
                                                                 { type: "group", children: [{ type: "text", text: "" }] },
                                                                 { type: "text", text: "Two" }] }))
            compare(gapAcross(pair, 0, 1), Theme.space.sm)
            compare(gapAcross(hollow, 0, 1), Theme.space.sm)
            compare(spanAcross(hollow), spanAcross(pair))
            compare(hollow.implicitHeight, pair.implicitHeight)
            drop(hollow)
            drop(pair)
        }

        function test_r9_whatStillDrawsKeepsItsRoom() {
            const bar = settle(make({ type: "progress" }))
            compare(bar.visible, true)
            const table = settle(make({ type: "table", columns: ["Name"], rows: [] }))
            compare(table.visible, true)
            verify(table.implicitHeight >= Theme.size.listItem)
            const chart = settle(make({ type: "chart" }))
            compare(chart.visible, true)
            drop(bar)
            drop(table)
            drop(chart)
        }

        function test_r9_aTextThatGetsWordsShowsAgain() {
            const node = settle(make({ type: "column", children: [{ type: "text", text: "Above" },
                                                                  { type: "text", text: "" },
                                                                  { type: "text", text: "Below" }] }))
            compare(cells(node).length, 2)
            node.host.tree = { type: "column", children: [{ type: "text", text: "Above" },
                                                          { type: "text", text: "Middle" },
                                                          { type: "text", text: "Below" }] }
            tryVerify(() => cells(node).length === 3, 2000)
            compare(gapDown(node, 0, 1), Theme.space.md)
            compare(gapDown(node, 1, 2), Theme.space.md)
            node.host.tree = { type: "column", children: [{ type: "text", text: "Above" },
                                                          { type: "text", text: "" },
                                                          { type: "text", text: "Below" }] }
            tryVerify(() => cells(node).length === 2, 2000)
            compare(gapDown(node, 0, 1), Theme.space.md)
            drop(node)
        }

        function test_r9_separatorHasNoMarginOfItsOwn() {
            const rule = settle(make({ type: "separator" }))
            const pair = settle(make({ type: "column", children: [{ type: "text", text: "Above" },
                                                                  { type: "text", text: "Below" }] }))
            const split = settle(make({ type: "column", children: [{ type: "text", text: "Above" },
                                                                   { type: "separator" },
                                                                   { type: "text", text: "Below" }] }))
            compare(rule.implicitHeight, Theme.size.stroke)
            compare(gapDown(split, 0, 1), Theme.space.md)
            compare(gapDown(split, 1, 2), Theme.space.md)
            compare(split.implicitHeight, pair.implicitHeight + Theme.size.stroke + Theme.space.md)
            drop(split)
            drop(pair)
            drop(rule)
        }
    }
}
