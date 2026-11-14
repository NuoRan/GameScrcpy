package com.gamescrcpy.server.video;

import java.nio.ByteBuffer;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

final class BlockModelPump {

    static final long REPEAT_NANOS = 100_000_000L;

    interface Image {
        long timestampNanos();

        void close();
    }

    interface Sink {
        boolean queue(int slot, Image image, long ptsUs);
    }

    interface Timer {
        void after(long delayNanos, Runnable task);
    }

    interface Clock {
        long nowNanos();
    }

    private static final class Held {
        final Image image;
        final long ptsUs;
        int holds = 1;

        Held(Image image, long ptsUs) {
            this.image = image;
            this.ptsUs = ptsUs;
        }

        Held retain() {
            ++holds;
            return this;
        }

        void release() {
            if (--holds == 0) {
                image.close();
            }
        }
    }

    private final Object lock = new Object();
    private final Sink sink;
    private final Timer timer;
    private final Clock clock;
    private final Held[] slotImages;
    private final boolean[] slotKnown;
    private final int maxFps;
    private final ArrayDeque<EncodingSession.Output> ready = new ArrayDeque<>();
    private final ArrayDeque<EncodingSession.Output> spare = new ArrayDeque<>();
    private final EncodingSession.Output end = new EncodingSession.Output();

    private Held pending;
    private Held last;
    private long lastQueuedNanos;
    private long lastPtsUs = -1;
    private boolean paused;
    private boolean closed;
    private boolean stopped;
    private boolean ended;
    private boolean repeatScheduled;
    private boolean frameAfterRequest;
    private EncoderException failure;
    private EncodingSession.Output inUse;
    private long dropped;

    BlockModelPump(Sink sink, Timer timer, Clock clock, int slots, int maxFps, int outputCapacity) {
        this.sink = sink;
        this.timer = timer;
        this.clock = clock;
        this.slotImages = new Held[slots];
        this.slotKnown = new boolean[slots];
        this.maxFps = maxFps;
        for (int i = 0; i < outputCapacity; ++i) {
            spare.add(new EncodingSession.Output());
        }
    }

    void frame(Image image) {
        synchronized (lock) {
            if (closed || stopped || failure != null) {
                image.close();
                return;
            }
            if (paused) {
                image.close();
                return;
            }
            final long timestamp = image.timestampNanos();
            final long ptsUs = timestamp > 0 ? timestamp / 1000 : clock.nowNanos() / 1000;
            if (maxFps > 0 && lastPtsUs >= 0 && ptsUs - lastPtsUs < 1_000_000L / maxFps) {
                ++dropped;
                image.close();
                return;
            }
            final Held held = new Held(image, ptsUs);
            if (pending != null) {
                ++dropped;
                pending.release();
            }
            pending = held;
            pumpLocked();
        }
    }

    void slotAvailable(int slot) {
        synchronized (lock) {
            if (closed || stopped || slot < 0 || slot >= slotImages.length) {
                return;
            }
            slotKnown[slot] = true;
            final Held previous = slotImages[slot];
            if (previous != null) {
                slotImages[slot] = null;
                previous.release();
            }
            pumpLocked();
        }
    }

    void pause(boolean newPaused) {
        synchronized (lock) {
            paused = newPaused;
            if (newPaused && pending != null) {
                ++dropped;
                pending.release();
                pending = null;
            }
            if (!newPaused) {
                pumpLocked();
            }
        }
    }

    void requestKeyFrame() {
        synchronized (lock) {
            frameAfterRequest = true;
            pumpLocked();
        }
    }

    void fail(String reason) {
        synchronized (lock) {
            if (failure == null && !closed && !stopped) {
                failure = new EncoderException(reason, null);
            }
            wakeLocked();
        }
    }

    void interrupt() {
        synchronized (lock) {
            stopped = true;
            wakeLocked();
        }
    }

    boolean output(EncodingSession.Kind kind, ByteBuffer data, long ptsUs, long readyUs, boolean keyFrame,
                   int index, Object payload, boolean lastOutput) {
        synchronized (lock) {
            while (!closed && !stopped && failure == null && spare.isEmpty()) {
                try {
                    lock.wait();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return false;
                }
            }
            if (closed || stopped || failure != null) {
                return false;
            }
            final EncodingSession.Output out = spare.poll();
            out.kind = kind;
            out.data = data;
            out.ptsUs = ptsUs;
            out.readyUs = readyUs;
            out.keyFrame = keyFrame;
            out.index = index;
            out.payload = payload;
            ready.add(out);
            if (lastOutput) {
                ended = true;
            }
            wakeLocked();
            return true;
        }
    }

    EncodingSession.Output next(long deadlineNanos) throws EncoderException {
        synchronized (lock) {
            while (true) {
                if (closed || stopped) {
                    return end;
                }
                if (!ready.isEmpty()) {
                    inUse = ready.poll();
                    return inUse;
                }
                if (ended) {
                    return end;
                }
                if (failure != null) {
                    throw failure;
                }
                final long remaining = deadlineNanos == EncodingSession.NO_DEADLINE
                        ? Long.MAX_VALUE : deadlineNanos - clock.nowNanos();
                if (remaining <= 0) {
                    failure = new EncoderException("no frame before the first-frame deadline", null);
                    throw failure;
                }
                try {
                    if (remaining == Long.MAX_VALUE) {
                        lock.wait();
                    } else {
                        lock.wait(remaining / 1_000_000, (int) (remaining % 1_000_000));
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return end;
                }
            }
        }
    }

    void release(EncodingSession.Output output) {
        synchronized (lock) {
            if (output != inUse) {
                return;
            }
            inUse = null;
            output.index = -1;
            output.data = null;
            output.payload = null;
            spare.add(output);
            wakeLocked();
        }
    }

    List<EncodingSession.Output> close() {
        synchronized (lock) {
            closed = true;
            for (int i = 0; i < slotImages.length; ++i) {
                if (slotImages[i] != null) {
                    slotImages[i].release();
                    slotImages[i] = null;
                }
            }
            if (pending != null) {
                pending.release();
                pending = null;
            }
            if (last != null) {
                last.release();
                last = null;
            }
            final List<EncodingSession.Output> leftovers = new ArrayList<>(ready);
            ready.clear();
            if (inUse != null) {
                leftovers.add(inUse);
                inUse = null;
            }
            wakeLocked();
            return leftovers;
        }
    }

    long droppedFrames() {
        synchronized (lock) {
            return dropped;
        }
    }

    private void pumpLocked() {
        if (closed || stopped || paused || failure != null) {
            return;
        }
        if (pending != null) {
            final int slot = freeSlotLocked();
            if (slot >= 0) {
                queueLocked(slot, pending);
            }
            return;
        }
        if (last != null && (frameAfterRequest || repeatDueLocked())) {
            final int slot = freeSlotLocked();
            if (slot >= 0) {
                queueLocked(slot, last);
            }
        }
    }

    private void queueLocked(int slot, Held held) {
        if (!sink.queue(slot, held.image, held.ptsUs)) {
            failure = new EncoderException("the encoder refused a frame", null);
            wakeLocked();
            return;
        }
        slotImages[slot] = held.retain();
        if (held == pending) {
            pending.release();
            pending = null;
        }
        if (last != held) {
            final Held previous = last;
            last = held.retain();
            if (previous != null) {
                previous.release();
            }
        }
        lastPtsUs = held.ptsUs;
        lastQueuedNanos = clock.nowNanos();
        frameAfterRequest = false;
        scheduleRepeatLocked();
    }

    private int freeSlotLocked() {
        for (int i = 0; i < slotImages.length; ++i) {
            if (slotKnown[i] && slotImages[i] == null) {
                return i;
            }
        }
        return -1;
    }

    private boolean repeatDueLocked() {
        return clock.nowNanos() - lastQueuedNanos >= repeatIntervalNanos();
    }

    private long repeatIntervalNanos() {
        return maxFps > 0 ? Math.max(REPEAT_NANOS, 1_000_000_000L / maxFps) : REPEAT_NANOS;
    }

    private void scheduleRepeatLocked() {
        if (repeatScheduled || closed || stopped || failure != null) {
            return;
        }
        repeatScheduled = true;
        timer.after(REPEAT_NANOS, this::repeatTick);
    }

    private void repeatTick() {
        synchronized (lock) {
            repeatScheduled = false;
            if (closed || stopped || paused || failure != null) {
                return;
            }
            final long remaining = lastQueuedNanos + repeatIntervalNanos() - clock.nowNanos();
            if (remaining > 0) {
                repeatScheduled = true;
                timer.after(remaining, this::repeatTick);
                return;
            }
            pumpLocked();
            scheduleRepeatLocked();
        }
    }

    private void wakeLocked() {
        lock.notifyAll();
    }
}
