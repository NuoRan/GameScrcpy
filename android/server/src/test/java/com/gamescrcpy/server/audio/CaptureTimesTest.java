package com.gamescrcpy.server.audio;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

class CaptureTimesTest {

    @Test
    void samplesWithinAChunkFollowTheSampleRate() {
        final CaptureTimes times = new CaptureTimes(48_000);
        times.add(0, 1_000_000);
        assertEquals(1_000_000, times.at(0));
        assertEquals(1_020_000, times.at(960));
    }

    @Test
    void eachChunkCarriesItsOwnCaptureTimeSoDriftDoesNotAccumulate() {
        final CaptureTimes times = new CaptureTimes(48_000);
        times.add(0, 1_000_000);
        times.add(1024, 1_021_500);
        assertEquals(1_020_000, times.at(960));
        assertEquals(1_021_500, times.at(1024));
        assertEquals(1_041_500, times.at(1984));
    }

    @Test
    void onlyTheNewestChunksAreKept() {
        final CaptureTimes times = new CaptureTimes(48_000);
        for (int chunk = 0; chunk < 100; chunk++) {
            times.add(chunk * 1024L, 5_000_000L + chunk * 21_333L);
        }
        assertEquals(5_000_000L + 99 * 21_333L, times.at(99 * 1024L));
        assertEquals(5_000_000L + 99 * 21_333L + 20_000, times.at(99 * 1024L + 960));
    }

    @Test
    void nothingRecordedYieldsZero() {
        assertEquals(0, new CaptureTimes(48_000).at(0));
    }
}
