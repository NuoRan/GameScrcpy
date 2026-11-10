import QtQuick

QtObject {
    property bool dark: true
    property color accent: "#FF6B3D"
    property color accentTextOnLight: "#C2461C"
    property var overrides: ({})

    readonly property color bgBase: overrides.bgBase ?? (dark ? "#0B0C0F" : "#E8EBEF")
    readonly property color bgRaised: overrides.bgRaised ?? (dark ? "#111317" : "#F4F5F8")
    readonly property color bgOverlay: overrides.bgOverlay ?? (dark ? "#171A1F" : "#FFFFFF")
    readonly property color bgElevated: overrides.bgElevated ?? (dark ? "#1D2127" : "#FFFFFF")
    readonly property color bgSunken: overrides.bgSunken ?? (dark ? "#08090B" : "#E7EAEF")
    readonly property color bgFrosted: overrides.bgFrosted ?? Qt.rgba(bgElevated.r, bgElevated.g, bgElevated.b, 0.86)
    readonly property color bgCode: overrides.bgCode ?? (dark ? bgSunken : bgOverlay)
    readonly property color bgEditor: overrides.bgEditor ?? (dark ? bgBase : bgOverlay)

    readonly property color strokeSubtle: overrides.strokeSubtle ?? (dark ? "#0FFFFFFF" : "#17000000")
    readonly property color strokeDefault: overrides.strokeDefault ?? (dark ? "#1AFFFFFF" : "#1F000000")
    readonly property color strokeStrong: overrides.strokeStrong ?? (dark ? "#29FFFFFF" : "#29000000")

    readonly property color stateHover: overrides.stateHover ?? (dark ? "#0AFFFFFF" : "#09000000")
    readonly property color statePress: overrides.statePress ?? (dark ? "#14FFFFFF" : "#12000000")
    readonly property color stateSelected: overrides.stateSelected ?? (dark ? "#12FFFFFF" : "#0F000000")
    readonly property color highlight: overrides.highlight ?? (dark ? "#0BFFFFFF" : "#E6FFFFFF")
    readonly property color knob: overrides.knob ?? (dark ? "#EDEFF3" : "#FFFFFF")

    readonly property color textPrimary: overrides.textPrimary ?? (dark ? "#EDEFF3" : "#111318")
    readonly property color textSecondary: overrides.textSecondary ?? (dark ? "#A3A9B6" : "#474E5C")
    readonly property color textTertiary: overrides.textTertiary ?? (dark ? "#7F8693" : "#5E6572")
    readonly property color textDisabled: overrides.textDisabled ?? (dark ? "#4A505A" : "#B4BAC4")
    readonly property color textOnAccent: overrides.textOnAccent ?? "#0B0C0F"

    readonly property color accentSubtle: overrides.accentSubtle ?? Qt.rgba(accent.r, accent.g, accent.b, 0.14)
    readonly property color accentLine: overrides.accentLine ?? Qt.rgba(accent.r, accent.g, accent.b, 0.45)
    readonly property color accentText: overrides.accentText ?? (dark ? accent : accentTextOnLight)
    readonly property color accentGlow: overrides.accentGlow ?? Qt.rgba(accent.r, accent.g, accent.b, 0.07)
    readonly property color scrim: overrides.scrim ?? (dark ? "#99000000" : "#59000000")
    readonly property color canvasDim: overrides.canvasDim ?? "#80000000"

    readonly property color success: overrides.success ?? "#3DDC97"
    readonly property color warning: overrides.warning ?? "#FFB020"
    readonly property color danger: overrides.danger ?? "#FF5A5F"
    readonly property color info: overrides.info ?? "#4C8DFF"
    readonly property color successText: overrides.successText ?? (dark ? success : "#12805C")
    readonly property color warningText: overrides.warningText ?? (dark ? warning : "#A15C00")
    readonly property color dangerText: overrides.dangerText ?? (dark ? danger : "#C8323A")
    readonly property color infoText: overrides.infoText ?? (dark ? info : "#2F6BD9")
    readonly property color successSubtle: overrides.successSubtle ?? Qt.rgba(success.r, success.g, success.b, 0.12)
    readonly property color warningSubtle: overrides.warningSubtle ?? Qt.rgba(warning.r, warning.g, warning.b, 0.13)
    readonly property color dangerSubtle: overrides.dangerSubtle ?? Qt.rgba(danger.r, danger.g, danger.b, 0.12)
    readonly property color infoSubtle: overrides.infoSubtle ?? Qt.rgba(info.r, info.g, info.b, 0.12)
    readonly property color dangerLine: overrides.dangerLine ?? Qt.rgba(danger.r, danger.g, danger.b, 0.28)
    readonly property color warningLine: overrides.warningLine ?? Qt.rgba(warning.r, warning.g, warning.b, 0.6)
    readonly property color accentRing: overrides.accentRing ?? Qt.rgba(accent.r, accent.g, accent.b, 0.25)
    readonly property color accentEdge: overrides.accentEdge ?? Qt.rgba(accent.r, accent.g, accent.b, 0.55)
    readonly property color accentHalo: overrides.accentHalo ?? Qt.rgba(accent.r, accent.g, accent.b, 0.18)
    readonly property color accentClear: overrides.accentClear ?? Qt.rgba(accent.r, accent.g, accent.b, 0)
    readonly property color keyHintFill: overrides.keyHintFill ?? "#A80B0C0F"
    readonly property color keyHintStroke: overrides.keyHintStroke ?? "#38FFFFFF"
    readonly property color keyHintText: overrides.keyHintText ?? "#FFFFFF"
    readonly property color keyHintRing: overrides.keyHintRing ?? "#47FFFFFF"
    readonly property color keyHintRingFill: overrides.keyHintRingFill ?? "#240B0C0F"
    readonly property color grip: overrides.grip ?? "#80FFFFFF"

    readonly property color bpCanvas: overrides.bpCanvas ?? (dark ? "#0C0E11" : "#ECEEF2")
    readonly property color bpGrid: overrides.bpGrid ?? (dark ? "#13FFFFFF" : "#24000000")
    readonly property color bpNode: overrides.bpNode ?? (dark ? "#F71B1F25" : "#FFFFFF")
    readonly property color bpNodeStroke: overrides.bpNodeStroke ?? (dark ? "#29FFFFFF" : "#29000000")
    readonly property color bpChip: overrides.bpChip ?? (dark ? "#59000000" : "#0A000000")
    readonly property color bpEventTint: overrides.bpEventTint ?? (dark ? "#33FF5A5F" : "#26FF5A5F")
    readonly property color bpEventInk: overrides.bpEventInk ?? (dark ? "#FF8A8E" : "#C8323A")
    readonly property color bpActionTint: overrides.bpActionTint ?? (dark ? "#334C8DFF" : "#1F4C8DFF")
    readonly property color bpActionInk: overrides.bpActionInk ?? (dark ? "#6EA2FF" : "#2F6BD9")
    readonly property color bpWaitTint: overrides.bpWaitTint ?? (dark ? "#2EFFB020" : "#2EFFB020")
    readonly property color bpWaitInk: overrides.bpWaitInk ?? (dark ? "#FFC560" : "#A15C00")
    readonly property color bpVisionTint: overrides.bpVisionTint ?? (dark ? "#24B8F34A" : "#40B8F34A")
    readonly property color bpVisionInk: overrides.bpVisionInk ?? (dark ? "#C9F57A" : "#4D7A0A")
    readonly property color bpFlowTint: overrides.bpFlowTint ?? (dark ? "#0FFFFFFF" : "#0A000000")
    readonly property color bpFlowInk: overrides.bpFlowInk ?? (dark ? "#C9CED6" : "#474E5C")
    readonly property color bpCustomTint: overrides.bpCustomTint ?? (dark ? "#2922D3C5" : "#2622D3C5")
    readonly property color bpCustomInk: overrides.bpCustomInk ?? (dark ? "#5FE3D7" : "#0E7C74")
    readonly property color bpCodeTint: overrides.bpCodeTint ?? (dark ? "#26FF4D8D" : "#1FFF4D8D")
    readonly property color bpCodeInk: overrides.bpCodeInk ?? (dark ? "#FF7FAE" : "#C21F5B")
    readonly property color bpExec: overrides.bpExec ?? (dark ? "#EDEFF3" : "#474E5C")
    readonly property color bpBool: overrides.bpBool ?? (dark ? "#FF5A5F" : "#C8323A")
    readonly property color bpNumber: overrides.bpNumber ?? (dark ? "#3DDC97" : "#12805C")
    readonly property color bpText: overrides.bpText ?? (dark ? "#FF4D8D" : "#BE1E78")
    readonly property color bpPoint: overrides.bpPoint ?? (dark ? "#FFB020" : "#A15C00")
    readonly property color bpRegion: overrides.bpRegion ?? (dark ? "#A78BFA" : "#6D4FD1")
    readonly property color bpImage: overrides.bpImage ?? (dark ? "#4C8DFF" : "#2F6BD9")
    readonly property color bpColor: overrides.bpColor ?? (dark ? "#B8F34A" : "#4D7A0A")
    readonly property color bpComponent: overrides.bpComponent ?? (dark ? "#FF8A63" : "#C2461C")
    readonly property color bpKey: overrides.bpKey ?? (dark ? "#22D3C5" : "#0E7C74")
    readonly property color bpDuration: overrides.bpDuration ?? (dark ? "#7DD3FC" : "#0B6A99")
    readonly property color bpList: overrides.bpList ?? (dark ? "#FF7FAE" : "#B23C7D")
    readonly property color bpAny: overrides.bpAny ?? (dark ? "#A3A9B6" : "#5E6572")
    readonly property color bpTransition: overrides.bpTransition ?? (dark ? "#A3A9B6" : "#5E6572")
    readonly property color bpComment: overrides.bpComment ?? (dark ? "#06FFFFFF" : "#80FFFFFF")
    readonly property color bpCommentStroke: overrides.bpCommentStroke ?? (dark ? "#1AFFFFFF" : "#1F000000")
    readonly property color bpCommentBlue: overrides.bpCommentBlue ?? (dark ? "#0F4C8DFF" : "#144C8DFF")
    readonly property color bpCommentBlueStroke: overrides.bpCommentBlueStroke ?? (dark ? "#594C8DFF" : "#732F6BD9")
    readonly property color bpCommentGreen: overrides.bpCommentGreen ?? (dark ? "#0F3DDC97" : "#143DDC97")
    readonly property color bpCommentGreenStroke: overrides.bpCommentGreenStroke ?? (dark ? "#593DDC97" : "#7312805C")
    readonly property color bpCommentAmber: overrides.bpCommentAmber ?? (dark ? "#0FFFB020" : "#1AFFB020")
    readonly property color bpCommentAmberStroke: overrides.bpCommentAmberStroke ?? (dark ? "#59FFB020" : "#73A15C00")
    readonly property color bpCommentRed: overrides.bpCommentRed ?? (dark ? "#0FFF5A5F" : "#14FF5A5F")
    readonly property color bpCommentRedStroke: overrides.bpCommentRedStroke ?? (dark ? "#59FF5A5F" : "#73C8323A")
    readonly property color bpCommentPurple: overrides.bpCommentPurple ?? (dark ? "#0FA78BFA" : "#14A78BFA")
    readonly property color bpCommentPurpleStroke: overrides.bpCommentPurpleStroke ?? (dark ? "#59A78BFA" : "#736D4FD1")
    readonly property color bpBubble: overrides.bpBubble ?? (dark ? "#1D2127" : "#FFFFFF")
    readonly property color bpBubbleStroke: overrides.bpBubbleStroke ?? (dark ? "#29FFFFFF" : "#29000000")
    readonly property color bpLabel: overrides.bpLabel ?? (dark ? "#D10B0C0F" : "#EBFFFFFF")
}
