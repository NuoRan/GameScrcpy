package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class BlockModelPumpTest {

    private static final long FRAME_NANOS = 1_000_000_000L;

    private static final class FakeImage implements BlockModelPump.Image {
        private final long timestampNanos;
        private int closes;

        FakeImage(long timestampNanos) {
            this.timestampNanos = timestampNanos;
        }

        @Override
        public long timestampNanos() {
            return timestampNanos;
        }

        @Override
        public void close() {
            ++closes;
        }

        boolean closed() {
            return closes > 0;
        }
    }

    private static final class FakeTimer implements BlockModelPump.Timer {
        private final List<Long> delays = new ArrayList<>();
        private final List<Runnable> tasks = new ArrayList<>();

        @Override
        public void after(long delayNanos, Runnable task) {
            delays.add(delayNanos);
            tasks.add(task);
        }

        long nextDelay() {
            return delays.get(0);
        }

        int pending() {
            return tasks.size();
        }

        void fireNext() {
            final Runnable task = tasks.remove(0);
            delays.remove(0);
            task.run();
        }
    }

    private static final class FakeClock implements BlockModelPump.Clock {
        private long nanos = FRAME_NANOS;

        @Override
        public long nowNanos() {
            return nanos;
        }

        void advance(long amountNanos) {
            nanos += amountNanos;
        }
    }

    private final FakeTimer timer = new FakeTimer();
    private final FakeClock clock = new FakeClock();
    private final List<Integer> slots = new ArrayList<>();
    private final List<Long> stamps = new ArrayList<>();
    private boolean refuse;

    private BlockModelPump pump(int maxFps) {
        final BlockModelPump.Sink sink = (slot, image, ptsUs) -> {
            if (refuse) {
                return false;
            }
            slots.add(slot);
            stamps.add(ptsUs);
            return true;
        };
        return new BlockModelPump(sink, timer, clock, 3, maxFps, 4);
    }

    @Test
    void adr0117AFrameWaitsForAnInputSlotAndIsClosedWhenThatSlotComesBack() {
        final BlockModelPump pump = pump(0);
        final FakeImage image = new FakeImage(FRAME_NANOS);
        pump.frame(image);
        assertEquals(List.of(), slots);
        assertFalse(image.closed());

        pump.slotAvailable(0);
        assertEquals(List.of(0), slots);
        assertEquals(List.of(1_000_000L), stamps);
        assertFalse(image.closed());

        pump.slotAvailable(0);
        assertFalse(image.closed());

        pump.frame(new FakeImage(FRAME_NANOS + 8_000_000L));
        pump.slotAvailable(0);
        assertTrue(image.closed());
    }

    @Test
    void adr0117OnlyTheNewestWaitingFrameIsKept() {
        final BlockModelPump pump = pump(0);
        final FakeImage older = new FakeImage(FRAME_NANOS);
        final FakeImage newer = new FakeImage(FRAME_NANOS + 8_000_000L);
        pump.frame(older);
        pump.frame(newer);
        assertTrue(older.closed());
        assertFalse(newer.closed());
        assertEquals(1, pump.droppedFrames());

        pump.slotAvailable(0);
        assertEquals(List.of(0), slots);
        assertEquals(List.of(1_008_000L), stamps);
    }

    @Test
    void adr0117AFrameWithoutATimestampIsStampedWhenItArrives() {
        final BlockModelPump pump = pump(0);
        pump.frame(new FakeImage(0));
        pump.slotAvailable(0);
        assertEquals(List.of(1_000_000L), stamps);
    }

    @Test
    void adr0117AnIdleScreenRepeatsItsLastFrameEveryHundredMilliseconds() {
        final BlockModelPump pump = pump(0);
        pump.slotAvailable(0);
        final FakeImage image = new FakeImage(FRAME_NANOS);
        pump.frame(image);
        assertEquals(1, slots.size());
        assertEquals(1, timer.pending());
        assertEquals(BlockModelPump.REPEAT_NANOS, timer.nextDelay());

        clock.advance(50_000_000L);
        timer.fireNext();
        assertEquals(1, slots.size());
        assertEquals(50_000_000L, timer.nextDelay());

        pump.slotAvailable(0);
        clock.advance(50_000_000L);
        timer.fireNext();
        assertEquals(2, slots.size());
        assertEquals(List.of(1_000_000L, 1_000_000L), stamps);
        assertEquals(1, timer.pending());
    }

    @Test
    void adr0117FramesArrivingFasterThanTheLimitAreDropped() {
        final BlockModelPump pump = pump(30);
        pump.slotAvailable(0);
        final FakeImage first = new FakeImage(FRAME_NANOS);
        pump.frame(first);
        assertEquals(1, slots.size());

        final FakeImage tooSoon = new FakeImage(FRAME_NANOS + 10_000_000L);
        pump.frame(tooSoon);
        assertTrue(tooSoon.closed());
        assertEquals(1, slots.size());
        assertEquals(1, pump.droppedFrames());

        pump.slotAvailable(0);
        final FakeImage later = new FakeImage(FRAME_NANOS + 40_000_000L);
        pump.frame(later);
        assertEquals(2, slots.size());
        assertEquals(1_040_000L, stamps.get(1).longValue());
    }

    @Test
    void adr0117NothingIsHandedOverWhilePaused() {
        final BlockModelPump pump = pump(0);
        pump.slotAvailable(0);
        pump.pause(true);
        final FakeImage ignored = new FakeImage(FRAME_NANOS);
        pump.frame(ignored);
        assertTrue(ignored.closed());
        assertEquals(List.of(), slots);
        assertEquals(0, pump.droppedFrames());

        pump.pause(false);
        pump.frame(new FakeImage(FRAME_NANOS + 8_000_000L));
        assertEquals(1, slots.size());
        assertEquals(1_008_000L, stamps.get(0).longValue());
    }

    @Test
    void adr0117AKeyFrameRequestWithoutANewFrameRepeatsTheLastOne() {
        final BlockModelPump pump = pump(0);
        pump.slotAvailable(0);
        pump.frame(new FakeImage(FRAME_NANOS));
        assertEquals(1, slots.size());

        pump.slotAvailable(0);
        pump.requestKeyFrame();
        assertEquals(2, slots.size());
        assertEquals(List.of(1_000_000L, 1_000_000L), stamps);
    }

    @Test
    void adr0117AnEncoderThatRefusesAFrameStopsTheSession() {
        final BlockModelPump pump = pump(0);
        pump.slotAvailable(0);
        refuse = true;
        pump.frame(new FakeImage(FRAME_NANOS));
        assertThrows(EncoderException.class, () -> pump.next(EncodingSession.NO_DEADLINE));
    }

    @Test
    void adr0117NoOutputBeforeTheDeadlineStopsTheSession() {
        final BlockModelPump pump = pump(0);
        assertThrows(EncoderException.class, () -> pump.next(clock.nowNanos() - 1));
    }

    @Test
    void anInterruptedSessionEndsAtOnce() throws EncoderException {
        final BlockModelPump pump = pump(0);
        pump.interrupt();
        assertEquals(EncodingSession.Kind.END, pump.next(EncodingSession.NO_DEADLINE).kind);
    }

    @Test
    void anOutputIsHandedOverOnceAndItsPayloadComesBackOnRelease() throws EncoderException {
        final BlockModelPump pump = pump(0);
        final ByteBuffer data = ByteBuffer.allocate(4);
        assertTrue(pump.output(EncodingSession.Kind.FRAME, data, 1_000, 2_000, true, 7, "block", false));
        final EncodingSession.Output output = pump.next(EncodingSession.NO_DEADLINE);
        assertEquals(EncodingSession.Kind.FRAME, output.kind);
        assertEquals(data, output.data);
        assertEquals(1_000, output.ptsUs);
        assertEquals(2_000, output.readyUs);
        assertTrue(output.keyFrame);
        assertEquals(7, output.index);
        assertEquals("block", output.payload);

        pump.release(output);
        assertEquals(-1, output.index);
        assertThrows(EncoderException.class, () -> pump.next(clock.nowNanos() - 1));
    }

    @Test
    void closingReturnsEveryHeldImageAndTheOutputsStillQueued() throws EncoderException {
        final BlockModelPump pump = pump(0);
        pump.slotAvailable(0);
        final FakeImage image = new FakeImage(FRAME_NANOS);
        pump.frame(image);
        assertTrue(pump.output(EncodingSession.Kind.FRAME, ByteBuffer.allocate(4), 1_000, 2_000, false, 5, "block",
                false));

        final List<EncodingSession.Output> leftovers = pump.close();
        assertEquals(1, leftovers.size());
        assertEquals(5, leftovers.get(0).index);
        assertEquals("block", leftovers.get(0).payload);
        assertTrue(image.closed());
        assertEquals(EncodingSession.Kind.END, pump.next(EncodingSession.NO_DEADLINE).kind);
    }
}
