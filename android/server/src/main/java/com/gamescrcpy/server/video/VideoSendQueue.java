package com.gamescrcpy.server.video;

import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Enums.MediaKind;
import com.gamescrcpy.protocol.Media;
import com.gamescrcpy.server.Log;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayDeque;
import java.util.Iterator;
import java.util.function.LongSupplier;

final class VideoSendQueue implements VideoPackets.Sink {

    static final long LIMIT_US = 150_000;
    static final long KEY_FRAME_SPACING_US = 1_000_000;
    static final long WINDOW_STALE_US = 400_000;
    static final long KEY_FRAME_ALLOWANCE_US = 1_000_000;

    private static final class Entry {
        final byte[] header = new byte[Constants.MEDIA_HEADER_SIZE];
        ByteBuffer payload = ByteBuffer.allocateDirect(64 * 1024);
        boolean frame;
        boolean keyFrame;
        long enqueuedUs;
    }

    private final VideoPackets.Sink channel;
    private final Runnable keyFrameRequest;
    private final long discardableAfterUs;
    private final LongSupplier clock;
    private final Thread sender;
    private final ArrayDeque<Entry> queue = new ArrayDeque<>();
    private final ArrayDeque<Entry> spare = new ArrayDeque<>();
    private long writingSinceUs = -1;
    private boolean writingKeyFrame;
    private long writeStartedUs;
    private long keyFrameOutUs = Long.MIN_VALUE / 2;
    private boolean awaitingKeyFrame;
    private boolean keyFrameDue;
    private long lastRequestUs = Long.MIN_VALUE / 2;
    private boolean closed;
    private IOException failure;
    private long droppedFrames;
    private long acceptedBytes;
    private long writtenBytes;
    private long receivedBytes;
    private long windowBytes = Long.MAX_VALUE;
    private long windowSetUs;

    VideoSendQueue(VideoPackets.Sink channel, Runnable keyFrameRequest, long discardableAfterUs, LongSupplier clock) {
        this.channel = channel;
        this.keyFrameRequest = keyFrameRequest;
        this.discardableAfterUs = discardableAfterUs;
        this.clock = clock;
        this.sender = new Thread(this::send, "gs-video-out");
        sender.start();
    }

    @Override
    public void write(byte[] header, ByteBuffer payload) throws IOException {
        final boolean frame = header[0] == MediaKind.FRAME.wireValue();
        final boolean keyFrame = frame && (header[1] & Media.FLAG_KEY_FRAME) != 0;
        final boolean discardable = frame && (header[1] & Media.FLAG_DISCARDABLE) != 0;
        boolean requestKeyFrame = false;
        synchronized (this) {
            if (failure != null) {
                throw new IOException("the video channel broke", failure);
            }
            if (closed) {
                throw new IOException("the video send queue is closed");
            }
            final long nowUs = clock.getAsLong();
            boolean drop = false;
            if (frame) {
                final long behindUs = oldestFrameAgeLocked(nowUs);
                if (awaitingKeyFrame) {
                    drop = !keyFrame;
                } else if (behindUs > LIMIT_US) {
                    final int cleared = clearFramesLocked();
                    droppedFrames += cleared;
                    drop = !keyFrame;
                    awaitingKeyFrame = drop;
                    keyFrameDue = drop;
                    Log.i("video send queue " + behindUs / 1000 + " ms behind: dropped " + cleared + " frames"
                            + (drop ? ", waiting for a key frame" : ""));
                } else {
                    drop = discardable && waitedLocked(nowUs) > discardableAfterUs;
                }
                if (keyFrame && !drop) {
                    awaitingKeyFrame = false;
                    keyFrameDue = false;
                }
            }
            if (drop) {
                ++droppedFrames;
            } else {
                acceptedBytes += header.length + payload.remaining();
                queue.add(copy(header, payload, frame, keyFrame, nowUs));
                notifyAll();
            }
            requestKeyFrame = keyFrameRequestDueLocked(nowUs);
        }
        payload.position(payload.limit());
        if (requestKeyFrame) {
            keyFrameRequest.run();
        }
    }

    synchronized long waitedUs() {
        return waitedLocked(clock.getAsLong());
    }

    synchronized long droppedFrames() {
        return droppedFrames;
    }

    synchronized void setLinkWindow(long received, long window) {
        receivedBytes = received;
        windowBytes = window;
        windowSetUs = clock.getAsLong();
        notifyAll();
    }

    synchronized long takeAcceptedBytes() {
        final long bytes = acceptedBytes;
        acceptedBytes = 0;
        return bytes;
    }

    synchronized int queued() {
        return queue.size();
    }

    synchronized void close() {
        closed = true;
        queue.clear();
        notifyAll();
    }

    void awaitEnd(long millis) throws InterruptedException {
        sender.join(millis);
    }

    private long waitedLocked(long nowUs) {
        if (writingSinceUs >= 0) {
            return nowUs - writingSinceUs;
        }
        return queue.isEmpty() ? 0 : nowUs - queue.peekFirst().enqueuedUs;
    }

    private boolean windowFullLocked() {
        return queue.peekFirst().frame && writtenBytes - receivedBytes > windowBytes
                && clock.getAsLong() - windowSetUs < WINDOW_STALE_US;
    }

    private boolean keyFrameRequestDueLocked(long nowUs) {
        if (!keyFrameDue || writingSinceUs >= 0 || !queue.isEmpty() || nowUs - lastRequestUs < KEY_FRAME_SPACING_US) {
            return false;
        }
        keyFrameDue = false;
        lastRequestUs = nowUs;
        return true;
    }

    private long oldestFrameAgeLocked(long nowUs) {
        for (Entry entry : queue) {
            if (entry.frame) {
                long fromUs = Math.max(entry.enqueuedUs, keyFrameOutUs);
                if (writingKeyFrame) {
                    fromUs = Math.max(fromUs, writeStartedUs + KEY_FRAME_ALLOWANCE_US);
                }
                return Math.max(0, nowUs - fromUs);
            }
        }
        return 0;
    }

    private int clearFramesLocked() {
        int cleared = 0;
        for (Iterator<Entry> it = queue.iterator(); it.hasNext(); ) {
            final Entry entry = it.next();
            if (entry.frame) {
                it.remove();
                spare.add(entry);
                ++cleared;
            }
        }
        return cleared;
    }

    private Entry copy(byte[] header, ByteBuffer payload, boolean frame, boolean keyFrame, long nowUs) {
        Entry entry = spare.poll();
        if (entry == null) {
            entry = new Entry();
        }
        System.arraycopy(header, 0, entry.header, 0, header.length);
        if (entry.payload.capacity() < payload.remaining()) {
            entry.payload = ByteBuffer.allocateDirect(payload.remaining() + payload.remaining() / 2);
        }
        entry.payload.clear();
        entry.payload.put(payload.duplicate());
        entry.payload.flip();
        entry.frame = frame;
        entry.keyFrame = keyFrame;
        entry.enqueuedUs = nowUs;
        return entry;
    }

    private void send() {
        while (true) {
            final Entry entry;
            final int size;
            synchronized (this) {
                while (!closed && (queue.isEmpty() || windowFullLocked())) {
                    try {
                        if (queue.isEmpty()) {
                            wait();
                        } else {
                            wait(WINDOW_STALE_US / 1000 + 1);
                        }
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        return;
                    }
                }
                if (closed) {
                    return;
                }
                entry = queue.poll();
                size = entry.header.length + entry.payload.remaining();
                writingSinceUs = entry.enqueuedUs;
                writingKeyFrame = entry.keyFrame;
                writeStartedUs = clock.getAsLong();
            }
            IOException broke = null;
            try {
                channel.write(entry.header, entry.payload);
            } catch (IOException e) {
                broke = e;
            }
            final boolean requestKeyFrame;
            synchronized (this) {
                final long nowUs = clock.getAsLong();
                if (writingKeyFrame) {
                    keyFrameOutUs = nowUs;
                }
                writingSinceUs = -1;
                writingKeyFrame = false;
                writtenBytes += size;
                spare.add(entry);
                if (broke != null) {
                    failure = broke;
                    queue.clear();
                    return;
                }
                requestKeyFrame = keyFrameRequestDueLocked(nowUs);
            }
            if (requestKeyFrame) {
                keyFrameRequest.run();
            }
        }
    }
}
