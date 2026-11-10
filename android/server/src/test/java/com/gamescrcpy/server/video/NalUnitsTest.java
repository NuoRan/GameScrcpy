package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Enums.VideoCodec;
import java.nio.ByteBuffer;
import org.junit.jupiter.api.Test;

class NalUnitsTest {

    private static ByteBuffer bytes(int... values) {
        final ByteBuffer buffer = ByteBuffer.allocateDirect(values.length + 2);
        buffer.put((byte) 0x7F);
        for (int value : values) {
            buffer.put((byte) value);
        }
        buffer.flip();
        buffer.position(1);
        return buffer;
    }

    @Test
    void h264ReferenceAndNonReferenceSlices() {
        assertFalse(NalUnits.discardable(VideoCodec.H264, bytes(0, 0, 0, 1, 0x65, 0x88)));
        assertFalse(NalUnits.discardable(VideoCodec.H264, bytes(0, 0, 0, 1, 0x41, 0x9A)));
        assertTrue(NalUnits.discardable(VideoCodec.H264, bytes(0, 0, 1, 0x01, 0x9E)));
    }

    @Test
    void h264NonSliceUnitsBeforeTheSliceAreSkipped() {
        assertTrue(NalUnits.discardable(VideoCodec.H264,
                bytes(0, 0, 0, 1, 0x09, 0x10, 0, 0, 0, 1, 0x06, 0x05, 0x01, 0, 0, 1, 0x01, 0x9E)));
        assertFalse(NalUnits.discardable(VideoCodec.H264, bytes(0, 0, 0, 1, 0x09, 0x10)));
    }

    @Test
    void h265SubLayerNonReferencePictures() {
        assertTrue(NalUnits.discardable(VideoCodec.H265, bytes(0, 0, 0, 1, 0x00, 0x01, 0xAF)));
        assertFalse(NalUnits.discardable(VideoCodec.H265, bytes(0, 0, 0, 1, 0x02, 0x01, 0xAF)));
        assertTrue(NalUnits.discardable(VideoCodec.H265, bytes(0, 0, 1, 0x10, 0x01, 0xAF)));
        assertFalse(NalUnits.discardable(VideoCodec.H265, bytes(0, 0, 0, 1, 0x26, 0x01, 0xAF)));
        assertTrue(NalUnits.discardable(VideoCodec.H265, bytes(0, 0, 0, 1, 0x46, 0x01, 0x50, 0, 0, 1, 0x00, 0x01)));
    }

    @Test
    void av1AndGarbageAreNeverDiscardable() {
        assertFalse(NalUnits.discardable(VideoCodec.AV1, bytes(0x12, 0x00, 0x0A)));
        assertFalse(NalUnits.discardable(VideoCodec.H264, bytes(1, 2, 3)));
        assertFalse(NalUnits.discardable(VideoCodec.H264, bytes()));
    }

    @Test
    void positionIsUnchanged() {
        final ByteBuffer frame = bytes(0, 0, 1, 0x01, 0x9E);
        NalUnits.discardable(VideoCodec.H264, frame);
        assertEquals(1, frame.position());
    }
}
