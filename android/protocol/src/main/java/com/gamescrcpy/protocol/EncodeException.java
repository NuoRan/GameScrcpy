package com.gamescrcpy.protocol;

public final class EncodeException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    private final EncodeError error;

    public EncodeException(EncodeError error) {
        super(error.name(), null, false, false);
        this.error = error;
    }

    public EncodeError error() {
        return error;
    }
}
