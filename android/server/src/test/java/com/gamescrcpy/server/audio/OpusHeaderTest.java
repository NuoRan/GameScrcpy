package com.gamescrcpy.server.audio;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.Test;

class OpusHeaderTest {

    private static final byte[] OPUS_HEAD = {
        'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 2, 0x38, 0x01, (byte) 0x80, (byte) 0xbb, 0, 0, 0, 0, 0,
    };

    private static ByteBuffer sections(byte[]... parts) {
        int size = 0;
        for (byte[] part : parts) {
            size += part.length;
        }
        final ByteBuffer buffer = ByteBuffer.allocate(size).order(ByteOrder.nativeOrder());
        for (byte[] part : parts) {
            buffer.put(part);
        }
        buffer.flip();
        return buffer;
    }

    private static byte[] section(String id, byte[] content) {
        final ByteBuffer buffer = ByteBuffer.allocate(16 + content.length).order(ByteOrder.nativeOrder());
        buffer.put(id.getBytes(StandardCharsets.US_ASCII)).putLong(content.length).put(content);
        return buffer.array();
    }

    private static byte[] bytes(ByteBuffer buffer) {
        final byte[] out = new byte[buffer.remaining()];
        buffer.duplicate().get(out);
        return out;
    }

    @Test
    void theHeaderSectionAmongTheEncodersSections() throws IOException {
        final ByteBuffer config = sections(section("AOPUSHDR", OPUS_HEAD), section("AOPUSDLY", new byte[8]),
                section("AOPUSPRL", new byte[8]));
        assertArrayEquals(OPUS_HEAD, bytes(OpusHeader.find(config)));
        assertEquals(0, config.position());
    }

    @Test
    void aBareHeaderPassesThrough() throws IOException {
        assertArrayEquals(OPUS_HEAD, bytes(OpusHeader.find(ByteBuffer.wrap(OPUS_HEAD))));
    }

    @Test
    void configurationsWithoutAUsableHeaderAreRejected() {
        assertThrows(IOException.class, () -> OpusHeader.find(sections(section("AOPUSDLY", new byte[8]))));
        assertThrows(IOException.class, () -> OpusHeader.find(sections(section("AOPUSHDR", new byte[19]))));
        final byte[] truncated = section("AOPUSHDR", OPUS_HEAD);
        assertThrows(IOException.class,
                () -> OpusHeader.find(ByteBuffer.wrap(truncated, 0, truncated.length - 1)));
    }
}
