package com.gamescrcpy.server.video;

import java.nio.ByteBuffer;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

final class BlockModelPump {

    static final long REPEAT_NANOS = 100_000_000L;
    static final long MAX_JITTER_US = 2_000;
    static final int MAX_SLOTS = 64;

    interface Image {
        long timestampNanos();

        void close();
    }

    interface Sink {
        boolean queue(int slot, Image image, long ptsUs);
    }

    interface Runner {
        void run(Runnable task);
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
    private final Runner input;
    private final Timer timer;
    private final Clock clock;
    private final Held[] slotImages = new Held[MAX_SLOTS];
    private final boolean[] slotKnown = new boolean[MAX_SLOTS];
    private final int maxInFlight;
    private final long minIntervalUs;
    private final ArrayDeque<EncodingSession.Output> ready = new ArrayDeque<>();
    private final ArrayDeque<EncodingSession.Output> spare = new ArrayDeque<>();
    private final EncodingSession.Output end = new EncodingSession.Output();

    private Held pending;
    private Held last;
    private int inFlight;
    private long lastQueuedNanos;
    private long desiredMinUs = -1;
    private boolean paused;
    private boolean closed;
    private boolean stopped;
    private boolean ended;
    private boolean repeatScheduled;
    private boolean drainScheduled;
    private boolean queueing;
    private boolean frameAfterRequest;
    private EncoderException failure;
    private EncodingSession.Output inUse;
    private long dropped;

    BlockModelPump(Sink sink, Runner input, Timer timer, Clock clock, int maxInFlight, int maxFps) {
        this.sink = sink;
        this.input = input;
        this.timer = timer;
        this.clock = clock;
        this.maxInFlight = Math.max(1, maxInFlight);
        this.minIntervalUs = maxFps > 0 ? 1_000_000L / maxFps : 0;
    }

    void frame(Image image) {
        synchronized (lock) {
            if (closed || stopped || failure != null || paused) {
                image.close();
                return;
            }
            final long timestamp = image.timestampNanos();
            final long ptsUs = timestamp > 0 ? timestamp / 1000 : clock.nowNanos() / 1000;
            if (tooSoonLocked(ptsUs)) {
                ++dropped;
                image.close();
                return;
            }
            if (pending != null) {
                ++dropped;
                pending.release();
            }
            pending = new Held(image, ptsUs);
        }
        drain();
    }

    void slotAvailable(int slot) {
        synchronized (lock) {
            if (closed || stopped || slot < 0 || slot >= MAX_SLOTS) {
                return;
            }
            slotKnown[slot] = true;
            final Held previous = slotImages[slot];
            if (previous != null) {
                slotImages[slot] = null;
                --inFlight;
                previous.release();
            }
            scheduleDrainLocked();
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
                scheduleDrainLocked();
            }
        }
    }

    void requestKeyFrame() {
        synchronized (lock) {
            frameAfterRequest = true;
            scheduleDrainLocked();
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

    boolean output(EncodingSession.Kind kind, ByteBuffer data, long ptsUs, long readyUs, boolean keyFrame, int index,
                   Object payload, boolean lastOutput) {
        synchronized (lock) {
            if (closed || stopped || failure != null) {
                return false;
            }
            EncodingSession.Output out = spare.poll();
            if (out == null) {
                out = new EncodingSession.Output();
            }
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
        }
    }

    List<EncodingSession.Output> close() {
        synchronized (lock) {
            closed = true;
            while (queueing) {
                try {
                    lock.wait();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
            for (int i = 0; i < slotImages.length; ++i) {
                if (slotImages[i] != null) {
                    slotImages[i].release();
                    slotImages[i] = null;
                }
            }
            inFlight = 0;
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

    private boolean tooSoonLocked(long ptsUs) {
        if (minIntervalUs <= 0) {
            return false;
        }
        if (desiredMinUs < 0) {
            desiredMinUs = ptsUs + minIntervalUs;
            return false;
        }
        if (ptsUs < desiredMinUs - MAX_JITTER_US) {
            return true;
        }
        if (ptsUs >= desiredMinUs + minIntervalUs) {
            desiredMinUs = ptsUs + minIntervalUs;
        } else {
            desiredMinUs += minIntervalUs;
        }
        return false;
    }

    private void scheduleDrainLocked() {
        if (drainScheduled || closed || stopped || paused || failure != null) {
            return;
        }
        if (pending == null && !(last != null && frameAfterRequest)) {
            return;
        }
        drainScheduled = true;
        input.run(this::drain);
    }

    private void drain() {
        while (true) {
            final int slot;
            final Held held;
            synchronized (lock) {
                drainScheduled = false;
                if (closed || stopped || paused || failure != null) {
                    return;
                }
                final Held candidate = pending != null ? pending
                        : last != null && (frameAfterRequest || repeatDueLocked()) ? last : null;
                if (candidate == null) {
                    return;
                }
                slot = freeSlotLocked();
                if (slot < 0) {
                    return;
                }
                held = candidate;
                slotImages[slot] = held.retain();
                ++inFlight;
                if (held == pending) {
                    pending = null;
                    if (last != null) {
                        last.release();
                    }
                    last = held;
                }
                lastQueuedNanos = clock.nowNanos();
                frameAfterRequest = false;
                queueing = true;
            }
            final boolean accepted = sink.queue(slot, held.image, held.ptsUs);
            synchronized (lock) {
                queueing = false;
                if (!accepted) {
                    if (slotImages[slot] == held) {
                        slotImages[slot] = null;
                        --inFlight;
                        held.release();
                    }
                    if (failure == null && !closed && !stopped) {
                        failure = new EncoderException("the encoder refused a frame", null);
                    }
                    wakeLocked();
                    return;
                }
                scheduleRepeatLocked();
                wakeLocked();
            }
        }
    }

    private int freeSlotLocked() {
        if (inFlight >= maxInFlight) {
            return -1;
        }
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
        return minIntervalUs > 0 ? Math.max(REPEAT_NANOS, minIntervalUs * 1000) : REPEAT_NANOS;
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
        }
        drain();
        synchronized (lock) {
            scheduleRepeatLocked();
        }
    }

    private void wakeLocked() {
        lock.notifyAll();
    }
}
