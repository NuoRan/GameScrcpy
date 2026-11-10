package com.gamescrcpy.server.video;

public record VideoSize(int width, int height) {

    public int longSide() {
        return Math.max(width, height);
    }

    public static VideoSize fit(int displayWidth, int displayHeight, int maxSize, int alignment) {
        final int longSide = Math.max(displayWidth, displayHeight);
        final double scale = maxSize > 0 && longSide > maxSize ? (double) maxSize / longSide : 1.0;
        return new VideoSize(align(displayWidth * scale, alignment, maxSize),
                align(displayHeight * scale, alignment, maxSize));
    }

    private static int align(double value, int alignment, int maxSize) {
        int aligned = (int) Math.round(value / alignment) * alignment;
        if (maxSize > 0 && aligned > maxSize) {
            aligned = maxSize / alignment * alignment;
        }
        return Math.max(alignment, aligned);
    }

    @Override
    public String toString() {
        return width + "x" + height;
    }
}
