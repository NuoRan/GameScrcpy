import QtQuick
import GameScrcpy.Theme

Item {
    id: host

    property int currentIndex: 0
    default property alias pages: stack.data

    readonly property Item current: currentIndex >= 0 && currentIndex < stack.children.length
                                    ? stack.children[currentIndex] : null
    property Item shown: null

    function show(page: Item) {
        if (transition.running)
            transition.complete()
        if (page === null || page === shown)
            return
        if (shown === null) {
            page.opacity = 1
            page.y = 0
            page.visible = true
            shown = page
            return
        }
        transition.from = shown
        transition.to = page
        shown = page
        transition.start()
    }

    onCurrentChanged: if (shown !== null) show(current)

    Component.onCompleted: {
        for (const page of stack.children) {
            page.x = 0
            page.width = Qt.binding(() => stack.width)
            page.height = Qt.binding(() => stack.height)
            page.opacity = 0
            page.visible = false
        }
        show(current)
    }

    Item {
        id: stack

        anchors.fill: parent
    }

    SequentialAnimation {
        id: transition

        property Item from: null
        property Item to: null

        NumberAnimation {
            target: transition.from
            property: "opacity"
            to: 0
            duration: Theme.motion.fast
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.exit
        }
        PropertyAction {
            target: transition.from
            property: "visible"
            value: false
        }
        PropertyAction {
            target: transition.to
            property: "visible"
            value: true
        }
        ParallelAnimation {
            NumberAnimation {
                target: transition.to
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.base
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
            NumberAnimation {
                target: transition.to
                property: "y"
                from: Theme.space.sm
                to: 0
                duration: Theme.motion.base
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
        }
    }
}
