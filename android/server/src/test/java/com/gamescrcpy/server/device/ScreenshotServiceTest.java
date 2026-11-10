package com.gamescrcpy.server.device;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Codec;
import com.gamescrcpy.protocol.Direction;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.ScreenshotFormat;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.AppIcon;
import com.gamescrcpy.protocol.Messages.RequestAppIcon;
import com.gamescrcpy.protocol.Messages.RequestFailed;
import com.gamescrcpy.protocol.Messages.RequestScreenshot;
import com.gamescrcpy.protocol.Messages.Screenshot;
import com.gamescrcpy.server.transport.FrameEndpoint;
import java.io.IOException;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class ScreenshotServiceTest {

    private static final long WAIT_SECONDS = 5;
    private static final int MAIN = 0;
    private static final byte[] PNG = {(byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 7};

    private static final class Bulk implements FrameEndpoint {
        final BlockingQueue<Message> written = new LinkedBlockingQueue<>();

        @Override
        public Message read(FrameContext context) throws IOException {
            throw new IOException("write only");
        }

        @Override
        public void write(Message message) {
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

    private static final class Phone implements ScreenshotService.Platform {
        final Map<Integer, DisplaySnapshot> displays = new ConcurrentHashMap<>();
        final BlockingQueue<Integer> captured = new LinkedBlockingQueue<>();
        volatile CountDownLatch gate = new CountDownLatch(0);
        volatile byte[] png = PNG;
        volatile RuntimeException crash;
        volatile BulkRequests.Failure failure;

        @Override
        public DisplaySnapshot display(int displayId) {
            return displays.get(displayId);
        }

        @Override
        public byte[] png(DisplaySnapshot display) throws BulkRequests.Failure {
            captured.add(display.displayId());
            await(gate);
            if (crash != null) {
                throw crash;
            }
            if (failure != null) {
                throw failure;
            }
            return png;
        }
    }

    private static final class Icons implements AppService.Platform {
        volatile CountDownLatch gate = new CountDownLatch(0);

        @Override
        public List<AppService.App> launchable(boolean includeSystem) {
            return List.of();
        }

        @Override
        public byte[] icon(String packageName, int sizePx) {
            await(gate);
            return new byte[] {(byte) 0x89, 'P', 'N', 'G'};
        }

        @Override
        public void start(String packageName, int displayId, boolean forceStop) {
        }
    }

    private final Phone phone = new Phone();
    private final Icons icons = new Icons();
    private final BulkRequests shared = new BulkRequests();
    private final ScreenshotService screenshots = new ScreenshotService(phone, shared);
    private final AppService apps = new AppService(icons, () -> AppService.NO_DISPLAY, (code, message) -> {
    }, shared);

    ScreenshotServiceTest() {
        phone.displays.put(MAIN, display(MAIN, 1080, 2400));
    }

    @AfterEach
    void tearDown() {
        screenshots.close();
        apps.close();
        shared.close();
    }

    private static void await(CountDownLatch gate) {
        try {
            gate.await(WAIT_SECONDS, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private static DisplaySnapshot display(int displayId, int width, int height) {
        return new DisplaySnapshot(displayId, width, height, 0, displayId, 0, 420, 60_000, DisplaySnapshot.STATE_ON,
                0, 0, 0, 0);
    }

    private static RequestScreenshot request(long requestId, long displayId) {
        final RequestScreenshot request = new RequestScreenshot();
        request.requestId = requestId;
        request.displayId = displayId;
        request.format = ScreenshotFormat.PNG;
        return request;
    }

    private static RequestAppIcon iconRequest(long requestId) {
        final RequestAppIcon request = new RequestAppIcon();
        request.requestId = requestId;
        request.packageName = "com.example.app" + requestId;
        request.sizePx = 96;
        return request;
    }

    private Bulk opened() {
        final Bulk bulk = new Bulk();
        shared.bulkOpened(bulk);
        return bulk;
    }

    private static RequestFailed failed(Message message, long requestId, ErrorCode code) {
        final RequestFailed failed = assertInstanceOf(RequestFailed.class, message);
        assertEquals(requestId, failed.requestId);
        assertEquals(code, failed.code);
        return failed;
    }

    @Test
    void aScreenshotCarriesTheRequestTheDisplaySizeAndTheLosslessPicture() throws Exception {
        final Bulk bulk = opened();

        screenshots.capture(request(5, MAIN));

        final Screenshot screenshot = assertInstanceOf(Screenshot.class, bulk.next());
        assertEquals(5, screenshot.requestId);
        assertEquals(1080, screenshot.width);
        assertEquals(2400, screenshot.height);
        assertEquals(ScreenshotFormat.PNG, screenshot.format);
        assertArrayEquals(PNG, screenshot.data);
        assertEquals(MAIN, phone.captured.poll());
    }

    @Test
    void anUnknownDisplayFailsWithNotFoundBeforeAnyCapture() throws Exception {
        final Bulk bulk = opened();

        screenshots.capture(request(6, 9));
        assertEquals("display 9 does not exist", failed(bulk.next(), 6, ErrorCode.NOT_FOUND).message);

        screenshots.capture(request(7, 0xFFFF_FFFFL));
        failed(bulk.next(), 7, ErrorCode.NOT_FOUND);
        assertNull(phone.captured.poll());
    }

    @Test
    void aCaptureThatFailsOrThrowsFailsWithInternalError() throws Exception {
        final Bulk bulk = opened();

        phone.failure = new BulkRequests.Failure(ErrorCode.INTERNAL_ERROR, "display 0 showed no picture within 2000 ms");
        screenshots.capture(request(8, MAIN));
        assertEquals("display 0 showed no picture within 2000 ms",
                failed(bulk.next(), 8, ErrorCode.INTERNAL_ERROR).message);

        phone.failure = null;
        phone.crash = new IllegalStateException("the surface was abandoned");
        screenshots.capture(request(9, MAIN));
        assertTrue(failed(bulk.next(), 9, ErrorCode.INTERNAL_ERROR).message.contains("the surface was abandoned"));

        phone.crash = null;
        screenshots.capture(request(10, MAIN));
        assertEquals(10, assertInstanceOf(Screenshot.class, bulk.next()).requestId);
    }

    @Test
    void aPictureOverTheWireLimitFailsWithInternalError() throws Exception {
        final Bulk bulk = opened();
        phone.png = new byte[ScreenshotService.MAX_PNG_BYTES + 1];

        screenshots.capture(request(11, MAIN));

        assertTrue(failed(bulk.next(), 11, ErrorCode.INTERNAL_ERROR).message.contains("over the limit"));
    }

    @Test
    void aDisplayTooLargeForTheWireFailsWithInternalErrorBeforeAnyCapture() throws Exception {
        final Bulk bulk = opened();
        phone.displays.put(3, display(3, 70_000, 1080));

        screenshots.capture(request(12, 3));

        failed(bulk.next(), 12, ErrorCode.INTERNAL_ERROR);
        assertNull(phone.captured.poll());
    }

    @Test
    void screenshotsAndAppRequestsShareTheLimitOfFourOutstanding() throws Exception {
        final Bulk bulk = opened();
        phone.gate = new CountDownLatch(1);
        icons.gate = new CountDownLatch(1);

        apps.icon(iconRequest(1));
        screenshots.capture(request(2, MAIN));
        apps.icon(iconRequest(3));
        screenshots.capture(request(4, MAIN));
        screenshots.capture(request(5, MAIN));
        apps.icon(iconRequest(6));

        failed(bulk.next(), 5, ErrorCode.RESOURCE_BUSY);
        failed(bulk.next(), 6, ErrorCode.RESOURCE_BUSY);

        phone.gate.countDown();
        icons.gate.countDown();
        final Set<Long> iconReplies = new HashSet<>();
        final Set<Long> screenshotReplies = new HashSet<>();
        for (int reply = 0; reply < BulkRequests.MAX_OUTSTANDING; ++reply) {
            final Message message = bulk.next();
            if (message instanceof AppIcon icon) {
                iconReplies.add(icon.requestId);
            } else {
                screenshotReplies.add(assertInstanceOf(Screenshot.class, message).requestId);
            }
        }
        assertEquals(Set.of(1L, 3L), iconReplies);
        assertEquals(Set.of(2L, 4L), screenshotReplies);

        screenshots.capture(request(7, MAIN));
        assertEquals(7, assertInstanceOf(Screenshot.class, bulk.next()).requestId);
    }

    @Test
    void aSlowCaptureDoesNotHoldBackAppIcons() throws Exception {
        final Bulk bulk = opened();
        phone.gate = new CountDownLatch(1);

        screenshots.capture(request(1, MAIN));
        assertEquals(MAIN, phone.captured.poll(WAIT_SECONDS, TimeUnit.SECONDS));
        apps.icon(iconRequest(2));

        assertEquals(2, assertInstanceOf(AppIcon.class, bulk.next()).requestId);
        phone.gate.countDown();
        assertEquals(1, assertInstanceOf(Screenshot.class, bulk.next()).requestId);
    }

    @Test
    void requestsWithoutABulkChannelAreDroppedUncaptured() throws Exception {
        screenshots.capture(request(1, MAIN));
        assertNull(phone.captured.poll(200, TimeUnit.MILLISECONDS));

        final Bulk bulk = opened();
        screenshots.capture(request(2, MAIN));
        assertEquals(2, assertInstanceOf(Screenshot.class, bulk.next()).requestId);
        assertNull(bulk.written.poll(200, TimeUnit.MILLISECONDS));

        shared.connectionLost();
        phone.captured.clear();
        screenshots.capture(request(3, MAIN));
        assertNull(phone.captured.poll(200, TimeUnit.MILLISECONDS));
        assertNull(bulk.written.poll(200, TimeUnit.MILLISECONDS));
    }
}
