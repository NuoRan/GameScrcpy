package com.genymobile.scrcpy.kcp;

import com.genymobile.scrcpy.device.IStreamer;
import com.genymobile.scrcpy.device.Size;
import com.genymobile.scrcpy.util.Codec;
import com.genymobile.scrcpy.util.Ln;

import android.media.MediaCodec;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;

/**
 * UDP 视频发送器 - 轻量无重传
 *
 * 替代 KcpVideoSender 用于视频通道。
 * KCP 的 ACK/重传/拥塞控制在低丢包 WiFi 环境下是纯开销。
 * 视频流天然容忍丢包：丢 1-2 帧不影响体验，丢包后请求关键帧即可恢复。
 *
 * 协议: 每个 UDP 包 = [uint32 seq (big-endian)] + [uint8 flags] + [payload]
 *   - flags bit 0 (SOF): 帧首包标志 (Start of Frame)
 *   - flags bit 1 (EOF): 帧尾包标志 (End of Frame)
 *   - 单包帧: flags = SOF|EOF (0x03)
 *
 * 帧完整性保证：
 *   客户端按 SOF→EOF 重组帧数据。若 SOF 到 EOF 之间有任何丢包
 *   （seq 不连续），整帧丢弃，不送解码器。
 *   解码器只收到完整的帧数据，不会因丢包导致字节流错位→画面脏污。
 *
 * 对比 KcpVideoSender:
 *   - 零 ACK 流量（KCP 每包一个 ACK 回包）
 *   - 无重传延迟（KCP 丢包后需等 RTO 重传）
 *   - 无协议头开销（KCP 24 字节 vs UDP 5 字节）
 *   - 无用户态锁/线程开销（KCP 需 updateLoop + receiveLoop）
 */
public final class UdpVideoSender implements IStreamer {

    private static final long PACKET_FLAG_CONFIG = 1L << 63;
    private static final long PACKET_FLAG_KEY_FRAME = 1L << 62;

    // UDP 分片参数
    private static final int MTU = 1400;
    private static final int SEQ_HEADER_SIZE = 5;       // uint32 seq + uint8 flags
    private static final int MAX_PAYLOAD = MTU - SEQ_HEADER_SIZE;  // 1395 字节

    // 帧边界标志
    private static final byte FLAG_SOF = 0x01;   // Start of Frame（帧首包）
    private static final byte FLAG_EOF = 0x02;   // End of Frame（帧尾包）

    private final DatagramSocket socket;
    private final InetSocketAddress target;
    private final Codec codec;
    private final boolean sendCodecMeta;
    private final boolean sendFrameMeta;

    // 单调递增序号（溢出自动回绕，客户端用差值检测丢包）
    private int seq = 0;

    // 统计
    private long totalPackets = 0;
    private long totalBytes = 0;

    // 预分配缓冲区（根据码率动态计算）
    private final byte[] frameBuffer;

    // 发送缓冲区（避免每个 UDP 包分配 byte[]）
    private final byte[] sendBuffer = new byte[MTU];

    /**
     * 创建 UDP 视频发送器
     *
     * @param clientIp      客户端 IP
     * @param port          客户端绑定的 UDP 端口
     * @param codec         编码器
     * @param sendCodecMeta 是否发送编码器信息
     * @param sendFrameMeta 是否发送帧元数据
     * @param bitrateBps    码率（用于配置 socket 缓冲区）
     */
    public UdpVideoSender(String clientIp, int port, Codec codec,
            boolean sendCodecMeta, boolean sendFrameMeta,
            int bitrateBps) throws IOException {

        this.codec = codec;
        this.sendCodecMeta = sendCodecMeta;
        this.sendFrameMeta = sendFrameMeta;

        // 根据码率动态计算帧缓冲区大小
        // I 帧可达平均帧大小的 10-15 倍，不能简单用 avg * 3 估算。
        // 上限取 bitrate/8（=1秒数据量=IDR间隔），单帧不可能超过这个值。
        // 最小 1MB，最大 8MB
        int maxFrameSize = Math.max(1024 * 1024,
                Math.min(8 * 1024 * 1024, bitrateBps / 8)) + 12;
        frameBuffer = new byte[maxFrameSize];

        // 发送缓冲区：约 1 秒数据，最小 1MB，最大 8MB
        int sendBufSize = Math.max(1024 * 1024,
                Math.min(8 * 1024 * 1024, bitrateBps / 8));

        // 创建 UDP socket
        socket = new DatagramSocket();
        socket.setSendBufferSize(sendBufSize);

        target = new InetSocketAddress(clientIp, port);
        // connect() 仅设置默认目标地址，不是 TCP 握手
        // 好处：send() 不用每次指定目标，且内核跳过路由查找
        socket.connect(target);

        Ln.i("UdpVideoSender: streaming to " + clientIp + ":" + port +
                " (pure UDP, no KCP)" +
                " bitrate=" + (bitrateBps / 1000000) + "Mbps" +
                " frameBuf=" + (frameBuffer.length / 1024) + "KB" +
                " sendBuf=" + (sendBufSize / 1024) + "KB");
    }

    @Override
    public Codec getCodec() {
        return codec;
    }

    @Override
    public void writeAudioHeader() throws IOException {
        // 不使用音频
    }

    @Override
    public void writeVideoHeader(Size videoSize) throws IOException {
        if (sendCodecMeta) {
            // 12 字节: [codec_id(4)][width(4)][height(4)]
            byte[] data = new byte[12];
            int id = codec.getId();
            data[0] = (byte) (id >> 24);
            data[1] = (byte) (id >> 16);
            data[2] = (byte) (id >> 8);
            data[3] = (byte) id;

            int w = videoSize.getWidth();
            data[4] = (byte) (w >> 24);
            data[5] = (byte) (w >> 16);
            data[6] = (byte) (w >> 8);
            data[7] = (byte) w;

            int h = videoSize.getHeight();
            data[8] = (byte) (h >> 24);
            data[9] = (byte) (h >> 16);
            data[10] = (byte) (h >> 8);
            data[11] = (byte) h;

            sendChunked(data, 0, 12);
        }
    }

    @Override
    public void writeDisableStream(boolean error) throws IOException {
        byte[] data = new byte[4];
        if (error) {
            data[3] = 1;
        }
        sendChunked(data, 0, 4);
    }

    @Override
    public void writePacket(ByteBuffer buffer, long pts, boolean config, boolean keyFrame)
            throws IOException {

        int dataSize = buffer.remaining();

        // 帧大小检查
        int headerSize = sendFrameMeta ? 12 : 0;
        int packetSize = headerSize + dataSize;
        if (packetSize > frameBuffer.length) {
            Ln.w("UdpVideoSender: frame too large (" + packetSize + " > " + frameBuffer.length + "), dropped");
            return; // 超大帧丢弃
        }

        // 构建帧数据: [PTS+flags(8)][dataSize(4)][data(N)]
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
            // PTS+flags (big-endian)
            frameBuffer[0] = (byte) (ptsAndFlags >> 56);
            frameBuffer[1] = (byte) (ptsAndFlags >> 48);
            frameBuffer[2] = (byte) (ptsAndFlags >> 40);
            frameBuffer[3] = (byte) (ptsAndFlags >> 32);
            frameBuffer[4] = (byte) (ptsAndFlags >> 24);
            frameBuffer[5] = (byte) (ptsAndFlags >> 16);
            frameBuffer[6] = (byte) (ptsAndFlags >> 8);
            frameBuffer[7] = (byte) ptsAndFlags;
            // dataSize (big-endian)
            frameBuffer[8] = (byte) (dataSize >> 24);
            frameBuffer[9] = (byte) (dataSize >> 16);
            frameBuffer[10] = (byte) (dataSize >> 8);
            frameBuffer[11] = (byte) dataSize;
            offset = 12;
        }

        // 从 codec buffer 拷贝到 frameBuffer（仅一次拷贝）
        buffer.get(frameBuffer, offset, dataSize);
        int totalSize = offset + dataSize;

        // 分片发送
        sendChunked(frameBuffer, 0, totalSize);
    }

    @Override
    public void writePacket(ByteBuffer codecBuffer, MediaCodec.BufferInfo bufferInfo)
            throws IOException {
        long pts = bufferInfo.presentationTimeUs;
        boolean config = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0;
        boolean keyFrame = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0;
        writePacket(codecBuffer, pts, config, keyFrame);
    }

    /**
     * 将数据分片为 MTU 大小的 UDP 包发送
     *
     * 每个包格式: [seq(4B, big-endian)] [flags(1B)] [payload(最多 1395B)]
     *   - 首包: flags |= SOF (0x01)
     *   - 尾包: flags |= EOF (0x02)
     *   - 单包: flags = SOF|EOF (0x03)
     *
     * 客户端按 SOF→EOF 重组完整帧，中间有丢包则整帧丢弃。
     */
    private void sendChunked(byte[] data, int off, int len) throws IOException {
        int pos = off;
        int remaining = len;
        boolean first = true;

        while (remaining > 0) {
            int chunkSize = Math.min(remaining, MAX_PAYLOAD);
            boolean last = (remaining - chunkSize == 0);

            // 序号头 (big-endian uint32)
            sendBuffer[0] = (byte) (seq >> 24);
            sendBuffer[1] = (byte) (seq >> 16);
            sendBuffer[2] = (byte) (seq >> 8);
            sendBuffer[3] = (byte) seq;
            seq++;

            // 帧边界标志
            byte flags = 0;
            if (first) flags |= FLAG_SOF;
            if (last)  flags |= FLAG_EOF;
            sendBuffer[4] = flags;

            // 拷贝 payload
            System.arraycopy(data, pos, sendBuffer, SEQ_HEADER_SIZE, chunkSize);

            // 发送
            DatagramPacket packet = new DatagramPacket(
                    sendBuffer, SEQ_HEADER_SIZE + chunkSize, target);
            socket.send(packet);

            totalPackets++;
            totalBytes += SEQ_HEADER_SIZE + chunkSize;
            pos += chunkSize;
            remaining -= chunkSize;
            first = false;
        }
    }

    public String getStats() {
        return String.format("packets=%d, bytes=%d, seq=%d",
                totalPackets, totalBytes, seq);
    }

    public void close() {
        Ln.i("UdpVideoSender closing: " + getStats());
        if (socket != null && !socket.isClosed()) {
            socket.close();
        }
    }
}
