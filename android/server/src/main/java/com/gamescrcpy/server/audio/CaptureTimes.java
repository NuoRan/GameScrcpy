package com.gamescrcpy.server.audio;

final class CaptureTimes {

    private static final int CAPACITY = 64;

    private final int sampleRate;
    private final long[] firstSamples = new long[CAPACITY];
    private final long[] captureTimesUs = new long[CAPACITY];
    private int count;

    CaptureTimes(int sampleRate) {
        this.sampleRate = sampleRate;
    }

    synchronized void add(long firstSample, long captureTimeUs) {
        firstSamples[count % CAPACITY] = firstSample;
        captureTimesUs[count % CAPACITY] = captureTimeUs;
        count++;
    }

    synchronized long at(long sample) {
        if (count == 0) {
            return 0;
        }
        final int oldest = Math.max(0, count - CAPACITY);
        int chunk = count - 1;
        while (chunk > oldest && firstSamples[chunk % CAPACITY] > sample) {
            chunk--;
        }
        final long offset = sample - firstSamples[chunk % CAPACITY];
        return captureTimesUs[chunk % CAPACITY] + offset * 1_000_000L / sampleRate;
    }
}
