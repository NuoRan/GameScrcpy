.pragma library

function bend(dx, minimum) {
    return Math.max(minimum, Math.abs(dx) / 2)
}

function wire(x0, y0, x1, y1, minimum) {
    const k = bend(x1 - x0, minimum)
    return { x0: x0, y0: y0, c0x: x0 + k, c0y: y0, c1x: x1 - k, c1y: y1, x1: x1, y1: y1 }
}

function straight(x0, y0, x1, y1) {
    return { x0: x0, y0: y0, c0x: x0 + (x1 - x0) / 3, c0y: y0 + (y1 - y0) / 3, c1x: x0 + 2 * (x1 - x0) / 3,
             c1y: y0 + 2 * (y1 - y0) / 3, x1: x1, y1: y1 }
}

function cubicAt(t, a, b, c, d) {
    const u = 1 - t
    return u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d
}

function quadAt(t, a, b, c) {
    const u = 1 - t
    return u * u * a + 2 * u * t * b + t * t * c
}

function distanceToSegment(px, py, ax, ay, bx, by) {
    const dx = bx - ax
    const dy = by - ay
    const length = dx * dx + dy * dy
    const t = length > 0 ? Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / length)) : 0
    const x = ax + t * dx - px
    const y = ay + t * dy - py
    return Math.sqrt(x * x + y * y)
}

function distanceToCubic(px, py, curve, samples) {
    let best = Infinity
    let lastX = curve.x0
    let lastY = curve.y0
    for (let i = 1; i <= samples; ++i) {
        const t = i / samples
        const x = cubicAt(t, curve.x0, curve.c0x, curve.c1x, curve.x1)
        const y = cubicAt(t, curve.y0, curve.c0y, curve.c1y, curve.y1)
        best = Math.min(best, distanceToSegment(px, py, lastX, lastY, x, y))
        lastX = x
        lastY = y
    }
    return best
}

function intersects(a, b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height
}

function encloses(outer, inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width
           && inner.y + inner.height <= outer.y + outer.height
}

function contains(rect, x, y) {
    return x >= rect.x && y >= rect.y && x <= rect.x + rect.width && y <= rect.y + rect.height
}

function union(rects) {
    if (rects.length === 0)
        return null
    let left = Infinity
    let top = Infinity
    let right = -Infinity
    let bottom = -Infinity
    for (const rect of rects) {
        left = Math.min(left, rect.x)
        top = Math.min(top, rect.y)
        right = Math.max(right, rect.x + rect.width)
        bottom = Math.max(bottom, rect.y + rect.height)
    }
    return { x: left, y: top, width: right - left, height: bottom - top }
}

function normalized(x0, y0, x1, y1) {
    return { x: Math.min(x0, x1), y: Math.min(y0, y1), width: Math.abs(x1 - x0), height: Math.abs(y1 - y0) }
}

function borderPoint(rect, tx, ty) {
    const cx = rect.x + rect.width / 2
    const cy = rect.y + rect.height / 2
    const dx = tx - cx
    const dy = ty - cy
    if (dx === 0 && dy === 0)
        return { x: cx, y: cy }
    const sx = dx !== 0 ? rect.width / 2 / Math.abs(dx) : Infinity
    const sy = dy !== 0 ? rect.height / 2 / Math.abs(dy) : Infinity
    const s = Math.min(sx, sy)
    return { x: cx + dx * s, y: cy + dy * s }
}

function arrowHead(tipX, tipY, fromX, fromY, length) {
    const dx = tipX - fromX
    const dy = tipY - fromY
    const size = Math.sqrt(dx * dx + dy * dy) || 1
    const ux = dx / size
    const uy = dy / size
    const baseX = tipX - ux * length
    const baseY = tipY - uy * length
    const half = length * 0.55
    return "M " + tipX + " " + tipY + " L " + (baseX - uy * half) + " " + (baseY + ux * half)
           + " L " + (baseX + uy * half) + " " + (baseY - ux * half) + " Z"
}

function twoDigits(value) {
    return value < 10 ? "0" + value : "" + value
}

function clock(ms) {
    const seconds = Math.max(0, Math.floor(ms / 1000))
    return twoDigits(Math.floor(seconds / 60)) + ":" + twoDigits(seconds % 60)
}

function preciseClock(ms) {
    const total = Math.max(0, Math.floor(ms / 10))
    const seconds = Math.floor(total / 100)
    return twoDigits(Math.floor(seconds / 60)) + ":" + twoDigits(seconds % 60) + "." + twoDigits(total % 100)
}
