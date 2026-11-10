pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme

import "Geometry.js" as Geometry

Rectangle {
    id: map

    property var model: null
    property var itemOf: null
    property int generation: 0
    property rect viewRect: Qt.rect(0, 0, 0, 0)
    property var contentBounds: null
    property var heldBounds: null
    readonly property real pad: Theme.space.sm
    readonly property var liveBounds: {
        const view = { x: viewRect.x, y: viewRect.y, width: viewRect.width, height: viewRect.height }
        return contentBounds === null ? view : Geometry.union([view, contentBounds])
    }
    readonly property var bounds: heldBounds ?? liveBounds
    readonly property real ratio: {
        const width = Math.max(1, bounds.width)
        const height = Math.max(1, bounds.height)
        return Math.min((map.width - 2 * pad) / width, (map.height - 2 * pad) / height)
    }
    readonly property real originX: (map.width - bounds.width * ratio) / 2
    readonly property real originY: (map.height - bounds.height * ratio) / 2

    signal panTo(real x, real y)
    signal panDone()

    function toCanvas(x: real, y: real): point {
        return Qt.point(bounds.x + (x - originX) / ratio, bounds.y + (y - originY) / ratio)
    }

    function measure() {
        const rects = []
        const count = map.model ? map.model.count : 0
        const lookup = map.itemOf
        for (let index = 0; index < count; ++index) {
            const item = typeof lookup === "function" ? lookup(map.model.get(index).key, map.generation) : null
            if (item)
                rects.push({ x: item.x, y: item.y, width: item.width, height: item.height })
        }
        map.contentBounds = rects.length > 0 ? Geometry.union(rects) : null
    }

    function remeasure() {
        Qt.callLater(map.measure)
    }

    objectName: "bpMinimapView"
    width: Theme.size.bpMinimapWidth
    height: Theme.size.bpMinimapHeight
    radius: Theme.radius.md
    color: Theme.reducedTransparency ? Theme.colors.bgElevated : Theme.colors.bgFrosted
    border.width: Theme.size.stroke
    border.color: Theme.colors.strokeStrong
    clip: true
    onGenerationChanged: remeasure()
    Component.onCompleted: measure()

    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Minimap")

    Item {
        x: map.originX - map.bounds.x * map.ratio
        y: map.originY - map.bounds.y * map.ratio
        scale: map.ratio
        transformOrigin: Item.TopLeft

        Repeater {
            model: map.model

            delegate: Rectangle {
                id: mark

                required property string key
                readonly property var item: {
                    const lookup = map.itemOf
                    return typeof lookup === "function" ? lookup(key, map.generation) : null
                }

                visible: item !== null
                x: item ? item.x : 0
                y: item ? item.y : 0
                width: item ? item.width : 0
                height: item ? item.height : 0
                radius: Theme.radius.lg
                color: Theme.colors.textTertiary
                onXChanged: map.remeasure()
                onYChanged: map.remeasure()
                onWidthChanged: map.remeasure()
                onHeightChanged: map.remeasure()
            }
        }
    }

    Rectangle {
        objectName: "bpMinimapViewport"
        x: map.originX + (map.viewRect.x - map.bounds.x) * map.ratio
        y: map.originY + (map.viewRect.y - map.bounds.y) * map.ratio
        width: map.viewRect.width * map.ratio
        height: map.viewRect.height * map.ratio
        color: Theme.colors.accentSubtle
        border.width: Theme.size.stroke
        border.color: Theme.colors.accentLine
    }

    MouseArea {
        id: area

        anchors.fill: parent
        preventStealing: true
        cursorShape: Qt.PointingHandCursor
        onPressed: mouse => {
            map.heldBounds = map.liveBounds
            const at = map.toCanvas(mouse.x, mouse.y)
            map.panTo(at.x, at.y)
        }
        onPositionChanged: mouse => {
            if (!area.pressed)
                return
            const at = map.toCanvas(mouse.x, mouse.y)
            map.panTo(at.x, at.y)
        }
        onReleased: {
            map.heldBounds = null
            map.panDone()
        }
        onCanceled: map.heldBounds = null
    }
}
