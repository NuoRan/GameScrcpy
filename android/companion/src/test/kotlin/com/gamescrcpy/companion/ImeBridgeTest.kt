package com.gamescrcpy.companion

import android.text.InputType
import android.view.inputmethod.EditorInfo
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class ImeBridgeTest {

    @Test
    fun anEditorWithoutTextInputIsNoInputBox() {
        assertEquals(InputBox.HIDDEN, InputBoxes.of(InputType.TYPE_NULL, EditorInfo.IME_ACTION_SEND))
    }

    @Test
    fun aMultiLineTextBoxReportsItsTypeAndAction() {
        val box = InputBoxes.of(
            InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE,
            EditorInfo.IME_ACTION_SEND or EditorInfo.IME_FLAG_NO_EXTRACT_UI,
        )
        assertTrue(box.shown)
        assertTrue(box.multiLine)
        assertEquals(EditorInfo.IME_ACTION_SEND, box.action)
        assertEquals((InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE).toLong(), box.inputType)
    }

    @Test
    fun onlyTextBoxesAreMultiLineAndUnknownActionsBecomeUnspecified() {
        val number = InputBoxes.of(InputType.TYPE_CLASS_NUMBER or 0x00020000, EditorInfo.IME_ACTION_DONE)
        assertTrue(number.shown)
        assertFalse(number.multiLine)
        assertEquals(EditorInfo.IME_ACTION_DONE, number.action)
        val odd = InputBoxes.of(InputType.TYPE_CLASS_TEXT, 0x42)
        assertEquals(EditorInfo.IME_ACTION_UNSPECIFIED, odd.action)
        val high = InputBoxes.of(Int.MIN_VALUE or InputType.TYPE_CLASS_TEXT, EditorInfo.IME_ACTION_PREVIOUS)
        assertEquals(0x80000001L, high.inputType)
        assertEquals(EditorInfo.IME_ACTION_PREVIOUS, high.action)
    }

    @Test
    fun theFallbackWaitsFiveSecondsAfterTheComputerIsGone() {
        val fallback = ImeFallback(5_000)
        assertNull(fallback.update(connected = true, now = 0))
        assertFalse(fallback.due(connected = false, now = 100))
        assertEquals(5_000L, fallback.update(connected = false, now = 1_000))
        assertEquals(3_000L, fallback.update(connected = false, now = 3_000))
        assertFalse(fallback.due(connected = false, now = 5_999))
        assertTrue(fallback.due(connected = false, now = 6_000))
        assertEquals(0L, fallback.update(connected = false, now = 9_000))
    }

    @Test
    fun reconnectingInTimeCancelsTheFallback() {
        val fallback = ImeFallback(5_000)
        assertEquals(5_000L, fallback.update(connected = false, now = 1_000))
        assertNull(fallback.update(connected = true, now = 4_000))
        assertFalse(fallback.due(connected = true, now = 6_000))
        assertFalse(fallback.due(connected = false, now = 6_500))
        assertEquals(5_000L, fallback.update(connected = false, now = 7_000))
        assertFalse(fallback.due(connected = false, now = 11_999))
        assertTrue(fallback.due(connected = false, now = 12_000))
    }

    @Test
    fun theBridgeTellsWatchersAboutChangesOnlyAndForwardsToTheInputMethod() {
        val seen = ArrayList<InputBox>()
        val watcher: (InputBox) -> Unit = { seen.add(it) }
        val typed = ArrayList<String>()
        val actions = ArrayList<Int>()
        val target = object : ImeTarget {
            override fun commit(text: String) {
                typed.add(text)
            }

            override fun perform(action: Int) {
                actions.add(action)
            }
        }
        val box = InputBoxes.of(InputType.TYPE_CLASS_TEXT, EditorInfo.IME_ACTION_SEARCH)
        ImeBridge.watch(watcher)
        try {
            ImeBridge.commit("lost")
            ImeBridge.attach(target)
            ImeBridge.report(box)
            ImeBridge.report(box)
            ImeBridge.commit("你好")
            ImeBridge.perform(EditorInfo.IME_ACTION_SEARCH)
            assertEquals(box, ImeBridge.box)
            ImeBridge.detach(target)
            assertEquals(InputBox.HIDDEN, ImeBridge.box)
            ImeBridge.commit("after")
        } finally {
            ImeBridge.detach(target)
            ImeBridge.unwatch(watcher)
        }
        assertEquals(listOf(box, InputBox.HIDDEN), seen)
        assertEquals(listOf("你好"), typed)
        assertEquals(listOf(EditorInfo.IME_ACTION_SEARCH), actions)
    }
}
