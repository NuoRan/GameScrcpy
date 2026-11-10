package com.gamescrcpy.server.device;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Codec;
import com.gamescrcpy.protocol.Direction;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.AppEntry;
import com.gamescrcpy.protocol.Messages.AppIcon;
import com.gamescrcpy.protocol.Messages.AppList;
import com.gamescrcpy.protocol.Messages.RequestAppIcon;
import com.gamescrcpy.protocol.Messages.RequestAppList;
import com.gamescrcpy.protocol.Messages.RequestFailed;
import com.gamescrcpy.protocol.Messages.StartApp;
import com.gamescrcpy.server.transport.FrameEndpoint;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class AppServiceTest {

    private static final long WAIT_SECONDS = 5;

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

    private static final class Phone implements AppService.Platform {
        final BlockingQueue<String> started = new LinkedBlockingQueue<>();
        final AtomicInteger iconCalls = new AtomicInteger();
        volatile CountDownLatch iconGate = new CountDownLatch(0);
        volatile List<AppService.App> apps = List.of();
        volatile boolean includedSystem;

        @Override
        public List<AppService.App> launchable(boolean includeSystem) {
            includedSystem = includeSystem;
            return apps;
        }

        @Override
        public byte[] icon(String packageName, int sizePx) throws AppService.Failure {
            iconCalls.incrementAndGet();
            try {
                iconGate.await(WAIT_SECONDS, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            if (packageName.equals("gone")) {
                throw new AppService.Failure(ErrorCode.NOT_FOUND, "gone is not installed");
            }
            return new byte[] {(byte) 0x89, 'P', 'N', 'G', (byte) sizePx};
        }

        @Override
        public void start(String packageName, int displayId, boolean forceStop) throws AppService.Failure {
            if (packageName.equals("nolauncher")) {
                throw new AppService.Failure(ErrorCode.NOT_FOUND, "nolauncher has no launcher entry");
            }
            started.add(packageName + "@" + displayId + (forceStop ? " stopped first" : ""));
        }
    }

    private final Phone phone = new Phone();
    private final BlockingQueue<String> notices = new LinkedBlockingQueue<>();
    private final AtomicInteger videoDisplay = new AtomicInteger(AppService.NO_DISPLAY);
    private final AppService service = new AppService(phone, videoDisplay::get,
            (code, message) -> notices.add(code.specName() + ": " + message));

    @AfterEach
    void tearDown() {
        service.close();
    }

    private static RequestAppList listRequest(long requestId, boolean includeSystem) {
        final RequestAppList request = new RequestAppList();
        request.requestId = requestId;
        request.includeSystem = includeSystem;
        return request;
    }

    private static RequestAppIcon iconRequest(long requestId, String packageName) {
        final RequestAppIcon request = new RequestAppIcon();
        request.requestId = requestId;
        request.packageName = packageName;
        request.sizePx = 96;
        return request;
    }

    private static StartApp startRequest(String packageName, int flags) {
        final StartApp request = new StartApp();
        request.packageName = packageName;
        request.flags = flags;
        return request;
    }

    @Test
    void listsTheLaunchableAppsWithTheirVersionAndFlags() throws Exception {
        phone.apps = List.of(new AppService.App("com.example.game", "Example Game", 412, false, true, true),
                new AppService.App("com.android.settings", "设置", 35, true, true, false));
        final Bulk bulk = new Bulk();
        service.bulkOpened(bulk);

        service.list(listRequest(7, true));

        final AppList list = assertInstanceOf(AppList.class, bulk.next());
        assertEquals(7, list.requestId);
        assertTrue(phone.includedSystem);
        assertEquals(2, list.apps.size());
        final AppEntry game = list.apps.get(0);
        assertEquals("com.example.game", game.packageName);
        assertEquals("Example Game", game.label);
        assertEquals(412, game.versionCode);
        assertEquals(AppEntry.ENABLED | AppEntry.GAME, game.flags);
        final AppEntry settings = list.apps.get(1);
        assertEquals("设置", settings.label);
        assertEquals(AppEntry.SYSTEM | AppEntry.ENABLED, settings.flags);
    }

    @Test
    void longLabelsAreClippedToTheWireLimitOnACharacterBoundary() throws Exception {
        phone.apps = List.of(new AppService.App("com.example.long", "\u6e38".repeat(100), 1, false, true, false));
        final Bulk bulk = new Bulk();
        service.bulkOpened(bulk);

        service.list(listRequest(1, false));

        final AppList list = assertInstanceOf(AppList.class, bulk.next());
        final String label = list.apps.get(0).label;
        assertTrue(label.getBytes(StandardCharsets.UTF_8).length <= 255);
        assertEquals(85, label.length());
    }

    @Test
    void iconsArriveAsPngAndAMissingPackageFailsWithNotFound() throws Exception {
        final Bulk bulk = new Bulk();
        service.bulkOpened(bulk);

        service.icon(iconRequest(3, "com.example.game"));
        final AppIcon icon = assertInstanceOf(AppIcon.class, bulk.next());
        assertEquals(3, icon.requestId);
        assertEquals("com.example.game", icon.packageName);
        assertArrayEquals(new byte[] {(byte) 0x89, 'P', 'N', 'G', 96}, icon.png);

        service.icon(iconRequest(4, "gone"));
        final RequestFailed failed = assertInstanceOf(RequestFailed.class, bulk.next());
        assertEquals(4, failed.requestId);
        assertEquals(ErrorCode.NOT_FOUND, failed.code);
    }

    @Test
    void aFifthOutstandingRequestIsRefusedAsBusy() throws Exception {
        final Bulk bulk = new Bulk();
        service.bulkOpened(bulk);
        phone.iconGate = new CountDownLatch(1);

        for (int request = 1; request <= AppService.MAX_OUTSTANDING; ++request) {
            service.icon(iconRequest(request, "com.example.app" + request));
        }
        service.icon(iconRequest(99, "com.example.extra"));

        final RequestFailed busy = assertInstanceOf(RequestFailed.class, bulk.next());
        assertEquals(99, busy.requestId);
        assertEquals(ErrorCode.RESOURCE_BUSY, busy.code);

        phone.iconGate.countDown();
        for (int request = 1; request <= AppService.MAX_OUTSTANDING; ++request) {
            assertEquals(request, assertInstanceOf(AppIcon.class, bulk.next()).requestId);
        }
        service.icon(iconRequest(100, "com.example.again"));
        assertEquals(100, assertInstanceOf(AppIcon.class, bulk.next()).requestId);
    }

    @Test
    void requestsWithoutABulkChannelAreDropped() throws Exception {
        service.list(listRequest(1, false));
        service.icon(iconRequest(2, "com.example.game"));

        final Bulk later = new Bulk();
        service.bulkOpened(later);
        service.icon(iconRequest(3, "com.example.game"));
        assertEquals(3, assertInstanceOf(AppIcon.class, later.next()).requestId);
        assertNull(later.written.poll(200, TimeUnit.MILLISECONDS));

        service.connectionLost();
        service.icon(iconRequest(4, "com.example.game"));
        assertNull(later.written.poll(200, TimeUnit.MILLISECONDS));
    }

    @Test
    void startsOnTheMainDisplayOrOnTheDisplayTheVideoShows() throws Exception {
        service.start(startRequest("com.example.game", 0));
        assertEquals("com.example.game@0", phone.started.poll(WAIT_SECONDS, TimeUnit.SECONDS));

        service.start(startRequest("com.example.game", StartApp.FORCE_STOP));
        assertEquals("com.example.game@0 stopped first", phone.started.poll(WAIT_SECONDS, TimeUnit.SECONDS));

        service.start(startRequest("com.example.game", StartApp.ON_VIDEO_DISPLAY));
        assertEquals("notFound: cannot start com.example.game on the video display: no video yet",
                notices.poll(WAIT_SECONDS, TimeUnit.SECONDS));

        videoDisplay.set(7);
        service.start(startRequest("com.example.game", StartApp.ON_VIDEO_DISPLAY));
        assertEquals("com.example.game@7", phone.started.poll(WAIT_SECONDS, TimeUnit.SECONDS));
    }

    @Test
    void aLaunchTheSystemRefusesBecomesANotice() throws Exception {
        service.start(startRequest("nolauncher", 0));
        assertEquals("notFound: nolauncher has no launcher entry", notices.poll(WAIT_SECONDS, TimeUnit.SECONDS));
        assertNull(phone.started.poll(100, TimeUnit.MILLISECONDS));
    }
}
