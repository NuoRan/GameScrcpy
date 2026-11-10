package com.gamescrcpy.server.input;

public record TouchGeometry(int session, int displayId, int displayWidth, int displayHeight) {

    private static final float FULL_SCALE = 0xFFFF;

    float displayX(int x) {
        return clamp(x * (float) displayWidth / FULL_SCALE, displayWidth);
    }

    float displayY(int y) {
        return clamp(y * (float) displayHeight / FULL_SCALE, displayHeight);
    }

    private static float clamp(float value, int size) {
        return Math.max(0f, Math.min(value, size - 1));
    }
}
