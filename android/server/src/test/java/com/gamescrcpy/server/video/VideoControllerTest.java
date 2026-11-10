package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Codec;
import com.gamescrcpy.protocol.Constants;
import com.gamescrcpy.protocol.Direction;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.MediaKind;
import com.gamescrcpy.protocol.Enums.NoticeLevel;
import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.protocol.Enums.VideoSource;
import com.gamescrcpy.protocol.FrameContext;
import com.gamescrcpy.protocol.Media;
import com.gamescrcpy.protocol.MediaChannel;
import com.gamescrcpy.protocol.Message;
import com.gamescrcpy.protocol.Messages.ConfigureVideo;
import com.gamescrcpy.protocol.Messages.Notice;
import com.gamescrcpy.protocol.Messages.ReceiverReport;
import com.gamescrcpy.protocol.Messages.VideoSessionInfo;
import com.gamescrcpy.server.MediaClock;
import com.gamescrcpy.server.device.DisplaySnapshot;
import com.gamescrcpy.server.transport.Connection;
import com.gamescrcpy.server.transport.StreamEndpoint;
import com.gamescrcpy.server.video.FakeEncoding.Script;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.LongSupplier;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class VideoControllerTest {

    private static final class Pipe implements Connection {
        final ByteArrayOutputStream messages = new ByteArrayOutputStream();
        final BlockingQueue<byte[]> packets = new LinkedBlockingQueue<>();
        final CountDownLatch closed = new CountDownLatch(1);

        @Override
        public InputStream input() {
            return new ByteArrayInputStream(new byte[0]);
        }

        @Override
        public OutputStream output() {
            return messages;
        }

        @Override
        public void setReadTimeout(int milliseconds) {
        }

        @Override
        public void writePacket(byte[] header, ByteBuffer payload) throws IOException {
            if (closed.getCount() == 0) {
                throw new IOException("closed");
            }
            final byte[] packet = new byte[header.length + payload.remaining()];
            System.arraycopy(header, 0, packet, 0, header.length);
            payload.get(packet, header.length, packet.length - header.length);
            packets.add(packet);
        }

        @Override
        public void close() {
            closed.countDown();
        }

        Media.Packet nextPacket() throws InterruptedException {
            final byte[] packet = packets.poll(FakeEncoding.WAIT_SECONDS, TimeUnit.SECONDS);
            if (packet == null) {
                throw new AssertionError("no packet arrived");
            }
            return Media.decodePacket(MediaChannel.VIDEO, packet, 0, packet.length);
        }

        List<Message> messages() {
            final byte[] bytes = messages.toByteArray();
            final List<Message> result = new ArrayList<>();
            for (int offset = 0; offset < bytes.length; ) {
                final Codec.FrameHeader header = Codec.decodeFrameHeader(FrameContext.CONTROL, Direction.TO_CLIENT,
                        bytes, offset, Constants.FRAME_HEADER_SIZE);
                offset += Constants.FRAME_HEADER_SIZE;
                result.add(Codec.decodePayload(header.type.create(), bytes, offset, header.length));
                offset += header.length;
            }
            return result;
        }
    }

    private final FakeEncoding.Factory factory = new FakeEncoding.Factory();
    private final FakeEncoding.Displays displays = new FakeEncoding.Displays();

    private VideoController controller(List<com.gamescrcpy.server.device.EncoderCatalog.Encoder> encoders) {
        return controller(encoders, MediaClock::nowUs);
    }

    private VideoController controller(List<com.gamescrcpy.server.device.EncoderCatalog.Encoder> encoders,
                                       LongSupplier clock) {
        return controller(encoders, clock, (e, w, h) -> true);
    }

    private VideoController controller(List<com.gamescrcpy.server.device.EncoderCatalog.Encoder> encoders,
                                       LongSupplier clock, EncoderLadder.SizeSupport sizes) {
        return new VideoController(factory, () -> encoders, sizes, displays, () -> {
        }, TimeUnit.SECONDS.toNanos(1), clock);
    }

    private final VideoController video = controller(FakeEncoding.ENCODERS);

    private static ConfigureVideo configure(VideoSource source) {
        final ConfigureVideo message = new ConfigureVideo();
        message.source = source;
        message.codec = VideoCodec.H264;
        message.bitrate = 4_000_000;
        return message;
    }

    @AfterEach
    void tearDown() {
        video.connectionLost();
    }

    @Test
    void streamsOnceConfigurationAndChannelBothExist() throws Exception {
        factory.then(new Script().frame(1, true, 0x65));
        video.configure(configure(VideoSource.DISPLAY));
        assertTrue(factory.attempts.isEmpty(), "streaming started without a channel");

        final Pipe pipe = new Pipe();
        video.videoChannelOpened(new StreamEndpoint(pipe));
        assertEquals(MediaKind.SESSION, pipe.nextPacket().header.kind);
        assertEquals(MediaKind.FRAME, pipe.nextPacket().header.kind);
        assertEquals(1, video.sessionNumber());
        assertEquals(4_000_000, factory.awaitOpened().settings.bitrate());
    }

    @Test
    void channelFirstThenConfiguration() throws Exception {
        final Pipe pipe = new Pipe();
        video.videoChannelOpened(new StreamEndpoint(pipe));
        assertTrue(factory.attempts.isEmpty(), "streaming started without a configuration");
        video.configure(configure(VideoSource.DISPLAY));
        assertEquals(MediaKind.SESSION, pipe.nextPacket().header.kind);
    }

    @Test
    void newChannelReleasesTheOldEncoderBeforeOpeningAnother() throws Exception {
        factory.then(new Script().frame(1, true, 0x65)).then(new Script().frame(2, true, 0x65));
        video.configure(configure(VideoSource.DISPLAY));
        final Pipe first = new Pipe();
        video.videoChannelOpened(new StreamEndpoint(first));
        first.nextPacket();

        final Pipe second = new Pipe();
        video.videoChannelOpened(new StreamEndpoint(second));
        assertEquals(2, second.nextPacket().header.session);
        assertEquals(List.of("open 1", "close 1", "open 2"), List.copyOf(factory.events).subList(0, 3));
    }

    @Test
    void lostConnectionStopsAndResumptionStreamsAgain() throws Exception {
        factory.then(new Script().frame(1, true, 0x65)).then(new Script().frame(2, true, 0x65));
        video.configure(configure(VideoSource.DISPLAY));
        final Pipe before = new Pipe();
        video.videoChannelOpened(new StreamEndpoint(before));
        before.nextPacket();

        video.connectionLost();
        assertTrue(factory.events.contains("close 1"), "the encoder outlived the connection");
        assertEquals(0, video.sessionNumber());

        final Pipe resumed = new Pipe();
        video.videoChannelOpened(new StreamEndpoint(resumed));
        assertEquals(2, resumed.nextPacket().header.session);
    }

    @Test
    void repeatedSettingsKeepTheRunningSessionAndChangedOnesRestartIt() throws Exception {
        factory.then(new Script().frame(1, true, 0x65)).then(new Script().frame(2, true, 0x65));
        video.configure(configure(VideoSource.DISPLAY));
        final Pipe pipe = new Pipe();
        video.videoChannelOpened(new StreamEndpoint(pipe));
        pipe.nextPacket();
        pipe.nextPacket();
        assertEquals(4_000_000, factory.awaitOpened().settings.bitrate());

        video.configure(configure(VideoSource.DISPLAY));
        assertNull(pipe.packets.poll(300, TimeUnit.MILLISECONDS), "identical settings rebuilt the encoder");
        assertEquals(1, video.sessionNumber());

        final ConfigureVideo changed = configure(VideoSource.DISPLAY);
        changed.bitrate = 2_000_000;
        video.configure(changed);
        assertEquals(2, pipe.nextPacket().header.session);
        assertEquals(2_000_000, factory.awaitOpened().settings.bitrate());
    }

    private static ReceiverReport report(long delayMinUs, long delayMeanUs, long bytes) {
        final ReceiverReport report = new ReceiverReport();
        report.videoSession = 1;
        report.intervalUs = 200_000;
        report.frames = 12;
        report.bytes = bytes;
        report.delayMinUs = delayMinUs;
        report.delayMeanUs = delayMeanUs;
        report.delayMaxUs = delayMeanUs;
        return report;
    }

    @Test
    void receiverReportsLowerTheBitRateWithinTheClientsCap() throws Exception {
        factory.then(new Script().frame(1, true, 0x65));
        video.configure(configure(VideoSource.DISPLAY));
        final Pipe pipe = new Pipe();
        video.videoChannelOpened(new StreamEndpoint(pipe));
        pipe.nextPacket();
        pipe.nextPacket();
        final FakeEncoding.Session session = factory.awaitOpened();

        video.receiverReport(report(50_000, 51_000, 100_000));
        video.receiverReport(report(50_000, 160_000, 50_000));
        assertTrue(session.calls.contains("bitrate 3400000"), session.calls.toString());

        video.setBitrate(1_000_000);
        assertTrue(session.calls.contains("bitrate 1000000"), session.calls.toString());
    }

    @Test
    void anEncoderTheLinkCannotCarryAtItsFloorStreamsSmallerWithANotice() throws Exception {
        final AtomicLong now = new AtomicLong(1_000_000);
        final VideoController stepping = controller(FakeEncoding.ENCODERS, now::get, (e, w, h) -> Math.max(w, h) <= 1920);
        final Script heavy = new Script().frame(1, true, 0x65, 25_000);
        for (int pts = 2; pts <= 40; ++pts) {
            heavy.frame(pts, false, 0x41, 25_000);
        }
        factory.then(heavy).then(new Script().frame(100, true, 0x65));
        final Pipe control = new Pipe();
        stepping.controlConnected(new StreamEndpoint(control));
        stepping.configure(configure(VideoSource.DISPLAY));
        final Pipe pipe = new Pipe();
        stepping.videoChannelOpened(new StreamEndpoint(pipe));
        for (int packet = 0; packet < 41; ++packet) {
            pipe.nextPacket();
        }
        assertEquals(0, factory.awaitOpened().settings.maxSize());

        stepping.receiverReport(report(50_000, 51_000, 5_000));
        for (int i = 0; i < 15; ++i) {
            now.addAndGet(200_000);
            stepping.receiverReport(report(50_000, 400_000, 5_000));
        }
        assertEquals(1440, factory.awaitOpened().settings.maxSize());
        final Notice notice = assertInstanceOf(Notice.class, control.messages().get(0));
        assertEquals(NoticeLevel.WARNING, notice.level);
        assertEquals(ErrorCode.VIDEO_REDUCED, notice.code);

        final Pipe resumed = new Pipe();
        stepping.controlConnected(new StreamEndpoint(resumed));
        assertEquals(ErrorCode.VIDEO_REDUCED, ((Notice) resumed.messages().get(0)).code);
        final ConfigureVideo changed = configure(VideoSource.DISPLAY);
        changed.bitrate = 2_000_000;
        stepping.configure(changed);
        assertEquals(ErrorCode.VIDEO_RESTORED, ((Notice) resumed.messages().get(1)).code);
        assertEquals(0, factory.awaitOpened().settings.maxSize());
        stepping.connectionLost();
    }

    @Test
    void virtualDisplayIsRefusedWithANotice() throws Exception {
        final Pipe control = new Pipe();
        video.controlConnected(new StreamEndpoint(control));
        video.configure(configure(VideoSource.VIRTUAL_DISPLAY));

        final Notice notice = assertInstanceOf(Notice.class, control.messages().get(0));
        assertEquals(NoticeLevel.ERROR, notice.level);
        assertEquals(ErrorCode.CHANNEL_UNAVAILABLE, notice.code);
        assertTrue(factory.attempts.isEmpty());
    }

    private static final class Screens implements IndependentDisplays {
        final List<String> calls = java.util.Collections.synchronizedList(new ArrayList<>());
        final java.util.Map<Integer, DisplaySnapshot> shown = new java.util.concurrent.ConcurrentHashMap<>();
        int nextId = 7;

        @Override
        public synchronized int create(int width, int height, int densityDpi) {
            final int displayId = nextId++;
            calls.add("create " + displayId + " " + width + "x" + height + " " + densityDpi);
            shown.put(displayId, snapshot(displayId, width, height, 0, densityDpi));
            return displayId;
        }

        @Override
        public void resize(int displayId, int width, int height, int densityDpi) {
            calls.add("resize " + displayId + " " + width + "x" + height + " " + densityDpi);
        }

        @Override
        public void release(int displayId) {
            calls.add("release " + displayId);
            shown.remove(displayId);
        }
    }

    private static DisplaySnapshot snapshot(int displayId, int width, int height, int rotation, int densityDpi) {
        return new DisplaySnapshot(displayId, width, height, rotation, displayId, 0, densityDpi, 60_000,
                DisplaySnapshot.STATE_ON, 0, 0, 0, 0);
    }

    private final Screens screens = new Screens();

    private VideoController withScreens(DisplaySnapshot main) {
        return new VideoController(factory, () -> FakeEncoding.ENCODERS, (e, w, h) -> true,
                displayId -> displayId == 0 ? main : screens.shown.get(displayId), screens, () -> {
                }, TimeUnit.SECONDS.toNanos(1), MediaClock::nowUs);
    }

    private static ConfigureVideo independent(int width, int height, int densityDpi) {
        final ConfigureVideo message = configure(VideoSource.VIRTUAL_DISPLAY);
        message.virtualWidth = width;
        message.virtualHeight = height;
        message.virtualDensity = densityDpi;
        return message;
    }

    @Test
    void anIndependentDisplayTakesTheMainDisplaysNaturalSizeAndStreamsFromIt() throws Exception {
        final VideoController apps = withScreens(snapshot(0, 2400, 1080, 1, 420));
        assertTrue(apps.supportsIndependentDisplays());
        factory.then(new Script().frame(1, true, 0x65));
        apps.configure(independent(0, 0, 0));
        assertEquals(List.of("create 7 1080x2400 420"), List.copyOf(screens.calls));
        assertEquals(7, apps.videoDisplay());
        assertTrue(apps.showsIndependent(7));

        final Pipe pipe = new Pipe();
        apps.videoChannelOpened(new StreamEndpoint(pipe));
        final Media.Packet session = pipe.nextPacket();
        assertEquals(MediaKind.SESSION, session.header.kind);
        assertEquals(7, assertInstanceOf(VideoSessionInfo.class, session.session).displayId);
        assertEquals(7, factory.awaitOpened().settings.displayId());
        apps.close();
        assertEquals("release 7", screens.calls.get(screens.calls.size() - 1));
    }

    @Test
    void theSameRequestKeepsTheDisplayAndAnotherSizeReplacesIt() throws Exception {
        final VideoController apps = withScreens(snapshot(0, 1080, 2400, 0, 440));
        apps.configure(independent(0, 0, 0));
        apps.configure(independent(0, 0, 0));
        assertEquals(List.of("create 7 1080x2400 440"), List.copyOf(screens.calls));

        apps.configure(independent(1920, 1080, 320));
        assertEquals(List.of("create 7 1080x2400 440", "create 8 1920x1080 320", "release 7"),
                List.copyOf(screens.calls));
        assertEquals(8, apps.videoDisplay());

        apps.configure(configure(VideoSource.DISPLAY));
        assertEquals("release 8", screens.calls.get(screens.calls.size() - 1));
        assertEquals(0, apps.videoDisplay());
        apps.close();
        assertEquals(4, screens.calls.size());
    }

    @Test
    void contentTurnedSidewaysSwapsTheDisplaySizeInsteadOfStreamingItSideways() throws Exception {
        final VideoController apps = withScreens(snapshot(0, 1080, 2400, 0, 420));
        factory.then(new Script().frame(1, true, 0x65)).then(new Script().frame(2, true, 0x65));
        apps.configure(independent(0, 0, 0));
        final Pipe pipe = new Pipe();
        apps.videoChannelOpened(new StreamEndpoint(pipe));
        assertEquals(1, pipe.nextPacket().header.session);
        pipe.nextPacket();

        apps.displayChanged(snapshot(7, 2400, 1080, 1, 420));
        assertEquals("resize 7 2400x1080 420", screens.calls.get(screens.calls.size() - 1));
        assertNull(pipe.packets.poll(300, TimeUnit.MILLISECONDS), "the sideways picture was streamed");

        final DisplaySnapshot upright = snapshot(7, 2400, 1080, 0, 420);
        screens.shown.put(7, upright);
        apps.displayChanged(upright);
        final Media.Packet turned = pipe.nextPacket();
        assertEquals(2, turned.header.session);
        assertEquals(2400, assertInstanceOf(VideoSessionInfo.class, turned.session).displayWidth);
        apps.close();
    }

    @Test
    void aDisplayThatKeepsTurningSidewaysIsStreamedAsItIs() {
        final VideoController apps = withScreens(snapshot(0, 1080, 2400, 0, 420));
        apps.configure(independent(0, 0, 0));
        apps.displayChanged(snapshot(7, 2400, 1080, 1, 420));
        apps.displayChanged(snapshot(7, 1080, 2400, 3, 420));
        apps.displayChanged(snapshot(7, 2400, 1080, 1, 420));
        assertEquals(2, screens.calls.stream().filter(call -> call.startsWith("resize")).count());
        apps.displayChanged(snapshot(7, 2400, 1080, 0, 420));
        apps.displayChanged(snapshot(7, 1080, 2400, 1, 420));
        assertEquals(3, screens.calls.stream().filter(call -> call.startsWith("resize")).count());
        apps.close();
    }

    @Test
    void exhaustedLadderClosesTheVideoChannel() throws Exception {
        final VideoController single = controller(List.of(FakeEncoding.AVC_HW));
        factory.fail("one").fail("two").fail("three");
        final Pipe control = new Pipe();
        single.controlConnected(new StreamEndpoint(control));
        single.configure(configure(VideoSource.DISPLAY));
        final Pipe pipe = new Pipe();
        single.videoChannelOpened(new StreamEndpoint(pipe));

        assertTrue(pipe.closed.await(FakeEncoding.WAIT_SECONDS, TimeUnit.SECONDS), "the channel stayed open");
        final List<Message> notices = control.messages();
        assertEquals(3, notices.size());
        assertEquals(NoticeLevel.ERROR, ((Notice) notices.get(2)).level);
        single.connectionLost();
    }
}
