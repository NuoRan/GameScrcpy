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

    DebugBar {
        id: bar

        x: Theme.space.md
        y: Theme.space.md
    }

    DebugTimeline {
        id: timeline

        x: 0
        y: 200
        width: 1000
    }

    DebugMoment {
        id: moment

        x: 1000
        y: 0
        width: 280
        height: 800
    }

    SignalSpy {
        id: continues

        target: bar
        signalName: "continueClicked"
    }

    SignalSpy {
        id: overs

        target: bar
        signalName: "stepOverClicked"
    }

    SignalSpy {
        id: intos

        target: bar
        signalName: "stepIntoClicked"
    }

    SignalSpy {
        id: outs

        target: bar
        signalName: "stepOutClicked"
    }

    SignalSpy {
        id: stops

        target: bar
        signalName: "stopClicked"
    }

    SignalSpy {
        id: seeks

        target: timeline
        signalName: "seeked"
    }

    SignalSpy {
        id: lives

        target: timeline
        signalName: "liveClicked"
    }

    SignalSpy {
        id: saves

        target: timeline
        signalName: "saveClicked"
    }

    SignalSpy {
        id: frames

        target: moment
        signalName: "stackChosen"
    }

    TestCase {
        name: "BlueprintDebug"
        when: windowShown

        function init() {
            failOnWarning(/.?/)
            bar.state = "paused"
            bar.pausedTitle = "找图"
            bar.othersPaused = 0
            bar.shortcutsEnabled = true
            const data = mock.debugTimeline()
            timeline.span = data.span
            timeline.playhead = data.playhead
            timeline.live = data.live
            timeline.frames = data.frames
            timeline.steps = data.steps
            timeline.lanes = data.lanes
            const now = mock.debugMoment()
            moment.title = now.title
            moment.subtitle = now.subtitle
            moment.boxes = now.boxes
            moment.result = now.result
            moment.variables = now.variables
            moment.stack = now.stack
            for (const spy of [continues, overs, intos, outs, stops, seeks, lives, saves, frames])
                spy.clear()
            wait(0)
        }

        function test_theBarShowsTheStateAndTheStepButtons() {
            compare(findChild(bar, "bpDebugState").text, "Paused · 找图")
            for (const name of ["bpContinue", "bpStepOver", "bpStepInto", "bpStepOut", "bpStopTask"])
                verify(findChild(bar, name).visible, name)
            verify(!findChild(bar, "bpOthersPaused").visible)
            bar.state = "running"
            compare(findChild(bar, "bpDebugState").text, "Debugging")
            verify(!findChild(bar, "bpContinue").visible)
            bar.state = ""
            verify(!findChild(bar, "bpDebugState").visible)
        }

        function test_theStepButtonsAsk() {
            waitForPolish(findChild(bar, "bpContinue").parent)
            mouseClick(findChild(bar, "bpContinue"))
            mouseClick(findChild(bar, "bpStepOver"))
            mouseClick(findChild(bar, "bpStepInto"))
            mouseClick(findChild(bar, "bpStepOut"))
            mouseClick(findChild(bar, "bpStopTask"))
            compare(continues.count, 1)
            compare(overs.count, 1)
            compare(intos.count, 1)
            compare(outs.count, 1)
            compare(stops.count, 1)
        }

        function test_theDebugKeys() {
            keyClick(Qt.Key_F5)
            keyClick(Qt.Key_F10)
            keyClick(Qt.Key_F11)
            keyClick(Qt.Key_F11, Qt.ShiftModifier)
            compare(continues.count, 1)
            compare(overs.count, 1)
            compare(intos.count, 1)
            compare(outs.count, 1)
            bar.shortcutsEnabled = false
            keyClick(Qt.Key_F5)
            compare(continues.count, 1)
            bar.shortcutsEnabled = true
            bar.state = "running"
            keyClick(Qt.Key_F10)
            compare(overs.count, 1)
        }

        function test_otherPausedTasksShowAChip() {
            bar.othersPaused = 2
            const chip = findChild(bar, "bpOthersPaused")
            verify(chip.visible)
            verify(chip.width > 0)
        }

        function test_theTimelineShowsThePlayheadTime() {
            compare(findChild(timeline, "bpTimelineClock").text, "00:12.40 / 00:42.10")
            verify(findChild(timeline, "bpLive").visible)
            timeline.live = true
            verify(!findChild(timeline, "bpLive").visible)
            const playhead = findChild(timeline, "bpPlayhead")
            fuzzyCompare(playhead.x + playhead.width / 2, timeline.xOf(12400), 0.5)
        }

        function test_draggingThePlayheadSeeksOncePerFrame() {
            const playhead = findChild(timeline, "bpPlayhead")
            const y = playhead.y + playhead.height / 2
            const fromX = timeline.xOf(2000)
            const toX = timeline.xOf(30000)
            mousePress(timeline, fromX, y)
            const steps = 8
            for (let step = 1; step <= steps; ++step)
                mouseMove(timeline, fromX + (toX - fromX) * step / steps, y, -1, Qt.LeftButton)
            fuzzyCompare(playhead.x + playhead.width / 2, toX, 1)
            mouseRelease(timeline, toX, y)
            verify(seeks.count >= 1)
            verify(seeks.count <= steps + 1)
            fuzzyCompare(seeks.signalArguments[seeks.count - 1][0], 30000, 50)
            const count = seeks.count
            wait(50)
            compare(seeks.count, count)
        }

        function test_theTimelineButtonsAsk() {
            waitForPolish(findChild(timeline, "bpLive").parent)
            mouseClick(findChild(timeline, "bpLive"))
            mouseClick(findChild(timeline, "bpSaveSegment"))
            compare(lives.count, 1)
            compare(saves.count, 1)
        }

        function test_severalLanesGetARowEach() {
            compare(timeline.rows.length, 2)
            timeline.lanes = []
            compare(timeline.rows.length, 1)
        }

        function test_theMomentShowsThePictureVariablesAndStack() {
            compare(findChild(moment, "bpMomentTitle").text, "此刻 · 00:12.40")
            verify(findChild(moment, "bpMomentResult").visible)
            verify(findChild(moment, "bpMomentPicture").visible)
            const entry = findChild(moment, "bpStack_2")
            verify(entry !== null)
            mouseClick(entry)
            compare(frames.count, 1)
            compare(frames.signalArguments[0][0], 2)
        }
    }
}
