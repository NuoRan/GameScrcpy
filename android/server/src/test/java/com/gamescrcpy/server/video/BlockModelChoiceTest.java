package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import android.os.Build;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class BlockModelChoiceTest {

    @AfterEach
    void forgetFailures() {
        MediaCodecSessionFactory.forgetBlockModelFailures();
    }

    @Test
    void adr0117TheBlockModelIsUsedForCodec2EncodersFromAndroidElevenOn() {
        assertTrue(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", true, Build.VERSION_CODES.R, false));
        assertTrue(MediaCodecSessionFactory.blockModelWanted("c2.android.hevc.encoder", true, 36, false));
        assertFalse(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", true, Build.VERSION_CODES.Q,
                false));
        assertFalse(MediaCodecSessionFactory.blockModelWanted("OMX.qcom.video.encoder.avc", true, 34, false));
        assertFalse(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", false, 34, false));
        assertFalse(MediaCodecSessionFactory.blockModelWanted("c2.qti.avc.encoder", true, 34, true));
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
