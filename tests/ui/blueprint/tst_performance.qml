import QtQuick
import QtTest
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
        check: mock.check
        catalog: mock.catalog
    }

    TestCase {
        name: "BlueprintPerformance"
        when: windowShown

        function init() {
            failOnWarning(/.?/)
        }

        function test_aLargeGraphBuildsPansAndRestsIdle() {
            verify(ThreadCpu.usable())
            const graph = mock.largeGraph(300, 400)
            compare(graph.nodes.length, 300)
            compare(graph.wires.length, 400)
            let started = ThreadCpu.microsNow()
            canvas.graph = graph
            const built = (ThreadCpu.microsNow() - started) / 1000
            verify(findChild(canvas, "bpNode_n299") !== null)
            verify(findChild(canvas, "bpWire_d114") !== null)
            waitForRendering(canvas)
            started = ThreadCpu.microsNow()
            for (let step = 0; step < 120; ++step)
                canvas.viewState = { x: step * 12, y: step * 6, zoom: 0.25 + step / 160 }
            const panned = (ThreadCpu.microsNow() - started) / 1000
            const moved = graph.nodes.slice(0, 20).map(node => node.key)
            const next = JSON.parse(JSON.stringify(graph))
            for (const node of next.nodes) {
                if (moved.indexOf(node.key) >= 0)
                    node.x += 16
            }
            started = ThreadCpu.microsNow()
            canvas.graph = next
            const synced = (ThreadCpu.microsNow() - started) / 1000
            compare(findChild(canvas, "bpNode_n0").x, 16)
            started = ThreadCpu.microsNow()
            canvas.graph = JSON.parse(JSON.stringify(next))
            const unchanged = (ThreadCpu.microsNow() - started) / 1000
            waitForRendering(canvas)
            wait(100)
            const scene = canvas.Window.window
            let frames = 0
            const count = () => ++frames
            let quiet = 0
            let rounds = 0
            scene.frameSwapped.connect(count)
            for (; rounds < 60 && quiet < 3; ++rounds) {
                const seen = frames
                wait(50)
                quiet = frames === seen ? quiet + 1 : 0
            }
            const settled = frames
            wait(500)
            const idle = frames - settled
            scene.frameSwapped.disconnect(count)
            console.info("blueprint 300 nodes / 400 wires, thread CPU time: build " + built + " ms, 120 view changes "
                         + panned + " ms, sync after moving 20 nodes " + synced + " ms, sync with no change "
                         + unchanged + " ms, frames while settling " + settled + " over " + rounds
                         + " windows, frames while idle " + idle)
            verify(built < 30000)
            verify(panned < 2000)
            verify(synced < 5000)
            verify(unchanged < 5000)
            verify(!canvas.animating)
            verify(quiet >= 3)
            compare(idle, 0)
        }
    }
}
