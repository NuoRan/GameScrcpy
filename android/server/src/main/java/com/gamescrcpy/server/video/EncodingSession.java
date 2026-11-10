package com.gamescrcpy.server.video;

import com.gamescrcpy.server.device.DisplaySnapshot;
import java.nio.ByteBuffer;

public interface EncodingSession extends AutoCloseable {

    long NO_DEADLINE = Long.MAX_VALUE;

    enum Kind { CONFIG, FRAME, END }

    final class Output {
        public Kind kind = Kind.END;
        public ByteBuffer data;
        public long ptsUs;
        public long readyUs;
        public boolean keyFrame;
        public int index = -1;
    }

    interface Factory {
        EncodingSession open(EncoderLadder.Attempt attempt, DisplaySnapshot display, VideoSettings settings)
                throws EncoderException;
    }

    Output next(long deadlineNanos) throws EncoderException;

    void release(Output output);

    void interrupt();

    void setBitrate(long bitrate);

    void requestKeyFrame();

    void setPaused(boolean paused);

    @Override
    void close();
}
