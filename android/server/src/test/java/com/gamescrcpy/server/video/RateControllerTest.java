package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Messages.ReceiverReport;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class RateControllerTest {

    private static final long SECOND = 1_000_000;
    private static final long FRAME_US = 16_667;
    private static final long REPORT_US = 200_000;
    private static final long PROPAGATION_US = 2_000;
    private static final long KEY_FRAME_US = 10 * SECOND;

    private record Step(long fromUs, long bps) {
    }

    private record Sample(long timeUs, long rate, long meanDelayUs, boolean decrease) {
    }

    private static final class Outcome {
        final List<Sample> samples = new ArrayList<>();

        List<Sample> between(long fromUs, long toUs) {
            return samples.stream().filter(s -> s.timeUs() >= fromUs && s.timeUs() < toUs).toList();
        }

        long decreases(long fromUs, long toUs) {
            return between(fromUs, toUs).stream().filter(Sample::decrease).count();
        }

        long maxDelay(long fromUs, long toUs) {
            return between(fromUs, toUs).stream().mapToLong(Sample::meanDelayUs).max().orElse(0);
        }

        double meanDelay(long fromUs, long toUs) {
            return between(fromUs, toUs).stream().filter(s -> s.meanDelayUs() >= 0)
                    .mapToLong(Sample::meanDelayUs).average().orElse(0);
        }

        long minRate(long fromUs, long toUs) {
            return between(fromUs, toUs).stream().mapToLong(Sample::rate).min().orElse(0);
        }

        long maxRate(long fromUs, long toUs) {
            return between(fromUs, toUs).stream().mapToLong(Sample::rate).max().orElse(0);
        }

        String trace(long fromUs, long toUs) {
            final StringBuilder text = new StringBuilder();
            for (Sample s : between(fromUs, toUs)) {
                text.append(String.format("%6.1f s %6d kbit/s %6.1f ms%s%n", s.timeUs() / 1e6, s.rate() / 1000,
                        s.meanDelayUs() / 1e3, s.decrease() ? " down" : ""));
            }
            return text.toString();
        }
    }

    private static long capacityAt(List<Step> link, long timeUs) {
        long bps = link.get(0).bps();
        for (Step step : link) {
            if (step.fromUs() <= timeUs) {
                bps = step.bps();
            }
        }
        return bps;
    }

    private static Outcome simulate(long ceiling, List<Step> link, long durationUs) {
        return simulate(ceiling, link, Long.MAX_VALUE, durationUs);
    }

    private static Outcome simulate(long ceiling, List<Step> link, long encoderBps, long durationUs) {
        return simulate(ceiling, link, List.of(new Step(0, encoderBps)), durationUs);
    }

    private static Outcome simulate(long ceiling, List<Step> link, List<Step> demand, long durationUs) {
        final RateController controller = new RateController(ceiling);
        final Outcome outcome = new Outcome();
        final ArrayDeque<long[]> inFlight = new ArrayDeque<>();
        long linkFreeUs = 0;
        long nextFrameUs = 0;
        long nextReportUs = REPORT_US;
        long previousRate = controller.rate();
        long sentBytes = 0;
        while (Math.min(nextFrameUs, nextReportUs) < durationUs) {
            if (nextFrameUs < nextReportUs) {
                final boolean key = nextFrameUs % KEY_FRAME_US < FRAME_US;
                final long bits = Math.min(controller.rate(), capacityAt(demand, nextFrameUs)) / 60 * (key ? 4 : 1);
                final long start = Math.max(nextFrameUs, linkFreeUs);
                linkFreeUs = start + bits * SECOND / capacityAt(link, start);
                inFlight.add(new long[] {nextFrameUs, linkFreeUs + PROPAGATION_US, bits / 8});
                sentBytes += bits / 8;
                nextFrameUs += FRAME_US;
            } else {
                final ReceiverReport report = new ReceiverReport();
                report.videoSession = 1;
                report.intervalUs = REPORT_US;
                long sum = 0;
                while (!inFlight.isEmpty() && inFlight.peek()[1] <= nextReportUs) {
                    final long[] frame = inFlight.poll();
                    final long delay = frame[1] - frame[0];
                    report.delayMinUs = report.frames == 0 ? delay : Math.min(report.delayMinUs, delay);
                    report.delayMaxUs = report.frames == 0 ? delay : Math.max(report.delayMaxUs, delay);
                    sum += delay;
                    report.bytes += frame[2];
                    ++report.frames;
                }
                if (report.frames > 0) {
                    report.delayMeanUs = sum / report.frames;
                }
                final long rate = controller.report(report, nextReportUs + PROPAGATION_US, 0, sentBytes);
                sentBytes = 0;
                outcome.samples.add(new Sample(nextReportUs, rate, report.frames > 0 ? report.delayMeanUs : -1,
                        rate < previousRate));
                previousRate = rate;
                nextReportUs += REPORT_US;
            }
        }
        return outcome;
    }

    private static ReceiverReport report(int frames, long bytes, long delayMinUs, long delayMeanUs) {
        final ReceiverReport report = new ReceiverReport();
        report.videoSession = 1;
        report.intervalUs = REPORT_US;
        report.frames = frames;
        report.bytes = bytes;
        report.delayMinUs = delayMinUs;
        report.delayMeanUs = delayMeanUs;
        report.delayMaxUs = delayMeanUs;
        return report;
    }

    @Test
    void aLinkWiderThanTheSettingKeepsTheSetting() {
        final Outcome wide = simulate(8_000_000, List.of(new Step(0, 20_000_000)), 60 * SECOND);
        assertEquals(0, wide.decreases(0, 60 * SECOND), wide.trace(0, 60 * SECOND));
        assertEquals(8_000_000, wide.minRate(0, 60 * SECOND));
    }

    @Test
    void aSteadyLinkHoldsTheRateNearItsCapacityWithoutOscillating() {
        final Outcome steady = simulate(8_000_000, List.of(new Step(0, 4_000_000)), 120 * SECOND);
        final String trace = steady.trace(20 * SECOND, 120 * SECOND);
        assertTrue(steady.minRate(20 * SECOND, 120 * SECOND) >= 3_000_000, trace);
        assertTrue(steady.maxRate(20 * SECOND, 120 * SECOND) <= 4_400_000, trace);
        assertTrue(steady.meanDelay(20 * SECOND, 120 * SECOND) < 40_000, trace);
        assertTrue(steady.maxDelay(20 * SECOND, 120 * SECOND) < 100_000, trace);
        assertTrue(steady.decreases(20 * SECOND, 120 * SECOND) <= 30, trace);
    }

    @Test
    void aNarrowerLinkCutsTheRateOnceWithoutFallingFarBelowIt() {
        final Outcome narrower = simulate(8_000_000,
                List.of(new Step(0, 10_000_000), new Step(20 * SECOND, 6_000_000)), 60 * SECOND);
        final String trace = narrower.trace(19 * SECOND, 30 * SECOND);
        assertEquals(0, narrower.decreases(0, 20 * SECOND), trace);
        assertTrue(narrower.maxRate(20 * SECOND + 2 * REPORT_US, 60 * SECOND) <= 6_500_000, trace);
        assertTrue(narrower.minRate(20 * SECOND, 60 * SECOND) >= 4_200_000, trace);
        assertTrue(narrower.maxDelay(20 * SECOND, 60 * SECOND) < 250_000, trace);
        assertTrue(narrower.maxDelay(22 * SECOND, 60 * SECOND) < 100_000, narrower.trace(22 * SECOND, 60 * SECOND));
    }

    @Test
    void aCollapsedLinkIsDrainedAndTheRateClimbsBackWhenItRecovers() {
        final Outcome collapse = simulate(8_000_000, List.of(new Step(0, 10_000_000), new Step(20 * SECOND, 2_000_000),
                new Step(60 * SECOND, 10_000_000)), 150 * SECOND);
        final String trace = collapse.trace(19 * SECOND, 40 * SECOND);
        assertEquals(0, collapse.decreases(0, 20 * SECOND), trace);
        assertTrue(collapse.maxRate(21 * SECOND, 60 * SECOND) <= 2_200_000, trace);
        assertTrue(collapse.maxDelay(30 * SECOND, 60 * SECOND) < 100_000, collapse.trace(30 * SECOND, 60 * SECOND));
        assertEquals(8_000_000, collapse.minRate(100 * SECOND, 150 * SECOND), collapse.trace(60 * SECOND, 150 * SECOND));
    }

    @Test
    void aLinkChangingInStepsIsFollowedEachTimeWithoutOscillating() {
        final Outcome steps = simulate(8_000_000, List.of(new Step(0, 10_000_000), new Step(20 * SECOND, 3_000_000),
                new Step(40 * SECOND, 10_000_000), new Step(100 * SECOND, 1_500_000),
                new Step(120 * SECOND, 10_000_000)), 180 * SECOND);
        assertEquals(0, steps.decreases(0, 20 * SECOND), steps.trace(0, 20 * SECOND));
        assertEquals(8_000_000, steps.minRate(0, 20 * SECOND));
        assertFollowed(steps, 20 * SECOND, 40 * SECOND, 3_000_000, 4 * SECOND);
        assertFollowed(steps, 100 * SECOND, 120 * SECOND, 1_500_000, 11 * SECOND);
        assertTrue(steps.maxDelay(100 * SECOND, 120 * SECOND) < 3_000_000, steps.trace(100 * SECOND, 111 * SECOND));
        assertEquals(8_000_000, steps.minRate(70 * SECOND, 100 * SECOND), steps.trace(40 * SECOND, 100 * SECOND));
        assertEquals(8_000_000, steps.minRate(150 * SECOND, 180 * SECOND), steps.trace(120 * SECOND, 180 * SECOND));
    }

    private static void assertFollowed(Outcome outcome, long stepUs, long endUs, long capacity, long settleUs) {
        final long fromUs = stepUs + settleUs;
        final String trace = outcome.trace(stepUs, endUs);
        assertTrue(outcome.maxRate(fromUs, endUs) <= capacity * 11 / 10, trace);
        assertTrue(outcome.minRate(fromUs, endUs) >= capacity * 7 / 10, trace);
        assertTrue(outcome.maxDelay(fromUs, endUs) < 100_000, trace);
        assertTrue(outcome.decreases(fromUs, endUs) <= (endUs - fromUs) / (3 * SECOND), trace);
    }

    @Test
    void aLinkFarNarrowerThanTheSettingIsFollowedToo() {
        final Outcome narrow = simulate(8_000_000, List.of(new Step(0, 800_000)), 120 * SECOND);
        final String trace = narrow.trace(0, 120 * SECOND);
        assertTrue(narrow.maxRate(10 * SECOND, 120 * SECOND) <= 1_200_000, trace);
        assertTrue(narrow.maxRate(40 * SECOND, 120 * SECOND) <= 1_000_000, trace);
        assertTrue(narrow.maxDelay(20 * SECOND, 120 * SECOND) < 300_000, trace);
    }

    @Test
    void anEncoderGivingLessThanItsTargetStillMeetsANarrowLinkQuickly() {
        final Outcome narrow = simulate(8_000_000, List.of(new Step(0, 800_000)), 1_600_000, 60 * SECOND);
        final String trace = narrow.trace(0, 15 * SECOND);
        assertTrue(narrow.maxRate(2 * SECOND, 60 * SECOND) <= 1_000_000, trace);
        assertTrue(narrow.maxDelay(10 * SECOND, 60 * SECOND) < 300_000, trace);
    }

    @Test
    void theSendQueueAloneTellsOfOverload() {
        final RateController controller = new RateController(8_000_000);
        controller.report(report(12, 200_000, 50_000, 52_000), SECOND, 0, 200_000);
        assertEquals(8_000_000, controller.rate());
        assertEquals(8_000_000, controller.report(report(0, 0, 0, 0), SECOND + REPORT_US, 60_000, 0));
        assertEquals(6_800_000, controller.report(report(0, 0, 0, 0), SECOND + 2 * REPORT_US, 60_000, 0));
    }

    @Test
    void aStillScreenSendingLittleIsNotTakenForANarrowLink() {
        final RateController controller = new RateController(8_000_000);
        long now = SECOND;
        controller.report(report(12, 200_000, 5_000, 6_000), now, 0, 200_000);
        for (int i = 0; i < 5; ++i) {
            now += REPORT_US;
            controller.report(report(1, 180, 5_000, 30_000 + i * 2_000), now, 0, 180);
        }
        assertEquals(8_000_000, controller.rate());
        now += REPORT_US;
        controller.report(report(1, 180, 5_000, 200_000), now, 0, 180);
        assertEquals(6_800_000, controller.rate());
        for (int i = 0; i < 25; ++i) {
            now += REPORT_US;
            controller.report(report(12, 150_000, 5_000, 20_000), now, 0, 150_000);
        }
        assertEquals(8_000_000, controller.rate());
    }

    @Test
    void aStillScreenAfterACutDoesNotClimbBackToTheSettingAndAMovingOneAfterItFindsTheLinkWithoutABacklog() {
        final List<Step> link = List.of(new Step(0, 10_000_000));
        final List<Step> demand = List.of(new Step(0, Long.MAX_VALUE), new Step(20 * SECOND, 2_000_000),
                new Step(60 * SECOND, Long.MAX_VALUE));
        final Outcome outcome = simulate(32_000_000, link, demand, 80 * SECOND);
        final String trace = outcome.trace(15 * SECOND, 80 * SECOND);
        assertTrue(outcome.maxRate(25 * SECOND, 60 * SECOND) <= 12_000_000, trace);
        assertTrue(outcome.maxDelay(60 * SECOND, 80 * SECOND) < RateController.SEVERE_US, trace);
        assertTrue(outcome.minRate(65 * SECOND, 80 * SECOND) >= 7_000_000, trace);
    }

    @Test
    void theClientsSettingCapsTheRate() {
        final RateController controller = new RateController(8_000_000);
        controller.setCeiling(4_000_000);
        assertEquals(4_000_000, controller.rate());
        controller.setCeiling(6_000_000);
        assertEquals(6_000_000, controller.rate());

        controller.report(report(12, 100_000, 50_000, 51_000), SECOND, 0, 150_000);
        controller.report(report(12, 100_000, 50_000, 160_000), 2 * SECOND, 0, 750_000);
        final long held = controller.rate();
        assertEquals(3_560_000, held);
        controller.setCeiling(12_000_000);
        assertEquals(held, controller.rate());
    }

    private static long overloaded(long ceiling) {
        final RateController controller = new RateController(ceiling);
        long now = SECOND;
        controller.report(report(12, 100_000, 50_000, 51_000), now, 0, 100_000);
        for (int i = 0; i < 40; ++i) {
            now += 2 * SECOND;
            controller.report(report(12, 1_000, 50_000, 500_000), now, 0, 100_000);
        }
        return controller.rate();
    }

    @Test
    void theRateNeverFallsBelowTheFloor() {
        assertEquals(200_000, overloaded(8_000_000));
        assertEquals(200_000, overloaded(1_000_000));
        assertEquals(150_000, overloaded(150_000));
    }
}
