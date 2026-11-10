package com.gamescrcpy.server.video;

import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.NoticeLevel;
import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.protocol.Media;
import com.gamescrcpy.protocol.MediaChannel;
import com.gamescrcpy.protocol.Messages.VideoSessionInfo;
import com.gamescrcpy.server.device.DisplaySnapshot;
import com.gamescrcpy.server.device.EncoderCatalog.Encoder;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Deque;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

final class FakeEncoding {

    static final Encoder AVC_HW = new Encoder(VideoCodec.H264, "avc.hw", true);
    static final Encoder AVC_SW = new Encoder(VideoCodec.H264, "avc.sw", false);
    static final Encoder HEVC_HW = new Encoder(VideoCodec.H265, "hevc.hw", true);
    static final List<Encoder> ENCODERS = List.of(AVC_HW, HEVC_HW, AVC_SW);
    static final long WAIT_SECONDS = 5;
    static final long ENCODE_DELAY_US = 8_250;

    static DisplaySnapshot display(int width, int height, int rotation, boolean on) {
        return new DisplaySnapshot(0, width, height, rotation, 0, 0, 420, 60_000, on ? DisplaySnapshot.STATE_ON : 1,
                0, 0, 0, 0);
    }

    static ByteBuffer frameData(int nalHeader) {
        final ByteBuffer data = ByteBuffer.allocateDirect(5);
        data.put(new byte[] {0, 0, 0, 1, (byte) nalHeader});
        data.flip();
        return data;
    }

    static final class Script {
        final List<EncodingSession.Output> outputs = new ArrayList<>();
        boolean silent;
        boolean failAfterOutputs;

        Script config() {
            return add(EncodingSession.Kind.CONFIG, frameData(0x67), 0, false);
        }

        Script frame(long ptsUs, boolean keyFrame, int nalHeader) {
            return add(EncodingSession.Kind.FRAME, frameData(nalHeader), ptsUs, keyFrame);
        }

        Script frame(long ptsUs, boolean keyFrame, int nalHeader, int size) {
            final ByteBuffer data = ByteBuffer.allocateDirect(size);
            data.put(new byte[] {0, 0, 0, 1, (byte) nalHeader});
            data.position(size);
            data.flip();
            return add(EncodingSession.Kind.FRAME, data, ptsUs, keyFrame);
        }

        private Script add(EncodingSession.Kind kind, ByteBuffer data, long ptsUs, boolean keyFrame) {
            final EncodingSession.Output output = new EncodingSession.Output();
            output.kind = kind;
            output.data = data;
            output.ptsUs = ptsUs;
            output.readyUs = ptsUs + ENCODE_DELAY_US;
            output.keyFrame = keyFrame;
            outputs.add(output);
            return this;
        }
    }

    static final class Session implements EncodingSession {
        final EncoderLadder.Attempt attempt;
        final VideoSettings settings;
        final BlockingQueue<Output> queue = new LinkedBlockingQueue<>();
        final Script script;
        final int id;
        final List<String> events;
        final List<String> calls = Collections.synchronizedList(new ArrayList<>());
        final BlockingQueue<Long> deadlines = new LinkedBlockingQueue<>();
        volatile boolean closed;

        long awaitDeadline() throws InterruptedException {
            final Long deadline = deadlines.poll(WAIT_SECONDS, TimeUnit.SECONDS);
            if (deadline == null) {
                throw new AssertionError("next() was not called");
            }
            return deadline;
        }

        Session(EncoderLadder.Attempt attempt, VideoSettings settings, Script script, int id, List<String> events) {
            this.attempt = attempt;
            this.settings = settings;
            this.script = script;
            this.id = id;
            this.events = events;
            queue.addAll(script.outputs);
        }

        @Override
        public Output next(long deadlineNanos) throws EncoderException {
            deadlines.add(deadlineNanos);
            Output output = queue.poll();
            if (output == null && script.failAfterOutputs) {
                throw new EncoderException("scripted failure", new IllegalStateException("codec died"));
            }
            if (output == null && script.silent && deadlineNanos != NO_DEADLINE) {
                return null;
            }
            try {
                return output != null ? output : queue.take();
            } catch (InterruptedException e) {
                throw new AssertionError(e);
            }
        }

        @Override
        public void release(Output output) {
            calls.add("release");
        }

        @Override
        public void interrupt() {
            final Output end = new Output();
            end.kind = Kind.END;
            queue.add(end);
        }

        @Override
        public void setBitrate(long bitrate) {
            calls.add("bitrate " + bitrate);
        }

        @Override
        public void requestKeyFrame() {
            calls.add("keyFrame");
        }

        @Override
        public void setPaused(boolean paused) {
            calls.add("paused " + paused);
        }

        @Override
        public void close() {
            closed = true;
            events.add("close " + id);
        }
    }

    static final class Factory implements EncodingSession.Factory {
        final Deque<Object> results = new ArrayDeque<>();
        final BlockingQueue<Session> opened = new LinkedBlockingQueue<>();
        final List<EncoderLadder.Attempt> attempts = Collections.synchronizedList(new ArrayList<>());
        final List<String> events = Collections.synchronizedList(new ArrayList<>());

        Factory fail(String message) {
            results.add(message);
            return this;
        }

        Factory then(Script script) {
            results.add(script);
            return this;
        }

        @Override
        public synchronized EncodingSession open(EncoderLadder.Attempt attempt, DisplaySnapshot display,
                                                 VideoSettings settings) throws EncoderException {
            attempts.add(attempt);
            final Object result = results.isEmpty() ? new Script() : results.poll();
            if (result instanceof String message) {
                throw new EncoderException("cannot start the encoder", new IllegalStateException(message));
            }
            final int id = attempts.size();
            final Session session = new Session(attempt, settings, (Script) result, id, events);
            events.add("open " + id);
            opened.add(session);
            return session;
        }

        Session awaitOpened() throws InterruptedException {
            final Session session = opened.poll(WAIT_SECONDS, TimeUnit.SECONDS);
            if (session == null) {
                throw new AssertionError("no session was opened");
            }
            return session;
        }
    }

    static final class Channel implements VideoPackets.Sink {
        final BlockingQueue<byte[]> packets = new LinkedBlockingQueue<>();
        volatile boolean broken;

        @Override
        public void write(byte[] header, ByteBuffer payload) throws IOException {
            if (broken) {
                throw new IOException("channel closed");
            }
            final ByteArrayOutputStream packet = new ByteArrayOutputStream();
            packet.write(header, 0, header.length);
            while (payload.hasRemaining()) {
                packet.write(payload.get());
            }
            packets.add(packet.toByteArray());
        }

        Media.Packet next() throws InterruptedException {
            final byte[] packet = packets.poll(WAIT_SECONDS, TimeUnit.SECONDS);
            if (packet == null) {
                throw new AssertionError("no packet arrived");
            }
            return Media.decodePacket(MediaChannel.VIDEO, packet, 0, packet.length);
        }
    }

    static final class Listener implements VideoStreamer.Listener {
        final BlockingQueue<String> notices = new LinkedBlockingQueue<>();
        final BlockingQueue<Integer> sessions = new LinkedBlockingQueue<>();

        @Override
        public void notice(NoticeLevel level, ErrorCode code, String message) {
            notices.add(level.specName() + " " + code.specName() + ": " + message);
        }

        @Override
        public void sessionStarted(int session, VideoSessionInfo info, String encoder) {
            sessions.add(session);
        }

        String nextNotice() throws InterruptedException {
            final String notice = notices.poll(WAIT_SECONDS, TimeUnit.SECONDS);
            if (notice == null) {
                throw new AssertionError("no notice arrived");
            }
            return notice;
        }
    }

    static final class Displays implements VideoStreamer.DisplaySource {
        final AtomicReference<DisplaySnapshot> current = new AtomicReference<>(display(1080, 2400, 0, true));

        @Override
        public DisplaySnapshot snapshot(int displayId) {
            return displayId == 0 ? current.get() : null;
        }
    }

    private FakeEncoding() {
    }
}
