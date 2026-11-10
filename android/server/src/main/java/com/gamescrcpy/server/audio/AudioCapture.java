package com.gamescrcpy.server.audio;

import android.annotation.SuppressLint;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.AudioTimestamp;
import android.media.MediaRecorder;
import android.os.Build;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.server.wrappers.ProcessEnvironment;
import java.nio.ByteBuffer;

final class AudioCapture implements AutoCloseable {

    static final int SAMPLE_RATE = 48_000;
    static final int CHANNELS = 2;
    static final int BYTES_PER_FRAME = CHANNELS * 2;
    static final int MAX_READ_BYTES = 1024 * BYTES_PER_FRAME;

    private final AudioRecord record;
    private final AudioTimestamp timestamp = new AudioTimestamp();
    private long framesRead;

    @SuppressLint({"MissingPermission", "WrongConstant"})
    AudioCapture() throws AudioException {
        final AudioFormat format = new AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(SAMPLE_RATE)
                .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                .build();
        final AudioRecord.Builder builder = new AudioRecord.Builder()
                .setAudioSource(MediaRecorder.AudioSource.REMOTE_SUBMIX)
                .setAudioFormat(format);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            builder.setContext(ProcessEnvironment.context());
        }
        final int minimum = AudioRecord.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_IN_STEREO,
                AudioFormat.ENCODING_PCM_16BIT);
        if (minimum > 0) {
            builder.setBufferSizeInBytes(8 * minimum);
        }
        try {
            record = builder.build();
        } catch (RuntimeException e) {
            throw new AudioException(ErrorCode.AUDIO_CAPTURE_FAILED, "cannot open the audio output for capture", e);
        }
        try {
            record.startRecording();
        } catch (RuntimeException e) {
            record.release();
            throw new AudioException(ErrorCode.AUDIO_CAPTURE_FAILED, "cannot start capturing the audio output", e);
        }
        if (record.getRecordingState() != AudioRecord.RECORDSTATE_RECORDING) {
            record.release();
            throw new AudioException(ErrorCode.AUDIO_CAPTURE_FAILED, "the audio output did not start recording");
        }
    }

    int read(ByteBuffer buffer) {
        final int read = record.read(buffer, Math.min(buffer.remaining(), MAX_READ_BYTES));
        if (read > 0) {
            buffer.position(buffer.position() + read);
            framesRead += read / BYTES_PER_FRAME;
        }
        return read;
    }

    long captureTimeUs(long frame) {
        if (record.getTimestamp(timestamp, AudioTimestamp.TIMEBASE_MONOTONIC) == AudioRecord.SUCCESS) {
            return (timestamp.nanoTime + (frame - timestamp.framePosition) * 1_000_000_000L / SAMPLE_RATE) / 1000;
        }
        return System.nanoTime() / 1000 - (framesRead - frame) * 1_000_000L / SAMPLE_RATE;
    }

    void stop() {
        try {
            record.stop();
        } catch (IllegalStateException ignored) {
        }
    }

    @Override
    public void close() {
        record.release();
    }
}
