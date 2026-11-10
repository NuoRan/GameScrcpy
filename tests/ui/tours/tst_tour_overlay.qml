import QtQuick
import QtTest
import GameScrcpy.Theme

Item {
    id: root

    width: 900
    height: 640

    QtObject {
        id: store

        property var seen: []
        property bool toursQuiet: false

        function tourSeen(key) {
            return store.seen.indexOf(key) >= 0
        }

        function markTourSeen(key) {
            if (store.seen.indexOf(key) < 0)
                store.seen.push(key)
        }
    }

    Item {
        id: page

        anchors.fill: parent

        Item {
            id: spot

            x: 40
            y: 40
            width: 160
            height: 48
            property int clicks: 0

            Rectangle {
                anchors.fill: parent
                color: Theme.colors.bgElevated
            }

            MouseArea {
                anchors.fill: parent
                onClicked: spot.clicks += 1
            }
        }

        Item {
            id: other

            x: 420
            y: 380
            width: 220
            height: 120
            property int clicks: 0

            MouseArea {
                anchors.fill: parent
                onClicked: other.clicks += 1
            }
        }
    }

    Loader {
        id: loader

        anchors.fill: parent
        source: "../../../src/app/qml/TourOverlay.qml"
    }

    Connections {
        target: loader.item

        function onStarted() {
            test.placedAtStart = test.placed
        }
    }

    TestCase {
        id: test

        name: "TourOverlay"
        when: windowShown

        property bool actionDone: false
        property int placed: 0
        property int placedAtStart: -1

        readonly property var overlay: loader.item

        function init() {
            failOnWarning(/.?/)
            Theme.reducedMotion = true
            overlay.startWhen = false
            overlay.cancel()
            loader.visible = true
            test.actionDone = false
            test.placed = 0
            test.placedAtStart = -1
            store.seen = []
            store.toursQuiet = false
            spot.x = 40
            spot.y = 40
            spot.clicks = 0
            other.clicks = 0
            other.visible = true
            overlay.tourKey = "devices"
            overlay.settings = store
            overlay.steps = [
                { target: spot, text: "Plug the phone in" },
                { target: other, text: "Press Mirroring" }
            ]
            overlay.advance = Qt.binding(function() {
                return test.actionDone && overlay.step === 0
            })
        }

        function test_theCutOutFollowsTheTarget() {
            overlay.start()
            verify(overlay.running)
            compare(overlay.step, 0)
            compare(overlay.hole.x, 40)
            compare(overlay.hole.y, 40)
            compare(overlay.hole.width, 160)
            compare(overlay.hole.height, 48)
            compare(findChild(overlay, "sentence").text, "Plug the phone in")
            compare(findChild(overlay, "counter").text, "Step 1 of 2")

            spot.x = 120
            spot.y = 200
            tryVerify(() => overlay.hole.x === 120 && overlay.hole.y === 200)
            tryVerify(() => overlay.hole.width === 160 && overlay.hole.height === 48)
        }

        function test_clicksOutsideTheCutOutDoNothing() {
            overlay.start()
            mouseClick(other, 10, 10, Qt.LeftButton, Qt.NoModifier)
            compare(other.clicks, 0)
            mouseClick(spot, 10, 10, Qt.LeftButton, Qt.NoModifier)
            compare(spot.clicks, 1)
        }

        function test_onlyTheRealActionMovesOn() {
            overlay.start()
            compare(overlay.step, 0)
            wait(20)
            compare(overlay.step, 0)

            test.actionDone = true
            tryCompare(overlay, "step", 1)
            tryCompare(findChild(overlay, "sentence"), "text", "Press Mirroring")
            compare(findChild(overlay, "counter").text, "Step 2 of 2")
            verify(overlay.running)
            wait(20)
            compare(overlay.step, 1)

            overlay.skip()
            compare(overlay.running, false)
            compare(store.seen, ["devices"])
        }

        function test_skipEndsTheTourAndRemembersIt() {
            overlay.start()
            const skip = findChild(overlay, "skip")
            verify(skip !== null)
            mouseClick(skip)
            compare(overlay.running, false)
            compare(store.seen, ["devices"])
        }

        function test_escapeSkipsTheTour() {
            overlay.start()
            verify(overlay.running)
            keyClick(Qt.Key_Escape)
            compare(overlay.running, false)
            compare(store.seen, ["devices"])
        }

        function test_aStepWithoutItsPlaceIsSkipped() {
            overlay.steps = [
                { target: null, text: "Nothing here" },
                { target: spot, text: "Now something" }
            ]
            overlay.start()
            tryVerify(() => overlay.step === 1)
            verify(overlay.running)
            compare(overlay.hole.width, 160)
            compare(findChild(overlay, "sentence").text, "Now something")
        }

        function test_aTargetThatIsHiddenIsSkipped() {
            overlay.steps = [
                { target: other, text: "Hidden place" },
                { target: spot, text: "Visible place" }
            ]
            other.visible = false
            overlay.start()
            tryVerify(() => overlay.step === 1)
            other.visible = true
            compare(overlay.hole.width, 160)
        }

        function test_aModalHidesTheTourWithoutEndingIt() {
            overlay.start()
            verify(overlay.visible)
            store.toursQuiet = true
            verify(!overlay.visible)
            verify(overlay.running)
            store.toursQuiet = false
            verify(overlay.visible)
            compare(overlay.step, 0)
        }

        function test_theTourOnlyComesByItselfOnce() {
            overlay.startWhen = true
            tryVerify(() => overlay.running)
            overlay.skip()
            compare(store.seen, ["devices"])

            overlay.startWhen = false
            overlay.startWhen = true
            wait(20)
            verify(!overlay.running)
        }

        function test_noTourStartsWhileAModalIsOpen() {
            store.toursQuiet = true
            overlay.startWhen = true
            wait(20)
            verify(!overlay.running)

            store.toursQuiet = false
            overlay.startWhen = false
            overlay.startWhen = true
            tryVerify(() => overlay.running)
        }

        function test_theTourMeasuresFromWhatTheTurnLeftBehind() {
            overlay.startWhen = true
            test.placed = 3
            tryVerify(() => overlay.running)
            compare(test.placedAtStart, 3)
        }

        function test_aHiddenPageKeepsItsTourForLater() {
            loader.visible = false
            overlay.startWhen = true
            wait(50)
            verify(!overlay.running)
            compare(store.seen, [])

            loader.visible = true
            tryVerify(() => overlay.running)
            compare(overlay.step, 0)
            tryVerify(() => overlay.hole.width === 160)
        }

        function test_leavingThePageMidwayDoesNotUseUpTheTour() {
            overlay.startWhen = true
            tryVerify(() => overlay.running)
            loader.visible = false
            verify(!overlay.running)
            compare(store.seen, [])

            loader.visible = true
            tryVerify(() => overlay.running)
            compare(overlay.step, 0)
        }

        function test_aTargetThatShowsUpLateIsWaitedFor() {
            other.visible = false
            overlay.steps = [
                { target: other, text: "Late place" },
                { target: spot, text: "Next place" }
            ]
            overlay.start()
            wait(200)
            compare(overlay.step, 0)
            verify(overlay.missing)

            other.visible = true
            tryVerify(() => overlay.hole.x === 420 && overlay.hole.width === 220)
            compare(overlay.step, 0)
        }

        function test_aModalPausesTheWatchdog() {
            other.visible = false
            overlay.steps = [
                { target: other, text: "Late place" },
                { target: spot, text: "Next place" }
            ]
            overlay.start()
            store.toursQuiet = true
            wait(1300)
            compare(overlay.step, 0)
            verify(overlay.running)
        }

        function test_doingTheLastStepAsThePageGoesAwayCounts() {
            overlay.steps = [{ target: spot, text: "Open the editor" }]
            overlay.start()
            test.actionDone = true
            overlay.cancel()
            verify(!overlay.running)
            compare(store.seen, ["devices"])
        }

        function test_escapeLeavesAHiddenTourAlone() {
            overlay.start()
            store.toursQuiet = true
            keyClick(Qt.Key_Escape)
            verify(overlay.running)
            compare(store.seen, [])
        }

        function test_theCardShowsTheTitleTheTextAndTheShortcut() {
            overlay.steps = [
                { target: spot, title: "Key mapping", text: "Turns the keys on and off", keys: ["Ctrl+K"] },
                { target: other, title: "", text: "Plain sentence" }
            ]
            overlay.start()
            compare(findChild(overlay, "title").text, "Key mapping")
            verify(findChild(overlay, "title").visible)
            compare(findChild(overlay, "sentence").text, "Turns the keys on and off")
            const keys = findChild(overlay, "keys")
            verify(keys.visible)
            compare(overlay.keys, ["Ctrl+K"])
            overlay.forward()
            compare(overlay.step, 1)
            verify(!findChild(overlay, "title").visible)
            verify(!findChild(overlay, "keys").visible)
        }

        function test_nextAndBackWalkThroughTheStepsAndDoneFinishes() {
            overlay.start()
            const next = findChild(overlay, "next")
            const back = findChild(overlay, "previous")
            verify(!back.visible)
            compare(next.text, "Next")
            waitForItemPolished(next.parent)
            mouseClick(next)
            compare(overlay.step, 1)
            verify(back.visible)
            compare(next.text, "Done")
            verify(!findChild(overlay, "skip").visible)
            waitForItemPolished(back.parent)
            mouseClick(back)
            compare(overlay.step, 0)
            waitForItemPolished(next.parent)
            mouseClick(next)
            waitForItemPolished(next.parent)
            mouseClick(next)
            verify(!overlay.running)
            compare(store.seen, ["devices"])
        }

        function test_aCenteredStepWaitsWithoutATarget() {
            overlay.steps = [
                { centered: true, title: "Welcome", text: "This is the page" },
                { target: spot, text: "Now here" }
            ]
            overlay.start()
            wait(1300)
            compare(overlay.step, 0)
            verify(overlay.missing)
            const card = findChild(overlay, "tourCard")
            tryVerify(() => Math.abs(card.x + card.width / 2 - overlay.width / 2) <= 1)
            overlay.forward()
            compare(overlay.step, 1)
            compare(overlay.hole.width, 160)
        }

        function test_comingBackToADoneStepDoesNotJumpAhead() {
            overlay.start()
            test.actionDone = true
            tryCompare(overlay, "step", 1)
            overlay.previous()
            compare(overlay.step, 0)
            wait(50)
            compare(overlay.step, 0)

            test.actionDone = false
            wait(20)
            test.actionDone = true
            tryCompare(overlay, "step", 1)
        }

        function test_doingTheActionAfterArrivingWithNextMovesOn() {
            overlay.steps = [
                { target: spot, text: "Read this" },
                { target: other, text: "Now do it" },
                { target: spot, text: "Done it" }
            ]
            overlay.advance = Qt.binding(function() {
                return test.actionDone && overlay.step === 1
            })
            overlay.start()
            overlay.forward()
            compare(overlay.step, 1)
            wait(20)
            test.actionDone = true
            tryCompare(overlay, "step", 2)
        }

        function test_optionalStepsWithoutTheirPlaceAreLeftOut() {
            other.visible = false
            overlay.steps = [
                { target: spot, text: "First" },
                { target: other, optional: true, text: "Only with the other place" },
                { target: spot, text: "Third" }
            ]
            overlay.start()
            overlay.forward()
            compare(overlay.step, 2)
            compare(findChild(overlay, "sentence").text, "Third")
            overlay.previous()
            compare(overlay.step, 0)

            other.visible = true
            overlay.forward()
            compare(overlay.step, 1)
        }

        function test_withLessMotionTheCardOnlyFades() {
            overlay.start()
            const card = findChild(overlay, "tourCard")
            compare(card.transform[0].y, 0)
            verify(card.opacity < 1)
            tryCompare(card, "opacity", 1)
            compare(card.transform[0].y, 0)
        }
    }
}
