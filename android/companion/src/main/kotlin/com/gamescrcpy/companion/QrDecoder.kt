package com.gamescrcpy.companion

import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.ReaderException
import com.google.zxing.common.HybridBinarizer
import com.google.zxing.qrcode.QRCodeReader
import java.nio.ByteBuffer

class QrDecoder {
    private val reader = QRCodeReader()
    private val hints = mapOf<DecodeHintType, Any>(
        DecodeHintType.TRY_HARDER to true,
        DecodeHintType.CHARACTER_SET to "UTF-8",
    )

    fun decode(luminance: ByteArray, width: Int, height: Int): String? {
        if (width <= 0 || height <= 0 || luminance.size < width * height) {
            return null
        }
        val source = PlanarYUVLuminanceSource(luminance, width, height, 0, 0, width, height, false)
        return try {
            reader.decode(BinaryBitmap(HybridBinarizer(source)), hints).text
        } catch (e: ReaderException) {
            null
        } finally {
            reader.reset()
        }
    }

    fun decode(plane: ByteBuffer, rowStride: Int, width: Int, height: Int): String? {
        if (rowStride < width || height <= 0) {
            return null
        }
        val luminance = ByteArray(width * height)
        val start = plane.position()
        for (row in 0 until height) {
            val offset = start + row * rowStride
            if (offset + width > plane.limit()) {
                return null
            }
            plane.position(offset)
            plane.get(luminance, row * width, width)
        }
        plane.position(start)
        return decode(luminance, width, height)
    }
}
