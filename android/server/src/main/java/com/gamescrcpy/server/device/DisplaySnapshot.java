package com.gamescrcpy.server.device;

import com.gamescrcpy.protocol.Messages.Display;

public record DisplaySnapshot(int displayId, int width, int height, int rotation, int layerStack, int flags,
                              int densityDpi, long refreshMilliHz, int state, int safeLeft, int safeTop,
                              int safeRight, int safeBottom) {

    public static final int STATE_ON = 2;

    public boolean isOn() {
        return state == STATE_ON;
    }

    public boolean sameGeometry(DisplaySnapshot other) {
        return other != null && width == other.width && height == other.height && rotation == other.rotation;
    }

    public boolean sameReport(DisplaySnapshot other) {
        return other != null && displayId == other.displayId && sameGeometry(other) && densityDpi == other.densityDpi
                && refreshMilliHz == other.refreshMilliHz && safeLeft == other.safeLeft && safeTop == other.safeTop
                && safeRight == other.safeRight && safeBottom == other.safeBottom;
    }

    public Display toProtocol() {
        final Display display = new Display();
        fill(display);
        return display;
    }

    public void fill(Display display) {
        display.displayId = displayId & 0xFFFFFFFFL;
        display.width = u16(width);
        display.height = u16(height);
        display.rotation = rotation & 3;
        display.densityDpi = u16(densityDpi);
        display.refreshMilliHz = Math.max(0, Math.min(refreshMilliHz, 0xFFFFFFFFL));
        display.safeLeft = u16(safeLeft);
        display.safeTop = u16(safeTop);
        display.safeRight = u16(safeRight);
        display.safeBottom = u16(safeBottom);
    }

    private static int u16(int value) {
        return Math.max(0, Math.min(value, 0xFFFF));
    }
}
