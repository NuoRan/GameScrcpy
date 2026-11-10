pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme

import "Geometry.js" as Geometry

Shape {
    id: wire

    required property string key
    property var info: ({})
    property string shape: ""
    property point start: Qt.point(0, 0)
    property point end: Qt.point(0, 0)
    property bool selected: false
    property bool faded: false
    readonly property var curve: Geometry.wire(start.x, start.y, end.x, end.y, Theme.size.bpWireBend)

    function apply(next: var) {
        const text = JSON.stringify(next)
        if (text !== shape) {
            shape = text
            info = next
        }
    }

    objectName: "bpWire_" + key
    preferredRendererType: Shape.CurveRenderer
    opacity: faded ? Theme.fadedOpacity : 1

    ShapePath {
        strokeColor: wire.selected ? Theme.colors.accent : BlueprintLook.pin(wire.info.type ?? "")
        strokeWidth: wire.selected ? Theme.size.bpWireSelected : Theme.size.bpWire
        strokeStyle: wire.info.dashed === true ? ShapePath.DashLine : ShapePath.SolidLine
        dashPattern: [Theme.space.xxs, Theme.space.xxs]
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        startX: wire.curve.x0
        startY: wire.curve.y0

        PathCubic {
            control1X: wire.curve.c0x
            control1Y: wire.curve.c0y
            control2X: wire.curve.c1x
            control2Y: wire.curve.c1y
            x: wire.curve.x1
            y: wire.curve.y1
        }
    }
}
