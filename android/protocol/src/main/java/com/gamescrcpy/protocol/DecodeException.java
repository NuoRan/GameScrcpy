package com.gamescrcpy.protocol;

public final class DecodeException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    private final DecodeError error;

    public DecodeException(DecodeError error) {
        super(error.specName(), null, false, false);
        this.error = error;
    }

    public DecodeError error() {
        return error;
    }
}
