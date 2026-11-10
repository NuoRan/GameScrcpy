pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme

Item {
    id: drawing

    property var node: null
    readonly property real logicalWidth: Math.max(1, drawing.node?.width ?? 1)
    readonly property real logicalHeight: Math.max(1, drawing.node?.height ?? 1)
    readonly property real fit: drawing.width > 0 ? Math.min(1, drawing.width / drawing.logicalWidth) : 1
    readonly property string title: drawing.node?.title ?? ""

    Text {
        id: fontProbe

        visible: false
        textFormat: Text.PlainText
    }

    function colorOf(name: string): var {
        if (name === "")
            return null
        if (name.startsWith("#"))
            return name
        if (name === "accent")
            return Theme.colors.accent
        return Theme.colors[name] ?? Theme.colors.textPrimary
    }

    function sizeOf(look: string): int {
        if (look === "heading")
            return Theme.type.heading.size
        if (look === "caption")
            return Theme.type.caption.size
        return Theme.type.body.size
    }

    function paintShape(ctx: var, shape: var) {
        const filled = drawing.colorOf(shape.fill ?? "")
        let stroked = drawing.colorOf(shape.stroke ?? "")
        if (shape.kind === "text") {
            ctx.font = drawing.sizeOf(shape.size ?? "body") + "px \"" + fontProbe.font.family + "\""
            ctx.textBaseline = "top"
            ctx.fillStyle = drawing.colorOf(shape.color ?? "") ?? Theme.colors.textPrimary
            ctx.fillText(shape.text ?? "", shape.x ?? 0, shape.y ?? 0)
            return
        }
        if (filled === null && stroked === null)
            stroked = Theme.colors.textPrimary
        const thickness = shape.strokeWidth ?? 1
        if (thickness > 0)
            ctx.lineWidth = thickness
        ctx.beginPath()
        switch (shape.kind) {
        case "rect": {
            const corner = Math.min(shape.radius ?? 0, (shape.width ?? 0) / 2, (shape.height ?? 0) / 2)
            if (corner > 0)
                ctx.roundedRect(shape.x ?? 0, shape.y ?? 0, shape.width ?? 0, shape.height ?? 0, corner, corner)
            else
                ctx.rect(shape.x ?? 0, shape.y ?? 0, shape.width ?? 0, shape.height ?? 0)
            break
        }
        case "circle":
            ctx.arc(shape.x ?? 0, shape.y ?? 0, shape.radius ?? 0, 0, 2 * Math.PI)
            break
        case "line":
            ctx.moveTo(shape.x1 ?? 0, shape.y1 ?? 0)
            ctx.lineTo(shape.x2 ?? 0, shape.y2 ?? 0)
            break
        case "polyline": {
            const points = shape.points ?? []
            for (let at = 0; at + 1 < points.length; at += 2) {
                if (at === 0)
                    ctx.moveTo(points[at], points[at + 1])
                else
                    ctx.lineTo(points[at], points[at + 1])
            }
            if (shape.closed === true)
                ctx.closePath()
            break
        }
        default:
            return
        }
        if (filled !== null && shape.kind !== "line") {
            ctx.fillStyle = filled
            ctx.fill()
        }
        if (stroked !== null && thickness > 0) {
            ctx.strokeStyle = stroked
            ctx.stroke()
        }
    }

    implicitWidth: drawing.logicalWidth
    implicitHeight: (heading.visible ? heading.implicitHeight + Theme.space.xs : 0)
                    + drawing.logicalHeight * drawing.fit

    Accessible.role: Accessible.Graphic
    Accessible.name: drawing.title

    onNodeChanged: surface.requestPaint()

    Connections {
        target: Theme

        function onDarkChanged() {
            surface.requestPaint()
        }

        function onAccentNameChanged() {
            surface.requestPaint()
        }

        function onPaletteChanged() {
            surface.requestPaint()
        }
    }

    Text {
        id: heading

        width: drawing.width
        visible: text !== ""
        textFormat: Text.PlainText
        text: drawing.title
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.caption.size
        font.weight: Font.Medium
        elide: Text.ElideRight
    }

    Canvas {
        id: surface

        y: heading.visible ? heading.implicitHeight + Theme.space.xs : 0
        width: drawing.logicalWidth * drawing.fit
        height: drawing.logicalHeight * drawing.fit
        onWidthChanged: surface.requestPaint()
        onHeightChanged: surface.requestPaint()
        onPaint: {
            const ctx = surface.getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, surface.width, surface.height)
            ctx.scale(drawing.fit, drawing.fit)
            for (const shape of drawing.node?.shapes ?? [])
                drawing.paintShape(ctx, shape)
        }
    }
}
