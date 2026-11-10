import QtQuick
import QtQuick.Shapes
import GameScrcpy.Theme

Shape {
    id: glyph

    property bool filled: false
    property color color: Theme.colors.bpExec
    readonly property real inset: Theme.size.stroke / 2

    width: Theme.size.bpExecWidth
    height: Theme.size.bpExecHeight
    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        strokeColor: glyph.color
        strokeWidth: Theme.size.stroke
        fillColor: glyph.filled ? glyph.color : "transparent"
        joinStyle: ShapePath.RoundJoin
        startX: glyph.inset
        startY: glyph.inset

        PathLine {
            x: glyph.width - glyph.inset
            y: glyph.height / 2
        }
        PathLine {
            x: glyph.inset
            y: glyph.height - glyph.inset
        }
        PathLine {
            x: glyph.inset
            y: glyph.inset
        }
    }
}
