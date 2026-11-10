package com.gamescrcpy.companion

import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel
import java.nio.ByteBuffer
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Test

class QrDecoderTest {
    private val text = LanVectors.load().getJSONArray("qr").getJSONObject(0).getString("url")

    @Test
    fun theComputersCodeIsReadFromACameraImage() {
        val (pixels, size) = render(text)
        assertEquals(text, QrDecoder().decode(pixels, size, size))
    }

    @Test
    fun rowsWithPaddingAreReadToo() {
        val (pixels, size) = render(text)
        val stride = size + 24
        val plane = ByteBuffer.allocate(stride * size)
        for (row in 0 until size) {
            plane.position(row * stride)
            plane.put(pixels, row * size, size)
        }
        plane.position(0)
        val decoder = QrDecoder()
        assertEquals(text, decoder.decode(plane, stride, size, size))
        assertEquals(0, plane.position())
        assertEquals(text, decoder.decode(plane, stride, size, size))
    }

    @Test
    fun anImageWithoutACodeGivesNothing() {
        val size = 200
        val noise = ByteArray(size * size) { ((it * 31) % 251).toByte() }
        assertNull(QrDecoder().decode(noise, size, size))
        assertNull(QrDecoder().decode(ByteArray(10), size, size))
        assertNull(QrDecoder().decode(ByteBuffer.allocate(100), 10, size, size))
    }

    private fun render(content: String): Pair<ByteArray, Int> {
        val hints = mapOf(EncodeHintType.ERROR_CORRECTION to ErrorCorrectionLevel.M, EncodeHintType.MARGIN to 4)
        val matrix = QRCodeWriter().encode(content, BarcodeFormat.QR_CODE, 320, 320, hints)
        val size = matrix.width
        assertEquals(size, matrix.height)
        val pixels = ByteArray(size * size)
        for (y in 0 until size) {
            for (x in 0 until size) {
                pixels[y * size + x] = if (matrix.get(x, y)) 0 else 0xFF.toByte()
            }
        }
        return pixels to size
    }
}
