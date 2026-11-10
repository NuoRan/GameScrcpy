package com.gamescrcpy.server.video;

import java.util.Arrays;

final class LinkFallback {

    enum Change { NONE, DOWN, UP }

    record Observation(long nowUs, long queueingUs, boolean loaded, long rateBps, long sentBps, long receivedBps,
                       boolean atFloor, boolean atCeiling) {

        static Observation of(long nowUs, RateController rate, long ceilingBps) {
            return new Observation(nowUs, rate.lastQueueingUs(), rate.lastLoaded(), rate.rate(),
                    rate.sentAverageBps(), rate.receivedAverageBps(), rate.atFloor(), rate.rate() >= ceilingBps);
        }
    }

    static final int WINDOW = 10;
    static final int STUCK_REPORTS = 8;
    static final long SETTLE_US = 3_000_000;
    static final long QUIET_US = 10_000_000;
    static final int MIN_LONG_SIDE = 640;
    private static final int[] FRAME_RATES = {30, 15};
    private static final int NOMINAL_FPS = 60;
    private static final int MAX_LEVEL = 15;

    private final long[] floors = new long[MAX_LEVEL + 1];
    private final boolean[] recent = new boolean[WINDOW];
    private int recentNext;
    private int recentCount;
    private int recentStuck;
    private int level;
    private long quietSinceUs = -1;
    private long lastChangeUs = Long.MIN_VALUE / 2;
    private int lastUpTo = -1;
    private long lastUpUs = Long.MIN_VALUE / 2;

    int level() {
        return level;
    }

    long floorAt(int at) {
        return floors[at];
    }

    void reset() {
        level = 0;
        Arrays.fill(floors, 0);
        forget();
        quietSinceUs = -1;
        lastUpTo = -1;
    }

    static int resolutionSteps(int base) {
        int steps = 0;
        for (int side = base; steps < MAX_LEVEL && side * 3 / 4 >= MIN_LONG_SIDE; side = side * 3 / 4) {
            ++steps;
        }
        return steps;
    }

    static int levels(int base, int clientMaxFps) {
        return Math.min(MAX_LEVEL, resolutionSteps(base) + frameRates(clientMaxFps).length);
    }

    int maxSize(int base, int clientMaxSize) {
        if (level == 0) {
            return clientMaxSize;
        }
        int side = base;
        for (int step = Math.min(level, resolutionSteps(base)); step > 0; --step) {
            side = side * 3 / 4;
        }
        return side;
    }

    int maxFps(int base, int clientMaxFps) {
        final int step = level - resolutionSteps(base);
        final int[] rates = frameRates(clientMaxFps);
        return step <= 0 || rates.length == 0 ? clientMaxFps : rates[Math.min(step, rates.length) - 1];
    }

    Change report(Observation seen, int levels) {
        level = Math.min(level, levels);
        final long nowUs = seen.nowUs();
        final boolean queued = seen.queueingUs() > RateController.OVERUSE_US;
        final boolean stuck = queued && seen.loaded() && seen.sentBps() > seen.receivedBps()
                && (seen.sentBps() > seen.rateBps() * 3 / 2 || seen.atFloor());
        remember(stuck);
        quietSinceUs = queued ? -1 : quietSinceUs < 0 ? nowUs : quietSinceUs;

        if (stuck && level < levels && recentCount == WINDOW && recentStuck >= STUCK_REPORTS
                && nowUs - lastChangeUs >= SETTLE_US) {
            final boolean bounced = lastUpTo == level && nowUs - lastUpUs < QUIET_US;
            floors[level] = bounced ? Math.max(floors[level] * 3 / 2, seen.sentBps()) : seen.sentBps();
            ++level;
            lastChangeUs = nowUs;
            forget();
            return Change.DOWN;
        }
        if (level > 0 && quietSinceUs >= 0 && nowUs - quietSinceUs >= QUIET_US && nowUs - lastChangeUs >= QUIET_US
                && (seen.atCeiling() || seen.rateBps() >= floors[level - 1] * 3 / 2)) {
            --level;
            lastUpTo = level;
            lastUpUs = nowUs;
            lastChangeUs = nowUs;
            quietSinceUs = -1;
            return Change.UP;
        }
        return Change.NONE;
    }

    private void remember(boolean stuck) {
        if (recentCount == WINDOW) {
            recentStuck -= recent[recentNext] ? 1 : 0;
        } else {
            ++recentCount;
        }
        recent[recentNext] = stuck;
        recentStuck += stuck ? 1 : 0;
        recentNext = (recentNext + 1) % WINDOW;
    }

    private void forget() {
        recentNext = 0;
        recentCount = 0;
        recentStuck = 0;
    }

    private static int[] frameRates(int clientMaxFps) {
        final int cap = clientMaxFps > 0 ? clientMaxFps : NOMINAL_FPS;
        int count = 0;
        for (int fps : FRAME_RATES) {
            if (fps < cap) {
                ++count;
            }
        }
        final int[] rates = new int[count];
        int next = 0;
        for (int fps : FRAME_RATES) {
            if (fps < cap) {
                rates[next++] = fps;
            }
        }
        return rates;
    }
}
