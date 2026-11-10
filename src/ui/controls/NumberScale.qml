import QtQuick

QtObject {
    property real minimum: 0
    property real maximum: 100
    property real from: minimum
    property real to: maximum
    property real step: 1
    property int decimals: 0
    property bool logarithmic: false
    property int logSteps: 40
    property bool exact: false

    function bounded(value: real, low: real, high: real): real {
        return Math.min(Math.max(value, low), high)
    }

    function rounded(value: real): real {
        return exact ? Number(value.toPrecision(12)) : Number(value.toFixed(decimals))
    }

    function snapped(value: real): real {
        if (!logarithmic)
            return rounded(from + Math.round((value - from) / step) * step)
        if (value <= 0)
            return rounded(value)
        const unit = Math.pow(10, Math.floor(Math.log10(value)) - 1)
        return rounded(Math.round(value / unit) * unit)
    }

    function position(value: real): real {
        if (to <= from)
            return 0
        const inside = bounded(value, from, to)
        if (logarithmic)
            return (Math.log(inside) - Math.log(from)) / (Math.log(to) - Math.log(from))
        return (inside - from) / (to - from)
    }

    function valueAt(position: real): real {
        const at = bounded(position, 0, 1)
        const raw = logarithmic ? Math.exp(Math.log(from) + at * (Math.log(to) - Math.log(from)))
                                : from + at * (to - from)
        return bounded(snapped(raw), from, to)
    }

    function stepped(value: real, direction: int): real {
        let next = rounded(value + direction * step)
        if (logarithmic) {
            next = snapped(value * Math.pow(to / from, direction / logSteps))
            if (next === rounded(value))
                next = rounded(value + direction * Math.pow(10, -decimals))
        }
        return bounded(next, minimum, maximum)
    }

    function parse(text: string): real {
        const typed = text.trim()
            .replace(/[\uFF10-\uFF19]/g, digit => String.fromCharCode(digit.charCodeAt(0) - 0xFEE0))
            .replace(/[,\uFF0C\uFF0E\u3002]/g, ".")
        const number = typed === "" ? NaN : Number(typed)
        return Number.isFinite(number) ? rounded(number) : NaN
    }

    function accepts(value: real): bool {
        return value >= minimum && value <= maximum
    }

    function text(value: real): string {
        if (!Number.isFinite(value))
            return ""
        return exact ? String(Number(value.toPrecision(12))) : value.toFixed(decimals)
    }
}
