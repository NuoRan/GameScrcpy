package com.gamescrcpy.server;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

class MediaClockTest {

    @Test
    void encodeDelayIsTheTimeFromCaptureToTheEncoderOutput() {
        assertEquals(8_250, MediaClock.encodeDelayUs(1_000, 9_250));
        assertEquals(0, MediaClock.encodeDelayUs(5_000, 5_000));
    }

    @Test
    void encodeDelayStaysWithinAU32() {
        assertEquals(0, MediaClock.encodeDelayUs(9_250, 1_000));
        assertEquals(0xFFFF_FFFFL, MediaClock.encodeDelayUs(0, 0x1_0000_0000L));
        assertEquals(0xFFFF_FFFFL, MediaClock.encodeDelayUs(Long.MIN_VALUE / 2, Long.MAX_VALUE / 2));
    }
}
