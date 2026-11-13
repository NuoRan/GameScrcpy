package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

import org.junit.jupiter.api.Test;

class EncodeDelayWindowTest {

    @Test
    void adr0114MinutesAreCountedWithRunningSumsAndALargestValue() {
        final EncodeDelayWindow window = new EncodeDelayWindow();
        assertNull(window.add(1_000_000, 10_000));
        assertNull(window.add(1_010_000, 30_000));
        assertNull(window.add(30_000_000, 20_000));
        assertNull(window.add(60_999_999, 20_000));
        assertEquals("frames 5, capture to encoder output mean 20.0 ms, largest 30 ms",
                window.add(61_000_000, 20_000));
    }

    @Test
    void adr0114TheNextMinuteStartsEmpty() {
        final EncodeDelayWindow window = new EncodeDelayWindow();
        assertNull(window.add(0, 5_000));
        assertEquals("frames 2, capture to encoder output mean 5.0 ms, largest 5 ms",
                window.add(EncodeDelayWindow.WINDOW_US, 5_000));
        assertNull(window.add(EncodeDelayWindow.WINDOW_US + 1_000, 1_000));
    }
}
