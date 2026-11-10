package com.gamescrcpy.server.video;

import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.NoticeLevel;
import com.gamescrcpy.protocol.Media;
import com.gamescrcpy.protocol.Messages.VideoSessionInfo;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.MediaClock;
import com.gamescrcpy.server.device.DisplaySnapshot;
import com.gamescrcpy.server.device.EncoderCatalog.Encoder;
import java.io.IOException;
import java.util.List;
import java.util.concurrent.TimeUnit;

public final class VideoStreamer implements Runnable {

    static final long FIRST_FRAME_TIMEOUT_NANOS = TimeUnit.SECONDS.toNanos(2);
    static final int MAX_STREAM_RETRIES = 2;

    public interface DisplaySource {
        DisplaySnapshot snapshot(int displayId);
    }

    public interface Listener {
        void notice(NoticeLevel level, ErrorCode code, String message);

        void sessionStarted(int session, VideoSessionInfo info, String encoder);
    }

    private final EncodingSession.Factory factory;
    private final List<Encoder> encoders;
    private final EncoderLadder.SizeSupport sizeSupport;
    private final DisplaySource displays;
    private final VideoPackets packets;
    private final SessionCounter counter;
    private final Listener listener;
    private final long firstFrameTimeoutNanos;

    private final Object lock = new Object();
    private VideoSettings settings;
    private DisplaySnapshot sessionDisplay;
    private EncodingSession current;
    private boolean resetRequested;
    private boolean paused;
    private boolean stopped;

    private EncoderLadder ladder;
    private VideoSettings ladderSettings;
    private int streamRetries;
    private String lastFailure = "";

    VideoStreamer(EncodingSession.Factory factory, List<Encoder> encoders, EncoderLadder.SizeSupport sizeSupport,
                  DisplaySource displays, VideoPackets.Sink sink, SessionCounter counter, Listener listener,
                  VideoSettings settings, boolean paused, long firstFrameTimeoutNanos) {
        this.factory = factory;
        this.encoders = encoders;
        this.sizeSupport = sizeSupport;
        this.displays = displays;
        this.packets = new VideoPackets(sink);
        this.counter = counter;
        this.listener = listener;
        this.settings = settings;
        this.paused = paused;
        this.firstFrameTimeoutNanos = firstFrameTimeoutNanos;
    }

    public void updateSettings(VideoSettings newSettings) {
        synchronized (lock) {
            settings = newSettings;
            interruptLocked();
        }
    }

    public void displayChanged(DisplaySnapshot snapshot) {
        synchronized (lock) {
            if (snapshot.displayId() == settings.displayId() && !snapshot.sameGeometry(sessionDisplay)) {
                interruptLocked();
            }
        }
    }

    public void setBitrate(long bitrate) {
        synchronized (lock) {
            settings = settings.withBitrate(bitrate);
            if (current != null) {
                current.setBitrate(settings.bitrate());
            }
        }
    }

    public void requestKeyFrame() {
        synchronized (lock) {
            if (current != null) {
                current.requestKeyFrame();
            }
        }
    }

    public void setPaused(boolean newPaused) {
        synchronized (lock) {
            paused = newPaused;
            if (current != null) {
                current.setPaused(newPaused);
                if (!newPaused) {
                    current.requestKeyFrame();
                }
            }
        }
    }

    public void stop() {
        synchronized (lock) {
            stopped = true;
            interruptLocked();
        }
    }

    @Override
    public void run() {
        try {
            while (true) {
                final VideoSettings wanted;
                synchronized (lock) {
                    if (stopped) {
                        return;
                    }
                    resetRequested = false;
                    wanted = settings;
                }
                if (!wanted.sameLadder(ladderSettings)) {
                    ladder = new EncoderLadder(wanted.codec(), encoders, wanted.maxSize(), sizeSupport);
                    ladderSettings = wanted;
                    streamRetries = 0;
                }
                final DisplaySnapshot display = displays.snapshot(wanted.displayId());
                if (display == null) {
                    listener.notice(NoticeLevel.ERROR, ErrorCode.NOT_FOUND,
                            "display " + wanted.displayId() + " does not exist");
                    return;
                }
                synchronized (lock) {
                    sessionDisplay = display;
                }
                final EncoderLadder.Attempt attempt = ladder.current(display.width(), display.height());
                if (attempt == null) {
                    listener.notice(NoticeLevel.ERROR, ErrorCode.ENCODER_FAILURE,
                            "no video encoder works on this device" + (lastFailure.isEmpty() ? ""
                                    : "; last failure: " + lastFailure));
                    return;
                }
                stream(attempt, display, wanted);
            }
        } catch (IOException e) {
        }
    }

    private void stream(EncoderLadder.Attempt attempt, DisplaySnapshot display, VideoSettings wanted)
            throws IOException {
        final EncodingSession session;
        try {
            session = factory.open(attempt, display, wanted);
        } catch (EncoderException e) {
            fallBack(attempt, display, e.describe());
            return;
        }
        final boolean startPaused;
        try {
            synchronized (lock) {
                if (stopped) {
                    return;
                }
                current = session;
                if (resetRequested) {
                    session.interrupt();
                }
                startPaused = paused;
                if (startPaused) {
                    session.setPaused(true);
                }
            }
            final int number = counter.next();
            final VideoSessionInfo info = describe(attempt, display);
            packets.session(number, info);
            listener.sessionStarted(number, info, attempt.encoder());
            final long deadline = display.isOn() && !startPaused ? System.nanoTime() + firstFrameTimeoutNanos
                    : EncodingSession.NO_DEADLINE;
            if (Log.enabled(Log.Level.DEBUG)) {
                Log.d("session " + number + ": " + attempt + (deadline == EncodingSession.NO_DEADLINE
                        ? ", no first-frame deadline (display state " + display.state() + ", paused " + startPaused + ")"
                        : ", first frame due within " + TimeUnit.NANOSECONDS.toMillis(firstFrameTimeoutNanos) + " ms"));
            }
            drain(session, attempt, display, number, deadline);
        } finally {
            synchronized (lock) {
                current = null;
            }
            session.close();
        }
    }

    private void drain(EncodingSession session, EncoderLadder.Attempt attempt, DisplaySnapshot display, int number,
                       long firstFrameDeadline) throws IOException {
        boolean frameSeen = false;
        while (true) {
            final EncodingSession.Output output;
            try {
                output = session.next(frameSeen ? EncodingSession.NO_DEADLINE : firstFrameDeadline);
            } catch (EncoderException e) {
                if (frameSeen && streamRetries < MAX_STREAM_RETRIES) {
                    ++streamRetries;
                    listener.notice(NoticeLevel.WARNING, ErrorCode.ENCODER_FAILURE,
                            attempt + " failed while streaming (" + e.describe() + "); restarting it");
                } else {
                    fallBack(attempt, display, e.describe());
                }
                return;
            }
            if (output == null) {
                fallBack(attempt, display, "no frame within "
                        + TimeUnit.NANOSECONDS.toMillis(firstFrameTimeoutNanos) + " ms");
                return;
            }
            try {
                switch (output.kind) {
                    case END -> {
                        return;
                    }
                    case CONFIG -> packets.config(number, output.data);
                    case FRAME -> {
                        if (!frameSeen) {
                            frameSeen = true;
                            streamRetries = 0;
                        }
                        final int flags = (output.keyFrame ? Media.FLAG_KEY_FRAME : 0)
                                | (NalUnits.discardable(attempt.codec(), output.data) ? Media.FLAG_DISCARDABLE : 0);
                        packets.frame(number, flags, output.ptsUs,
                                MediaClock.encodeDelayUs(output.ptsUs, output.readyUs), output.data);
                    }
                }
            } finally {
                session.release(output);
            }
        }
    }

    private void fallBack(EncoderLadder.Attempt failed, DisplaySnapshot display, String reason) {
        lastFailure = failed + ": " + reason;
        streamRetries = 0;
        ladder.failed();
        final EncoderLadder.Attempt next = ladder.current(display.width(), display.height());
        if (next != null) {
            listener.notice(NoticeLevel.WARNING, ErrorCode.ENCODER_FAILURE,
                    failed + " failed (" + reason + "); trying " + next);
        }
    }

    private void interruptLocked() {
        resetRequested = true;
        if (current != null) {
            current.interrupt();
        }
    }

    private static VideoSessionInfo describe(EncoderLadder.Attempt attempt, DisplaySnapshot display) {
        final VideoSessionInfo info = new VideoSessionInfo();
        info.codec = attempt.codec();
        info.frameWidth = attempt.size().width();
        info.frameHeight = attempt.size().height();
        info.displayId = display.displayId() & 0xFFFFFFFFL;
        info.displayWidth = display.width();
        info.displayHeight = display.height();
        info.rotation = display.rotation() & 3;
        return info;
    }
}
