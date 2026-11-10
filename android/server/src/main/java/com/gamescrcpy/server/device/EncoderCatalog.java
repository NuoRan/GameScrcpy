package com.gamescrcpy.server.device;

import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.os.Build;
import com.gamescrcpy.protocol.Enums.VideoCodec;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class EncoderCatalog {

    public static final int MAX_ENCODERS = 32;

    public record Encoder(VideoCodec codec, String name, boolean hardware) {
    }

    public record CodecDescription(String name, boolean encoder, boolean alias, Boolean hardwareAccelerated,
                                   List<String> mimeTypes) {
    }

    private static List<Encoder> cached;
    private static final Map<String, MediaCodecInfo> INFOS = new HashMap<>();

    private EncoderCatalog() {
    }

    public static synchronized List<Encoder> available() {
        if (cached == null) {
            final List<CodecDescription> descriptions = new ArrayList<>();
            for (MediaCodecInfo info : new MediaCodecList(MediaCodecList.REGULAR_CODECS).getCodecInfos()) {
                final boolean modern = Build.VERSION.SDK_INT >= 29;
                descriptions.add(new CodecDescription(info.getName(), info.isEncoder(), modern && info.isAlias(),
                        modern ? info.isHardwareAccelerated() : null, List.of(info.getSupportedTypes())));
                INFOS.put(info.getName(), info);
            }
            cached = List.copyOf(select(descriptions));
        }
        return cached;
    }

    public static synchronized boolean supportsSize(Encoder encoder, int width, int height) {
        available();
        final MediaCodecInfo info = INFOS.get(encoder.name());
        if (info == null) {
            return false;
        }
        try {
            final MediaCodecInfo.VideoCapabilities video = info.getCapabilitiesForType(mimeType(encoder.codec()))
                    .getVideoCapabilities();
            return video != null && video.isSizeSupported(width, height);
        } catch (IllegalArgumentException e) {
            return false;
        }
    }

    public static List<Encoder> forCodec(List<Encoder> encoders, VideoCodec codec) {
        final List<Encoder> result = new ArrayList<>();
        for (Encoder encoder : encoders) {
            if (encoder.codec() == codec) {
                result.add(encoder);
            }
        }
        return result;
    }

    public static String mimeType(VideoCodec codec) {
        return switch (codec) {
            case H264 -> "video/avc";
            case H265 -> "video/hevc";
            case AV1 -> "video/av01";
        };
    }

    static List<Encoder> select(List<CodecDescription> descriptions) {
        final List<Encoder> result = new ArrayList<>();
        for (VideoCodec codec : VideoCodec.values()) {
            for (boolean hardwarePass : new boolean[] {true, false}) {
                for (CodecDescription description : descriptions) {
                    if (!description.encoder() || description.alias()
                            || !description.mimeTypes().contains(mimeType(codec))) {
                        continue;
                    }
                    final boolean hardware = description.hardwareAccelerated() != null
                            ? description.hardwareAccelerated() : !isSoftwareName(description.name());
                    if (hardware == hardwarePass && result.size() < MAX_ENCODERS) {
                        result.add(new Encoder(codec, description.name(), hardware));
                    }
                }
            }
        }
        return result;
    }

    static boolean isSoftwareName(String name) {
        return name.startsWith("OMX.google.") || name.startsWith("c2.android.");
    }
}
