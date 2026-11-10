package com.gamescrcpy.server.video;

import com.gamescrcpy.protocol.Messages.ReceiverReport;

final class RateController {

    static final long OVERUSE_US = 40_000;
    static final long SEVERE_US = 100_000;
    static final long QUIET_US = 20_000;
    static final long HOLD_US = 1_000_000;
    private static final long WINDOW_US = 10_000_000;
    private static final long FLOOR_BPS = 200_000;
    private static final long MIN_STEP_BPS = 100_000;
    private static final long PROBE_SHARE = 70;

    private final long[] minimumTimes = new long[64];
    private final long[] minimumDelays = new long[64];
    private int minimumStart;
    private int minimumCount;

    private long ceiling;
    private long rate;
    private boolean changed;
    private long lastChangeUs;
    private long previousQueueingUs;
    private long lastReportUs = -1;
    private long sentAverageBps = -1;
    private long receivedAverageBps = -1;
    private long capacityBps = -1;
    private long lastQueueingUs;
    private long lastReceivedBps;
    private boolean lastLoaded;

    RateController(long ceiling) {
        this.ceiling = ceiling;
        this.rate = ceiling;
    }

    long rate() {
        return rate;
    }

    long lastQueueingUs() {
        return lastQueueingUs;
    }

    long lastReceivedBps() {
        return lastReceivedBps;
    }

    boolean lastLoaded() {
        return lastLoaded;
    }

    long sentAverageBps() {
        return Math.max(0, sentAverageBps);
    }

    long receivedAverageBps() {
        return Math.max(0, receivedAverageBps);
    }

    boolean atFloor() {
        return rate <= floor();
    }

    void setCeiling(long bps) {
        final boolean atCeiling = rate >= ceiling;
        ceiling = bps;
        rate = atCeiling ? bps : Math.min(rate, bps);
    }

    void restart() {
        minimumCount = 0;
        previousQueueingUs = 0;
        lastReportUs = -1;
        sentAverageBps = -1;
        receivedAverageBps = -1;
        capacityBps = -1;
    }

    long report(ReceiverReport report, long nowUs, long sendQueueUs, long sentBytes) {
        long queueingUs = Math.max(0, sendQueueUs);
        if (report.frames > 0) {
            remember(nowUs, report.delayMinUs);
            queueingUs += Math.max(0, report.delayMeanUs - base());
        }
        final boolean draining = queueingUs < previousQueueingUs * 9 / 10;
        final boolean overuse = !draining
                && (queueingUs > SEVERE_US || queueingUs > OVERUSE_US && previousQueueingUs > OVERUSE_US);
        previousQueueingUs = queueingUs;

        final long receivedBps = report.intervalUs > 0 ? report.bytes * 8_000_000L / report.intervalUs : 0;
        final long elapsedUs = lastReportUs >= 0 && nowUs > lastReportUs ? nowUs - lastReportUs : report.intervalUs;
        lastReportUs = nowUs;
        final long sentBps = elapsedUs > 0 ? sentBytes * 8_000_000L / elapsedUs : 0;
        sentAverageBps = average(sentAverageBps, sentBps);
        receivedAverageBps = average(receivedAverageBps, receivedBps);
        final boolean loaded = receivedBps > 0
                && (sentAverageBps >= rate / 2 || receivedAverageBps < sentAverageBps * 8 / 10);
        if (loaded && !lastLoaded) {
            receivedAverageBps = receivedBps;
        }
        final long carriedBps = Math.min(receivedBps, receivedAverageBps);

        final boolean holding = changed && nowUs - lastChangeUs < HOLD_US;
        lastQueueingUs = queueingUs;
        lastReceivedBps = receivedBps;
        lastLoaded = loaded;
        long next = rate;
        if (overuse) {
            if (!holding || loaded && rate > carriedBps * 95 / 100) {
                long target = rate * 85 / 100;
                if (loaded) {
                    final long share = Math.max(50, Math.min(90, 100 - queueingUs / 10_000));
                    target = Math.min(target, carriedBps * share / 100);
                    capacityBps = carriedBps;
                }
                next = Math.max(floor(), Math.max(rate / 2, target));
            }
        } else if (queueingUs < QUIET_US && report.frames > 0 && !holding && rate < ceiling
                && sentAverageBps * 100 >= rate * PROBE_SHARE) {
            final boolean near = capacityBps > 0 && rate >= capacityBps * 9 / 10;
            final long step = near ? Math.max(rate / 40, MIN_STEP_BPS / 4) : Math.max(rate * 8 / 100, MIN_STEP_BPS);
            next = Math.min(ceiling, rate + step);
            if (capacityBps > 0 && next > capacityBps * 115 / 100) {
                capacityBps = -1;
            }
        }
        if (next != rate) {
            rate = next;
            lastChangeUs = nowUs;
            changed = true;
        }
        return rate;
    }

    private long floor() {
        return Math.min(ceiling, FLOOR_BPS);
    }

    private static long average(long average, long sample) {
        return average < 0 ? sample : average + (sample - average) / 4;
    }

    private void remember(long nowUs, long delayUs) {
        while (minimumCount > 0 && nowUs - minimumTimes[minimumStart] > WINDOW_US) {
            minimumStart = (minimumStart + 1) % minimumTimes.length;
            --minimumCount;
        }
        if (minimumCount == minimumTimes.length) {
            minimumStart = (minimumStart + 1) % minimumTimes.length;
            --minimumCount;
        }
        final int slot = (minimumStart + minimumCount) % minimumTimes.length;
        minimumTimes[slot] = nowUs;
        minimumDelays[slot] = delayUs;
        ++minimumCount;
    }

    private long base() {
        long base = Long.MAX_VALUE;
        for (int i = 0; i < minimumCount; ++i) {
            base = Math.min(base, minimumDelays[(minimumStart + i) % minimumDelays.length]);
        }
        return base;
    }
}
