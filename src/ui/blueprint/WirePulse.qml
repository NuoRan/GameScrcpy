pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme

Item {
    id: flow

    property var curve: ({ x0: 0, y0: 0, c0x: 0, c0y: 0, c1x: 0, c1y: 0, x1: 0, y1: 0 })
    property real progress: 0
    property real fade: 0
    readonly property bool running: run.running
    readonly property real lag: 0.12

    function play(next: var) {
        curve = next
        run.restart()
    }

    visible: fade > 0

    SequentialAnimation {
        id: run

        PropertyAction {
            target: flow
            property: "fade"
            value: 1
        }
        NumberAnimation {
            target: flow
            property: "progress"
            from: 0
            to: 1 + 2 * flow.lag
            duration: Theme.motion.slow
            easing.type: Easing.InOutQuad
        }
        NumberAnimation {
            target: flow
            property: "fade"
            to: 0
            duration: Theme.motion.pulse
            easing.type: Easing.OutCubic
        }
    }

    Path {
        id: track

        startX: flow.curve.x0
        startY: flow.curve.y0

        PathCubic {
            control1X: flow.curve.c0x
            control1Y: flow.curve.c0y
            control2X: flow.curve.c1x
            control2Y: flow.curve.c1y
            x: flow.curve.x1
            y: flow.curve.y1
        }
    }

    Shape {
        opacity: flow.fade
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.colors.accent
            strokeWidth: Theme.size.bpWire
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: flow.curve.x0
            startY: flow.curve.y0

            PathCubic {
                control1X: flow.curve.c0x
                control1Y: flow.curve.c0y
                control2X: flow.curve.c1x
                control2Y: flow.curve.c1y
                x: flow.curve.x1
                y: flow.curve.y1
            }
        }
    }

    Repeater {
        model: 3

        delegate: Rectangle {
            id: dot

            required property int index
            readonly property real at: Math.max(0, Math.min(1, flow.progress - index * flow.lag))

            x: along.x - width / 2
            y: along.y - height / 2
            width: Theme.size.bpFlowDot + 2 * Theme.size.fieldGlow
            height: width
            radius: width / 2
            visible: flow.progress > dot.index * flow.lag
            opacity: flow.fade
            color: Theme.colors.accentRing

            PathInterpolator {
                id: along

                path: track
                progress: dot.at
            }

            Rectangle {
                anchors.centerIn: parent
                width: Theme.size.bpFlowDot
                height: width
                radius: width / 2
                color: Theme.colors.accent
            }
        }
    }
}
