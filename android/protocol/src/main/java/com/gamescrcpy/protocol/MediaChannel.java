package com.gamescrcpy.protocol;

public enum MediaChannel {
    VIDEO(Constants.VIDEO_PAYLOAD_LIMIT),
    AUDIO(Constants.AUDIO_PAYLOAD_LIMIT);

    private final int payloadLimit;

    MediaChannel(int payloadLimit) {
        this.payloadLimit = payloadLimit;
    }

    public int payloadLimit() {
        return payloadLimit;
    }
}
