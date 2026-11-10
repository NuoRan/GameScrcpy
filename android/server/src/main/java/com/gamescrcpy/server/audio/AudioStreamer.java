package com.gamescrcpy.server.audio;

import android.media.MediaCodec;
import android.media.MediaFormat;
import com.gamescrcpy.protocol.Enums.AudioCodec;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Messages.AudioSessionInfo;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.MediaClock;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;

final class AudioStreamer {

    interface FailureListener {
        void failed(ErrorCode code, String message);
    }

    private final AudioPackets packets;
    private final int session;
    private final int bitrate;
    private final FailureListener listener;
    private final CaptureTimes captureTimes = new CaptureTimes(AudioCapture.SAMPLE_RATE);
    private final AtomicBoolean ended = new AtomicBoolean();
    private AudioCapture capture;
    private MediaCodec codec;
    private Thread feeder;
    private Thread sender;

    AudioStreamer(AudioPackets packets, int session, int bitrate, FailureListener listener) {
        this.packets = packets;
        this.session = session;
        this.bitrate = bitrate;
        this.listener = listener;
    }

    void start() throws AudioException, IOException {
        capture = new AudioCapture();
        try {
            codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_OPUS);
            final MediaFormat format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_OPUS,
                    AudioCapture.SAMPLE_RATE, AudioCapture.CHANNELS);
            format.setInteger(MediaFormat.KEY_BIT_RATE, bitrate);
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            codec.start();
        } catch (IOException | RuntimeException e) {
            release();
            throw new AudioException(ErrorCode.ENCODER_FAILURE, "no Opus encoder", e);
        }
        final AudioSessionInfo info = new AudioSessionInfo();
        info.codec = AudioCodec.OPUS;
        info.sampleRate = AudioCapture.SAMPLE_RATE;
        info.channels = AudioCapture.CHANNELS;
        try {
            packets.session(session, info);
        } catch (IOException e) {
            release();
            throw e;
        }
        Log.i("audio session " + session + ": opus " + AudioCapture.SAMPLE_RATE + " Hz, "
                + AudioCapture.CHANNELS + " channels, " + bitrate + " bps");
        feeder = new Thread(this::feed, "gs-audio-in");
        sender = new Thread(this::send, "gs-audio-out");
        feeder.start();
        sender.start();
    }

    void stop() {
        if (ended.compareAndSet(false, true)) {
            halt();
        }
        join(feeder);
        join(sender);
        release();
    }

    private void fail(ErrorCode code, String message) {
        if (ended.compareAndSet(false, true)) {
            halt();
            listener.failed(code, message);
        }
    }

    private void halt() {
        capture.stop();
        try {
            codec.stop();
        } catch (IllegalStateException ignored) {
        }
    }

    private void feed() {
        long frame = 0;
        try {
            while (!ended.get()) {
                final int index = codec.dequeueInputBuffer(-1);
                final ByteBuffer buffer = codec.getInputBuffer(index);
                if (buffer == null) {
                    fail(ErrorCode.ENCODER_FAILURE, "the audio encoder has no input buffer");
                    return;
                }
                final int read = capture.read(buffer);
                if (read <= 0) {
                    fail(ErrorCode.AUDIO_CAPTURE_FAILED, "the audio capture stopped (read returned " + read + ")");
                    return;
                }
                captureTimes.add(frame, capture.captureTimeUs(frame));
                codec.queueInputBuffer(index, 0, read, positionUs(frame), 0);
                frame += read / AudioCapture.BYTES_PER_FRAME;
            }
        } catch (IllegalStateException e) {
            fail(ErrorCode.ENCODER_FAILURE, "the audio encoder failed: " + e.getMessage());
        }
    }

    private void send() {
        final MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        long previousPts = Long.MIN_VALUE;
        try {
            while (!ended.get()) {
                final int index = codec.dequeueOutputBuffer(info, -1);
                if (index < 0) {
                    continue;
                }
                final long readyUs = MediaClock.nowUs();
                final ByteBuffer data = codec.getOutputBuffer(index);
                if (data != null && (info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                    final ByteBuffer head;
                    try {
                        head = OpusHeader.find(data);
                    } catch (IOException e) {
                        fail(ErrorCode.ENCODER_FAILURE, "the Opus encoder's configuration is not understood: "
                                + e.getMessage());
                        return;
                    }
                    packets.config(session, head);
                } else if (data != null && info.size > 0) {
                    final long sample = info.presentationTimeUs * AudioCapture.SAMPLE_RATE / 1_000_000L;
                    final long pts = Math.max(captureTimes.at(sample), previousPts + 1);
                    previousPts = pts;
                    packets.frame(session, pts, MediaClock.encodeDelayUs(pts, readyUs), data);
                }
                codec.releaseOutputBuffer(index, false);
            }
        } catch (IllegalStateException e) {
            fail(ErrorCode.ENCODER_FAILURE, "the audio encoder failed: " + e.getMessage());
        } catch (IOException e) {
            if (!ended.get()) {
                Log.d("audio channel closed: " + e.getMessage());
            }
        }
    }

    private static long positionUs(long frame) {
        return frame * 1_000_000L / AudioCapture.SAMPLE_RATE;
    }

    private static void join(Thread thread) {
        if (thread == null) {
            return;
        }
        boolean interrupted = false;
        while (thread.isAlive()) {
            try {
                thread.join();
            } catch (InterruptedException e) {
                interrupted = true;
            }
        }
        if (interrupted) {
            Thread.currentThread().interrupt();
        }
    }

    private void release() {
        if (codec != null) {
            codec.release();
            codec = null;
        }
        if (capture != null) {
            capture.close();
            capture = null;
        }
    }
}
