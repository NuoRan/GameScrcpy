import QtQuick
import QtTest
import GameScrcpy.Theme

TestCase {
    name: "ThemePalette"

    function cleanup() {
        Theme.palette = {}
        Theme.dark = true
    }

    function test_paletteOverridesTheTokensOfTheCurrentMode() {
        Theme.dark = true
        const original = Theme.colors.bgBase
        Theme.palette = { dark: { bgBase: "#102030" }, light: { bgBase: "#F0E0D0" } }
        verify(Qt.colorEqual(Theme.colors.bgBase, "#102030"))
        Theme.dark = false
        verify(Qt.colorEqual(Theme.colors.bgBase, "#F0E0D0"))
        Theme.palette = {}
        Theme.dark = true
        verify(Qt.colorEqual(Theme.colors.bgBase, original))
    }

    function test_derivedTokensFollowAnOverriddenBase() {
        Theme.palette = { dark: { bgElevated: "#204060" } }
        verify(Qt.colorEqual(Theme.colors.bgFrosted, Qt.rgba(0x20 / 255, 0x40 / 255, 0x60 / 255, 0.86)))
    }

    function test_unknownTokenNamesAreIgnored() {
        Theme.palette = { dark: { notAToken: "#FFFFFF" } }
        verify(Qt.colorEqual(Theme.colors.bgBase, "#0B0C0F"))
    }
}
