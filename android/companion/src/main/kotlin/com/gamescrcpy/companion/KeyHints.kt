package com.gamescrcpy.companion

import com.gamescrcpy.protocol.Enums.KeyHintShape
import com.gamescrcpy.protocol.Messages

data class HintItem(
    val shape: KeyHintShape,
    val x: Int,
    val y: Int,
    val width: Int,
    val height: Int,
    val label: String,
)

data class HintState(val shown: Boolean, val faded: Boolean, val opacity: Int) {
    companion object {
        val HIDDEN = HintState(shown = false, faded = false, opacity = 100)
    }
}

interface KeyHintSink {
    fun layout(items: List<HintItem>)

    fun state(state: HintState)

    companion object {
        val NONE = object : KeyHintSink {
            override fun layout(items: List<HintItem>) = Unit

            override fun state(state: HintState) = Unit
        }
    }
}

object KeyHints {
    fun from(message: Messages.KeyHintLayout): List<HintItem> =
        message.hints.map { HintItem(it.shape, it.x, it.y, it.width, it.height, it.label) }

    fun from(message: Messages.KeyHintState): HintState =
        HintState(shown = message.shown(), faded = message.faded(), opacity = message.opacity.coerceIn(0, 100))

    fun alphaOf(state: HintState): Float = when {
        !state.shown -> 0f
        state.faded -> state.opacity.coerceIn(0, 100) / 100f
        else -> 1f
    }

    fun extent(value: Int, displaySize: Int): Float =
        if (displaySize <= 0) 0f else value.toFloat() * displaySize / CursorMapper.FULL_SCALE
}

object KeyHintLook {
    const val FILL = 0xA80B0C0F.toInt()
    const val STROKE = 0x38FFFFFF
    const val TEXT = 0xFFFFFFFF.toInt()
    const val RING = 0x47FFFFFF
    const val RING_FILL = 0x240B0C0F
    const val ECHO_FAR = 0x240B0C0F
    const val ROUND_DP = 34f
    const val KEY_DP = 26f
    const val KEY_RADIUS_DP = 8f
    const val HALO_DP = 4f
    const val ECHO_DP = 4f
    const val TICK_DP = 6f
    const val RING_STROKE_DP = 2f
    const val STROKE_DP = 1f
    const val PADDING_DP = 4f
    const val KEY_PADDING_DP = 8f
    const val TEXT_SP = 13f
    const val SMALL_TEXT_SP = 12f
    const val TEXT_WEIGHT = 600
    const val FADE_MILLIS = 240L
}
