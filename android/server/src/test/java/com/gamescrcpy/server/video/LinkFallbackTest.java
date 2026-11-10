package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Messages.ReceiverReport;
import com.gamescrcpy.server.video.LinkFallback.Change;
import com.gamescrcpy.server.video.LinkFallback.Observation;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class LinkFallbackTest {

    private static final long SECOND = 1_000_000;
    private static final long REPORT_US = 200_000;
    private static final int BASE = 2400;
    private static final int LEVELS = LinkFallback.levels(BASE, 0);

    private static Observation stuck(long nowUs) {
        return new Observation(nowUs, 350_000, true, 200_000, 920_000, 800_000, true, false);
    }

    private static Observation quiet(long nowUs, long rateBps) {
        return new Observation(nowUs, 5_000, false, rateBps, rateBps, rateBps, false, false);
    }

    private static long stepDown(LinkFallback fallback, long fromUs) {
        for (long now = fromUs; now < fromUs + 10 * SECOND; now += REPORT_US) {
            if (fallback.report(stuck(now), LEVELS) == Change.DOWN) {
                return now;
            }
        }
        throw new AssertionError("no step down");
    }

    @Test
    void twoSecondsStuckStepDownOnce() {
        final LinkFallback fallback = new LinkFallback();
        long now = SECOND;
        for (int report = 1; report < LinkFallback.WINDOW; ++report, now += REPORT_US) {
            assertEquals(Change.NONE, fallback.report(stuck(now), LEVELS));
        }
        assertEquals(Change.DOWN, fallback.report(stuck(now), LEVELS));
        assertEquals(1, fallback.level());
        assertEquals(920_000, fallback.floorAt(0));
    }

    @Test
    void aQueueTheEncoderCouldMeetIsNotStuck() {
        final LinkFallback fallback = new LinkFallback();
        for (long now = SECOND; now < 20 * SECOND; now += REPORT_US) {
            assertEquals(Change.NONE, fallback.report(
                    new Observation(now, 350_000, true, 600_000, 800_000, 700_000, false, false), LEVELS));
            assertEquals(Change.NONE, fallback.report(
                    new Observation(now, 350_000, false, 200_000, 920_000, 100_000, true, false), LEVELS));
            assertEquals(Change.NONE, fallback.report(
                    new Observation(now, 350_000, true, 200_000, 700_000, 800_000, true, false), LEVELS));
        }
        assertEquals(0, fallback.level());
    }

    @Test
    void theOddOtherReportIsToleratedButNotMore() {
        final LinkFallback tolerant = new LinkFallback();
        long now = SECOND;
        for (int report = 1; report <= LinkFallback.WINDOW; ++report, now += REPORT_US) {
            final Observation seen = report == 3 || report == 7 ? quiet(now, 200_000) : stuck(now);
            assertEquals(report == LinkFallback.WINDOW ? Change.DOWN : Change.NONE, tolerant.report(seen, LEVELS));
        }
        final LinkFallback strict = new LinkFallback();
        now = SECOND;
        for (int report = 1; report <= LinkFallback.WINDOW + 1; ++report, now += REPORT_US) {
            final Observation seen = report == 2 || report == 5 || report == 8 ? quiet(now, 200_000) : stuck(now);
            assertEquals(Change.NONE, strict.report(seen, LEVELS));
        }
        assertEquals(Change.DOWN, strict.report(stuck(now), LEVELS));
    }

    @Test
    void theResolutionGoesFirstThenTheFrameRate() {
        assertEquals(4, LinkFallback.resolutionSteps(BASE));
        assertEquals(6, LEVELS);
        final LinkFallback fallback = new LinkFallback();
        assertEquals(0, fallback.maxSize(BASE, 0));
        final int[][] expected = {{1800, 0}, {1350, 0}, {1012, 0}, {759, 0}, {759, 30}, {759, 15}};
        long now = SECOND;
        long last = Long.MIN_VALUE;
        for (int[] limits : expected) {
            now = stepDown(fallback, now);
            assertTrue(last == Long.MIN_VALUE || now - last >= LinkFallback.SETTLE_US);
            last = now;
            assertEquals(limits[0], fallback.maxSize(BASE, 0));
            assertEquals(limits[1], fallback.maxFps(BASE, 0));
        }
        for (long t = now; t < now + 10 * SECOND; t += REPORT_US) {
            assertEquals(Change.NONE, fallback.report(stuck(t), LEVELS));
        }
    }

    @Test
    void aSmallDisplayAndALowCapHaveFewerSteps() {
        assertEquals(2, LinkFallback.resolutionSteps(1280));
        assertEquals(3, LinkFallback.levels(1280, 30));
        assertEquals(0, LinkFallback.resolutionSteps(800));
        assertEquals(0, LinkFallback.levels(800, 15));
        final LinkFallback fallback = new LinkFallback();
        long now = SECOND;
        for (int level = 1; level <= 3; ++level) {
            for (long t = now; ; t += REPORT_US) {
                if (fallback.report(stuck(t), 3) == Change.DOWN) {
                    now = t;
                    break;
                }
            }
        }
        assertEquals(720, fallback.maxSize(1280, 0));
        assertEquals(15, fallback.maxFps(1280, 30));
    }

    @Test
    void tenQuietSecondsWithRoomStepBackUp() {
        final LinkFallback fallback = new LinkFallback();
        long now = stepDown(fallback, SECOND) + REPORT_US;
        final long quietFrom = now;
        for (; now < quietFrom + 12 * SECOND; now += REPORT_US) {
            assertEquals(Change.NONE, fallback.report(quiet(now, 1_200_000), LEVELS));
        }
        assertEquals(Change.UP, fallback.report(quiet(now, 1_400_000), LEVELS));
        assertEquals(0, fallback.level());

        now = stepDown(fallback, now + 20 * SECOND) + REPORT_US;
        final long ceilingFrom = now;
        for (; now < ceilingFrom + LinkFallback.QUIET_US; now += REPORT_US) {
            assertEquals(Change.NONE, fallback.report(quiet(now, 400_000), LEVELS));
        }
        assertEquals(Change.UP, fallback.report(new Observation(now, 5_000, false, 400_000, 400_000, 400_000, false,
                true), LEVELS));
    }

    @Test
    void aQuickReturnRaisesTheBar() {
        final LinkFallback fallback = new LinkFallback();
        long now = stepDown(fallback, SECOND) + REPORT_US;
        final long quietFrom = now;
        for (; now < quietFrom + LinkFallback.QUIET_US; now += REPORT_US) {
            fallback.report(quiet(now, 1_400_000), LEVELS);
        }
        assertEquals(Change.UP, fallback.report(quiet(now, 1_400_000), LEVELS));
        final long upAt = now;
        now = stepDown(fallback, now + REPORT_US);
        assertTrue(now - upAt < LinkFallback.QUIET_US);
        assertEquals(1_380_000, fallback.floorAt(0));

        final long secondFrom = now + REPORT_US;
        for (now = secondFrom; now < secondFrom + 20 * SECOND; now += REPORT_US) {
            assertEquals(Change.NONE, fallback.report(quiet(now, 1_400_000), LEVELS));
        }
        assertEquals(Change.UP, fallback.report(quiet(now, 2_100_000), LEVELS));
    }

    @Test
    void newSettingsGoBackToThem() {
        final LinkFallback fallback = new LinkFallback();
        stepDown(fallback, SECOND);
        fallback.reset();
        assertEquals(0, fallback.level());
        assertEquals(0, fallback.floorAt(0));
        assertEquals(1280, fallback.maxSize(BASE, 1280));
    }

    private static final long PROPAGATION_US = 2_000;
    private static final int SIM_BASE = 1920;
    private static final long CEILING_BPS = 8_000_000;

    private record Step(long fromUs, long bps) {
    }

    private static final class Run {
        final List<long[]> changes = new ArrayList<>();
        final List<long[]> delays = new ArrayList<>();

        int levelAt(long timeUs) {
            int level = 0;
            for (long[] change : changes) {
                if (change[0] <= timeUs) {
                    level = (int) change[1];
                }
            }
            return level;
        }

        long maxDelay(long fromUs, long toUs) {
            return delays.stream().filter(d -> d[0] >= fromUs && d[0] < toUs).mapToLong(d -> d[1]).max().orElse(0);
        }

        String trace() {
            final StringBuilder text = new StringBuilder();
            for (long[] change : changes) {
                text.append(String.format("%6.1f s: level %d%n", change[0] / 1e6, change[1]));
            }
            for (long[] delay : delays) {
                if (delay[0] % SECOND == 0) {
                    text.append(String.format("%6.1f s: %6.1f ms%n", delay[0] / 1e6, delay[1] / 1e3));
                }
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

    private static Run simulate(List<Step> link, long durationUs) {
        final RateController rate = new RateController(CEILING_BPS);
        final LinkFallback fallback = new LinkFallback();
        final Run run = new Run();
        final ArrayDeque<long[]> inFlight = new ArrayDeque<>();
        long linkFreeUs = 0;
        long nextFrameUs = 0;
        long nextReportUs = REPORT_US;
        long sentBytes = 0;
        while (Math.min(nextFrameUs, nextReportUs) < durationUs) {
            final int side = fallback.level() == 0 ? SIM_BASE : fallback.maxSize(SIM_BASE, 0);
            final int fps = fallback.maxFps(SIM_BASE, 0) > 0 ? fallback.maxFps(SIM_BASE, 0) : 30;
            if (nextFrameUs < nextReportUs) {
                final long floorBps = 900_000L * side * side / ((long) SIM_BASE * SIM_BASE) * fps / 30;
                final long bits = Math.max(Math.min(rate.rate(), 1_600_000), floorBps) / fps;
                final long start = Math.max(nextFrameUs, linkFreeUs);
                linkFreeUs = start + bits * SECOND / capacityAt(link, start);
                inFlight.add(new long[] {nextFrameUs, linkFreeUs + PROPAGATION_US, bits / 8});
                sentBytes += bits / 8;
                nextFrameUs += SECOND / fps;
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
                final long nowUs = nextReportUs + PROPAGATION_US;
                rate.report(report, nowUs, 0, sentBytes);
                sentBytes = 0;
                final Change change = fallback.report(Observation.of(nowUs, rate, CEILING_BPS),
                        LinkFallback.levels(SIM_BASE, 0));
                if (change != Change.NONE) {
                    run.changes.add(new long[] {nextReportUs, fallback.level()});
                }
                run.delays.add(new long[] {nextReportUs, report.frames > 0 ? report.delayMeanUs : -1});
                nextReportUs += REPORT_US;
            }
        }
        return run;
    }

    @Test
    void aLinkUnderTheEncodersFloorIsMetAStepDown() {
        final Run run = simulate(List.of(new Step(0, 800_000)), 120 * SECOND);
        assertEquals(1, run.levelAt(15 * SECOND), run.trace());
        assertEquals(1, run.changes.size(), run.trace());
        assertTrue(run.maxDelay(30 * SECOND, 120 * SECOND) < 300_000, run.trace());
    }

    @Test
    void aNarrowerLinkTakesTwoSteps() {
        final Run run = simulate(List.of(new Step(0, 400_000)), 120 * SECOND);
        assertEquals(2, run.levelAt(30 * SECOND), run.trace());
        assertEquals(2, run.changes.size(), run.trace());
        assertTrue(run.maxDelay(45 * SECOND, 120 * SECOND) < 300_000, run.trace());
    }

    @Test
    void aWiderLinkBringsTheSettingBack() {
        final Run run = simulate(List.of(new Step(0, 800_000), new Step(60 * SECOND, 20_000_000)), 120 * SECOND);
        assertEquals(1, run.levelAt(59 * SECOND), run.trace());
        assertEquals(0, run.levelAt(110 * SECOND), run.trace());
        assertEquals(2, run.changes.size(), run.trace());
    }
}
