package com.gamescrcpy.server;

public final class MediaClock {

    private static final long MAX_U32 = 0xFFFF_FFFFL;

    private MediaClock() {
    }

    public static long nowUs() {
        return System.nanoTime() / 1000;
    }

    public static long encodeDelayUs(long ptsUs, long readyUs) {
        return Math.min(Math.max(readyUs - ptsUs, 0), MAX_U32);
    }
}
