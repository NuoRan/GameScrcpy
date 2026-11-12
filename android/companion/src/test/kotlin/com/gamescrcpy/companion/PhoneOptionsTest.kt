package com.gamescrcpy.companion

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.DisplayName
import org.junit.jupiter.api.Test

class PhoneOptionsTest {
    private class MemoryStore : SyncStore {
        val values = HashMap<String, String?>()

        override fun read(key: String): String? = values[key]

        override fun write(key: String, value: String?) {
            values[key] = value
        }
    }

    @Test
    @DisplayName("ADR-0112: the phone's own options start at the current look and survive a restart")
    fun optionsStartAtTheCurrentLookAndSurvive() {
        val store = MemoryStore()
        val options = PhoneOptions(store)
        assertEquals(CursorSize.MEDIUM, options.cursorSize)
        assertEquals(CursorStyle.ARROW, options.cursorStyle)
        assertEquals(ImeBar.BOTTOM, options.imeBar)
        assertEquals(1.25f, CursorSize.MEDIUM.scale)

        var heard = 0
        options.addListener { heard++ }
        options.chooseCursorSize(CursorSize.LARGE)
        options.chooseCursorSize(CursorSize.LARGE)
        options.chooseCursorStyle(CursorStyle.DOT)
        options.chooseImeBar(ImeBar.TOP)
        assertEquals(3, heard)

        val again = PhoneOptions(store)
        assertEquals(CursorSize.LARGE, again.cursorSize)
        assertEquals(CursorStyle.DOT, again.cursorStyle)
        assertEquals(ImeBar.TOP, again.imeBar)
    }

    @Test
    @DisplayName("ADR-0112: unknown stored options fall back to the defaults")
    fun unknownStoredOptionsFallBack() {
        val store = MemoryStore()
        store.values["cursorSize"] = "huge"
        store.values["cursorStyle"] = ""
        store.values["imeBar"] = "left"
        val options = PhoneOptions(store)
        assertEquals(CursorSize.MEDIUM, options.cursorSize)
        assertEquals(CursorStyle.ARROW, options.cursorStyle)
        assertEquals(ImeBar.BOTTOM, options.imeBar)
    }
}
