package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;

import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.server.device.DisplaySnapshot;
import org.junit.jupiter.api.Test;

class FrameRateTest {

    private static DisplaySnapshot display(long refreshMilliHz) {
        return new DisplaySnapshot(0, 1080, 2400, 0, 0, 0, 420, refreshMilliHz, DisplaySnapshot.STATE_ON, 0, 0, 0,
                0);
    }

    private static VideoSettings limit(int maxFps) {
        return new VideoSettings(0, VideoCodec.H264, 0, maxFps, VideoSettings.DEFAULT_BITRATE, 0);
    }

    @Test
    void withoutALimitTheEncoderIsToldThePhonesRefreshRate() {
        assertEquals(60, MediaCodecSessionFactory.frameRate(display(60_000), limit(0)));
        assertEquals(90, MediaCodecSessionFactory.frameRate(display(90_000), limit(0)));
        assertEquals(120, MediaCodecSessionFactory.frameRate(display(119_880), limit(0)));
        assertEquals(144, MediaCodecSessionFactory.frameRate(display(144_000), limit(0)));
        assertEquals(165, MediaCodecSessionFactory.frameRate(display(165_000), limit(0)));
        assertEquals(240, MediaCodecSessionFactory.frameRate(display(480_000), limit(0)));
    }

    @Test
    void anUnknownRefreshRateFallsBackToSixty() {
        assertEquals(60, MediaCodecSessionFactory.frameRate(display(0), limit(0)));
    }

    @Test
    void aLimitFromTheClientWins() {
        assertEquals(30, MediaCodecSessionFactory.frameRate(display(120_000), limit(30)));
        assertEquals(90, MediaCodecSessionFactory.frameRate(display(120_000), limit(90)));
    }
}
