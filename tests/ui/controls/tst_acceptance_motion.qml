import QtQuick
import QtTest
import GameScrcpy.Theme
import GameScrcpy.Controls

Item {
    id: root

    width: 640
    height: 480

    StatusCapsule {
        id: capsule

        x: Theme.space.lg
        y: Theme.space.lg
        cameraLocked: true
    }

    TestCase {
        name: "UI-010"
        when: windowShown

        function cleanup() {
            Theme.reducedMotion = false
            Theme.reducedTransparency = false
        }

        function test_reduceMotionTurnsEveryAnimationDurationToZero() {
            verify(Theme.motion.fast > 0)
            verify(Theme.motion.base > 0)
            verify(Theme.motion.medium > 0)
            verify(Theme.motion.slow > 0)
            Theme.reducedMotion = true
            compare(Theme.motion.fast, 0)
            compare(Theme.motion.base, 0)
            compare(Theme.motion.medium, 0)
            compare(Theme.motion.slow, 0)
            Theme.reducedMotion = false
            verify(Theme.motion.base > 0)
        }

        function test_reduceTransparencyDrawsTheCapsuleSolid() {
            verify(capsule.visible)
            verify(Qt.colorEqual(capsule.color, Theme.colors.bgFrosted))
            Theme.reducedTransparency = true
            verify(Qt.colorEqual(capsule.color, Theme.colors.bgElevated))
            Theme.reducedTransparency = false
            verify(Qt.colorEqual(capsule.color, Theme.colors.bgFrosted))
        }
    }
}
