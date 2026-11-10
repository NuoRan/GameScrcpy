package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

class VideoSizeTest {

    @Test
    void nativeSizeIsKeptWhenAligned() {
        assertEquals(new VideoSize(1080, 2400), VideoSize.fit(1080, 2400, 0, 8));
        assertEquals(new VideoSize(2400, 1080), VideoSize.fit(2400, 1080, 2400, 8));
    }

    @Test
    void longSideIsLimitedWithTheAspectRatioKept() {
        assertEquals(new VideoSize(864, 1920), VideoSize.fit(1080, 2400, 1920, 16));
        assertEquals(new VideoSize(720, 1600), VideoSize.fit(1080, 2400, 1600, 16));
        assertEquals(new VideoSize(464, 1024), VideoSize.fit(1080, 2400, 1024, 16));
        assertEquals(new VideoSize(1920, 864), VideoSize.fit(2400, 1080, 1920, 16));
    }

    @Test
    void roundingNeverExceedsTheLimit() {
        assertEquals(new VideoSize(448, 992), VideoSize.fit(1080, 2400, 1000, 16));
        assertEquals(new VideoSize(1080, 2344), VideoSize.fit(1080, 2340, 0, 8));
    }

    @Test
    void tinyDisplaysKeepOneAlignmentUnit() {
        assertEquals(new VideoSize(16, 16), VideoSize.fit(10, 4, 0, 16));
        assertEquals("16x16", VideoSize.fit(10, 4, 0, 16).toString());
    }
}
