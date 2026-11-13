package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

import android.os.Build;
import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.server.device.DisplaySnapshot;
import org.junit.jupiter.api.Test;

class EncoderFormatTest {

    private static DisplaySnapshot display(long refreshMilliHz) {
        return new DisplaySnapshot(0, 1080, 2400, 0, 0, 0, 420, refreshMilliHz, DisplaySnapshot.STATE_ON, 0, 0, 0,
                0);
    }

    private static VideoSettings settings(int maxFps, int intraRefreshMs) {
        return new VideoSettings(0, VideoCodec.H264, 0, maxFps, VideoSettings.DEFAULT_BITRATE, intraRefreshMs);
    }

    private static MediaCodecSessionFactory.EncoderFormat keys(int maxFps, int intraRefreshMs, int sdkInt,
                                                               boolean keyFramesOnRequest) {
        return MediaCodecSessionFactory.formatKeys(display(120_000), settings(maxFps, intraRefreshMs), sdkInt,
                keyFramesOnRequest);
    }

    @Test
    void adr0114ThePipelineLatencyIsRequestedOnEveryVersionTheServerRunsOn() {
        assertEquals(1, keys(0, 0, Build.VERSION_CODES.O, true).latency);
        assertEquals(1, keys(0, 0, Build.VERSION_CODES.P, true).latency);
        assertEquals(1, keys(0, 0, Build.VERSION_CODES.TIRAMISU, true).latency);
    }

    @Test
    void adr0114KeyFramesComeEveryHourUnlessTheEncoderIgnoresRequests() {
        assertEquals(3600, keys(0, 0, 35, true).keyFrameIntervalSeconds);
        assertEquals(10, keys(0, 0, 35, false).keyFrameIntervalSeconds);
    }

    @Test
    void adr0114TheEncoderIsToldTheFrameRateAndRepeatsIdleFrames() {
        assertEquals(120, keys(0, 0, 35, true).frameRate);
        assertEquals(60, keys(60, 0, 35, true).frameRate);
        assertEquals(100_000, keys(0, 0, 35, true).repeatAfterUs);
        assertEquals(0, keys(0, 0, 35, true).priority);
    }

    @Test
    void adr0114OptionalKeysAreOnlySetWhenTheyAreAskedFor() {
        final MediaCodecSessionFactory.EncoderFormat plain = keys(0, 0, 35, true);
        assertNull(plain.maxFpsToEncoder);
        assertNull(plain.intraRefreshPeriod);
        assertEquals(0, plain.maxBFrames.intValue());

        final MediaCodecSessionFactory.EncoderFormat limited = keys(60, 2000, 35, true);
        assertEquals(60f, limited.maxFpsToEncoder.floatValue(), 0f);
        assertEquals(120, limited.intraRefreshPeriod.intValue());

        assertNull(keys(0, 0, Build.VERSION_CODES.P, true).maxBFrames);
    }
}
