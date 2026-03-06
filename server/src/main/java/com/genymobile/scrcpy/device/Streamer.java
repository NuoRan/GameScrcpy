package com.genymobile.scrcpy.device;

import com.genymobile.scrcpy.util.Codec;
import com.genymobile.scrcpy.util.IO;

import android.media.MediaCodec;

import java.io.FileDescriptor;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;

public final class Streamer implements IStreamer {

    private static final long PACKET_FLAG_CONFIG = 1L << 63;
    private static final long PACKET_FLAG_KEY_FRAME = 1L << 62;

    private final FileDescriptor fd;       // 用于 LocalSocket/adb forward 模式
    private final OutputStream outputStream; // 用于 WiFi TCP Socket 模式
    private final Codec codec;
    private final boolean sendCodecMeta;
    private final boolean sendFrameMeta;

    private final ByteBuffer headerBuffer = ByteBuffer.allocate(12);

    /**
     * FileDescriptor 模式（USB/adb forward）
     */
    public Streamer(FileDescriptor fd, Codec codec, boolean sendCodecMeta, boolean sendFrameMeta) {
        this.fd = fd;
        this.outputStream = null;
        this.codec = codec;
        this.sendCodecMeta = sendCodecMeta;
        this.sendFrameMeta = sendFrameMeta;
    }

    /**
     * OutputStream 模式（WiFi TCP Socket）
     */
    public Streamer(OutputStream outputStream, Codec codec, boolean sendCodecMeta, boolean sendFrameMeta) {
        this.fd = null;
        this.outputStream = outputStream;
        this.codec = codec;
        this.sendCodecMeta = sendCodecMeta;
        this.sendFrameMeta = sendFrameMeta;
    }

    public Codec getCodec() {
        return codec;
    }

    /**
     * 统一的写 ByteBuffer 方法，支持 fd 和 OutputStream 两种底层
     */
    private void writeBuffer(ByteBuffer buffer) throws IOException {
        if (fd != null) {
            IO.writeFully(fd, buffer);
        } else if (outputStream != null) {
            byte[] data;
            if (buffer.hasArray()) {
                data = buffer.array();
                outputStream.write(data, buffer.arrayOffset() + buffer.position(), buffer.remaining());
                buffer.position(buffer.limit());
            } else {
                data = new byte[buffer.remaining()];
                buffer.get(data);
                outputStream.write(data);
            }
            outputStream.flush();
        }
    }

    private void writeByteArray(byte[] data, int offset, int len) throws IOException {
        if (fd != null) {
            IO.writeFully(fd, data, offset, len);
        } else if (outputStream != null) {
            outputStream.write(data, offset, len);
            outputStream.flush();
        }
    }

    public void writeAudioHeader() throws IOException {
        if (sendCodecMeta) {
            ByteBuffer buffer = ByteBuffer.allocate(4);
            buffer.putInt(codec.getId());
            buffer.flip();
            writeBuffer(buffer);
        }
    }

    public void writeVideoHeader(Size videoSize) throws IOException {
        if (sendCodecMeta) {
            ByteBuffer buffer = ByteBuffer.allocate(12);
            buffer.putInt(codec.getId());
            buffer.putInt(videoSize.getWidth());
            buffer.putInt(videoSize.getHeight());
            buffer.flip();
            writeBuffer(buffer);
        }
    }

    public void writeDisableStream(boolean error) throws IOException {
        byte[] code = new byte[4];
        if (error) {
            code[3] = 1;
        }
        writeByteArray(code, 0, code.length);
    }

    public void writePacket(ByteBuffer buffer, long pts, boolean config, boolean keyFrame) throws IOException {
        if (sendFrameMeta) {
            writeFrameMeta(buffer.remaining(), pts, config, keyFrame);
        }

        writeBuffer(buffer);
    }

    public void writePacket(ByteBuffer codecBuffer, MediaCodec.BufferInfo bufferInfo) throws IOException {
        long pts = bufferInfo.presentationTimeUs;
        boolean config = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0;
        boolean keyFrame = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0;
        writePacket(codecBuffer, pts, config, keyFrame);
    }

    private void writeFrameMeta(int packetSize, long pts, boolean config, boolean keyFrame)
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
        writeBuffer(headerBuffer);
    }
}
