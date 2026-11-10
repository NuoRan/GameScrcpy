package com.gamescrcpy.companion

import com.gamescrcpy.protocol.Constants
import com.gamescrcpy.protocol.Enums.ScreenshotProblem
import com.gamescrcpy.protocol.Messages
import java.nio.charset.StandardCharsets
import kotlin.math.roundToInt

sealed interface ScreenshotResult {
    class Picture(val jpeg: ByteArray) : ScreenshotResult

    class Failure(val problem: ScreenshotProblem, val text: String) : ScreenshotResult
}

interface ScreenshotPort {
    fun capture(maxSide: Int, quality: Int, done: (ScreenshotResult) -> Unit)

    fun stop()

    companion object {
        val NONE = object : ScreenshotPort {
            override fun capture(maxSide: Int, quality: Int, done: (ScreenshotResult) -> Unit) {
                done(ScreenshotResult.Failure(ScreenshotProblem.UNSUPPORTED, "this companion cannot take screenshots"))
            }

            override fun stop() = Unit
        }
    }
}

data class ShotSize(val width: Int, val height: Int)

object ScreenshotChunks {
    const val MIN_QUALITY = 30
    private const val QUALITY_STEP = 15
    private const val MAX_MESSAGE_BYTES = 1024

    fun split(requestId: Long, jpeg: ByteArray): List<Messages.ScreenshotChunk> {
        require(jpeg.isNotEmpty() && jpeg.size <= Constants.MAX_SCREENSHOT_BYTES)
        val total = (jpeg.size + Constants.MAX_SCREENSHOT_CHUNK - 1) / Constants.MAX_SCREENSHOT_CHUNK
        return (0 until total).map { index ->
            val start = index * Constants.MAX_SCREENSHOT_CHUNK
            val end = minOf(jpeg.size, start + Constants.MAX_SCREENSHOT_CHUNK)
            Messages.ScreenshotChunk().also {
                it.requestId = requestId
                it.index = index
                it.total = total
                it.data = jpeg.copyOfRange(start, end)
            }
        }
    }

    fun failed(requestId: Long, problem: ScreenshotProblem, text: String): Messages.ScreenshotFailed =
        Messages.ScreenshotFailed().also {
            it.requestId = requestId
            it.reason = problem
            it.message = clipped(text)
        }

    fun fits(jpeg: ByteArray): Boolean = jpeg.isNotEmpty() && jpeg.size <= Constants.MAX_SCREENSHOT_BYTES

    fun fit(width: Int, height: Int, maxSide: Int): ShotSize {
        if (width <= 0 || height <= 0) {
            return ShotSize(0, 0)
        }
        val longest = if (maxSide <= 0) Constants.DEFAULT_SCREENSHOT_SIDE else maxSide
        val side = maxOf(width, height)
        val scale = if (side > longest) longest.toDouble() / side else 1.0
        return ShotSize(even(width * scale), even(height * scale))
    }

    fun qualities(start: Int): List<Int> {
        val first = start.coerceIn(1, 100)
        if (first <= MIN_QUALITY) {
            return listOf(first)
        }
        val steps = mutableListOf(first)
        var next = first - QUALITY_STEP
        while (next > MIN_QUALITY) {
            steps.add(next)
            next -= QUALITY_STEP
        }
        steps.add(MIN_QUALITY)
        return steps
    }

    private fun even(value: Double): Int = maxOf(2, (value / 2).roundToInt() * 2)

    private fun clipped(text: String): String {
        if (text.toByteArray(StandardCharsets.UTF_8).size <= MAX_MESSAGE_BYTES) {
            return text
        }
        var end = minOf(text.length, MAX_MESSAGE_BYTES)
        while (end > 0 && text.substring(0, end).toByteArray(StandardCharsets.UTF_8).size > MAX_MESSAGE_BYTES) {
            end--
        }
        if (end > 0 && Character.isHighSurrogate(text[end - 1])) {
            end--
        }
        return text.substring(0, end)
    }
}
