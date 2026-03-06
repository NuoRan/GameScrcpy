package com.genymobile.scrcpy.auxiliary;

import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;

/**
 * 辅助通道消息读取器
 *
 * 从 InputStream（TCP LocalSocket）或 byte[]（UDP DatagramPacket）解析 AuxMessage。
 * 消息格式: [type:1B] [payload]
 */
public final class AuxMessageReader {

    private final DataInputStream dis;

    public AuxMessageReader(InputStream inputStream) {
        this.dis = new DataInputStream(inputStream);
    }

    /**
     * 阻塞读取下一条消息（用于 TCP 模式的流式读取）
     */
    public AuxMessage recv() throws IOException {
        int type = dis.readUnsignedByte();
        return parseByType(type);
    }

    /**
     * 从 byte[] 解析单条消息（用于 UDP 模式）
     */
    public static AuxMessage parseFromBytes(byte[] data, int offset, int length) throws IOException {
        if (length < 1) {
            throw new IOException("AuxMessage too short: " + length);
        }
        int type = data[offset] & 0xFF;

        switch (type) {
            case AuxMessage.TYPE_SET_VIDEO_PARAMS:
                if (length < 9) throw new IOException("SET_VIDEO_PARAMS requires 9 bytes, got " + length);
                return parseVideoParams(data, offset + 1);
            case AuxMessage.TYPE_SET_VIDEO_STREAMING:
                if (length < 2) throw new IOException("SET_VIDEO_STREAMING requires 2 bytes, got " + length);
                return AuxMessage.createSetVideoStreaming((data[offset + 1] & 0xFF) != 0);
            default:
                throw new IOException("Unknown aux message type: " + type);
        }
    }

    private AuxMessage parseByType(int type) throws IOException {
        switch (type) {
            case AuxMessage.TYPE_SET_VIDEO_PARAMS:
                return parseSetVideoParams();
            case AuxMessage.TYPE_SET_VIDEO_STREAMING:
                return parseSetVideoStreaming();
            default:
                throw new IOException("Unknown aux message type: " + type);
        }
    }

    private AuxMessage parseSetVideoParams() throws IOException {
        int bitRate = dis.readInt();
        int maxFps = dis.readUnsignedShort();
        int maxSize = dis.readUnsignedShort();
        return AuxMessage.createSetVideoParams(bitRate, maxFps, maxSize);
    }

    private AuxMessage parseSetVideoStreaming() throws IOException {
        int mode = dis.readUnsignedByte();
        return AuxMessage.createSetVideoStreaming(mode != 0);
    }

    private static AuxMessage parseVideoParams(byte[] data, int off) {
        int bitRate = ((data[off] & 0xFF) << 24) | ((data[off + 1] & 0xFF) << 16)
                    | ((data[off + 2] & 0xFF) << 8) | (data[off + 3] & 0xFF);
        int maxFps = ((data[off + 4] & 0xFF) << 8) | (data[off + 5] & 0xFF);
        int maxSize = ((data[off + 6] & 0xFF) << 8) | (data[off + 7] & 0xFF);
        return AuxMessage.createSetVideoParams(bitRate, maxFps, maxSize);
    }
}
