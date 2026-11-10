package com.gamescrcpy.server.video;

public final class EncoderException extends Exception {

    private static final long serialVersionUID = 1L;

    public EncoderException(String message, Throwable cause) {
        super(message, cause);
    }

    public String describe() {
        final Throwable cause = getCause();
        return cause == null ? getMessage()
                : getMessage() + ": " + cause.getClass().getSimpleName()
                        + (cause.getMessage() == null ? "" : " " + cause.getMessage());
    }
}
