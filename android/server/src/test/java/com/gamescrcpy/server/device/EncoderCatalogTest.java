package com.gamescrcpy.server.device;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.server.device.EncoderCatalog.CodecDescription;
import com.gamescrcpy.server.device.EncoderCatalog.Encoder;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class EncoderCatalogTest {

    private static CodecDescription encoder(String name, Boolean hardware, String... types) {
        return new CodecDescription(name, true, false, hardware, List.of(types));
    }

    @Test
    void groupsByCodecWithHardwareFirstInListOrder() {
        final List<Encoder> encoders = EncoderCatalog.select(List.of(
                encoder("c2.android.avc.encoder", false, "video/avc"),
                encoder("c2.qti.hevc.encoder", true, "video/hevc"),
                encoder("c2.qti.avc.encoder", true, "video/avc"),
                new CodecDescription("c2.qti.avc.decoder", false, false, true, List.of("video/avc")),
                new CodecDescription("OMX.qcom.video.encoder.avc", true, true, true, List.of("video/avc")),
                encoder("c2.android.aac.encoder", false, "audio/mp4a-latm"),
                encoder("c2.android.av1.encoder", false, "video/av01"),
                encoder("c2.exynos.multi.encoder", true, "video/hevc", "video/avc")));

        assertEquals(List.of(
                new Encoder(VideoCodec.H264, "c2.qti.avc.encoder", true),
                new Encoder(VideoCodec.H264, "c2.exynos.multi.encoder", true),
                new Encoder(VideoCodec.H264, "c2.android.avc.encoder", false),
                new Encoder(VideoCodec.H265, "c2.qti.hevc.encoder", true),
                new Encoder(VideoCodec.H265, "c2.exynos.multi.encoder", true),
                new Encoder(VideoCodec.AV1, "c2.android.av1.encoder", false)), encoders);
    }

    @Test
    void beforeAndroid10TheNameDecidesWhatIsSoftware() {
        final List<Encoder> encoders = EncoderCatalog.select(List.of(
                encoder("OMX.google.h264.encoder", null, "video/avc"),
                encoder("OMX.qcom.video.encoder.avc", null, "video/avc")));
        assertEquals("OMX.qcom.video.encoder.avc", encoders.get(0).name());
        assertTrue(encoders.get(0).hardware());
        assertFalse(encoders.get(1).hardware());
        assertTrue(EncoderCatalog.isSoftwareName("c2.android.hevc.encoder"));
        assertFalse(EncoderCatalog.isSoftwareName("c2.mtk.avc.encoder"));
    }

    @Test
    void listIsCappedAtTheProtocolLimit() {
        final List<CodecDescription> many = new ArrayList<>();
        for (int i = 0; i < 40; ++i) {
            many.add(encoder("vendor.encoder." + i, true, "video/avc"));
        }
        assertEquals(EncoderCatalog.MAX_ENCODERS, EncoderCatalog.select(many).size());
    }

    @Test
    void forCodecKeepsTheOrder() {
        final List<Encoder> encoders = List.of(new Encoder(VideoCodec.H264, "a", true),
                new Encoder(VideoCodec.H265, "b", true), new Encoder(VideoCodec.H264, "c", false));
        assertEquals(List.of(encoders.get(0), encoders.get(2)), EncoderCatalog.forCodec(encoders, VideoCodec.H264));
        assertEquals("video/hevc", EncoderCatalog.mimeType(VideoCodec.H265));
    }
}
