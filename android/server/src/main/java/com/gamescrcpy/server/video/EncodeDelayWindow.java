package com.gamescrcpy.server.video;

import java.util.Locale;

final class EncodeDelayWindow {

    static final long WINDOW_US = 60_000_000;

    private long startUs;
    private long frames;
    private long sumUs;
    private long maxUs;

    String add(long readyUs, long delayUs) {
        if (frames == 0) {
            startUs = readyUs;
        }
        ++frames;
        sumUs += delayUs;
        maxUs = Math.max(maxUs, delayUs);
        if (readyUs - startUs < WINDOW_US) {
            return null;
        }
        final String line = String.format(Locale.ROOT,
                "frames %d, capture to encoder output mean %.1f ms, largest %d ms", frames,
                sumUs / 1000.0 / frames, maxUs / 1000);
        startUs = readyUs;
        frames = 0;
        sumUs = 0;
        maxUs = 0;
        return line;
    }
}
