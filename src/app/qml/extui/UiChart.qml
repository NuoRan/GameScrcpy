pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme

Item {
    id: chart

    property var node: null
    readonly property var series: chart.node?.series ?? []
    readonly property bool bars: (chart.node?.kind ?? "line") === "bar"
    readonly property real plotHeight: (chart.node?.height ?? 0) > 0 ? chart.node.height : Theme.size.extensionChart
    readonly property string title: chart.node?.title ?? ""
    readonly property bool named: chart.series.some(line => (line.title ?? "") !== "")

    Text {
        id: fontProbe

        visible: false
        textFormat: Text.PlainText
    }

    function colorOf(name: string, index: int): color {
        if (name === "") {
            const palette = [Theme.colors.accent, Theme.colors.info, Theme.colors.success, Theme.colors.warning]
            return palette[index % palette.length]
        }
        if (name.startsWith("#"))
            return name
        if (name === "accent")
            return Theme.colors.accent
        return Theme.colors[name] ?? Theme.colors.textPrimary
    }

    function shortNumber(value: real): string {
        return String(Math.round(value * 100) / 100)
    }

    function paint() {
        const ctx = plot.getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, plot.width, plot.height)
        const lines = chart.series
        let least = Infinity
        let most = -Infinity
        let count = 0
        for (const line of lines) {
            const points = line.points ?? []
            count = Math.max(count, points.length)
            for (const point of points) {
                least = Math.min(least, point)
                most = Math.max(most, point)
            }
        }
        if (!Number.isFinite(least)) {
            least = 0
            most = 1
        }
        if (chart.bars) {
            least = Math.min(least, 0)
            most = Math.max(most, 0)
        }
        const low = chart.node?.min ?? least
        const wanted = chart.node?.max ?? most
        const high = wanted > low ? wanted : low + 1
        const inset = Theme.space.xs
        const yOf = value => inset + (1 - (Math.min(Math.max(value, low), high) - low) / (high - low))
                    * (plot.height - 2 * inset)
        ctx.lineWidth = Theme.size.stroke
        ctx.strokeStyle = Theme.colors.strokeSubtle
        ctx.beginPath()
        ctx.moveTo(0, yOf(high))
        ctx.lineTo(plot.width, yOf(high))
        ctx.moveTo(0, yOf(low))
        ctx.lineTo(plot.width, yOf(low))
        ctx.stroke()
        if (chart.bars) {
            const group = count > 0 ? plot.width / count : plot.width
            const barWidth = lines.length > 0 ? group * 0.8 / lines.length : 0
            const base = yOf(Math.min(Math.max(0, low), high))
            lines.forEach((line, index) => {
                const points = line.points ?? []
                ctx.fillStyle = chart.colorOf(line.color ?? "", index)
                points.forEach((point, at) => {
                    const top = yOf(point)
                    ctx.fillRect(at * group + group * 0.1 + index * barWidth, Math.min(top, base),
                                 Math.max(1, barWidth - 1), Math.max(1, Math.abs(base - top)))
                })
            })
        } else {
            ctx.lineWidth = Theme.size.focusRing
            ctx.lineJoin = "round"
            ctx.lineCap = "round"
            lines.forEach((line, index) => {
                const points = line.points ?? []
                ctx.strokeStyle = chart.colorOf(line.color ?? "", index)
                ctx.beginPath()
                points.forEach((point, at) => {
                    const across = points.length > 1 ? at / (points.length - 1) * plot.width : plot.width / 2
                    if (at === 0)
                        ctx.moveTo(across, yOf(point))
                    else
                        ctx.lineTo(across, yOf(point))
                })
                ctx.stroke()
            })
        }
        ctx.fillStyle = Theme.colors.textTertiary
        ctx.font = Theme.type.caption.size + "px \"" + fontProbe.font.family + "\""
        ctx.textBaseline = "top"
        ctx.fillText(chart.shortNumber(high), Theme.space.xxs, yOf(high) + Theme.space.xxs)
        ctx.textBaseline = "bottom"
        ctx.fillText(chart.shortNumber(low), Theme.space.xxs, yOf(low) - Theme.space.xxs)
    }

    implicitWidth: Theme.size.extensionPanel
    implicitHeight: (heading.visible ? heading.implicitHeight + Theme.space.xs : 0) + chart.plotHeight
                    + (legend.visible ? legend.implicitHeight + Theme.space.xs : 0)

    Accessible.role: Accessible.Chart
    Accessible.name: chart.title

    onNodeChanged: plot.requestPaint()

    Connections {
        target: Theme

        function onDarkChanged() {
            plot.requestPaint()
        }

        function onAccentNameChanged() {
            plot.requestPaint()
        }

        function onPaletteChanged() {
            plot.requestPaint()
        }
    }

    Text {
        id: heading

        width: chart.width
        visible: text !== ""
        textFormat: Text.PlainText
        text: chart.title
        color: Theme.colors.textSecondary
        font.pixelSize: Theme.type.caption.size
        font.weight: Font.Medium
        elide: Text.ElideRight
    }

    Canvas {
        id: plot

        y: heading.visible ? heading.implicitHeight + Theme.space.xs : 0
        width: chart.width
        height: chart.plotHeight
        onWidthChanged: plot.requestPaint()
        onHeightChanged: plot.requestPaint()
        onPaint: chart.paint()
    }

    Flow {
        id: legend

        y: plot.y + plot.height + Theme.space.xs
        width: chart.width
        visible: chart.named
        spacing: Theme.space.md

        Repeater {
            model: chart.series

            delegate: Row {
                id: key

                required property var modelData
                required property int index

                spacing: Theme.space.xs

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.space.sm
                    height: Theme.space.sm
                    radius: Theme.radius.xs
                    color: chart.colorOf(key.modelData.color ?? "", key.index)
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: key.modelData.title ?? ""
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                }
            }
        }
    }
}
