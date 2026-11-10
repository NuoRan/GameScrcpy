package com.gamescrcpy.companion

import com.gamescrcpy.protocol.Enums.KeyHintShape
import com.gamescrcpy.protocol.Messages
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

class KeyHintsTest {

    @Test
    fun aLayoutBecomesTheItemsToDraw() {
        val message = Messages.KeyHintLayout()
        message.hints.add(hint(KeyHintShape.RING, 16000, 50000, 9000, 20000, ""))
        message.hints.add(hint(KeyHintShape.TURBO, 60000, 30000, 0, 0, "J"))
        message.hints.add(hint(KeyHintShape.KEY, 16000, 41000, 0, 0, "W"))
        assertEquals(
            listOf(
                HintItem(KeyHintShape.RING, 16000, 50000, 9000, 20000, ""),
                HintItem(KeyHintShape.TURBO, 60000, 30000, 0, 0, "J"),
                HintItem(KeyHintShape.KEY, 16000, 41000, 0, 0, "W"),
            ),
            KeyHints.from(message),
        )
        assertEquals(emptyList<HintItem>(), KeyHints.from(Messages.KeyHintLayout()))
    }

    @Test
    fun theStateDecidesHowStronglyTheHintsShow() {
        val message = Messages.KeyHintState()
        message.flags = Messages.KeyHintState.SHOWN or Messages.KeyHintState.FADED
        message.opacity = 40
        val faded = KeyHints.from(message)
        assertEquals(HintState(shown = true, faded = true, opacity = 40), faded)
        assertEquals(0.4f, KeyHints.alphaOf(faded), 1e-6f)
        assertEquals(1f, KeyHints.alphaOf(faded.copy(faded = false)), 0f)
        assertEquals(0f, KeyHints.alphaOf(faded.copy(shown = false)), 0f)
        assertEquals(0f, KeyHints.alphaOf(HintState.HIDDEN), 0f)
        assertEquals(0f, KeyHints.alphaOf(HintState(shown = true, faded = true, opacity = 0)), 0f)
    }

    @Test
    fun ringSizesScaleWithTheScreen() {
        assertEquals(1080f, KeyHints.extent(65535, 1080), 1e-3f)
        assertEquals(540f, KeyHints.extent(32767, 1080), 0.02f)
        assertEquals(0f, KeyHints.extent(32767, 0), 0f)
    }

    private fun hint(shape: KeyHintShape, x: Int, y: Int, width: Int, height: Int, label: String) =
        Messages.KeyHint().also {
            it.shape = shape
            it.x = x
            it.y = y
            it.width = width
            it.height = height
            it.label = label
        }
}
