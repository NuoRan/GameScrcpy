pragma Singleton

import QtQuick

QtObject {
    id: theme

    property bool dark: true
    property string accentName: "ember"
    property bool reducedMotion: false
    property bool reducedTransparency: false
    property var palette: ({})

    readonly property var accentChoices: ({
        ember: "#FF6B3D",
        teal: "#22D3C5",
        azure: "#4C8DFF",
        lime: "#B8F34A",
        rose: "#FF4D8D",
        amber: "#FFB020"
    })

    readonly property var accentTextChoices: ({
        ember: "#C2461C",
        teal: "#0E7C74",
        azure: "#2F6BD9",
        lime: "#4D7A0A",
        rose: "#C21F5B",
        amber: "#A15C00"
    })

    readonly property var toneHues: ({
        orange: "ember",
        teal: "teal",
        azure: "azure",
        lime: "lime",
        rose: "rose",
        amber: "amber"
    })

    function toneFill(tone: string): color {
        const hue = accentChoices[toneHues[tone] ?? ""]
        if (hue === undefined)
            return colors.statePress
        const base = Qt.color(hue)
        return Qt.rgba(base.r, base.g, base.b, dark ? 0.16 : 0.12)
    }

    function toneInk(tone: string): color {
        const hue = (dark ? accentChoices : accentTextChoices)[toneHues[tone] ?? ""]
        return hue === undefined ? colors.textSecondary : Qt.color(hue)
    }

    readonly property ColorTokens colors: ColorTokens {
        dark: theme.dark
        accent: theme.accentChoices[theme.accentName] ?? theme.accentChoices.ember
        accentTextOnLight: theme.accentTextChoices[theme.accentName] ?? theme.accentTextChoices.ember
        overrides: (theme.dark ? theme.palette.dark : theme.palette.light) ?? ({})
    }
    readonly property Typography type: Typography {}
    readonly property Size size: Size {}
    readonly property real disabledOpacity: 0.4
    readonly property real fadedOpacity: 0.55
    readonly property Spacing space: Spacing {}
    readonly property Radius radius: Radius {}
    readonly property Shadow shadow: Shadow {
        dark: theme.dark
    }
    readonly property Motion motion: Motion {
        reduced: theme.reducedMotion
    }
}
