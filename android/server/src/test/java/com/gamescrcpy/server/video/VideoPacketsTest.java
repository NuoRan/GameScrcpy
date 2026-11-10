package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Enums.MediaKind;
import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.protocol.Media;
import com.gamescrcpy.protocol.MediaChannel;
import com.gamescrcpy.protocol.Messages.VideoSessionInfo;
import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.junit.jupiter.api.Test;

class VideoPacketsTest {

    private final List<byte[]> packets = new ArrayList<>();
    private final VideoPackets writer = new VideoPackets((header, payload) -> {
        final ByteArrayOutputStream packet = new ByteArrayOutputStream();
        packet.write(header, 0, header.length);
        while (payload.hasRemaining()) {
            packet.write(payload.get());
        }
        packets.add(packet.toByteArray());
    });

    private static Media.Packet decode(byte[] packet) {
        return Media.decodePacket(MediaChannel.VIDEO, packet, 0, packet.length);
    }

    @Test
    void sessionConfigAndFrameFollowSpec32() throws Exception {
        final VideoSessionInfo info = new VideoSessionInfo();
        info.codec = VideoCodec.H265;
        info.frameWidth = 864;
        info.frameHeight = 1920;
        info.displayWidth = 1080;
        info.displayHeight = 2400;
        info.rotation = 1;
        writer.session(7, info);
        writer.config(7, ByteBuffer.wrap(new byte[] {0, 0, 0, 1, 0x40}));
        final ByteBuffer frame = ByteBuffer.allocateDirect(4).put(new byte[] {0, 0, 1, 0x26});
        frame.flip();
        writer.frame(7, Media.FLAG_KEY_FRAME, 123_456_789L, 8_250, frame);

        final Media.Packet session = decode(packets.get(0));
        assertEquals(MediaKind.SESSION, session.header.kind);
        assertEquals(7, session.header.session);
        final VideoSessionInfo decoded = (VideoSessionInfo) session.session;
        assertEquals(VideoCodec.H265, decoded.codec);
        assertEquals(864, decoded.frameWidth);
        assertEquals(2400, decoded.displayHeight);
        assertEquals(1, decoded.rotation);

        final Media.Packet config = decode(packets.get(1));
        assertEquals(MediaKind.CONFIG, config.header.kind);
        assertEquals(0, config.header.ptsUs);
        assertArrayEquals(new byte[] {0, 0, 0, 1, 0x40},
                Arrays.copyOfRange(packets.get(1), config.payloadOffset, packets.get(1).length));

        final Media.Packet decodedFrame = decode(packets.get(2));
        assertEquals(MediaKind.FRAME, decodedFrame.header.kind);
        assertTrue(decodedFrame.header.keyFrame);
        assertFalse(decodedFrame.header.discardable);
        assertEquals(123_456_789L, decodedFrame.header.ptsUs);
        assertEquals(8_250, decodedFrame.header.encodeDelayUs);
        assertEquals(4, decodedFrame.header.size);
        assertFalse(frame.hasRemaining());
    }
}
