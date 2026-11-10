package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.server.device.EncoderCatalog.Encoder;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class EncoderLadderTest {

    private static final Encoder AVC_HW = new Encoder(VideoCodec.H264, "avc.hw", true);
    private static final Encoder HEVC_HW = new Encoder(VideoCodec.H265, "hevc.hw", true);
    private static final Encoder HEVC_SW = new Encoder(VideoCodec.H265, "hevc.sw", false);
    private static final Encoder AVC_SW = new Encoder(VideoCodec.H264, "avc.sw", false);
    private static final List<Encoder> ALL = List.of(AVC_HW, HEVC_HW, HEVC_SW, AVC_SW);

    private static List<String> walk(EncoderLadder ladder, int width, int height) {
        final List<String> attempts = new ArrayList<>();
        for (EncoderLadder.Attempt attempt; (attempt = ladder.current(width, height)) != null; ladder.failed()) {
            attempts.add(attempt.encoder() + "@" + attempt.size());
        }
        return attempts;
    }

    @Test
    void smallerSizesThenHardwareH264BeforeAnySoftwareEncoder() {
        final EncoderLadder ladder = new EncoderLadder(VideoCodec.H265, ALL, 0, (e, w, h) -> true);
        assertEquals(List.of(
                "hevc.hw@1080x2400", "hevc.hw@864x1920", "hevc.hw@720x1600",
                "avc.hw@1080x2400", "avc.hw@864x1920", "avc.hw@720x1600",
                "hevc.sw@1080x2400", "hevc.sw@864x1920", "hevc.sw@720x1600",
                "avc.sw@1080x2400", "avc.sw@864x1920", "avc.sw@720x1600"), walk(ladder, 1080, 2400));
        assertNull(ladder.current(1080, 2400));
    }

    @Test
    void requestedH264HasNoSecondCodec() {
        final EncoderLadder ladder = new EncoderLadder(VideoCodec.H264, ALL, 1600, (e, w, h) -> true);
        assertEquals(List.of("avc.hw@720x1600", "avc.hw@576x1280", "avc.hw@464x1024",
                "avc.sw@720x1600", "avc.sw@576x1280", "avc.sw@464x1024"), walk(ladder, 1080, 2400));
    }

    @Test
    void undeclaredSizesAreSkippedWithoutTryingThem() {
        final EncoderLadder ladder = new EncoderLadder(VideoCodec.H264, List.of(AVC_HW), 0,
                (e, w, h) -> Math.max(w, h) <= 1920);
        assertEquals(List.of("avc.hw@864x1920", "avc.hw@720x1600", "avc.hw@576x1280"), walk(ladder, 1080, 2400));
    }

    @Test
    void portraitSizesWithADeclaredLandscapeTwinAreTried() {
        final EncoderLadder ladder = new EncoderLadder(VideoCodec.H264, List.of(AVC_HW), 0,
                (e, w, h) -> w <= 2048 && h <= 1088);
        assertEquals(List.of("avc.hw@864x1920", "avc.hw@720x1600", "avc.hw@576x1280"), walk(ladder, 1080, 2400));
    }

    @Test
    void capabilitiesRulingOutEverySizeAreIgnored() {
        final EncoderLadder ladder = new EncoderLadder(VideoCodec.H264, List.of(AVC_HW), 0, (e, w, h) -> false);
        assertEquals(List.of("avc.hw@1080x2400", "avc.hw@864x1920", "avc.hw@720x1600"), walk(ladder, 1080, 2400));
    }

    @Test
    void codecWithoutEncodersGoesStraightToH264() {
        final EncoderLadder ladder = new EncoderLadder(VideoCodec.AV1, List.of(AVC_HW), 0, (e, w, h) -> true);
        assertEquals(new EncoderLadder.Attempt(VideoCodec.H264, "avc.hw", new VideoSize(1080, 2400)),
                ladder.current(1080, 2400));
    }

    @Test
    void sizesFollowTheCurrentRotation() {
        final EncoderLadder ladder = new EncoderLadder(VideoCodec.H264, List.of(AVC_HW), 0, (e, w, h) -> true);
        ladder.failed();
        assertEquals(new VideoSize(864, 1920), ladder.current(1080, 2400).size());
        assertEquals(new VideoSize(1920, 864), ladder.current(2400, 1080).size());
        assertEquals("h264 encoder avc.hw at 1920x864", ladder.current(2400, 1080).toString());
    }

    @Test
    void displaysSmallerThanTheFallbacksTryOnlyTheirOwnSize() {
        final EncoderLadder ladder = new EncoderLadder(VideoCodec.H264, List.of(AVC_HW), 0, (e, w, h) -> true);
        assertEquals(List.of("avc.hw@480x800"), walk(ladder, 480, 800));
    }
}
