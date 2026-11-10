import QtQuick
import QtTest

Item {
    id: root

    width: 640
    height: 480

    TestCase {
        id: testCase

        property var hintLayer: null

        name: "INP-012"
        when: windowShown

        function init() {
            const component = Qt.createComponent(Qt.resolvedUrl("../../../src/app/qml/KeyHintLayer.qml"))
            compare(component.status, Component.Ready, component.errorString())
            testCase.hintLayer = component.createObject(root, { width: root.width, height: root.height, shown: true,
                                                                restingOpacity: 0.3, intro: false })
            verify(testCase.hintLayer !== null)
        }

        function cleanup() {
            testCase.hintLayer.destroy()
            testCase.hintLayer = null
        }

        function test_revealedHintsAreDrawnAtFullOpacity() {
            tryCompare(testCase.hintLayer, "opacity", 0.3)
            testCase.hintLayer.revealed = true
            tryCompare(testCase.hintLayer, "opacity", 1)
            testCase.hintLayer.revealed = false
            tryCompare(testCase.hintLayer, "opacity", 0.3)
        }

        function test_revealDoesNotShowHintsThatAreHidden() {
            testCase.hintLayer.revealed = true
            tryCompare(testCase.hintLayer, "opacity", 1)
            testCase.hintLayer.shown = false
            tryCompare(testCase.hintLayer, "opacity", 0)
            verify(!testCase.hintLayer.visible)
        }
    }
}
