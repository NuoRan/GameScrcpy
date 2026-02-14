package com.genymobile.scrcpy.kcp;

import com.genymobile.scrcpy.control.ControlMessage;
import com.genymobile.scrcpy.control.ControlMessageReader;
import com.genymobile.scrcpy.control.DeviceMessage;
import com.genymobile.scrcpy.control.DeviceMessageWriter;
import com.genymobile.scrcpy.control.IControlChannel;
import com.genymobile.scrcpy.util.Ln;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.SocketException;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * KcpControlChannel - 基于KCP的控制通道 - 全新重构
 *
 * 特点:
 * - 双向通信
 * - 低延迟
 * - 消息边界保留
 * - 无超时断开（支持用户切换窗口后恢复）
 */
public final class KcpControlChannel implements IControlChannel, KcpTransport.Listener {

    // [极致低延迟优化] 缩短轮询超时，加速通道关闭检测
    // 不影响数据延迟（BlockingQueue.poll 有数据时立即返回）
    private static final int POLL_INTERVAL_MS = 50;

    private final KcpTransport transport;
    private final ControlMessageReader reader;
    private final DeviceMessageWriter writer;

    // 接收队列
    private final BlockingQueue<byte[]> receiveQueue = new LinkedBlockingQueue<>();

    // 流适配器
    private final QueueInputStream inputStream;
    private final KcpOutputStream outputStream;

    /**
     * 创建控制通道 (服务端模式)
     */
    public KcpControlChannel(String clientIp, int port) throws IOException {
        transport = new KcpTransport(KcpTransport.CONV_CONTROL);
        transport.setListener(this);

        // 控制通道使用消息模式（保持消息边界）
        transport.setStreamMode(0);
        // 控制通道使用较小窗口
        transport.setWindowSize(64, 64);
        // P-KCP: 控制通道极致低延迟 — minRTO=1ms
        transport.setMinRto(1);

        // 绑定端口
        try {
            transport.bind(port);
        } catch (SocketException e) {
            throw new IOException("Failed to bind control port " + port, e);
        }

        // 设置远端地址
        transport.setRemoteAddress(clientIp, port);
        transport.start();

        // 创建流适配器
        inputStream = new QueueInputStream(receiveQueue);
        outputStream = new KcpOutputStream();

        // 创建消息读写器
        reader = new ControlMessageReader(inputStream);
        writer = new DeviceMessageWriter(outputStream);

        Ln.i("KcpControlChannel: waiting on port " + port);
    }

    @Override
    public ControlMessage recv() throws IOException {
        return reader.read();
    }

    @Override
    public void send(DeviceMessage msg) throws IOException {
        writer.write(msg);
    }

    @Override
    public void close() {
        // 先标记输入流关闭，让阻塞的读取操作退出
        inputStream.setClosed();
        transport.close();
    }

    // =========================================================================
    // KcpTransport.Listener 实现
    // =========================================================================

    @Override
    public void onReceive(byte[] data, int offset, int len) {
        byte[] copy = new byte[len];
        System.arraycopy(data, offset, copy, 0, len);
        receiveQueue.offer(copy);
    }

    @Override
    public void onError(Exception e) {
        Ln.e("KCP control error: " + e.getMessage());
    }

    // =========================================================================
    // 输入流适配器
    // =========================================================================

    private final class QueueInputStream extends InputStream {
        private final BlockingQueue<byte[]> queue;
        private byte[] currentBuffer = null;
        private int currentPos = 0;
        private volatile boolean closed = false;

        QueueInputStream(BlockingQueue<byte[]> queue) {
            this.queue = queue;
        }

        void setClosed() {
            closed = true;
        }

        @Override
        public int read() throws IOException {
            byte[] buf = new byte[1];
            int n = read(buf, 0, 1);
            if (n < 0)
                return -1;
            return buf[0] & 0xFF;
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (len == 0)
                return 0;

            // 确保有数据 - 无限等待直到有数据或通道关闭
            if (currentBuffer == null || currentPos >= currentBuffer.length) {
                while (!closed) {
                    try {
                        // 短轮询，定期检查是否关闭
                        currentBuffer = queue.poll(POLL_INTERVAL_MS, TimeUnit.MILLISECONDS);
                        if (currentBuffer != null) {
                            currentPos = 0;
                            break;
                        }
                        // 没有数据，继续等待（不断开连接）
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        throw new IOException("Interrupted");
                    }
                }
                // 如果关闭了且没有数据，返回EOF
                if (closed && currentBuffer == null) {
                    return -1;
                }
            }

            // 读取
            int available = currentBuffer.length - currentPos;
            int toRead = Math.min(available, len);
            System.arraycopy(currentBuffer, currentPos, b, off, toRead);
            currentPos += toRead;
            return toRead;
        }

        @Override
        public int available() {
            if (currentBuffer != null && currentPos < currentBuffer.length) {
                return currentBuffer.length - currentPos;
            }
            byte[] peek = queue.peek();
            return peek != null ? peek.length : 0;
        }
    }

    // =========================================================================
    // 输出流适配器
    // =========================================================================

    private final class KcpOutputStream extends OutputStream {
        @Override
        public void write(int b) throws IOException {
            write(new byte[] { (byte) b }, 0, 1);
        }

        @Override
        public void write(byte[] b, int off, int len) throws IOException {
            // P-KCP: 使用 sendImmediate 直接 kcp.send()+flush()，绕过排队延迟
            int ret = transport.sendImmediate(b, off, len);
            if (ret < 0) {
                throw new IOException("KCP send failed: " + ret);
            }
        }
    }
}
