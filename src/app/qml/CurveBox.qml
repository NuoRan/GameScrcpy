pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme

Rectangle {
    id: box

    property var points: [[0, 0], [1, 1]]
    property var working: null
    readonly property var shownPoints: working !== null ? working : points
    readonly property real inset: Theme.space.sm
    readonly property real plotWidth: Math.max(1, width - 2 * inset)
    readonly property real plotHeight: Math.max(1, height - 2 * inset)
    readonly property int mostPoints: 16
    readonly property real gap: 0.01

    signal edited(var points)

    function clamped(value: real): real {
        return Math.min(1, Math.max(0, value))
    }

    function rounded(value: real): real {
        return Math.round(value * 1000) / 1000
    }

    function pixelOf(point: var): point {
        return Qt.point(inset + point[0] * plotWidth, inset + (1 - point[1]) * plotHeight)
    }

    function copied(list: var): var {
        return list.map(point => [point[0], point[1]])
    }

    function moved(index: int, px: real, py: real): var {
        const next = copied(working !== null ? working : points)
        const last = next.length - 1
        let x = clamped((px - inset) / plotWidth)
        const y = clamped(1 - (py - inset) / plotHeight)
        if (index === 0 || index === last)
            x = next[index][0]
        else
            x = Math.min(next[index + 1][0] - gap, Math.max(next[index - 1][0] + gap, x))
        next[index] = [rounded(x), rounded(y)]
        return next
    }

    function add(px: real, py: real) {
        if (points.length >= mostPoints)
            return
        const near = points.some(point => {
            const at = pixelOf(point)
            return Math.abs(at.x - px) < Theme.size.sliderHandle && Math.abs(at.y - py) < Theme.size.sliderHandle
        })
        if (near)
            return
        const x = rounded(clamped((px - inset) / plotWidth))
        const y = rounded(clamped(1 - (py - inset) / plotHeight))
        const next = copied(points)
        const after = next.findIndex(point => point[0] > x)
        if (after <= 0 || x - next[after - 1][0] < gap || next[after][0] - x < gap)
            return
        next.splice(after, 0, [x, y])
        edited(next)
    }

    function remove(index: int) {
        if (index <= 0 || index >= points.length - 1 || points.length <= 2)
            return
        const next = copied(points)
        next.splice(index, 1)
        edited(next)
    }

    implicitHeight: Theme.size.previewLong
    radius: Theme.radius.md
    color: Theme.colors.bgSunken
    border.width: Theme.size.stroke
    border.color: Theme.colors.strokeDefault
    Accessible.role: Accessible.Graphic

    TapHandler {
        objectName: "curveAdd"
        onDoubleTapped: eventPoint => box.add(eventPoint.position.x, eventPoint.position.y)
    }

    Shape {
        anchors.fill: parent

        ShapePath {
            strokeColor: Theme.colors.strokeSubtle
            strokeWidth: Theme.size.stroke
            fillColor: "transparent"

            PathPolyline {
                path: [box.pixelOf([0, 0]), box.pixelOf([1, 1])]
            }
        }

        ShapePath {
            strokeColor: Theme.colors.accent
            strokeWidth: Theme.size.focusRing
            fillColor: "transparent"
            joinStyle: ShapePath.RoundJoin

            PathPolyline {
                path: box.shownPoints.map(point => box.pixelOf(point))
            }
        }
    }

    Repeater {
        model: box.shownPoints.length

        delegate: Rectangle {
            id: handle

            required property int index
            readonly property var point: index < box.shownPoints.length ? box.shownPoints[index] : [0, 0]
            readonly property point at: box.pixelOf(point)

            objectName: "curvePoint"
            x: at.x - width / 2
            y: at.y - height / 2
            width: Theme.size.sliderHandle
            height: Theme.size.sliderHandle
            radius: width / 2
            color: Theme.colors.knob
            border.width: Theme.size.focusRing
            border.color: drag.active ? Theme.colors.accent : Theme.colors.accentEdge

            DragHandler {
                id: drag

                target: null
                onActiveChanged: {
                    if (active) {
                        box.working = box.copied(box.points)
                        return
                    }
                    const done = box.working
                    box.working = null
                    if (done !== null)
                        box.edited(done)
                }
                onCentroidChanged: {
                    if (!active)
                        return
                    const local = handle.mapToItem(box, centroid.position.x, centroid.position.y)
                    box.working = box.moved(handle.index, local.x, local.y)
                }
            }

            TapHandler {
                onDoubleTapped: box.remove(handle.index)
            }
        }
    }
}
