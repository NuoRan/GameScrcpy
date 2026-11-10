import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme

T.Menu {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    padding: Theme.space.xs + Theme.space.xxs
    margins: Theme.space.sm

    delegate: MenuItem {}

    contentItem: ListView {
        implicitWidth: {
            let widest = 0
            for (let i = 0; i < control.count; ++i)
                widest = Math.max(widest, control.itemAt(i).implicitWidth)
            return widest
        }
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > control.height : false
        clip: true
        currentIndex: control.currentIndex
        boundsBehavior: Flickable.StopAtBounds
    }

    background: PopupSurface {
        implicitWidth: Theme.size.menuMinWidth
        implicitHeight: Theme.size.menuItem
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.motion.fast
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.standard
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: Theme.motion.fast
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Theme.motion.exit
        }
    }
}
