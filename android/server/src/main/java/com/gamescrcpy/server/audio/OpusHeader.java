package com.gamescrcpy.server.audio;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

final class OpusHeader {

    private static final byte[] HEADER_SECTION = "AOPUSHDR".getBytes(StandardCharsets.US_ASCII);
    private static final byte[] OPUS_HEAD = "OpusHead".getBytes(StandardCharsets.US_ASCII);

    private OpusHeader() {
    }

    static ByteBuffer find(ByteBuffer config) throws IOException {
        final ByteBuffer sections = config.slice().order(ByteOrder.nativeOrder());
        if (startsWith(sections, OPUS_HEAD)) {
            return sections;
        }
        while (sections.remaining() >= 16) {
            final boolean header = startsWith(sections, HEADER_SECTION);
            sections.position(sections.position() + 8);
            final long length = sections.getLong();
            if (length < 0 || length > sections.remaining()) {
                throw new IOException("malformed Opus codec configuration");
            }
            if (header) {
                final ByteBuffer head = sections.slice();
                head.limit((int) length);
                if (!startsWith(head, OPUS_HEAD)) {
                    throw new IOException("the Opus header section holds no OpusHead");
                }
                return head;
            }
            sections.position(sections.position() + (int) length);
        }
        throw new IOException("no Opus header in the codec configuration");
    }

    private static boolean startsWith(ByteBuffer buffer, byte[] prefix) {
        if (buffer.remaining() < prefix.length) {
            return false;
        }
        for (int i = 0; i < prefix.length; i++) {
            if (buffer.get(buffer.position() + i) != prefix[i]) {
                return false;
            }
        }
        return true;
    }
}
