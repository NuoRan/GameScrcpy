import QtQuick
import QtTest
import GameScrcpy.Theme
import GameScrcpy.Controls

Item {
    id: root

    width: 640
    height: 480

    MouseArea {
        id: picture

        property int presses: 0

        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: ++presses
    }

    ToolbarRevealArea {
        id: area

        anchors.fill: parent
        toolbar: toolbar
    }

    FloatingToolbar {
        id: toolbar

        x: (root.width - width) / 2
        y: Theme.space.x5l

        ToolbarButton {
            id: back

            iconName: "arrow-left"
            text: "Back"
        }
        ToolbarButton {
            iconName: "house"
            text: "Home"
        }
    }

    SignalSpy {
        id: backClicks

        target: back
        signalName: "clicked"
    }

    TestCase {
        name: "ToolbarRevealArea"
        when: windowShown

        readonly property int quick: 120

        function moveAway() {
            mouseMove(root, root.width / 2, root.height - 1)
        }

        function hoverTop(x, y) {
            tryVerify(() => {
                mouseMove(root, x, y)
                return area.hoveringTop
            })
        }

        function showToolbar() {
            toolbar.reveal()
            tryCompare(toolbar, "opacity", 1)
        }

        function init() {
            failOnWarning(/.?/)
            area.active = true
            area.dwell = quick
            area.cooldown = quick
            moveAway()
            toolbar.conceal()
            tryVerify(() => !toolbar.visible && !area.cooling && !area.holding)
            picture.presses = 0
            backClicks.clear()
        }

        function test_defaultsComeFromTheTheme() {
            const fresh = createTemporaryQmlObject(
                "import GameScrcpy.Controls\nToolbarRevealArea { toolbar: FloatingToolbar {} }", root)
            compare(fresh.dwell, Theme.motion.toolbarDwell)
            compare(fresh.cooldown, Theme.motion.toolbarCooldown)
            compare(fresh.zoneHeight, Theme.size.toolbarReveal)
        }

        function test_restingAtTheTopRevealsTheToolbar() {
            area.dwell = 300
            hoverTop(100, 10)
            verify(!toolbar.shown)
            tryCompare(toolbar, "shown", true)
        }

        function test_comingBackToTheSameSpotStillCounts() {
            hoverTop(100, 10)
            tryCompare(toolbar, "shown", true)
            moveAway()
            toolbar.conceal()
            tryVerify(() => !toolbar.visible)
            hoverTop(100, 10)
            tryCompare(toolbar, "shown", true)
        }

        function test_movingAlongTheTopKeepsWaiting() {
            area.dwell = 300
            hoverTop(100, 10)
            for (let x = 110; x <= 230; x += 15) {
                mouseMove(root, x, 10)
                wait(40)
            }
            verify(!toolbar.shown)
            tryCompare(toolbar, "shown", true)
        }

        function test_pressAtTheTopGoesToThePictureInstead() {
            area.dwell = 300
            hoverTop(100, 10)
            mousePress(root, 100, 10)
            compare(picture.presses, 1)
            verify(area.holding)
            verify(toolbar.blocked)
            wait(400)
            verify(!toolbar.shown)
            mouseMove(root, 100, 120)
            mouseMove(root, 100, 240)
            mouseRelease(root, 100, 240)
            verify(!area.holding)
            verify(area.cooling)
            verify(!toolbar.shown)
        }

        function test_noRevealRightAfterARelease() {
            area.cooldown = 400
            mouseClick(root, 100, 10)
            verify(area.cooling)
            mouseMove(root, 110, 10)
            mouseMove(root, 120, 10)
            wait(quick * 2)
            verify(!toolbar.shown)
            tryVerify(() => !area.cooling)
            hoverTop(130, 10)
            tryCompare(toolbar, "shown", true)
        }

        function test_pressOnThePictureHidesTheShownToolbar() {
            showToolbar()
            mouseClick(root, 100, 300)
            compare(picture.presses, 1)
            tryCompare(toolbar, "shown", false)
        }

        function test_buttonsKeepTheirClicks() {
            showToolbar()
            mouseClick(back)
            compare(backClicks.count, 1)
            compare(picture.presses, 0)
            verify(toolbar.shown)
            verify(!area.cooling)
        }

        function test_gapsPassThePressToThePicture() {
            showToolbar()
            mouseClick(toolbar, Theme.space.xxs, toolbar.height / 2)
            compare(picture.presses, 1)
            compare(backClicks.count, 0)
        }

        function test_gripShowsNearTheTopAndRevealsAtOnce() {
            area.dwell = 5000
            const grip = findChild(area, "grip")
            verify(!grip.visible)
            hoverTop(20, 20)
            tryCompare(grip, "opacity", 1)
            verify(!toolbar.shown)
            mouseMove(root, root.width / 2, grip.y + grip.height / 2)
            tryCompare(toolbar, "shown", true)
            tryCompare(grip, "opacity", 0)
        }

        function test_inactiveAreaNeverReveals() {
            area.active = false
            mouseMove(root, 100, 10)
            mouseMove(root, root.width / 2, 8)
            wait(quick * 3)
            verify(!toolbar.shown)
            verify(!findChild(area, "grip").visible)
        }

        function test_holdingKeepsTheToolbarFromComingBack() {
            showToolbar()
            mousePress(root, 100, 300)
            tryCompare(toolbar, "shown", false)
            const middle = Qt.point(toolbar.x + toolbar.width / 2, toolbar.y + toolbar.height / 2)
            mouseMove(root, middle.x, middle.y)
            wait(quick)
            verify(!toolbar.shown)
            mouseRelease(root, middle.x, middle.y)
            verify(!toolbar.shown)
        }
    }
}
