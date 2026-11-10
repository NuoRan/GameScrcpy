package com.gamescrcpy.companion

import com.gamescrcpy.protocol.Codec
import com.gamescrcpy.protocol.Constants
import com.gamescrcpy.protocol.Enums.ScreenshotProblem
import java.nio.charset.StandardCharsets
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class ScreenshotChunksTest {

    private fun picture(size: Int) = ByteArray(size) { (it * 31 + 7).toByte() }

    @Test
    fun aPictureIsCutIntoFullChunksAndOneLastChunkInOrder() {
        val jpeg = picture(40_000)
        val chunks = ScreenshotChunks.split(77L, jpeg)
        assertEquals(3, chunks.size)
        assertEquals(listOf(0, 1, 2), chunks.map { it.index })
        assertTrue(chunks.all { it.total == 3 && it.requestId == 77L })
        assertEquals(listOf(Constants.MAX_SCREENSHOT_CHUNK, Constants.MAX_SCREENSHOT_CHUNK, 40_000 - 2 * Constants.MAX_SCREENSHOT_CHUNK),
            chunks.map { it.data.size })
        assertArrayEquals(jpeg, chunks.fold(ByteArray(0)) { all, chunk -> all + chunk.data })
        chunks.forEach { chunk ->
            assertTrue(Codec.encodeFrame(chunk).size <= Constants.FRAME_HEADER_SIZE + Constants.COMPANION_PAYLOAD_LIMIT)
        }
    }

    @Test
    fun theChunkCountFollowsTheSizeUpToTheLimit() {
        assertEquals(1, ScreenshotChunks.split(1L, picture(1)).single().total)
        assertEquals(1, ScreenshotChunks.split(1L, picture(Constants.MAX_SCREENSHOT_CHUNK)).size)
        val twice = ScreenshotChunks.split(1L, picture(Constants.MAX_SCREENSHOT_CHUNK + 1))
        assertEquals(2, twice.size)
        assertEquals(1, twice.last().data.size)
        val largest = ScreenshotChunks.split(0xFFFFFFFFL, picture(Constants.MAX_SCREENSHOT_BYTES))
        assertEquals(Constants.MAX_SCREENSHOT_CHUNKS, largest.size)
        assertEquals(Constants.MAX_SCREENSHOT_CHUNKS - 1, largest.last().index)
        assertEquals(Constants.MAX_SCREENSHOT_BYTES, largest.sumOf { it.data.size })
        assertTrue(ScreenshotChunks.fits(picture(Constants.MAX_SCREENSHOT_BYTES)))
        assertFalse(ScreenshotChunks.fits(picture(Constants.MAX_SCREENSHOT_BYTES + 1)))
        assertFalse(ScreenshotChunks.fits(ByteArray(0)))
        assertThrows(IllegalArgumentException::class.java) { ScreenshotChunks.split(1L, ByteArray(0)) }
        assertThrows(IllegalArgumentException::class.java) {
            ScreenshotChunks.split(1L, picture(Constants.MAX_SCREENSHOT_BYTES + 1))
        }
    }

    @Test
    fun aFailureCarriesItsReasonAndAShortEnoughText() {
        val refused = ScreenshotChunks.failed(5L, ScreenshotProblem.REFUSED, "the screen capture was not allowed on the phone")
        assertEquals(5L, refused.requestId)
        assertEquals(ScreenshotProblem.REFUSED, refused.reason)
        assertEquals("the screen capture was not allowed on the phone", refused.message)
        for (problem in ScreenshotProblem.values()) {
            assertEquals(problem, ScreenshotChunks.failed(1L, problem, "").reason)
        }
        val long = ScreenshotChunks.failed(1L, ScreenshotProblem.ERROR, "x".repeat(2000))
        assertEquals(1024, long.message.length)
        val chinese = ScreenshotChunks.failed(1L, ScreenshotProblem.ERROR, "截".repeat(500))
        assertTrue(chinese.message.toByteArray(StandardCharsets.UTF_8).size <= 1024)
        assertEquals(341, chinese.message.length)
        val emoji = ScreenshotChunks.failed(1L, ScreenshotProblem.ERROR, "a" + "\uD83D\uDCF7".repeat(400))
        assertFalse(Character.isHighSurrogate(emoji.message.last()))
        assertTrue(Codec.encodeFrame(emoji).isNotEmpty())
    }

    @Test
    fun theScreenshotKeepsItsShapeWithTheLongSideWithinTheLimit() {
        assertEquals(ShotSize(864, 1920), ScreenshotChunks.fit(1080, 2400, 0))
        assertEquals(ShotSize(1920, 864), ScreenshotChunks.fit(2400, 1080, 0))
        assertEquals(ShotSize(1080, 2400), ScreenshotChunks.fit(1080, 2400, 4000))
        assertEquals(ShotSize(1280, 576), ScreenshotChunks.fit(2400, 1080, 1280))
        assertEquals(ShotSize(720, 1280), ScreenshotChunks.fit(720, 1280, 1920))
        assertEquals(ShotSize(0, 0), ScreenshotChunks.fit(0, 2400, 0))
        val odd = ScreenshotChunks.fit(1081, 2401, 0)
        assertEquals(0, odd.width % 2)
        assertEquals(0, odd.height % 2)
    }

    @Test
    fun theQualityStepsDownToTheLeastAcceptable() {
        assertEquals(listOf(90, 75, 60, 45, 30), ScreenshotChunks.qualities(90))
        assertEquals(listOf(100, 85, 70, 55, 40, 30), ScreenshotChunks.qualities(100))
        assertEquals(listOf(40, 30), ScreenshotChunks.qualities(40))
        assertEquals(listOf(30), ScreenshotChunks.qualities(30))
        assertEquals(listOf(1), ScreenshotChunks.qualities(0))
    }
}
