package com.gamescrcpy.server.video;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.os.Build;
import android.os.Bundle;
import android.view.Surface;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.MediaClock;
import com.gamescrcpy.server.device.DisplaySnapshot;
import com.gamescrcpy.server.device.EncoderCatalog;
import com.gamescrcpy.server.wrappers.DisplayMirror;
import com.gamescrcpy.server.wrappers.PlatformException;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Locale;

public final class MediaCodecSessionFactory implements EncodingSession.Factory {

    private static final int KEY_FRAME_INTERVAL_SECONDS = 10;
    private static final long REPEAT_FRAME_AFTER_US = 100_000;
    private static final String KEY_MAX_FPS_TO_ENCODER = "max-fps-to-encoder";
    private static final String KEY_QTI_LOW_LATENCY = "vendor.qti-ext-enc-low-latency.enable";
    private static final int NOMINAL_FPS = 60;
    private static final int MAX_FPS = 240;

    private final NewDisplays independent;

    public MediaCodecSessionFactory(NewDisplays independent) {
        this.independent = independent;
    }

    @Override
    public EncodingSession open(EncoderLadder.Attempt attempt, DisplaySnapshot display, VideoSettings settings)
            throws EncoderException {
        MediaCodec codec = null;
        Surface surface = null;
        Runnable projection = null;
        try {
            codec = MediaCodec.createByCodecName(attempt.encoder());
            configure(codec, attempt, display, settings);
            surface = codec.createInputSurface();
            projection = project(display, attempt, surface);
            codec.start();
            return new Session(codec, surface, projection);
        } catch (IOException | RuntimeException | PlatformException e) {
            if (projection != null) {
                projection.run();
            }
            if (codec != null) {
                codec.release();
            }
            if (surface != null) {
                surface.release();
            }
            throw new EncoderException("cannot start the encoder", e);
        }
    }

    private Runnable project(DisplaySnapshot display, EncoderLadder.Attempt attempt, Surface surface)
            throws PlatformException {
        if (independent != null && independent.owns(display.displayId())) {
            independent.attach(display.displayId(), surface);
            return () -> {
            };
        }
        final DisplayMirror mirror = DisplayMirror.create(display, attempt.size().width(), attempt.size().height(),
                surface);
        return mirror::close;
    }

    static int frameRate(DisplaySnapshot display, VideoSettings settings) {
        if (settings.maxFps() > 0) {
            return settings.maxFps();
        }
        final long hz = Math.round(display.refreshMilliHz() / 1000.0);
        return hz > 0 ? (int) Math.min(hz, MAX_FPS) : NOMINAL_FPS;
    }

    static boolean qualcomm(String encoder) {
        final String name = encoder.toLowerCase(Locale.ROOT);
        return name.startsWith("c2.qti.") || name.startsWith("omx.qcom.");
    }

    private static void configure(MediaCodec codec, EncoderLadder.Attempt attempt, DisplaySnapshot display,
                                  VideoSettings settings) {
        if (qualcomm(attempt.encoder())) {
            final MediaFormat lowLatency = format(attempt, display, settings);
            lowLatency.setInteger(KEY_QTI_LOW_LATENCY, 1);
            lowLatency.setInteger(MediaFormat.KEY_OPERATING_RATE, Short.MAX_VALUE);
            try {
                codec.configure(lowLatency, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
                return;
            } catch (IllegalArgumentException | IllegalStateException e) {
                Log.i(attempt.encoder() + " refused its low-latency mode (" + e.getMessage() + "); configuring without it");
                codec.reset();
            }
        }
        codec.configure(format(attempt, display, settings), null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
    }

    private static MediaFormat format(EncoderLadder.Attempt attempt, DisplaySnapshot display, VideoSettings settings) {
        final MediaFormat format = MediaFormat.createVideoFormat(EncoderCatalog.mimeType(attempt.codec()),
                attempt.size().width(), attempt.size().height());
        final int fps = frameRate(display, settings);
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
        format.setInteger(MediaFormat.KEY_COLOR_RANGE, MediaFormat.COLOR_RANGE_LIMITED);
        format.setInteger(MediaFormat.KEY_BIT_RATE, (int) Math.min(settings.bitrate(), Integer.MAX_VALUE));
        format.setInteger(MediaFormat.KEY_FRAME_RATE, fps);
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, KEY_FRAME_INTERVAL_SECONDS);
        format.setLong(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, REPEAT_FRAME_AFTER_US);
        format.setInteger(MediaFormat.KEY_PRIORITY, 0);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            format.setInteger(MediaFormat.KEY_MAX_B_FRAMES, 0);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            format.setInteger(MediaFormat.KEY_LATENCY, 1);
        }
        if (settings.maxFps() > 0) {
            format.setFloat(KEY_MAX_FPS_TO_ENCODER, settings.maxFps());
        }
        if (settings.intraRefreshMs() > 0) {
            format.setInteger(MediaFormat.KEY_INTRA_REFRESH_PERIOD,
                    Math.max(1, (int) Math.ceil(settings.intraRefreshMs() * fps / 1000.0)));
        }
        return format;
    }

    private static final class Session implements EncodingSession {
        private final MediaCodec codec;
        private final Surface surface;
        private final Runnable projection;
        private final MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        private final Output output = new Output();
        private boolean endPending;

        Session(MediaCodec codec, Surface surface, Runnable projection) {
            this.codec = codec;
            this.surface = surface;
            this.projection = projection;
        }

        @Override
        public Output next(long deadlineNanos) throws EncoderException {
            if (endPending) {
                return end();
            }
            while (true) {
                final long timeoutUs = deadlineNanos == NO_DEADLINE ? -1
                        : Math.max(0, (deadlineNanos - System.nanoTime()) / 1000);
                final int index;
                try {
                    index = codec.dequeueOutputBuffer(info, timeoutUs);
                } catch (IllegalStateException e) {
                    throw new EncoderException("the encoder failed", e);
                }
                if (index == MediaCodec.INFO_TRY_AGAIN_LATER) {
                    if (deadlineNanos == NO_DEADLINE) {
                        continue;
                    }
                    return null;
                }
                if (index < 0) {
                    continue;
                }
                final boolean last = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                if (info.size <= 0) {
                    codec.releaseOutputBuffer(index, false);
                    if (last) {
                        return end();
                    }
                    continue;
                }
                final ByteBuffer data = codec.getOutputBuffer(index);
                data.limit(info.offset + info.size);
                data.position(info.offset);
                output.kind = (info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0 ? Kind.CONFIG : Kind.FRAME;
                output.data = data;
                output.ptsUs = info.presentationTimeUs;
                output.readyUs = MediaClock.nowUs();
                output.keyFrame = (info.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0;
                output.index = index;
                endPending = last;
                return output;
            }
        }

        private Output end() {
            output.kind = Kind.END;
            output.data = null;
            output.index = -1;
            return output;
        }

        @Override
        public void release(Output released) {
            if (released.index >= 0) {
                try {
                    codec.releaseOutputBuffer(released.index, false);
                } catch (IllegalStateException ignored) {
                }
                released.index = -1;
            }
        }

        @Override
        public void interrupt() {
            try {
                codec.signalEndOfInputStream();
            } catch (IllegalStateException ignored) {
            }
        }

        @Override
        public void setBitrate(long bitrate) {
            final Bundle parameters = new Bundle();
            parameters.putInt(MediaCodec.PARAMETER_KEY_VIDEO_BITRATE, (int) Math.min(bitrate, Integer.MAX_VALUE));
            setParameters(parameters);
        }

        @Override
        public void requestKeyFrame() {
            final Bundle parameters = new Bundle();
            parameters.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
            setParameters(parameters);
        }

        @Override
        public void setPaused(boolean paused) {
            final Bundle parameters = new Bundle();
            parameters.putInt(MediaCodec.PARAMETER_KEY_SUSPEND, paused ? 1 : 0);
            setParameters(parameters);
        }

        private void setParameters(Bundle parameters) {
            try {
                codec.setParameters(parameters);
            } catch (IllegalStateException ignored) {
            }
        }

        @Override
        public void close() {
            projection.run();
            try {
                codec.stop();
            } catch (IllegalStateException ignored) {
            }
            codec.release();
            surface.release();
        }
    }
}
