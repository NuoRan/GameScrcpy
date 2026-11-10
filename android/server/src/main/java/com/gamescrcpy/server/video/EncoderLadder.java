package com.gamescrcpy.server.video;

import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.server.device.EncoderCatalog;
import com.gamescrcpy.server.device.EncoderCatalog.Encoder;
import java.util.ArrayList;
import java.util.List;

public final class EncoderLadder {

    static final int[] FALLBACK_LONG_SIDES = {1920, 1600, 1280, 1024, 800};
    static final int MAX_SIZES_PER_ENCODER = 3;

    public interface SizeSupport {
        boolean supports(Encoder encoder, int width, int height);
    }

    public record Attempt(VideoCodec codec, String encoder, VideoSize size) {
        @Override
        public String toString() {
            return codec.specName() + " encoder " + encoder + " at " + size;
        }
    }

    private final List<List<Encoder>> rungs = new ArrayList<>();
    private final int maxSize;
    private final SizeSupport support;
    private int codecIndex;
    private int encoderIndex;
    private int sizeIndex;

    public EncoderLadder(VideoCodec requested, List<Encoder> encoders, int maxSize, SizeSupport support) {
        this.maxSize = maxSize;
        this.support = support;
        final List<VideoCodec> codecs = requested == VideoCodec.H264 ? List.of(requested)
                : List.of(requested, VideoCodec.H264);
        for (boolean hardware : new boolean[] {true, false}) {
            for (VideoCodec codec : codecs) {
                final List<Encoder> rung = new ArrayList<>();
                for (Encoder encoder : EncoderCatalog.forCodec(encoders, codec)) {
                    if (encoder.hardware() == hardware) {
                        rung.add(encoder);
                    }
                }
                if (!rung.isEmpty()) {
                    rungs.add(rung);
                }
            }
        }
    }

    public Attempt current(int displayWidth, int displayHeight) {
        while (codecIndex < rungs.size()) {
            final List<Encoder> encoders = rungs.get(codecIndex);
            if (encoderIndex >= encoders.size()) {
                ++codecIndex;
                encoderIndex = 0;
                sizeIndex = 0;
                continue;
            }
            final Encoder encoder = encoders.get(encoderIndex);
            final List<VideoSize> sizes = sizes(encoder, displayWidth, displayHeight);
            if (sizeIndex >= sizes.size()) {
                ++encoderIndex;
                sizeIndex = 0;
                continue;
            }
            return new Attempt(encoder.codec(), encoder.name(), sizes.get(sizeIndex));
        }
        return null;
    }

    public void failed() {
        ++sizeIndex;
    }

    List<VideoSize> sizes(Encoder encoder, int displayWidth, int displayHeight) {
        final List<VideoSize> candidates = new ArrayList<>();
        final VideoSize requested = VideoSize.fit(displayWidth, displayHeight, maxSize, 8);
        candidates.add(requested);
        for (int longSide : FALLBACK_LONG_SIDES) {
            if (longSide < requested.longSide()) {
                candidates.add(VideoSize.fit(displayWidth, displayHeight, longSide, 16));
            }
        }
        final List<VideoSize> sizes = pick(candidates, encoder, true);
        return sizes.isEmpty() ? pick(candidates, encoder, false) : sizes;
    }

    private List<VideoSize> pick(List<VideoSize> candidates, Encoder encoder, boolean declaredOnly) {
        final List<VideoSize> sizes = new ArrayList<>();
        for (VideoSize size : candidates) {
            if (sizes.size() < MAX_SIZES_PER_ENCODER && !sizes.contains(size)
                    && (!declaredOnly || declared(encoder, size))) {
                sizes.add(size);
            }
        }
        return sizes;
    }

    private boolean declared(Encoder encoder, VideoSize size) {
        return support.supports(encoder, size.width(), size.height())
                || support.supports(encoder, size.height(), size.width());
    }
}
