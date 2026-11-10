package com.gamescrcpy.companion

import com.gamescrcpy.protocol.Codec
import com.gamescrcpy.protocol.Constants
import com.gamescrcpy.protocol.Direction
import com.gamescrcpy.protocol.FrameContext
import com.gamescrcpy.protocol.Message
import java.io.EOFException
import java.io.IOException
import java.io.InputStream

class FrameReader(private val input: InputStream) {
    private val header = ByteArray(Constants.FRAME_HEADER_SIZE)
    private val payload = ByteArray(Constants.COMPANION_PAYLOAD_LIMIT)

    @Throws(IOException::class)
    fun read(): Message {
        readFully(header, header.size)
        val frame = Codec.decodeFrameHeader(FrameContext.COMPANION, Direction.TO_SERVER, header, 0, header.size)
        readFully(payload, frame.length)
        return Codec.decodePayload(frame.type.create(), payload, 0, frame.length)
    }

    private fun readFully(buffer: ByteArray, length: Int) {
        var offset = 0
        while (offset < length) {
            val count = input.read(buffer, offset, length - offset)
            if (count < 0) {
                throw EOFException()
            }
            offset += count
        }
    }
}
