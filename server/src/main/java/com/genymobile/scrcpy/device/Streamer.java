package com.genymobile.scrcpy.device;

import com.genymobile.scrcpy.util.Codec;
import com.genymobile.scrcpy.util.IO;

import android.media.MediaCodec;

import java.io.FileDescriptor;
import java.io.IOException;
import java.nio.ByteBuffer;

public final class Streamer implements IStreamer {

    private static final long PACKET_FLAG_CONFIG = 1L << 63;
    private static final long PACKET_FLAG_KEY_FRAME = 1L << 62;

    private final FileDescriptor fd;
    private final Codec codec;
    private final boolean sendCodecMeta;
    private final boolean sendFrameMeta;

    private final ByteBuffer headerBuffer = ByteBuffer.allocate(12);

    public Streamer(FileDescriptor fd, Codec codec, boolean sendCodecMeta, boolean sendFrameMeta) {
        this.fd = fd;
        this.codec = codec;
        this.sendCodecMeta = sendCodecMeta;
        this.sendFrameMeta = sendFrameMeta;
    }

    public Codec getCodec() {
        return codec;
    }

    public void writeAudioHeader() throws IOException {
        if (sendCodecMeta) {
            ByteBuffer buffer = ByteBuffer.allocate(4);
            buffer.putInt(codec.getId());
            buffer.flip();
            IO.writeFully(fd, buffer);
        }
    }

    public void writeVideoHeader(Size videoSize) throws IOException {
        if (sendCodecMeta) {
            ByteBuffer buffer = ByteBuffer.allocate(12);
            buffer.putInt(codec.getId());
            buffer.putInt(videoSize.getWidth());
            buffer.putInt(videoSize.getHeight());
            buffer.flip();
            IO.writeFully(fd, buffer);
        }
    }

    public void writeDisableStream(boolean error) throws IOException {
        // Writing a specific code as codec-id means that the device disables the stream
        // code 0: it explicitly disables the stream (because it could not capture
        // audio), scrcpy should continue mirroring video only
        // code 1: a configuration error occurred, scrcpy must be stopped
        byte[] code = new byte[4];
        if (error) {
            code[3] = 1;
        }
        IO.writeFully(fd, code, 0, code.length);
    }

    public void writePacket(ByteBuffer buffer, long pts, boolean config, boolean keyFrame) throws IOException {
        // Audio codec config packet handling removed (audio disabled)

        if (sendFrameMeta) {
            writeFrameMeta(fd, buffer.remaining(), pts, config, keyFrame);
        }

        IO.writeFully(fd, buffer);
    }

    public void writePacket(ByteBuffer codecBuffer, MediaCodec.BufferInfo bufferInfo) throws IOException {
        long pts = bufferInfo.presentationTimeUs;
        boolean config = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0;
        boolean keyFrame = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0;
        writePacket(codecBuffer, pts, config, keyFrame);
    }

    private void writeFrameMeta(FileDescriptor fd, int packetSize, long pts, boolean config, boolean keyFrame)
            throws IOException {
        headerBuffer.clear();

        long ptsAndFlags;
        if (config) {
            ptsAndFlags = PACKET_FLAG_CONFIG; // non-media data packet
        } else {
            ptsAndFlags = pts;
            if (keyFrame) {
                ptsAndFlags |= PACKET_FLAG_KEY_FRAME;
            }
        }

        headerBuffer.putLong(ptsAndFlags);
        headerBuffer.putInt(packetSize);
        headerBuffer.flip();
        IO.writeFully(fd, headerBuffer);
    }
}
