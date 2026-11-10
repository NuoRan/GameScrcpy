package com.gamescrcpy.protocol;

public enum FrameContext {
    HANDSHAKE(Constants.HANDSHAKE_PAYLOAD_LIMIT),
    CONTROL(Constants.CONTROL_PAYLOAD_LIMIT),
    BULK(Constants.BULK_PAYLOAD_LIMIT),
    COMPANION(Constants.COMPANION_PAYLOAD_LIMIT);

    private final int payloadLimit;

    FrameContext(int payloadLimit) {
        this.payloadLimit = payloadLimit;
    }

    public int payloadLimit() {
        return payloadLimit;
    }

    public boolean allows(MessageType type) {
        if (this != COMPANION) {
            return type.context() == this;
        }
        final int base = type.code() & 0x7FFF;
        return type.context() == COMPANION || base == MessageType.PING.code() || base == MessageType.GOODBYE.code();
    }
}
