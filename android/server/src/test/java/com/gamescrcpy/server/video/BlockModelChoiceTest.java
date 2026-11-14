package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import android.os.Build;
import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.server.Options;
import com.gamescrcpy.server.device.DisplaySnapshot;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class BlockModelChoiceTest {

    private static VideoSettings settings(int maxFps) {
        return new VideoSettings(0, VideoCodec.H264, 0, maxFps, VideoSettings.DEFAULT_BITRATE, 0);
    }

    @AfterEach
    void forgetFailures() {
        MediaCodecSessionFactory.forgetBlockModelFailures();
    }

    @Test
    void adr0117TheBlockModelIsUsedForCodec2EncodersFromAndroidElevenOn() {
        final int on = Options.BLOCK_INPUT_ON;
        assertTrue(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", on, Build.VERSION_CODES.R, false,
                60));
        assertTrue(MediaCodecSessionFactory.blockModelWanted("c2.android.hevc.encoder", on, 36, false, 60));
        assertFalse(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", on, Build.VERSION_CODES.Q,
                false, 120));
        assertFalse(MediaCodecSessionFactory.blockModelWanted("OMX.qcom.video.encoder.avc", on, 34, false, 120));
        assertFalse(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", Options.BLOCK_INPUT_OFF, 34,
                false, 120));
        assertFalse(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", on, 34, true, 120));
    }

    @Test
    void adr0118OnItsOwnTheBlockModelIsUsedOnlyAboveAHundredFramesASecond() {
        final int automatic = Options.BLOCK_INPUT_AUTOMATIC;
        assertTrue(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", automatic, 36, false, 120));
        assertTrue(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", automatic, 36, false, 101));
        assertFalse(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", automatic, 36, false, 100));
        assertFalse(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", automatic, 36, false, 60));
    }

    @Test
    void adr0118ThePeakFrameRateIsTheCapOrElseTheScreensFastestMode() {
        final DisplaySnapshot idle = new DisplaySnapshot(0, 1080, 2400, 0, 0, 0, 450, 60_000, DisplaySnapshot.STATE_ON,
                0, 0, 0, 0, 120_000);
        assertEquals(120, MediaCodecSessionFactory.peakFrameRate(idle, settings(0)));
        assertEquals(60, MediaCodecSessionFactory.peakFrameRate(idle, settings(60)));
        final DisplaySnapshot plain = new DisplaySnapshot(0, 1080, 2400, 0, 0, 0, 450, 60_000,
                DisplaySnapshot.STATE_ON, 0, 0, 0, 0);
        assertEquals(60, MediaCodecSessionFactory.peakFrameRate(plain, settings(0)));
    }

    @Test
    void adr0117AnEncoderThatFailedOnceIsNotTriedAgainInThisProcess() {
        assertFalse(MediaCodecSessionFactory.blockModelFailed("c2.qti.avc.encoder"));
        assertTrue(MediaCodecSessionFactory.noteBlockModelFailed("c2.qti.avc.encoder"));
        assertTrue(MediaCodecSessionFactory.blockModelFailed("c2.qti.avc.encoder"));
        assertFalse(MediaCodecSessionFactory.noteBlockModelFailed("c2.qti.avc.encoder"));
    }

    @Test
    void adr0117AtMostSixteenEncodersAreRemembered() {
        for (int i = 0; i < MediaCodecSessionFactory.MAX_BLOCK_MODEL_FAILURES; ++i) {
            assertTrue(MediaCodecSessionFactory.noteBlockModelFailed("encoder" + i));
        }
        assertFalse(MediaCodecSessionFactory.noteBlockModelFailed("encoder16"));
        assertTrue(MediaCodecSessionFactory.blockModelFailed("encoder15"));
        assertFalse(MediaCodecSessionFactory.blockModelFailed("encoder16"));
    }
}
