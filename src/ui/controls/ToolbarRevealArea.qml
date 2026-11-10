import QtQuick
import GameScrcpy.Theme

Item {
    id: area

    required property FloatingToolbar toolbar
    property bool active: true
    property int zoneTop: 0
    property int zoneHeight: Theme.size.toolbarReveal
    property int dwell: Theme.motion.toolbarDwell
    property int cooldown: Theme.motion.toolbarCooldown
    readonly property bool hoveringTop: zoneHover.hovered
    readonly property bool holding: presses.active
    readonly property bool cooling: cooldownTimer.running

    Binding {
        target: area.toolbar
        property: "blocked"
        value: area.holding || area.cooling
    }

    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        y: area.zoneTop
        height: area.zoneHeight

        HoverHandler {
            id: zoneHover

            property point last: Qt.point(-1, -1)

            enabled: area.active
            onHoveredChanged: {
                if (hovered)
                    return
                last = Qt.point(-1, -1)
                dwellTimer.stop()
            }
            onPointChanged: {
                const position = point.position
                if (!hovered || (position.x === last.x && position.y === last.y))
                    return
                last = position
                if (!area.holding && !area.cooling)
                    dwellTimer.restart()
            }
        }
    }

    Rectangle {
        id: grip

        objectName: "grip"
        anchors.horizontalCenter: parent.horizontalCenter
        y: area.zoneTop + Theme.space.xs + Theme.space.xxs
        width: Theme.size.gripWidth
        height: Theme.size.gripHeight
        radius: height / 2
        color: Theme.colors.grip
        opacity: zoneHover.hovered && area.active && !area.holding && !area.toolbar.shown ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motion.fast
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
        }
    }

    Item {
        anchors.centerIn: grip
        width: Theme.size.gripTargetWidth
        height: Theme.size.gripTargetHeight

        HoverHandler {
            enabled: area.active
            onHoveredChanged: {
                if (hovered && !area.holding && !area.cooling)
                    area.toolbar.reveal()
            }
        }
    }

    PointHandler {
        id: presses

        acceptedButtons: Qt.AllButtons
        onActiveChanged: {
            if (active) {
                dwellTimer.stop()
                area.toolbar.conceal()
            } else {
                cooldownTimer.restart()
            }
        }
    }

    Timer {
        id: dwellTimer

        interval: area.dwell
        onTriggered: {
            if (area.active && zoneHover.hovered && !area.holding && !area.cooling)
                area.toolbar.reveal()
        }
    }

    Timer {
        id: cooldownTimer

        interval: area.cooldown
    }
}
