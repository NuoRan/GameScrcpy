package com.gamescrcpy.server.wrappers;

public final class PlatformException extends Exception {

    private static final long serialVersionUID = 1L;

    public PlatformException(String message) {
        super(message);
    }

    public PlatformException(String message, Throwable cause) {
        super(message, cause);
    }
}
