package com.gamescrcpy.server.video;

import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.protocol.Messages.ConfigureVideo;

public record VideoSettings(int displayId, VideoCodec codec, int maxSize, int maxFps, long bitrate,
                            int intraRefreshMs) {

    public static final long DEFAULT_BITRATE = 8_000_000;

    public static VideoSettings from(ConfigureVideo message) {
        return new VideoSettings((int) message.displayId, message.codec, message.maxSize, message.maxFps,
                message.bitrate > 0 ? message.bitrate : DEFAULT_BITRATE, message.intraRefreshMs);
    }

    public VideoSettings withBitrate(long newBitrate) {
        return new VideoSettings(displayId, codec, maxSize, maxFps, newBitrate > 0 ? newBitrate : DEFAULT_BITRATE,
                intraRefreshMs);
    }

    public VideoSettings withMaxFps(int newMaxFps) {
        return new VideoSettings(displayId, codec, maxSize, newMaxFps, bitrate, intraRefreshMs);
    }

    public VideoSettings withMaxSize(int newMaxSize) {
        return new VideoSettings(displayId, codec, newMaxSize, maxFps, bitrate, intraRefreshMs);
    }

    public VideoSettings withDisplayId(int newDisplayId) {
        return new VideoSettings(newDisplayId, codec, maxSize, maxFps, bitrate, intraRefreshMs);
    }

    boolean sameLadder(VideoSettings other) {
        return other != null && codec == other.codec && maxSize == other.maxSize && displayId == other.displayId;
    }
}
