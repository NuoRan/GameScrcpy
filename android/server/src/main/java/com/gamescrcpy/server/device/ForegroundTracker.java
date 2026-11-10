package com.gamescrcpy.server.device;

import android.content.pm.ApplicationInfo;
import com.gamescrcpy.protocol.Messages.ForegroundAppChanged;
import java.nio.charset.StandardCharsets;

public final class ForegroundTracker {

    private static final int LIMIT = 255;

    private String packageName = "";
    private String activity = "";
    private boolean game;

    public synchronized boolean update(String newPackage, String newActivity, boolean newGame) {
        final String nextPackage = clip(newPackage);
        final String nextActivity = clip(newActivity);
        final boolean nextGame = newGame && !nextPackage.isEmpty();
        if (nextPackage.equals(packageName) && nextActivity.equals(activity) && nextGame == game) {
            return false;
        }
        packageName = nextPackage;
        activity = nextActivity;
        game = nextGame;
        return true;
    }

    public synchronized ForegroundAppChanged message() {
        final ForegroundAppChanged message = new ForegroundAppChanged();
        message.packageName = packageName;
        message.activity = activity;
        message.flags = game ? ForegroundAppChanged.GAME : 0;
        return message;
    }

    @SuppressWarnings("deprecation")
    public static boolean isGame(int category, int flags) {
        return category == ApplicationInfo.CATEGORY_GAME || (flags & ApplicationInfo.FLAG_IS_GAME) != 0;
    }

    static String clip(String text) {
        if (text == null) {
            return "";
        }
        String clipped = text;
        while (clipped.getBytes(StandardCharsets.UTF_8).length > LIMIT) {
            int end = clipped.length() - 1;
            if (end > 0 && Character.isLowSurrogate(clipped.charAt(end))) {
                --end;
            }
            clipped = clipped.substring(0, end);
        }
        return clipped;
    }
}
