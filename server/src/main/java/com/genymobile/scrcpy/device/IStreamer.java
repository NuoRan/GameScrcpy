package com.genymobile.scrcpy.device;

import com.genymobile.scrcpy.util.Codec;

import android.media.MediaCodec;

import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * Common interface for video streamers (TCP and KCP)
 */
public interface IStreamer {
    /**
     * Get the codec used by this streamer
     */
    Codec getCodec();

    /**
     * Write audio header
     */
    void writeAudioHeader() throws IOException;

    /**
     * Write video header with size info
     */
    void writeVideoHeader(Size videoSize) throws IOException;

    /**
     * Write disable stream marker
     */
    void writeDisableStream(boolean error) throws IOException;

    /**
     * Write a video packet
     */
    void writePacket(ByteBuffer buffer, long pts, boolean config, boolean keyFrame) throws IOException;

    /**
     * Write a video packet from MediaCodec buffer info
     */
    void writePacket(ByteBuffer codecBuffer, MediaCodec.BufferInfo bufferInfo) throws IOException;
}
