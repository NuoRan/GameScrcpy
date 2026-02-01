package com.genymobile.scrcpy.kcp;

import com.genymobile.scrcpy.device.IStreamer;
import com.genymobile.scrcpy.device.Size;
import com.genymobile.scrcpy.util.Codec;
import com.genymobile.scrcpy.util.Ln;

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
public final class KcpVideoSender implements IStreamer {

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
    private static final long RTT_UPDATE_INTERVAL = 1000; // RTT更新间隔 (ms)
    private int smoothedRtt = 100; // 平滑RTT (ms)
    private int rttVariation = 50; // RTT变化量

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
        // P-01: 基于码率计算窗口大小 (150ms 缓冲，降低延迟)
        int window = (bitrateBps / 8 * 150 / 1000) / 1376;
        if (window < 128)
            window = 128;
        else if (window > 2048)
            window = 2048;

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
            Ln.d("Start dropping frames: pending=" + pending + ", threshold=" + dropThreshold);
        }
        if (dropping && pending < resumeThreshold) {
            dropping = false;
            Ln.d("Stop dropping frames: pending=" + pending + ", resume=" + resumeThreshold);
        }

        // 丢弃非关键帧
        if (dropping && !config && !keyFrame) {
            droppedFrames++;
            return;
        }

        // 帧大小检查
        if (dataSize > 2 * 1024 * 1024) {
            Ln.w("Frame too large: " + dataSize);
            droppedFrames++;
            return;
        }

        // 构造数据包
        int packetSize = (sendFrameMeta ? 12 : 0) + dataSize;
        ByteBuffer packet = ByteBuffer.allocate(packetSize);

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
            packet.putLong(ptsAndFlags);
            packet.putInt(dataSize);
        }

        packet.put(buffer);
        packet.flip();

        // 发送
        byte[] data = new byte[packet.remaining()];
        packet.get(data);

        int ret = transport.send(data);
        if (ret >= 0) {
            totalPackets++;
            totalBytes += data.length;
        } else if (ret == -2) {
            Ln.w("Frame too large for KCP: " + data.length);
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

    private void sendBuffer(ByteBuffer buffer) throws IOException {
        byte[] data = new byte[buffer.remaining()];
        buffer.get(data);
        int ret = transport.send(data);
        if (ret < 0) {
            throw new IOException("KCP send failed: " + ret);
        }
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
