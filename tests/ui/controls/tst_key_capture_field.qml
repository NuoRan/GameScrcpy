import QtQuick
import QtTest
import GameScrcpy.Theme
import GameScrcpy.Controls

Item {
    id: root

    width: 400
    height: 300

    KeyCaptureField {
        id: field

        width: 280
    }

    KeyCaptureField {
        id: other

        y: 120
        width: 280
        keys: [{ text: "F" }]
    }

    SignalSpy {
        id: keys

        target: field
        signalName: "keyCaptured"
    }

    SignalSpy {
        id: mice

        target: field
        signalName: "mouseCaptured"
    }

    SignalSpy {
        id: wheels

        target: field
        signalName: "wheelCaptured"
    }

    SignalSpy {
        id: clears

        target: field
        signalName: "cleared"
    }

    TestCase {
        name: "KeyCaptureField"
        when: windowShown

        readonly property real middle: Theme.size.control / 2

        function init() {
            failOnWarning(/.?/)
            field.cancel()
            field.keys = []
            field.clearable = true
            field.conflictText = ""
            for (const spy of [keys, mice, wheels, clears])
                spy.clear()
            root.forceActiveFocus()
        }

        function start() {
            mouseClick(field, Theme.space.x4l, middle)
            verify(field.capturing)
            verify(field.activeFocus)
        }

        function test_aClickStartsWaitingAndAKeyWithModifiersEndsIt() {
            start()
            keyClick(Qt.Key_F, Qt.ControlModifier)
            compare(keys.count, 1)
            compare(keys.signalArguments[0][0], Qt.Key_F)
            compare(keys.signalArguments[0][2] & Qt.ControlModifier, Qt.ControlModifier)
            verify(!field.capturing)
            verify(field.activeFocus)
        }

        function test_escapeAndTabAreKeysWhileWaiting() {
            start()
            keyClick(Qt.Key_Escape)
            compare(keys.count, 1)
            compare(keys.signalArguments[0][0], Qt.Key_Escape)
            start()
            keyClick(Qt.Key_Tab)
            compare(keys.count, 2)
            compare(keys.signalArguments[1][0], Qt.Key_Tab)
            verify(field.activeFocus)
        }

        function test_aModifierOnItsOwnIsTakenWhenReleased() {
            start()
            keyPress(Qt.Key_Shift)
            compare(keys.count, 0)
            verify(field.capturing)
            keyRelease(Qt.Key_Shift)
            compare(keys.count, 1)
            compare(keys.signalArguments[0][0], Qt.Key_Shift)
            compare(keys.signalArguments[0][2], Qt.NoModifier)
        }

        function test_mouseButtonsAndTheWheelInsideTheFieldAreTaken() {
            start()
            mouseClick(field, Theme.space.x4l, middle, Qt.RightButton)
            compare(mice.count, 1)
            compare(mice.signalArguments[0][0], Qt.RightButton)
            verify(!field.capturing)
            start()
            mouseClick(field, Theme.space.x4l, middle)
            compare(mice.count, 2)
            compare(mice.signalArguments[1][0], Qt.LeftButton)
            start()
            mouseWheel(field, Theme.space.x4l, middle, 0, -120)
            compare(wheels.count, 1)
            compare(wheels.signalArguments[0][1], -120)
            verify(!field.capturing)
        }

        function test_aRightClickOrTheWheelDoNothingWhileIdle() {
            mouseClick(field, Theme.space.x4l, middle, Qt.RightButton)
            mouseWheel(field, Theme.space.x4l, middle, 0, 120)
            verify(!field.capturing)
            compare(mice.count, 0)
            compare(wheels.count, 0)
        }

        function test_theCrossOrFocusElsewhereStopsWaiting() {
            start()
            const end = findChild(field, "keyCaptureEnd")
            verify(end.visible)
            mouseClick(end)
            verify(!field.capturing)
            compare(clears.count, 0)
            compare(keys.count, 0)
            compare(mice.count, 0)
            start()
            other.forceActiveFocus()
            verify(!field.capturing)
            compare(keys.count, 0)
        }

        function test_theCrossRemovesABoundKeyUnlessTheFieldCannotBeCleared() {
            field.keys = [{ text: "Ctrl" }, { text: "F" }]
            verify(findChild(field, "keyCaps").visible)
            verify(!findChild(field, "keyPlaceholder").visible)
            const end = findChild(field, "keyCaptureEnd")
            verify(end.visible)
            mouseClick(end)
            compare(clears.count, 1)
            verify(!field.capturing)
            field.clearable = false
            verify(!end.visible)
        }

        function test_keyboardUsersStartWithSpaceAndClearWithBackspace() {
            field.keys = [{ text: "F" }]
            field.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Backspace)
            compare(clears.count, 1)
            keyClick(Qt.Key_Space)
            verify(field.capturing)
            keyClick(Qt.Key_Space)
            compare(keys.count, 1)
            compare(keys.signalArguments[0][0], Qt.Key_Space)
            field.clearable = false
            keyClick(Qt.Key_Delete)
            compare(clears.count, 1)
        }

        function test_aConflictIsWrittenUnderTheField() {
            const before = field.implicitHeight
            field.keys = [{ text: "R" }]
            field.conflictText = "Also bound to “Reload”"
            const message = findChild(field, "keyConflict")
            verify(message.visible)
            verify(field.implicitHeight > before)
            field.conflictText = ""
            verify(!message.visible)
            compare(field.implicitHeight, before)
        }
    }
}
