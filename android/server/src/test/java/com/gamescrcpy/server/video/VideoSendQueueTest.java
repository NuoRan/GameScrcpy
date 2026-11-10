package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Enums.MediaKind;
import com.gamescrcpy.protocol.Media;
import com.gamescrcpy.protocol.MediaChannel;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class VideoSendQueueTest {

    private static final long FRAME_US = 16_667;

    private static final class Channel implements VideoPackets.Sink {
        final Semaphore permits = new Semaphore(0);
        final List<String> written = Collections.synchronizedList(new ArrayList<>());
        volatile boolean broken;

        @Override
        public void write(byte[] header, ByteBuffer payload) throws IOException {
            permits.acquireUninterruptibly();
            if (broken) {
                throw new IOException("channel closed");
            }
            final Media.Header decoded = Media.decodeHeader(MediaChannel.VIDEO, header, 0, header.length);
            written.add(describe(decoded.kind, decoded.keyFrame, decoded.ptsUs, payload.remaining()));
            payload.position(payload.limit());
        }

        void awaitWritten(int count) throws InterruptedException {
            for (int i = 0; i < 500 && written.size() < count; ++i) {
                TimeUnit.MILLISECONDS.sleep(2);
            }
            assertEquals(count, written.size(), written.toString());
        }
    }

    private static String describe(MediaKind kind, boolean keyFrame, long ptsUs, int size) {
        return kind == MediaKind.FRAME ? (keyFrame ? "key " : "frame ") + ptsUs : kind.specName() + " " + size;
    }

    private final AtomicLong now = new AtomicLong(1_000_000);
    private final AtomicInteger keyFrameRequests = new AtomicInteger();
    private final Channel channel = new Channel();
    private final VideoSendQueue queue = new VideoSendQueue(channel, keyFrameRequests::incrementAndGet, FRAME_US,
            now::get);

    @AfterEach
    void close() throws InterruptedException {
        channel.permits.release(1_000);
        queue.close();
        queue.awaitEnd(1_000);
    }

    private static byte[] header(MediaKind kind, int flags, long ptsUs, int size) {
        final byte[] header = new byte[Constants.MEDIA_HEADER_SIZE];
        Media.writeHeader(header, 0, MediaChannel.VIDEO, kind, flags, 1, ptsUs, kind == MediaKind.FRAME ? 1_000 : 0,
                size);
        return header;
    }

    private void send(MediaKind kind, int flags, long ptsUs) throws IOException {
        final ByteBuffer payload = ByteBuffer.allocateDirect(4).put(new byte[] {0, 0, 1, 0x26});
        payload.flip();
        queue.write(header(kind, flags, ptsUs, 4), payload);
        assertFalse(payload.hasRemaining());
    }

    private void frame(long ptsUs, int flags) throws IOException {
        send(MediaKind.FRAME, flags, ptsUs);
    }

    private void stallOnFirstFrame() throws Exception {
        frame(1, 0);
        awaitTaken();
    }

    private void awaitTaken() throws Exception {
        awaitQueued(0);
    }

    private void awaitQueued(int count) throws Exception {
        for (int i = 0; i < 500 && queue.queued() > count; ++i) {
            TimeUnit.MILLISECONDS.sleep(2);
        }
        assertEquals(count, queue.queued());
    }

    private void awaitRequests(int count) throws Exception {
        for (int i = 0; i < 500 && keyFrameRequests.get() < count; ++i) {
            TimeUnit.MILLISECONDS.sleep(2);
        }
        assertEquals(count, keyFrameRequests.get());
    }

    @Test
    void packetsGoOutInOrderWhileTheChannelKeepsUp() throws Exception {
        channel.permits.release(4);
        send(MediaKind.SESSION, 0, 0);
        send(MediaKind.CONFIG, 0, 0);
        frame(1, Media.FLAG_KEY_FRAME);
        frame(2, Media.FLAG_DISCARDABLE);
        channel.awaitWritten(4);
        assertEquals(List.of("session 4", "config 4", "key 1", "frame 2"), channel.written);
        assertEquals(0, queue.droppedFrames());
        assertEquals(0, keyFrameRequests.get());
    }

    @Test
    void aDiscardableFrameIsDroppedOnceTheOldestWaitedLongerThanAFrame() throws Exception {
        stallOnFirstFrame();
        now.addAndGet(10_000);
        frame(2, Media.FLAG_DISCARDABLE);
        now.addAndGet(10_000);
        assertEquals(20_000, queue.waitedUs());
        frame(3, Media.FLAG_DISCARDABLE);
        frame(4, 0);
        channel.permits.release(10);
        channel.awaitWritten(3);
        assertEquals(List.of("frame 1", "frame 2", "frame 4"), channel.written);
        assertEquals(1, queue.droppedFrames());
        assertEquals(0, keyFrameRequests.get());
    }

    @Test
    void beyondTheLimitTheFramesGoUntilAKeyFrameAskedForOnceTheQueueDrained() throws Exception {
        stallOnFirstFrame();
        frame(2, 0);
        send(MediaKind.SESSION, 0, 0);
        send(MediaKind.CONFIG, 0, 0);
        frame(3, 0);
        now.addAndGet(VideoSendQueue.LIMIT_US + 1);
        frame(4, 0);
        frame(5, 0);
        assertEquals(0, keyFrameRequests.get());
        channel.permits.release(3);
        channel.awaitWritten(3);
        awaitRequests(1);
        frame(6, Media.FLAG_KEY_FRAME);
        frame(7, 0);
        channel.permits.release(10);
        channel.awaitWritten(5);
        assertEquals(List.of("frame 1", "session 4", "config 4", "key 6", "frame 7"), channel.written);
        assertEquals(4, queue.droppedFrames());
        assertEquals(1, keyFrameRequests.get());
    }

    @Test
    void keyFramesAreAskedForAtMostOnceASecond() throws Exception {
        stallOnFirstFrame();
        frame(2, 0);
        now.addAndGet(VideoSendQueue.LIMIT_US + 1);
        frame(3, 0);
        channel.permits.release(1);
        channel.awaitWritten(1);
        awaitRequests(1);

        frame(4, Media.FLAG_KEY_FRAME);
        frame(5, 0);
        channel.permits.release(1);
        channel.awaitWritten(2);
        awaitTaken();
        frame(6, 0);
        now.addAndGet(VideoSendQueue.LIMIT_US + 1);
        frame(7, 0);
        channel.permits.release(1);
        channel.awaitWritten(3);
        TimeUnit.MILLISECONDS.sleep(50);
        assertEquals(1, keyFrameRequests.get());

        now.addAndGet(VideoSendQueue.KEY_FRAME_SPACING_US);
        frame(8, 0);
        assertEquals(2, keyFrameRequests.get());
        assertEquals(5, queue.droppedFrames());
    }

    @Test
    void framesBehindAKeyFrameWaitForItWithoutFallingBehind() throws Exception {
        frame(1, Media.FLAG_KEY_FRAME);
        awaitTaken();
        frame(2, 0);
        now.addAndGet(500_000);
        frame(3, 0);
        channel.permits.release(10);
        channel.awaitWritten(3);
        assertEquals(List.of("key 1", "frame 2", "frame 3"), channel.written);
        assertEquals(0, queue.droppedFrames());
        assertEquals(0, keyFrameRequests.get());
    }

    @Test
    void theirTimeStartsOnceTheKeyFrameIsOut() throws Exception {
        frame(1, Media.FLAG_KEY_FRAME);
        awaitTaken();
        frame(2, 0);
        frame(3, 0);
        now.addAndGet(500_000);
        channel.permits.release(1);
        channel.awaitWritten(1);
        awaitQueued(1);
        now.addAndGet(VideoSendQueue.LIMIT_US + 1);
        frame(4, 0);
        assertEquals(2, queue.droppedFrames());
    }

    @Test
    void aKeyFrameGoingOutForOverASecondIsAStalledLink() throws Exception {
        frame(1, Media.FLAG_KEY_FRAME);
        awaitTaken();
        frame(2, 0);
        now.addAndGet(VideoSendQueue.KEY_FRAME_ALLOWANCE_US + VideoSendQueue.LIMIT_US + 1);
        frame(3, 0);
        assertEquals(2, queue.droppedFrames());
    }

    @Test
    void aKeyFrameBeyondTheLimitReplacesTheQueuedFramesWithoutARequest() throws Exception {
        stallOnFirstFrame();
        frame(2, 0);
        frame(3, 0);
        now.addAndGet(VideoSendQueue.LIMIT_US + 1);
        frame(4, Media.FLAG_KEY_FRAME);
        frame(5, 0);
        channel.permits.release(10);
        channel.awaitWritten(3);
        assertEquals(List.of("frame 1", "key 4", "frame 5"), channel.written);
        assertEquals(2, queue.droppedFrames());
        assertEquals(0, keyFrameRequests.get());
    }

    @Test
    void framesWaitWhileMoreThanTheWindowIsOnTheWay() throws Exception {
        channel.permits.release(10);
        queue.setLinkWindow(0, 30);
        frame(1, Media.FLAG_KEY_FRAME);
        channel.awaitWritten(1);
        frame(2, 0);
        channel.awaitWritten(2);
        send(MediaKind.SESSION, 0, 0);
        channel.awaitWritten(3);
        frame(3, 0);
        TimeUnit.MILLISECONDS.sleep(50);
        assertEquals(3, channel.written.size());
        queue.setLinkWindow(72, 30);
        channel.awaitWritten(4);
        assertEquals(List.of("key 1", "frame 2", "session 4", "frame 3"), channel.written);
    }

    @Test
    void aWindowNoReportRenewedHoldsNothingBack() throws Exception {
        channel.permits.release(10);
        queue.setLinkWindow(0, 30);
        frame(1, Media.FLAG_KEY_FRAME);
        frame(2, 0);
        channel.awaitWritten(2);
        now.addAndGet(VideoSendQueue.WINDOW_STALE_US);
        frame(3, Media.FLAG_KEY_FRAME);
        channel.awaitWritten(3);
        assertEquals(List.of("key 1", "frame 2", "key 3"), channel.written);
    }

    @Test
    void aBrokenChannelFailsTheNextWrite() throws Exception {
        channel.broken = true;
        channel.permits.release(1);
        frame(1, Media.FLAG_KEY_FRAME);
        final long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(1);
        IOException failure = null;
        while (failure == null && System.nanoTime() < deadline) {
            try {
                frame(2, 0);
                TimeUnit.MILLISECONDS.sleep(2);
            } catch (IOException e) {
                failure = e;
            }
        }
        assertTrue(failure != null, "writes kept succeeding after the channel broke");
        assertThrows(IOException.class, () -> frame(3, 0));
    }
}
