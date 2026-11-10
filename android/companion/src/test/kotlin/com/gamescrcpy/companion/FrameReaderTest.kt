package com.gamescrcpy.companion

import com.gamescrcpy.protocol.Codec
import com.gamescrcpy.protocol.DecodeError
import com.gamescrcpy.protocol.DecodeException
import com.gamescrcpy.protocol.Messages
import java.io.ByteArrayInputStream
import java.io.EOFException
import java.io.InputStream
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertInstanceOf
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class FrameReaderTest {

    @Test
    fun readsFramesThatArriveOneByteAtATime() {
        val token = ByteArray(16) { it.toByte() }
        val hello = Messages.CompanionHello().also { it.token = token }
        val cursor = Messages.CursorState().also {
            it.x = 100
            it.y = 65535
            it.flags = Messages.CursorState.VISIBLE or Messages.CursorState.PRESSED
        }
        val reader = FrameReader(Trickle(Codec.encodeFrame(hello) + Codec.encodeFrame(cursor)))

        val first = assertInstanceOf(Messages.CompanionHello::class.java, reader.read())
        assertEquals(1, first.version)
        assertArrayEquals(token, first.token)
        val second = assertInstanceOf(Messages.CursorState::class.java, reader.read())
        assertEquals(100, second.x)
        assertEquals(65535, second.y)
        assertTrue(second.visible())
        assertTrue(second.pressed())
        assertThrows(EOFException::class.java) { reader.read() }
    }

    @Test
    fun keepAliveMessagesAreReadToo() {
        val ping = Messages.Ping().also {
            it.nonce = 3
            it.clientTimeUs = 1234
        }
        val read = assertInstanceOf(Messages.Ping::class.java, FrameReader(bytes(Codec.encodeFrame(ping))).read())
        assertEquals(3L, read.nonce)
        assertEquals(1234L, read.clientTimeUs)
        assertInstanceOf(Messages.Goodbye::class.java, FrameReader(bytes(Codec.encodeFrame(Messages.Goodbye()))).read())
    }

    @Test
    fun refusesWhatTheCompanionMustNotReceive() {
        assertDecodeError(DecodeError.WRONG_DIRECTION, Codec.encodeFrame(Messages.CompanionWelcome()))
        assertDecodeError(DecodeError.UNEXPECTED_TYPE, byteArrayOf(0x01, 0x01, 0, 0, 0, 0))
        assertDecodeError(DecodeError.UNEXPECTED_TYPE, byteArrayOf(0x04, 0x03, 0, 0, 0, 0))
        assertDecodeError(DecodeError.UNKNOWN_TYPE, byteArrayOf(0x06, 0x7F, 0, 0, 0, 0))
        assertDecodeError(DecodeError.FRAME_TOO_LARGE, byteArrayOf(0x06, 0x01, 0, 0, 0x40, 0x01))
        assertDecodeError(DecodeError.RESERVED_BITS_SET, byteArrayOf(0x06, 0x02, 0, 0, 0, 5, 0, 1, 0, 2, 4))
        assertThrows(EOFException::class.java) { FrameReader(bytes(byteArrayOf(0x06, 0x02, 0, 0))).read() }
        assertThrows(EOFException::class.java) { FrameReader(bytes(byteArrayOf(0x06, 0x02, 0, 0, 0, 5, 0))).read() }
    }

    private fun assertDecodeError(expected: DecodeError, data: ByteArray) {
        val failure = assertThrows(DecodeException::class.java) { FrameReader(bytes(data)).read() }
        assertEquals(expected, failure.error())
    }

    private fun bytes(data: ByteArray): InputStream = ByteArrayInputStream(data)

    private class Trickle(private val data: ByteArray) : InputStream() {
        private var position = 0

        override fun read(): Int = if (position < data.size) data[position++].toInt() and 0xFF else -1

        override fun read(b: ByteArray, off: Int, len: Int): Int {
            if (len == 0) {
                return 0
            }
            if (position >= data.size) {
                return -1
            }
            b[off] = data[position++]
            return 1
        }
    }
}
