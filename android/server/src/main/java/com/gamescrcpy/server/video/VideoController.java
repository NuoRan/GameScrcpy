package com.gamescrcpy.server.video;

import android.os.Looper;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.NoticeLevel;
import com.gamescrcpy.protocol.Enums.VideoSource;
import com.gamescrcpy.protocol.Messages.ConfigureVideo;
import com.gamescrcpy.protocol.Messages.Notice;
import com.gamescrcpy.protocol.Messages.ReceiverReport;
import com.gamescrcpy.protocol.Messages.VideoSessionInfo;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.MediaClock;
import com.gamescrcpy.server.device.DisplaySnapshot;
import com.gamescrcpy.server.device.EncoderCatalog.Encoder;
import com.gamescrcpy.server.transport.FrameEndpoint;
import com.gamescrcpy.server.transport.MediaEndpoint;
import com.gamescrcpy.server.wrappers.PlatformException;
import java.io.IOException;
import java.util.List;
import java.util.function.LongSupplier;
import java.util.function.Supplier;

public final class VideoController {

    public static final int NO_DISPLAY = -1;

    private static final long STOP_WAIT_MILLIS = 2_000;
    private static final int SEND_BUFFER_BYTES = 64 * 1024;
    private static final long DEFAULT_FRAME_US = 16_667;
    private static final long WINDOW_DIVISOR = 32;
    private static final long MIN_WINDOW_BYTES = 16 * 1024;
    private static final int MAX_UPRIGHT_TURNS = 2;

    private final EncodingSession.Factory factory;
    private final Supplier<List<Encoder>> encoders;
    private final EncoderLadder.SizeSupport sizeSupport;
    private final VideoStreamer.DisplaySource displays;
    private final IndependentDisplays independent;
    private final Runnable threadSetup;
    private final long firstFrameTimeoutNanos;
    private final LongSupplier clock;
    private final SessionCounter counter = new SessionCounter();
    private final Object lifecycle = new Object();
    private final LinkFallback fallback = new LinkFallback();

    public record Current(int number, VideoSessionInfo info) {
    }

    private record Independent(int displayId, int requestedWidth, int requestedHeight, int requestedDensity,
                               int densityDpi) {
        boolean matches(ConfigureVideo message) {
            return requestedWidth == message.virtualWidth && requestedHeight == message.virtualHeight
                    && requestedDensity == message.virtualDensity;
        }
    }

    private Independent shown;
    private int uprightTurns;
    private VideoSettings settings;
    private RateController rate;
    private boolean paused;
    private MediaEndpoint channel;
    private VideoSendQueue queue;
    private VideoStreamer streamer;
    private Thread thread;
    private volatile FrameEndpoint control;
    private volatile Current current;
    private int encodedLongSide;
    private long receivedBytes;

    public VideoController(EncodingSession.Factory factory, Supplier<List<Encoder>> encoders,
                           EncoderLadder.SizeSupport sizeSupport, VideoStreamer.DisplaySource displays,
                           IndependentDisplays independent) {
        this(factory, encoders, sizeSupport, displays, independent, VideoController::prepareLooper,
                VideoStreamer.FIRST_FRAME_TIMEOUT_NANOS, MediaClock::nowUs);
    }

    VideoController(EncodingSession.Factory factory, Supplier<List<Encoder>> encoders,
                    EncoderLadder.SizeSupport sizeSupport, VideoStreamer.DisplaySource displays, Runnable threadSetup,
                    long firstFrameTimeoutNanos, LongSupplier clock) {
        this(factory, encoders, sizeSupport, displays, null, threadSetup, firstFrameTimeoutNanos, clock);
    }

    VideoController(EncodingSession.Factory factory, Supplier<List<Encoder>> encoders,
                    EncoderLadder.SizeSupport sizeSupport, VideoStreamer.DisplaySource displays,
                    IndependentDisplays independent, Runnable threadSetup, long firstFrameTimeoutNanos,
                    LongSupplier clock) {
        this.factory = factory;
        this.encoders = encoders;
        this.sizeSupport = sizeSupport;
        this.displays = displays;
        this.independent = independent;
        this.threadSetup = threadSetup;
        this.firstFrameTimeoutNanos = firstFrameTimeoutNanos;
        this.clock = clock;
    }

    public Current current() {
        return current;
    }

    public int sessionNumber() {
        final Current running = current;
        return running == null ? 0 : running.number();
    }

    public void controlConnected(FrameEndpoint newControl) {
        control = newControl;
        final boolean reduced;
        synchronized (this) {
            reduced = fallback.level() > 0;
        }
        if (reduced) {
            notice(NoticeLevel.WARNING, ErrorCode.VIDEO_REDUCED, "video still reduced for the link");
        }
    }

    public void videoChannelOpened(MediaEndpoint video) {
        try {
            video.setSendBufferSize(SEND_BUFFER_BYTES);
        } catch (IOException e) {
            Log.w("cannot shrink the video socket's send buffer", e);
        }
        synchronized (lifecycle) {
            final Thread previous;
            synchronized (this) {
                previous = detach();
                channel = video;
                receivedBytes = 0;
                if (rate != null) {
                    rate.restart();
                }
            }
            awaitEnd(previous);
            synchronized (this) {
                startIfReady();
            }
        }
    }

    public void configure(ConfigureVideo message) {
        final boolean independentWanted = message.source != VideoSource.DISPLAY;
        if (independentWanted && independent == null) {
            notice(NoticeLevel.ERROR, ErrorCode.CHANNEL_UNAVAILABLE, "this phone cannot run an independent display");
            return;
        }
        boolean restored;
        Independent released = null;
        synchronized (lifecycle) {
            final int displayId;
            if (independentWanted) {
                final Independent previous;
                synchronized (this) {
                    previous = shown;
                }
                if (previous != null && previous.matches(message)) {
                    displayId = previous.displayId();
                } else {
                    final Independent created;
                    try {
                        created = createIndependent(message);
                    } catch (PlatformException e) {
                        notice(NoticeLevel.ERROR, ErrorCode.CHANNEL_UNAVAILABLE,
                                "cannot create the independent display: " + e.getMessage());
                        return;
                    }
                    synchronized (this) {
                        released = shown;
                        shown = created;
                        uprightTurns = 0;
                    }
                    displayId = created.displayId();
                }
            } else {
                synchronized (this) {
                    released = shown;
                    shown = null;
                }
                displayId = (int) message.displayId;
            }
            synchronized (this) {
                final VideoSettings requested = VideoSettings.from(message).withDisplayId(displayId);
                if (streamer != null && requested.equals(settings)) {
                    return;
                }
                settings = requested;
                restored = fallback.level() > 0;
                fallback.reset();
                if (rate == null) {
                    rate = new RateController(settings.bitrate());
                } else {
                    rate.setCeiling(settings.bitrate());
                }
                if (streamer != null) {
                    streamer.updateSettings(streamed());
                } else {
                    startIfReady();
                }
            }
        }
        if (released != null) {
            independent.release(released.displayId());
        }
        if (restored) {
            notice(NoticeLevel.INFO, ErrorCode.VIDEO_RESTORED, "video back to the new setting");
        }
    }

    public synchronized int videoDisplay() {
        return settings != null ? settings.displayId() : NO_DISPLAY;
    }

    public synchronized boolean showsIndependent(int displayId) {
        return shown != null && shown.displayId() == displayId;
    }

    public boolean supportsIndependentDisplays() {
        return independent != null;
    }

    public void close() {
        connectionLost();
        final Independent released;
        synchronized (lifecycle) {
            synchronized (this) {
                released = shown;
                shown = null;
            }
        }
        if (released != null) {
            independent.release(released.displayId());
        }
    }

    private Independent createIndependent(ConfigureVideo message) throws PlatformException {
        int width = message.virtualWidth;
        int height = message.virtualHeight;
        int densityDpi = message.virtualDensity;
        if (width == 0 || height == 0 || densityDpi == 0) {
            final DisplaySnapshot main = displays.snapshot(0);
            if (main == null) {
                throw new PlatformException("the size of the main display is unknown");
            }
            if (width == 0 || height == 0) {
                final boolean turned = (main.rotation() & 1) != 0;
                width = turned ? main.height() : main.width();
                height = turned ? main.width() : main.height();
            }
            if (densityDpi == 0) {
                densityDpi = main.densityDpi();
            }
        }
        final int displayId = independent.create(width, height, densityDpi);
        return new Independent(displayId, message.virtualWidth, message.virtualHeight, message.virtualDensity,
                densityDpi);
    }

    public synchronized void setBitrate(long bitrate) {
        if (settings == null) {
            return;
        }
        settings = settings.withBitrate(bitrate);
        rate.setCeiling(settings.bitrate());
        if (streamer != null) {
            streamer.setBitrate(rate.rate());
        }
    }

    public synchronized void setMaxFps(int maxFps) {
        if (settings == null) {
            return;
        }
        settings = settings.withMaxFps(maxFps);
        if (streamer != null) {
            streamer.updateSettings(streamed());
        }
    }

    public void receiverReport(ReceiverReport report) {
        final Runnable announcement;
        final MediaEndpoint media;
        synchronized (this) {
            if (streamer == null || queue == null) {
                return;
            }
            media = channel;
            final long nowUs = clock.getAsLong();
            final long before = rate.rate();
            final long after = rate.report(report, nowUs, queue.waitedUs(), queue.takeAcceptedBytes());
            receivedBytes += report.bytes;
            final boolean congested = rate.lastLoaded() && rate.lastQueueingUs() > RateController.OVERUSE_US;
            queue.setLinkWindow(receivedBytes, congested
                    ? Math.max(MIN_WINDOW_BYTES, rate.receivedAverageBps() / WINDOW_DIVISOR) : Long.MAX_VALUE);
            final int base = baseLongSide();
            final LinkFallback.Change change = fallback.report(
                    LinkFallback.Observation.of(nowUs, rate, settings.bitrate()), LinkFallback.levels(base, settings.maxFps()));
            if (change != LinkFallback.Change.NONE) {
                streamer.updateSettings(streamed());
                announcement = announcement(change, base);
            } else {
                announcement = null;
                if (after != before) {
                    streamer.setBitrate(after);
                }
            }
            if (after < before) {
                Log.i("video bit rate down to " + after / 1000 + " kbit/s for the link (queueing "
                        + rate.lastQueueingUs() / 1000 + " ms, " + rate.lastReceivedBps() / 1000 + " kbit/s arriving)");
            } else if (after > before && after == settings.bitrate()) {
                Log.i("video bit rate back up to the setting, " + after / 1000 + " kbit/s");
            } else if (after > before) {
                Log.d("video bit rate up to " + after / 1000 + " kbit/s");
            }
        }
        if (media != null) {
            media.feedback(report.lostPackets, report.recoveredPackets);
        }
        if (announcement != null) {
            announcement.run();
        }
    }

    public synchronized void requestKeyFrame() {
        if (streamer != null) {
            streamer.requestKeyFrame();
        }
    }

    public synchronized void setPaused(boolean newPaused) {
        paused = newPaused;
        if (streamer != null) {
            streamer.setPaused(newPaused);
        }
    }

    public synchronized void displayChanged(DisplaySnapshot snapshot) {
        if (turnedUpright(snapshot)) {
            return;
        }
        if (streamer != null) {
            streamer.displayChanged(snapshot);
        }
    }

    private boolean turnedUpright(DisplaySnapshot snapshot) {
        if (shown == null || snapshot.displayId() != shown.displayId()) {
            return false;
        }
        if ((snapshot.rotation() & 1) == 0) {
            uprightTurns = 0;
            return false;
        }
        if (uprightTurns >= MAX_UPRIGHT_TURNS) {
            return false;
        }
        ++uprightTurns;
        independent.resize(shown.displayId(), snapshot.width(), snapshot.height(), shown.densityDpi());
        return true;
    }

    public void connectionLost() {
        synchronized (lifecycle) {
            final Thread previous;
            synchronized (this) {
                previous = detach();
                channel = null;
                control = null;
            }
            awaitEnd(previous);
        }
    }

    private void startIfReady() {
        if (settings == null || channel == null || streamer != null) {
            return;
        }
        final long frameUs = settings.maxFps() > 0 ? 1_000_000L / settings.maxFps() : DEFAULT_FRAME_US;
        queue = new VideoSendQueue(channel::writePacket, this::requestKeyFrame, frameUs, clock);
        final VideoStreamer started = new VideoStreamer(factory, encoders.get(), sizeSupport, displays, queue, counter,
                new StreamerListener(), streamed(), paused, firstFrameTimeoutNanos);
        streamer = started;
        thread = new Thread(() -> {
            threadSetup.run();
            started.run();
            streamerEnded(started);
        }, "gs-video");
        thread.start();
    }

    private Thread detach() {
        if (streamer == null) {
            return null;
        }
        streamer.stop();
        queue.close();
        final Thread stopping = thread;
        streamer = null;
        queue = null;
        thread = null;
        current = null;
        return stopping;
    }

    private VideoSettings streamed() {
        final VideoSettings atRate = settings.withBitrate(rate.rate());
        if (fallback.level() == 0) {
            return atRate;
        }
        final int base = baseLongSide();
        return atRate.withMaxSize(fallback.maxSize(base, settings.maxSize()))
                .withMaxFps(fallback.maxFps(base, settings.maxFps()));
    }

    private int baseLongSide() {
        return encodedLongSide > 0 ? encodedLongSide : settings.maxSize();
    }

    private Runnable announcement(LinkFallback.Change change, int base) {
        final int level = fallback.level();
        final int fps = fallback.maxFps(base, settings.maxFps());
        final String limits = "a long side of " + fallback.maxSize(base, settings.maxSize()) + " px"
                + (fps != settings.maxFps() ? " at " + fps + " fps" : "");
        if (change == LinkFallback.Change.DOWN) {
            final String message = "video reduced to " + limits + " for the link (the encoder gave "
                    + fallback.floorAt(level - 1) / 1000 + " kbit/s at " + rate.rate() / 1000 + ")";
            return () -> notice(NoticeLevel.WARNING, ErrorCode.VIDEO_REDUCED, message);
        }
        if (level == 0) {
            return () -> notice(NoticeLevel.INFO, ErrorCode.VIDEO_RESTORED, "video back to the setting as the link widened");
        }
        Log.i("video raised to " + limits + " as the link widened");
        return null;
    }

    private static void awaitEnd(Thread stopping) {
        if (stopping == null) {
            return;
        }
        try {
            stopping.join(STOP_WAIT_MILLIS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        if (stopping.isAlive()) {
            Log.w("the video thread did not release the encoder within " + STOP_WAIT_MILLIS + " ms");
        }
    }

    private synchronized void streamerEnded(VideoStreamer ended) {
        if (streamer != ended) {
            return;
        }
        streamer = null;
        queue.close();
        queue = null;
        thread = null;
        current = null;
        if (channel != null) {
            channel.close();
            channel = null;
        }
    }

    private void notice(NoticeLevel level, ErrorCode code, String message) {
        if (level == NoticeLevel.INFO) {
            Log.i(message);
        } else {
            Log.w(message);
        }
        final FrameEndpoint target = control;
        if (target == null) {
            return;
        }
        final Notice notice = new Notice();
        notice.level = level;
        notice.code = code;
        notice.message = message.length() > 1000 ? message.substring(0, 1000) : message;
        try {
            target.write(notice);
        } catch (IOException e) {
        }
    }

    private static void prepareLooper() {
        if (Looper.myLooper() == null) {
            Looper.prepare();
        }
    }

    private final class StreamerListener implements VideoStreamer.Listener {
        @Override
        public void notice(NoticeLevel level, ErrorCode code, String message) {
            VideoController.this.notice(level, code, message);
        }

        @Override
        public void sessionStarted(int session, VideoSessionInfo info, String encoder) {
            current = new Current(session, info);
            synchronized (VideoController.this) {
                if (fallback.level() == 0) {
                    encodedLongSide = Math.max(info.frameWidth, info.frameHeight);
                }
            }
            Log.i("video session " + session + ": " + info.codec.specName() + " " + info.frameWidth + "x"
                    + info.frameHeight + " of display " + info.displayId + " (" + info.displayWidth + "x"
                    + info.displayHeight + ", rotation " + info.rotation + ") with " + encoder);
        }
    }
}
