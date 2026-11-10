package com.gamescrcpy.server.device;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTimeoutPreemptively;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Codec;
import com.gamescrcpy.protocol.Direction;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.RequestFailed;
import com.gamescrcpy.protocol.Messages.Screenshot;
import com.gamescrcpy.server.transport.FrameEndpoint;
import java.io.IOException;
import java.time.Duration;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class BulkRequestsTest {

    private static final long WAIT_SECONDS = 5;

    private static final class Bulk implements FrameEndpoint {
        final BlockingQueue<Message> written = new LinkedBlockingQueue<>();
        final CountDownLatch writing = new CountDownLatch(1);
        volatile CountDownLatch release = new CountDownLatch(0);

        @Override
        public Message read(FrameContext context) throws IOException {
            throw new IOException("write only");
        }

        @Override
        public void write(Message message) {
            writing.countDown();
            try {
                release.await(WAIT_SECONDS, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            final byte[] frame = Codec.encodeFrame(message);
            written.add(Codec.decodeFrame(FrameContext.BULK, Direction.TO_CLIENT, frame, 0, frame.length));
        }

        @Override
        public void setReadTimeout(int milliseconds) {
        }

        @Override
        public void close() {
        }

        Message next() throws InterruptedException {
            final Message message = written.poll(WAIT_SECONDS, TimeUnit.SECONDS);
            if (message == null) {
                throw new AssertionError("nothing was written to the bulk channel");
            }
            return message;
        }
    }

    private final BulkRequests requests = new BulkRequests();
    private final ExecutorService worker = Executors.newSingleThreadExecutor();

    @AfterEach
    void tearDown() {
        worker.shutdownNow();
        requests.close();
    }

    private static Screenshot reply(long requestId) {
        final Screenshot screenshot = new Screenshot();
        screenshot.requestId = requestId;
        screenshot.width = 1;
        screenshot.height = 1;
        return screenshot;
    }

    @Test
    void aReplyStuckInTheSocketNeverHoldsUpTheControlThread() throws Exception {
        final Bulk bulk = new Bulk();
        bulk.release = new CountDownLatch(1);
        requests.bulkOpened(bulk);

        requests.submit(1, worker, () -> reply(1));
        assertTrue(bulk.writing.await(WAIT_SECONDS, TimeUnit.SECONDS));
        assertTimeoutPreemptively(Duration.ofSeconds(WAIT_SECONDS), () -> {
            for (long id = 2; id <= 6; ++id) {
                final long requestId = id;
                requests.submit(requestId, worker, () -> reply(requestId));
            }
        });
        bulk.release.countDown();

        final Set<Long> answered = new HashSet<>();
        final Set<Long> busy = new HashSet<>();
        for (int reply = 0; reply < 6; ++reply) {
            final Message message = bulk.next();
            if (message instanceof RequestFailed failed) {
                assertEquals(ErrorCode.RESOURCE_BUSY, failed.code);
                busy.add(failed.requestId);
            } else {
                answered.add(assertInstanceOf(Screenshot.class, message).requestId);
            }
        }
        assertEquals(Set.of(1L, 2L, 3L, 4L), answered);
        assertEquals(Set.of(5L, 6L), busy);
    }

    @Test
    void requestsTheServerCanNoLongerRunAreDroppedAndFreeTheirPlace() throws Exception {
        final Bulk bulk = new Bulk();
        requests.bulkOpened(bulk);
        final Executor closed = command -> {
            throw new RejectedExecutionException("closed");
        };

        for (long id = 1; id <= BulkRequests.MAX_OUTSTANDING + 1; ++id) {
            final long requestId = id;
            requests.submit(requestId, closed, () -> reply(requestId));
        }
        assertNull(bulk.written.poll(200, TimeUnit.MILLISECONDS));

        for (long id = 11; id <= 10 + BulkRequests.MAX_OUTSTANDING; ++id) {
            final long requestId = id;
            requests.submit(requestId, Runnable::run, () -> reply(requestId));
        }
        for (long id = 11; id <= 10 + BulkRequests.MAX_OUTSTANDING; ++id) {
            assertEquals(id, assertInstanceOf(Screenshot.class, bulk.next()).requestId);
        }

        requests.close();
        requests.submit(21, Runnable::run, () -> reply(21));
        assertNull(bulk.written.poll(200, TimeUnit.MILLISECONDS));
    }
}
