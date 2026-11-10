package com.gamescrcpy.server.audio;

import com.gamescrcpy.protocol.Enums.ErrorCode;

final class AudioException extends Exception {

    private static final long serialVersionUID = 1L;

    final ErrorCode code;

    AudioException(ErrorCode code, String message) {
        super(message);
        this.code = code;
    }

    AudioException(ErrorCode code, String message, Throwable cause) {
        super(message, cause);
        this.code = code;
    }
}
