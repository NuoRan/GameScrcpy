package com.gamescrcpy.server.device;

import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.SystemPanelAction;
import com.gamescrcpy.protocol.Messages.SystemPanel;
import com.gamescrcpy.server.Log;

public final class SystemPanels {

    public static final String NOTICE_PREFIX = "system panel: ";

    public static final class Failure extends Exception {
        private static final long serialVersionUID = 1L;

        private final ErrorCode code;

        public Failure(ErrorCode code, String message) {
            super(message);
            this.code = code;
        }

        public Failure(ErrorCode code, String message, Throwable cause) {
            super(message, cause);
            this.code = code;
        }

        public ErrorCode code() {
            return code;
        }
    }

    public interface Platform {
        void show(SystemPanelAction action) throws Failure;
    }

    public interface Notices {
        void panelFailed(ErrorCode code, String message);
    }

    private final Platform platform;
    private final Notices notices;

    public SystemPanels(Platform platform, Notices notices) {
        this.platform = platform;
        this.notices = notices;
    }

    public static Platform unavailable(String reason) {
        return action -> {
            throw new Failure(ErrorCode.UNSUPPORTED_ANDROID,
                    "the status bar service cannot be reached to " + describe(action) + " (" + reason + ")");
        };
    }

    public static String describe(SystemPanelAction action) {
        return switch (action) {
            case EXPAND_NOTIFICATIONS -> "expand the notifications";
            case EXPAND_QUICK_SETTINGS -> "expand the quick settings";
            case COLLAPSE -> "collapse the panels";
        };
    }

    public void handle(SystemPanel request) {
        final SystemPanelAction action = request.action;
        try {
            platform.show(action);
            Log.i("asked the status bar to " + describe(action));
        } catch (Failure e) {
            notices.panelFailed(e.code(), NOTICE_PREFIX + e.getMessage());
        } catch (RuntimeException e) {
            notices.panelFailed(ErrorCode.INTERNAL_ERROR, NOTICE_PREFIX + "cannot " + describe(action) + ": " + e);
        }
    }
}
