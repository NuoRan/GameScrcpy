import QtQuick
import QtTest
import GameScrcpy.Theme

TestCase {
    name: "ThemePaletteOverrides"

    readonly property var tokenNames: [
        "bgBase", "bgRaised", "bgOverlay", "bgElevated", "bgSunken", "bgFrosted", "bgCode", "bgEditor",
        "strokeSubtle", "strokeDefault", "strokeStrong",
        "stateHover", "statePress", "stateSelected", "highlight", "knob",
        "textPrimary", "textSecondary", "textTertiary", "textDisabled", "textOnAccent",
        "accentSubtle", "accentLine", "accentText", "accentGlow", "scrim", "canvasDim",
        "success", "warning", "danger", "info",
        "successText", "warningText", "dangerText", "infoText",
        "successSubtle", "warningSubtle", "dangerSubtle", "infoSubtle", "dangerLine", "warningLine",
        "accentRing", "accentEdge", "accentHalo", "accentClear",
        "keyHintFill", "keyHintStroke", "keyHintText", "keyHintRing", "keyHintRingFill", "grip",
        "bpCanvas", "bpGrid", "bpNode", "bpNodeStroke", "bpChip",
        "bpEventTint", "bpEventInk", "bpActionTint", "bpActionInk", "bpWaitTint", "bpWaitInk",
        "bpVisionTint", "bpVisionInk", "bpFlowTint", "bpFlowInk", "bpCustomTint", "bpCustomInk",
        "bpCodeTint", "bpCodeInk",
        "bpExec", "bpBool", "bpNumber", "bpText", "bpPoint", "bpRegion", "bpImage", "bpColor",
        "bpComponent", "bpKey", "bpDuration", "bpList", "bpAny", "bpTransition",
        "bpComment", "bpCommentStroke", "bpCommentBlue", "bpCommentBlueStroke", "bpCommentGreen",
        "bpCommentGreenStroke", "bpCommentAmber", "bpCommentAmberStroke", "bpCommentRed", "bpCommentRedStroke",
        "bpCommentPurple", "bpCommentPurpleStroke",
        "bpBubble", "bpBubbleStroke", "bpLabel"
    ]
    property var savedPalette: ({})
    property bool savedDark: true
    property var darkDefaults: ({})
    property var lightDefaults: ({})

    function init() {
        savedPalette = Theme.palette
        savedDark = Theme.dark
        failOnWarning(/.?/)
        Theme.palette = ({})
        Theme.dark = false
        lightDefaults = snapshot()
        Theme.dark = true
        darkDefaults = snapshot()
    }

    function cleanup() {
        Theme.palette = savedPalette
        Theme.dark = savedDark
    }

    function copyOf(token) {
        return Qt.rgba(token.r, token.g, token.b, token.a)
    }

    function snapshot() {
        const values = {}
        for (const tokenName of tokenNames)
            values[tokenName] = copyOf(Theme.colors[tokenName])
        return values
    }

    function distinctTable() {
        const table = {}
        for (const [index, tokenName] of tokenNames.entries())
            table[tokenName] = "#20" + (index + 16).toString(16) + "40"
        return table
    }

    function verifyTokens(expected, mode) {
        for (const tokenName of tokenNames)
            verify(Qt.colorEqual(Theme.colors[tokenName], expected[tokenName]), `${tokenName} in ${mode} mode`)
    }

    function test_eachModeTakesItsOwnTable() {
        Theme.palette = { dark: { bgBase: "#102030" }, light: { bgBase: "#F0E0D0" } }
        verify(Qt.colorEqual(Theme.colors.bgBase, "#102030"))
        verifyTokens(Object.assign({}, darkDefaults, { bgBase: "#102030", bgEditor: "#102030" }), "dark")
        Theme.dark = false
        verify(Qt.colorEqual(Theme.colors.bgBase, "#F0E0D0"))
        verifyTokens(Object.assign({}, lightDefaults, { bgBase: "#F0E0D0" }), "light")
        Theme.dark = true
        verify(Qt.colorEqual(Theme.colors.bgBase, "#102030"))
    }

    function test_codeSurfacesFollowTheirLayers() {
        verify(Qt.colorEqual(Theme.colors.bgCode, Theme.colors.bgSunken))
        verify(Qt.colorEqual(Theme.colors.bgEditor, Theme.colors.bgBase))
        Theme.dark = false
        verify(Qt.colorEqual(Theme.colors.bgCode, "#FFFFFF"))
        verify(Qt.colorEqual(Theme.colors.bgEditor, "#FFFFFF"))
        verify(!Qt.colorEqual(Theme.colors.bgEditor, Theme.colors.bgBase))
        Theme.palette = { dark: { bgSunken: "#010203" }, light: { bgOverlay: "#FAFBFC" } }
        verify(Qt.colorEqual(Theme.colors.bgCode, "#FAFBFC"))
        verify(Qt.colorEqual(Theme.colors.bgEditor, "#FAFBFC"))
        Theme.dark = true
        verify(Qt.colorEqual(Theme.colors.bgCode, "#010203"))
        verify(Qt.colorEqual(Theme.colors.bgEditor, darkDefaults.bgBase))
        Theme.palette = { dark: { bgCode: "#112233", bgEditor: "#445566" } }
        verifyTokens(Object.assign({}, darkDefaults, { bgCode: "#112233", bgEditor: "#445566" }), "dark")
    }

    function test_frostedFollowsAnOverriddenElevated_data() {
        return [
            { tag: "dark", dark: true },
            { tag: "light", dark: false }
        ]
    }

    function test_frostedFollowsAnOverriddenElevated(data) {
        Theme.dark = data.dark
        const before = copyOf(Theme.colors.bgFrosted)
        const table = { bgElevated: "#204060" }
        Theme.palette = data.dark ? { dark: table } : { light: table }
        verify(Qt.colorEqual(Theme.colors.bgElevated, "#204060"))
        verify(!Qt.colorEqual(Theme.colors.bgFrosted, before))
        fuzzyCompare(Theme.colors.bgFrosted.r, 0x20 / 255, 0.001)
        fuzzyCompare(Theme.colors.bgFrosted.g, 0x40 / 255, 0.001)
        fuzzyCompare(Theme.colors.bgFrosted.b, 0x60 / 255, 0.001)
        fuzzyCompare(Theme.colors.bgFrosted.a, before.a, 0.001)
    }

    function test_frostedCanBeOverriddenOnItsOwn() {
        Theme.palette = { dark: { bgFrosted: "#80112233" } }
        verify(Qt.colorEqual(Theme.colors.bgFrosted, "#80112233"))
        verifyTokens(Object.assign({}, darkDefaults, { bgFrosted: "#80112233" }), "dark")
        Theme.palette = { dark: { bgElevated: "#204060", bgFrosted: "#80112233" } }
        verify(Qt.colorEqual(Theme.colors.bgElevated, "#204060"))
        verify(Qt.colorEqual(Theme.colors.bgFrosted, "#80112233"))
    }

    function test_unknownNamesChangeNoToken() {
        Theme.palette = {
            dark: { bgSurface: "#FF00FF", BgBase: "#FF00FF", background: "#FF00FF" },
            light: { bgSurface: "#00FF00", BgBase: "#00FF00", background: "#00FF00" }
        }
        verifyTokens(darkDefaults, "dark")
        Theme.dark = false
        verifyTokens(lightDefaults, "light")
    }

    function test_accentCannotBeOverridden() {
        const accent = copyOf(Theme.colors.accent)
        Theme.palette = { dark: { accent: "#00FF00" }, light: { accent: "#00FF00" } }
        verify(Qt.colorEqual(Theme.colors.accent, accent))
        verifyTokens(darkDefaults, "dark")
        Theme.dark = false
        verify(Qt.colorEqual(Theme.colors.accent, accent))
        verifyTokens(lightDefaults, "light")
    }

    function test_aDarkOnlyPaletteLeavesTheLightDefaults() {
        const table = distinctTable()
        Theme.palette = { dark: table }
        verifyTokens(table, "dark")
        Theme.dark = false
        verifyTokens(lightDefaults, "light")
        verify(Qt.colorEqual(Theme.colors.bgBase, "#E8EBEF"))
    }

    function test_anEmptyPaletteRestoresTheDefaults() {
        const table = distinctTable()
        Theme.palette = { dark: table, light: table }
        verifyTokens(table, "dark")
        Theme.palette = ({})
        verifyTokens(darkDefaults, "dark")
        verify(Qt.colorEqual(Theme.colors.bgBase, "#0B0C0F"))
        Theme.dark = false
        Theme.palette = { dark: table, light: table }
        verifyTokens(table, "light")
        Theme.palette = ({})
        verifyTokens(lightDefaults, "light")
        verify(Qt.colorEqual(Theme.colors.bgBase, "#E8EBEF"))
    }
}
