package com.gamescrcpy.server.video;

import android.annotation.SuppressLint;
import android.hardware.HardwareBuffer;
import android.media.Image;
import android.media.ImageReader;
import android.media.MediaCodec;
import android.media.MediaFormat;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.MediaClock;
import java.nio.ByteBuffer;
import java.util.List;

@SuppressLint({"NewApi", "InlinedApi"})
final class BlockModelSession implements EncodingSession {

    private static final long JOIN_MILLIS = 1_000;

    private final String encoder;
    private final MediaCodec codec;
    private final ImageReader reader;
    private final HandlerThread inputThread;
    private final Handler inputHandler;
    private final HandlerThread codecThread;
    private final int keyFrameIntervalSeconds;
    private final BlockModelPump pump;
    private final Callback callback = new Callback();
    private Runnable projection = () -> {
    };

    BlockModelSession(String encoder, MediaCodec codec, ImageReader reader, HandlerThread inputThread,
                      Handler inputHandler, HandlerThread codecThread, int keyFrameIntervalSeconds, int maxFps,
                      int maxInFlight) {
        this.encoder = encoder;
        this.codec = codec;
        this.reader = reader;
        this.inputThread = inputThread;
        this.inputHandler = inputHandler;
        this.codecThread = codecThread;
        this.keyFrameIntervalSeconds = keyFrameIntervalSeconds;
        this.pump = new BlockModelPump(this::queueFrame, inputHandler::post, new HandlerTimer(inputHandler),
                System::nanoTime, maxInFlight, maxFps);
        reader.setOnImageAvailableListener(this::imageAvailable, inputHandler);
    }

    MediaCodec.Callback callback() {
        return callback;
    }

    void start() {
        codec.start();
    }

    void attach(Runnable displayProjection) {
        projection = displayProjection;
    }

    @Override
    public Output next(long deadlineNanos) throws EncoderException {
        try {
            return pump.next(deadlineNanos);
        } catch (EncoderException e) {
            if (MediaCodecSessionFactory.noteBlockModelFailed(encoder)) {
                throw new BlockModelException("the block model stopped", e);
            }
            throw e;
        }
    }

    @Override
    public void release(Output output) {
        final int index = output.index;
        final Object payload = output.payload;
        output.index = -1;
        pump.release(output);
        if (payload instanceof MediaCodec.LinearBlock block) {
            try {
                block.recycle();
            } catch (IllegalStateException ignored) {
            }
        }
        if (index >= 0) {
            try {
                codec.releaseOutputBuffer(index, false);
            } catch (IllegalStateException ignored) {
            }
        }
    }

    @Override
    public void interrupt() {
        pump.interrupt();
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
        pump.requestKeyFrame();
    }

    @Override
    public void setPaused(boolean paused) {
        final Bundle parameters = new Bundle();
        parameters.putInt(MediaCodec.PARAMETER_KEY_SUSPEND, paused ? 1 : 0);
        setParameters(parameters);
        pump.pause(paused);
    }

    @Override
    public String outputFormatSummary() {
        return MediaCodecSessionFactory.formatSummary(codec, keyFrameIntervalSeconds);
    }

    @Override
    public void close() {
        projection.run();
        try {
            reader.setOnImageAvailableListener(null, null);
        } catch (RuntimeException ignored) {
        }
        for (Output leftover : pump.close()) {
            if (leftover.payload instanceof MediaCodec.LinearBlock block) {
                try {
                    block.recycle();
                } catch (IllegalStateException ignored) {
                }
            }
            leftover.payload = null;
            leftover.index = -1;
        }
        inputHandler.removeCallbacksAndMessages(null);
        inputThread.quitSafely();
        codecThread.quitSafely();
        try {
            inputThread.join(JOIN_MILLIS);
            codecThread.join(JOIN_MILLIS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        if (inputThread.isAlive() || codecThread.isAlive()) {
            Log.w("the block model threads did not stop within " + JOIN_MILLIS + " ms");
        }
        reader.close();
        try {
            codec.stop();
        } catch (IllegalStateException ignored) {
        }
        codec.release();
        if (Log.enabled(Log.Level.DEBUG)) {
            Log.d("block model input closed with " + pump.droppedFrames() + " frames dropped");
        }
    }

    private void setParameters(Bundle parameters) {
        try {
            codec.setParameters(parameters);
        } catch (IllegalStateException ignored) {
        }
    }

    private void imageAvailable(ImageReader source) {
        final Image image;
        try {
            image = source.acquireLatestImage();
        } catch (IllegalStateException e) {
            return;
        }
        if (image != null) {
            pump.frame(new Captured(image));
        }
    }

    private boolean queueFrame(int slot, BlockModelPump.Image image, long ptsUs) {
        try {
            final HardwareBuffer buffer = ((Captured) image).image().getHardwareBuffer();
            if (buffer == null) {
                return false;
            }
            codec.getQueueRequest(slot)
                    .setHardwareBuffer(buffer)
                    .setPresentationTimeUs(ptsUs)
                    .queue();
            return true;
        } catch (RuntimeException e) {
            Log.w("the block model cannot hand a frame to the encoder", e);
            return false;
        }
    }

    private void deliver(int index, MediaCodec.BufferInfo bufferInfo) {
        final boolean last = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
        final boolean config = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0;
        ByteBuffer data = null;
        Object payload = null;
        boolean unreadable = false;
        try {
            final MediaCodec.OutputFrame frame = codec.getOutputFrame(index);
            if (bufferInfo.size > 0) {
                final MediaCodec.LinearBlock block = frame.getLinearBlock();
                if (block == null || !block.isMappable()) {
                    unreadable = true;
                } else {
                    final ByteBuffer mapped = block.map();
                    final int offset = Math.max(0, Math.min(bufferInfo.offset, mapped.capacity()));
                    final int end = Math.max(offset,
                            Math.min(bufferInfo.offset + bufferInfo.size, mapped.capacity()));
                    mapped.position(offset);
                    mapped.limit(end);
                    data = mapped;
                    payload = block;
                }
            }
        } catch (RuntimeException e) {
            Log.w("the block model cannot read an output frame", e);
            unreadable = true;
        }
        if (unreadable) {
            pump.fail("the encoder output is not readable");
            return;
        }
        if (data == null) {
            try {
                codec.releaseOutputBuffer(index, false);
            } catch (IllegalStateException ignored) {
            }
            if (last) {
                pump.output(Kind.END, null, 0, MediaClock.nowUs(), false, -1, null, true);
            }
            return;
        }
        if (!pump.output(config ? Kind.CONFIG : Kind.FRAME, data, bufferInfo.presentationTimeUs, MediaClock.nowUs(),
                (bufferInfo.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0, index, payload, last)) {
            if (payload instanceof MediaCodec.LinearBlock block) {
                try {
                    block.recycle();
                } catch (IllegalStateException ignored) {
                }
            }
            try {
                codec.releaseOutputBuffer(index, false);
            } catch (IllegalStateException ignored) {
            }
        }
    }

    private static final class HandlerTimer implements BlockModelPump.Timer {
        private final Handler handler;

        HandlerTimer(Handler handler) {
            this.handler = handler;
        }

        @Override
        public void after(long delayNanos, Runnable task) {
            final long millis = Math.max(1, delayNanos / 1_000_000);
            handler.postDelayed(task, millis); // source-rule-allow R9: an idle screen repeats its last frame every 100 ms, as the input surface did
        }
    }

    private record Captured(Image image) implements BlockModelPump.Image {
        @Override
        public long timestampNanos() {
            return image.getTimestamp();
        }

        @Override
        public void close() {
            image.close();
        }
    }

    private final class Callback extends MediaCodec.Callback {
        @Override
        public void onInputBufferAvailable(MediaCodec source, int index) {
            pump.slotAvailable(index);
        }

        @Override
        public void onOutputBufferAvailable(MediaCodec source, int index, MediaCodec.BufferInfo bufferInfo) {
            deliver(index, bufferInfo);
        }

        @Override
        public void onOutputFormatChanged(MediaCodec source, MediaFormat format) {
            Log.d("block model output format: " + format);
        }

        @Override
        public void onError(MediaCodec source, MediaCodec.CodecException e) {
            Log.w("the block model encoder failed", e);
            pump.fail("the encoder failed");
        }
    }
}
