package com.genymobile.scrcpy.kcp;

import com.genymobile.scrcpy.device.IStreamer;
import com.genymobile.scrcpy.device.Size;
import com.genymobile.scrcpy.util.Codec;
import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.video.BitrateControl;

import android.media.MediaCodec;

import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * KcpVideoSender - 基于KCP的视频发送器 - 全新重构
 *
 * 特点:
 * - 使用全新重构的KcpTransport
 * - 低延迟可靠传输
 * - 自动丢帧控制
 */
public final class KcpVideoSender implements IStreamer, BitrateControl {

    private static final long PACKET_FLAG_CONFIG = 1L << 63;
    private static final long PACKET_FLAG_KEY_FRAME = 1L << 62;

    private final KcpTransport transport;
    private final Codec codec;
    private final boolean sendCodecMeta;
    private final boolean sendFrameMeta;

    // 统计
    private long totalPackets = 0;
    private long totalBytes = 0;
    private long droppedFrames = 0;

    // P-01: 动态丢帧控制 (基于RTT自适应)
    private int dropThreshold = 128;
    private int resumeThreshold = 32;
    private volatile boolean dropping = false;

    // P-01: RTT 自适应参数
    private int baseWindowSize; // 基础窗口大小
    private long lastRttUpdateTime = 0; // 上次RTT更新时间
    private static final long RTT_UPDATE_INTERVAL = 500; // RTT更新间隔 (ms)，更频繁响应

    // 预分配缓冲区 (避免每帧分配)
    private static final int MAX_FRAME_SIZE = 512 * 1024 + 12; // 512KB + 头部 (单帧H.264通常远小于此)
    private final byte[] packetData = new byte[MAX_FRAME_SIZE];

    /**
     * 创建视频发送器
     *
     * @param clientIp      客户端IP
     * @param port          KCP端口
     * @param codec         编码器
     * @param sendCodecMeta 是否发送编码器信息
     * @param sendFrameMeta 是否发送帧元数据
     * @param bitrateBps    码率（用于计算窗口大小）
     */
    public KcpVideoSender(String clientIp, int port, Codec codec,
            boolean sendCodecMeta, boolean sendFrameMeta,
            int bitrateBps) throws IOException {

        this.codec = codec;
        this.sendCodecMeta = sendCodecMeta;
        this.sendFrameMeta = sendFrameMeta;

        // 创建传输层
        transport = new KcpTransport(KcpTransport.CONV_VIDEO);

        // 启用流模式，允许 KCP 合并小包减少协议开销
        transport.setStreamMode(1);

        // 根据码率配置窗口
        int windowSize = calculateWindowSize(bitrateBps);
        transport.setWindowSize(windowSize, windowSize);

        // 丢帧阈值
        dropThreshold = windowSize / 4;
        resumeThreshold = windowSize / 8;

        // 连接到客户端
        if (!transport.connect(clientIp, port)) {
            throw new IOException("Failed to connect KCP to " + clientIp + ":" + port);
        }

        Ln.i("KcpVideoSender: connected to " + clientIp + ":" + port +
                ", window=" + windowSize);
    }

    private int calculateWindowSize(int bitrateBps) {
        // 基于码率计算窗口大小 (300ms 缓冲，为 VBR 运动峰值预留余量)
        int window = (bitrateBps / 8 * 300 / 1000) / 1376;
        if (window < 256)
            window = 256;
        else if (window > 4096)
            window = 4096;

        // P-01: 保存基础窗口大小用于动态调整
        baseWindowSize = window;
        return window;
    }

    /**
     * P-01: 根据网络状况动态调整丢帧阈值
     */
    private void updateDynamicThresholds(int pending) {
        long now = System.currentTimeMillis();
        if (now - lastRttUpdateTime < RTT_UPDATE_INTERVAL) {
            return;
        }
        lastRttUpdateTime = now;

        // 根据 pending 数量估算网络状况
        // pending 越高说明网络越差，需要更激进地丢帧
        float congestionRatio = (float) pending / baseWindowSize;

        if (congestionRatio > 0.8f) {
            // 网络拥塞严重，降低阈值更激进丢帧
            dropThreshold = Math.max(16, baseWindowSize / 8);
            resumeThreshold = Math.max(8, dropThreshold / 4);
        } else if (congestionRatio > 0.5f) {
            // 中等拥塞
            dropThreshold = baseWindowSize / 4;
            resumeThreshold = dropThreshold / 4;
        } else {
            // 网络状况良好，恢复默认阈值
            dropThreshold = baseWindowSize / 2;
            resumeThreshold = dropThreshold / 4;
        }
    }

    @Override
    public Codec getCodec() {
        return codec;
    }

    @Override
    public void writeAudioHeader() throws IOException {
        if (sendCodecMeta) {
            ByteBuffer buf = ByteBuffer.allocate(4);
            buf.putInt(codec.getId());
            buf.flip();
            sendBuffer(buf);
        }
    }

    @Override
    public void writeVideoHeader(Size videoSize) throws IOException {
        checkDeadLink();

        if (sendCodecMeta) {
            ByteBuffer buf = ByteBuffer.allocate(12);
            buf.putInt(codec.getId());
            buf.putInt(videoSize.getWidth());
            buf.putInt(videoSize.getHeight());
            buf.flip();
            sendBuffer(buf);
        }
    }

    @Override
    public void writeDisableStream(boolean error) throws IOException {
        byte[] data = new byte[4];
        if (error) {
            data[3] = 1;
        }
        transport.send(data);
    }

    @Override
    public void writePacket(ByteBuffer buffer, long pts, boolean config, boolean keyFrame)
            throws IOException {

        checkDeadLink();

        int pending = transport.pending();
        int dataSize = buffer.remaining();

        // P-01: 动态调整丢帧阈值
        updateDynamicThresholds(pending);

        // 滞回丢帧控制
        if (pending > dropThreshold && !dropping) {
            dropping = true;
        }
        if (dropping && pending < resumeThreshold) {
            dropping = false;
        }

        // 丢弃非关键帧
        if (dropping && !config && !keyFrame) {
            droppedFrames++;
            return;
        }

        // 帧大小检查
        int headerSize = sendFrameMeta ? 12 : 0;
        int packetSize = headerSize + dataSize;
        if (packetSize > MAX_FRAME_SIZE) {
            droppedFrames++;
            return;
        }

        // 直接写入预分配 byte[]，避免 ByteBuffer 中转拷贝
        int offset = 0;
        if (sendFrameMeta) {
            long ptsAndFlags;
            if (config) {
                ptsAndFlags = PACKET_FLAG_CONFIG;
            } else {
                ptsAndFlags = pts;
                if (keyFrame) {
                    ptsAndFlags |= PACKET_FLAG_KEY_FRAME;
                }
            }
            // 手动写入 long (big-endian)
            packetData[0] = (byte) (ptsAndFlags >> 56);
            packetData[1] = (byte) (ptsAndFlags >> 48);
            packetData[2] = (byte) (ptsAndFlags >> 40);
            packetData[3] = (byte) (ptsAndFlags >> 32);
            packetData[4] = (byte) (ptsAndFlags >> 24);
            packetData[5] = (byte) (ptsAndFlags >> 16);
            packetData[6] = (byte) (ptsAndFlags >> 8);
            packetData[7] = (byte) ptsAndFlags;
            // 手动写入 int (big-endian)
            packetData[8] = (byte) (dataSize >> 24);
            packetData[9] = (byte) (dataSize >> 16);
            packetData[10] = (byte) (dataSize >> 8);
            packetData[11] = (byte) dataSize;
            offset = 12;
        }

        // 直接从 codecBuffer 拷贝到 packetData，仅一次拷贝
        buffer.get(packetData, offset, dataSize);
        int sendSize = offset + dataSize;

        int ret = transport.send(packetData, 0, sendSize);
        totalPackets++;
        totalBytes += sendSize;
        if (ret < 0) {
            droppedFrames++;
        }
    }

    @Override
    public void writePacket(ByteBuffer codecBuffer, MediaCodec.BufferInfo bufferInfo)
            throws IOException {
        long pts = bufferInfo.presentationTimeUs;
        boolean config = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0;
        boolean keyFrame = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0;
        writePacket(codecBuffer, pts, config, keyFrame);
    }

    // =========================================================================
    // BitrateControl: 基于 KCP 拥塞状态反馈建议码率
    // =========================================================================

    @Override
    public int getSuggestedBitrate(int baseBitrate) {
        int pending = transport.pending();
        if (pending <= 0) {
            return baseBitrate;
        }

        float ratio = (float) pending / baseWindowSize;

        if (ratio > 0.5f) {
            // 严重拥塞：降至 33%，大幅减少编码输出
            return baseBitrate / 3;
        } else if (ratio > 0.3f) {
            // 中等拥塞：降至 50%
            return baseBitrate / 2;
        } else if (ratio > 0.15f) {
            // 轻度拥塞：降至 75%
            return baseBitrate * 3 / 4;
        }

        return baseBitrate; // 无拥塞，保持原始码率
    }

    private void sendBuffer(ByteBuffer buffer) throws IOException {
        byte[] data = new byte[buffer.remaining()];
        buffer.get(data);
        transport.send(data);
        totalBytes += data.length;
    }

    private void checkDeadLink() throws IOException {
        if (transport.isDeadLink()) {
            throw new IOException("KCP dead link");
        }
    }

    public String getStats() {
        return String.format("packets=%d, bytes=%d, dropped=%d, pending=%d",
                totalPackets, totalBytes, droppedFrames, transport.pending());
    }

    public void close() {
        Ln.i("KcpVideoSender closing: " + getStats());
        transport.close();
    }
}
