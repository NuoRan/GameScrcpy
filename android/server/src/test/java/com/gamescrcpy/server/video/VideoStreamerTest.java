package com.gamescrcpy.server.video;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.gamescrcpy.protocol.Enums.MediaKind;
import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.protocol.Media;
import com.gamescrcpy.protocol.Messages.VideoSessionInfo;
import com.gamescrcpy.server.device.EncoderCatalog.Encoder;
import com.gamescrcpy.server.video.FakeEncoding.Script;
import java.util.List;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class VideoStreamerTest {

    private final FakeEncoding.Factory factory = new FakeEncoding.Factory();
    private final FakeEncoding.Channel channel = new FakeEncoding.Channel();
    private final FakeEncoding.Listener listener = new FakeEncoding.Listener();
    private final FakeEncoding.Displays displays = new FakeEncoding.Displays();
    private VideoStreamer streamer;
    private Thread thread;

    private static VideoSettings settings(VideoCodec codec) {
        return new VideoSettings(0, codec, 0, 0, 8_000_000, 0);
    }

    private void start(VideoSettings settings, List<Encoder> encoders, boolean paused) {
        streamer = new VideoStreamer(factory, encoders, (e, w, h) -> true, displays, channel, new SessionCounter(),
                listener, settings, paused, TimeUnit.SECONDS.toNanos(1));
        thread = new Thread(streamer, "test-video");
        thread.start();
    }

    private void start(VideoCodec codec) {
        start(settings(codec), FakeEncoding.ENCODERS, false);
    }

    private VideoSessionInfo expectSession(int number) throws InterruptedException {
        final Media.Packet packet = channel.next();
        assertEquals(MediaKind.SESSION, packet.header.kind);
        assertEquals(number, packet.header.session);
        return (VideoSessionInfo) packet.session;
    }

    private Media.Packet expect(MediaKind kind) throws InterruptedException {
        final Media.Packet packet = channel.next();
        assertEquals(kind, packet.header.kind);
        return packet;
    }

    private void awaitEnd() throws InterruptedException {
        thread.join(TimeUnit.SECONDS.toMillis(FakeEncoding.WAIT_SECONDS));
        assertFalse(thread.isAlive(), "the streamer did not stop");
    }

    @AfterEach
    void stop() throws InterruptedException {
        if (streamer != null) {
            streamer.stop();
            awaitEnd();
        }
    }

    @Test
    void streamsSessionThenConfigThenFrames() throws Exception {
        factory.then(new Script().config().frame(1_000, true, 0x65).frame(2_000, false, 0x01));
        start(VideoCodec.H264);

        final VideoSessionInfo info = expectSession(1);
        assertEquals(VideoCodec.H264, info.codec);
        assertEquals(1080, info.frameWidth);
        assertEquals(2400, info.frameHeight);
        assertEquals(2400, info.displayHeight);
        expect(MediaKind.CONFIG);
        final Media.Packet key = expect(MediaKind.FRAME);
        assertTrue(key.header.keyFrame);
        assertFalse(key.header.discardable);
        assertEquals(1_000, key.header.ptsUs);
        assertEquals(FakeEncoding.ENCODE_DELAY_US, key.header.encodeDelayUs);
        final Media.Packet nonReference = expect(MediaKind.FRAME);
        assertFalse(nonReference.header.keyFrame);
        assertTrue(nonReference.header.discardable);
        assertEquals(1, listener.sessions.poll(FakeEncoding.WAIT_SECONDS, TimeUnit.SECONDS));
        assertEquals("avc.hw", factory.attempts.get(0).encoder());
    }

    @Test
    void configurationFailuresWalkTheLadderAndAreReported() throws Exception {
        factory.fail("too big").fail("still too big").then(new Script().frame(1, true, 0x65));
        start(VideoCodec.H264);

        expectSession(1);
        assertEquals("warning encoderFailure: h264 encoder avc.hw at 1080x2400 failed (cannot start the encoder: "
                + "IllegalStateException too big); trying h264 encoder avc.hw at 864x1920", listener.nextNotice());
        assertTrue(listener.nextNotice().endsWith("trying h264 encoder avc.hw at 720x1600"));
        assertEquals(3, factory.attempts.size());
    }

    @Test
    void encoderWithoutFirstFrameTimesOutAndFallsBack() throws Exception {
        final Script silent = new Script();
        silent.silent = true;
        factory.then(silent).then(new Script().frame(1, true, 0x65));
        start(VideoCodec.H264);

        expectSession(1);
        assertTrue(listener.nextNotice().contains("failed (no frame within 1000 ms); trying h264 encoder avc.hw at 864x1920"));
        expectSession(2);
        assertTrue(factory.awaitOpened().closed);
        expect(MediaKind.FRAME);
    }

    @Test
    void exhaustedLadderReportsTheLastFailureAndEnds() throws Exception {
        factory.fail("first").fail("second").fail("third");
        start(settings(VideoCodec.H264), List.of(FakeEncoding.AVC_HW), false);

        listener.nextNotice();
        listener.nextNotice();
        assertEquals("error encoderFailure: no video encoder works on this device; last failure: h264 encoder avc.hw at "
                + "720x1600: cannot start the encoder: IllegalStateException third", listener.nextNotice());
        awaitEnd();
    }

    @Test
    void rotationStartsANewSessionWithTheSameEncoder() throws Exception {
        factory.then(new Script().frame(1, true, 0x65)).then(new Script().frame(2, true, 0x65));
        start(VideoCodec.H264);
        expectSession(1);
        expect(MediaKind.FRAME);
        final FakeEncoding.Session first = factory.awaitOpened();

        streamer.displayChanged(displays.current.get());
        displays.current.set(FakeEncoding.display(2400, 1080, 1, true));
        streamer.displayChanged(displays.current.get());

        final VideoSessionInfo rotated = expectSession(2);
        assertEquals(2400, rotated.frameWidth);
        assertEquals(1080, rotated.frameHeight);
        assertEquals(1, rotated.rotation);
        assertTrue(first.closed);
        assertEquals("avc.hw", factory.attempts.get(1).encoder());
    }

    @Test
    void newCodecRebuildsTheLadder() throws Exception {
        factory.then(new Script().frame(1, true, 0x65)).then(new Script().frame(2, true, 0x26));
        start(VideoCodec.H264);
        expectSession(1);
        expect(MediaKind.FRAME);

        streamer.updateSettings(settings(VideoCodec.H265));
        assertEquals(VideoCodec.H265, expectSession(2).codec);
        assertEquals("hevc.hw", factory.attempts.get(1).encoder());
    }

    @Test
    void failureWhileStreamingRestartsTheSameEncoder() throws Exception {
        final Script dying = new Script().frame(1, true, 0x65);
        dying.failAfterOutputs = true;
        factory.then(dying).then(new Script().frame(2, true, 0x65));
        start(VideoCodec.H264);

        expectSession(1);
        expect(MediaKind.FRAME);
        assertTrue(listener.nextNotice().endsWith("failed while streaming (scripted failure: IllegalStateException "
                + "codec died); restarting it"));
        expectSession(2);
        assertEquals(factory.attempts.get(0), factory.attempts.get(1));
    }

    @Test
    void brokenChannelEndsTheStreamer() throws Exception {
        channel.broken = true;
        start(VideoCodec.H264);
        awaitEnd();
        assertTrue(factory.awaitOpened().closed);
    }

    @Test
    void noFirstFrameDeadlineWhileTheDisplayIsOff() throws Exception {
        displays.current.set(FakeEncoding.display(1080, 2400, 0, false));
        final Script silent = new Script();
        silent.silent = true;
        factory.then(silent);
        start(VideoCodec.H264);

        expectSession(1);
        assertEquals(EncodingSession.NO_DEADLINE, factory.awaitOpened().awaitDeadline());
    }

    @Test
    void controlsReachTheRunningSession() throws Exception {
        factory.then(new Script());
        start(settings(VideoCodec.H264), FakeEncoding.ENCODERS, true);
        expectSession(1);
        final FakeEncoding.Session session = factory.awaitOpened();
        assertEquals(EncodingSession.NO_DEADLINE, session.awaitDeadline());

        streamer.setBitrate(4_000_000);
        streamer.requestKeyFrame();
        streamer.setPaused(false);
        assertEquals(List.of("paused true", "bitrate 4000000", "keyFrame", "paused false", "keyFrame"),
                List.copyOf(session.calls));
    }
}
