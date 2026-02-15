package com.genymobile.scrcpy.kcp;

import com.genymobile.scrcpy.util.Ln;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketException;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.locks.LockSupport;

/**
 * KcpTransport - KCP传输层 - 全新重构
 *
 * 封装UDP收发和KCP定时更新
 *
 * 使用示例 (参考 test.cpp 主循环):
 *
 * <pre>
 * KcpTransport transport = new KcpTransport(0x11223344);
 * transport.setListener(new KcpTransport.Listener() {
 *     public void onReceive(byte[] data, int len) {
 *         processData(data, len);
 *     }
 *
 *     public void onError(Exception e) {
 *         handleError(e);
 *     }
 * });
 *
 * // 连接
 * transport.connect("192.168.1.100", 27185);
 *
 * // 发送
 * transport.send(data);
 *
 * // 关闭
 * transport.close();
 * </pre>
 */
public final class KcpTransport implements KcpCore.Output {

    // MERGE: 使用统一配置
    public static final int CONV_VIDEO = KcpConfig.CONV_VIDEO;
    public static final int CONV_CONTROL = KcpConfig.CONV_CONTROL;

    // 常量
    private static final int DEFAULT_UPDATE_INTERVAL = KcpConfig.DEFAULT_INTERVAL;
    private static final int UDP_BUFFER_SIZE = KcpConfig.DEFAULT_MTU + 100;
    private static final int SOCKET_RECV_BUFFER = KcpConfig.SOCKET_RECV_BUFFER;
    private static final int SOCKET_SEND_BUFFER = KcpConfig.SOCKET_SEND_BUFFER;

    /**
     * 监听器接口
     */
    public interface Listener {
        void onReceive(byte[] data, int offset, int len);

        void onError(Exception e);
    }

    // KCP核心
    private final KcpCore kcp;
    private final int conv;

    // 网络
    private DatagramSocket socket;
    private InetSocketAddress remoteAddress;

    // 状态
    private final AtomicBoolean running = new AtomicBoolean(false);
    private Listener listener;

    // 线程
    private Thread updateThread;
    private Thread recvThread;

    // 发送唤醒标志
    private volatile boolean sendPending = false;

    // 无锁发送队列：发送线程投递数据，updateLoop 线程统一消费
    // 消除 send() 的 synchronized 锁竞争
    private final ConcurrentLinkedQueue<byte[]> pendingSendQueue = new ConcurrentLinkedQueue<>();

    // 缓冲区
    private final byte[] recvBuffer = new byte[UDP_BUFFER_SIZE];
    private final byte[] dataBuffer = new byte[64 * 1024]; // 64KB for assembled data
    private final DatagramPacket sendPacket = new DatagramPacket(new byte[0], 0); // 复用发送包

    // 统计
    private volatile long bytesSent = 0;
    private volatile long bytesRecv = 0;

    // FEC 前向纠错
    private volatile boolean fecEnabled = false;
    private FecCodec.FecEncoder fecEncoder;
    private FecCodec.FecDecoder fecDecoder;

    /**
     * 创建传输层
     *
     * @param conv 会话ID
     */
    public KcpTransport(int conv) {
        this.conv = conv;
        this.kcp = new KcpCore(conv, this);

        // 默认快速模式 (参考 test.cpp mode=2)
        kcp.setFastMode();
    }

    /**
     * 设置监听器
     */
    public void setListener(Listener listener) {
        this.listener = listener;
    }

    // =========================================================================
    // 配置
    // =========================================================================

    /**
     * 快速模式 (游戏/投屏)
     */
    public void setFastMode() {
        kcp.setFastMode();
    }

    /**
     * 普通模式
     */
    public void setNormalMode() {
        kcp.setNormalMode();
    }

    /**
     * 设置窗口大小
     */
    public void setWindowSize(int sndwnd, int rcvwnd) {
        kcp.setWindowSize(sndwnd, rcvwnd);
    }

    /**
     * 设置MTU
     */
    public void setMtu(int mtu) {
        kcp.setMtu(mtu);
    }

    /**
     * 设置nodelay参数
     */
    public void setNoDelay(int nodelay, int interval, int resend, int nc) {
        kcp.setNoDelay(nodelay, interval, resend, nc);
    }

    /**
     * 设置最小RTO
     */
    public void setMinRto(int minrto) {
        kcp.setMinRto(minrto);
    }

    /**
     * 设置流模式
     *
     * @param stream 0=消息模式(保持消息边界), 1=流模式(合并数据)
     */
    public void setStreamMode(int stream) {
        kcp.setStream(stream);
    }

    /**
     * 设置死链阈值
     */
    public void setDeadLink(int deadlink) {
        kcp.setDeadLink(deadlink);
    }

    /**
     * 启用/禁用 FEC 前向纠错 (XOR 冗余，允许丢 1 包恢复)
     */
    public void setFecEnabled(boolean enabled, int groupSize) {
        this.fecEnabled = enabled;
        if (enabled) {
            fecEncoder = new FecCodec.FecEncoder(groupSize);
            fecDecoder = new FecCodec.FecDecoder();
            Ln.i("KCP FEC enabled: groupSize=" + groupSize);
        } else {
            fecEncoder = null;
            fecDecoder = null;
            Ln.i("KCP FEC disabled");
        }
    }

    public void setFecEnabled(boolean enabled) {
        setFecEnabled(enabled, 10);
    }

    // =========================================================================
    // 连接管理
    // =========================================================================

    /**
     * 绑定本地端口 (服务端模式)
     */
    public void bind(int port) throws SocketException {
        socket = new DatagramSocket(port);
        configureSocket(socket);
        Ln.i("KCP transport bound to port " + port);
    }

    /**
     * 连接到远端 (客户端模式)
     */
    public boolean connect(String host, int port) {
        try {
            remoteAddress = new InetSocketAddress(host, port);
            socket = new DatagramSocket();
            configureSocket(socket);
            start();
            Ln.i("KCP transport connected to " + host + ":" + port);
            return true;
        } catch (SocketException e) {
            Ln.e("KCP connect failed: " + e.getMessage());
            return false;
        }
    }

    /**
     * 设置远端地址 (用于bind模式)
     */
    public void setRemoteAddress(String host, int port) {
        this.remoteAddress = new InetSocketAddress(host, port);
    }

    /**
     * 配置Socket
     */
    private void configureSocket(DatagramSocket sock) throws SocketException {
        sock.setReceiveBufferSize(SOCKET_RECV_BUFFER);
        sock.setSendBufferSize(SOCKET_SEND_BUFFER);
    }

    /**
     * 启动传输
     */
    public void start() {
        if (running.getAndSet(true)) {
            return;
        }

        // 更新线程 (参考 test.cpp: 主循环中的 ikcp_update)
        updateThread = new Thread(this::updateLoop, "KCP-Update-" + conv);
        updateThread.setPriority(Thread.MAX_PRIORITY);  // KCP 线程最高优先级
        updateThread.start();

        // 接收线程 (参考 test.cpp: vnet->recv)
        recvThread = new Thread(this::receiveLoop, "KCP-Recv-" + conv);
        recvThread.setPriority(Thread.MAX_PRIORITY);
        recvThread.start();
    }

    /**
     * 关闭传输
     */
    public void close() {
        running.set(false);

        if (socket != null) {
            socket.close();
        }

        if (updateThread != null) {
            updateThread.interrupt();
            try {
                updateThread.join(100);
            } catch (InterruptedException ignored) {
            }
        }

        if (recvThread != null) {
            recvThread.interrupt();
            try {
                recvThread.join(100);
            } catch (InterruptedException ignored) {
            }
        }

        Ln.i("KCP transport closed: sent=" + bytesSent + ", recv=" + bytesRecv);
    }

    // =========================================================================
    // 数据传输
    // =========================================================================

    /**
     * 发送数据 (可靠传输)
     */
    public int send(byte[] data) {
        return send(data, 0, data.length);
    }

    /**
     * 发送数据 (可靠传输) - 无锁，通过 ConcurrentLinkedQueue 将数据排入队列
     * 由 updateLoop 统一消费，避免发送线程被 flush() 的长锁阻塞
     */
    public int send(byte[] data, int offset, int len) {
        byte[] copy = new byte[len];
        System.arraycopy(data, offset, copy, 0, len);
        pendingSendQueue.offer(copy);
        // 通知 updateLoop 尽快 flush，实现低延迟
        sendPending = true;
        Thread t = updateThread;
        if (t != null) {
            LockSupport.unpark(t);
        }
        return 0;
    }

    /**
     * P-KCP: 立即发送数据 — 绕过队列，直接 kcp.send() + flush()
     * 用于控制通道等对延迟极其敏感的场景，避免排队+线程调度延迟
     */
    public int sendImmediate(byte[] data, int offset, int len) {
        synchronized (this) {
            int ret = kcp.send(data, offset, len);
            if (ret >= 0) {
                kcp.flush();
            }
            return ret;
        }
    }

    /**
     * 获取待发送数量
     */
    public int pending() {
        return kcp.waitSnd();
    }

    /**
     * 获取状态
     */
    public int getState() {
        return kcp.getState();
    }

    /**
     * 是否死链
     */
    public boolean isDeadLink() {
        return kcp.getState() == -1;
    }

    // =========================================================================
    // KcpCore.Output 实现 - UDP输出回调
    // =========================================================================

    @Override
    public int send(byte[] data, int offset, int len, KcpCore kcp) {
        // 参考 test.cpp: udp_output
        if (socket == null || remoteAddress == null) {
            return -1;
        }

        try {
            // FEC 编码
            if (fecEnabled && fecEncoder != null) {
                fecEncoder.encode(data, offset, len, (fecData, fecOffset, fecLen) -> {
                    try {
                        DatagramPacket pkt = new DatagramPacket(fecData, fecOffset, fecLen, remoteAddress);
                        socket.send(pkt);
                        bytesSent += fecLen;
                    } catch (IOException e) {
                        Ln.e("FEC UDP send error: " + e.getMessage());
                    }
                });
                return len;
            }

            // 复用 DatagramPacket 对象，避免每次发送都创建新对象
            sendPacket.setData(data, offset, len);
            sendPacket.setSocketAddress(remoteAddress);
            socket.send(sendPacket);
            bytesSent += len;
            return len;
        } catch (IOException e) {
            Ln.e("UDP send error: " + e.getMessage());
            return -1;
        }
    }

    // =========================================================================
    // 内部线程
    // =========================================================================

    /**
     * 更新循环 (参考 test.cpp 主循环)
     * P-06: 使用 LockSupport.parkNanos 替代 Thread.sleep (更精确)
     */
    private void updateLoop() {
        while (running.get()) {
            long now = System.currentTimeMillis();
            long nextTime;

            synchronized (this) {
                // 先消费无锁发送队列 (不需要额外加锁即可 drain)
                byte[] d;
                while ((d = pendingSendQueue.poll()) != null) {
                    int ret = kcp.send(d);
                    if (ret < 0) {
                        Ln.w("KCP send failed: " + ret);
                    }
                }
                // 参考 test.cpp: ikcp_check + ikcp_update
                nextTime = kcp.check(now);
                if (now >= nextTime || sendPending) {
                    sendPending = false;
                    kcp.update(now);
                }
            }

            long sleepTime = Math.min(nextTime - now, DEFAULT_UPDATE_INTERVAL);
            if (sleepTime > 0) {
                // P-06: LockSupport.parkNanos 比 Thread.sleep 更精确
                // 不会抛出 InterruptedException，通过 running 标志检查中断
                LockSupport.parkNanos(sleepTime * 1_000_000L);
            } else {
                // 避免忙等待(Thread.yield)导致 CPU 100%，至少休眠 1ms
                LockSupport.parkNanos(1_000_000L);
            }

            // 检查中断状态
            if (Thread.interrupted()) {
                break;
            }
        }
    }

    /**
     * 接收循环 (参考 test.cpp: vnet->recv)
     */
    private void receiveLoop() {
        DatagramPacket packet = new DatagramPacket(recvBuffer, recvBuffer.length);

        while (running.get()) {
            try {
                socket.receive(packet);
                bytesRecv += packet.getLength();

                // 仅在远端地址未设置时获取，避免每包分配 InetSocketAddress 对象
                if (remoteAddress == null) {
                    remoteAddress = (InetSocketAddress) packet.getSocketAddress();
                }

                synchronized (this) {
                    // FEC 解码
                    if (fecEnabled && fecDecoder != null) {
                        fecDecoder.decode(packet.getData(), packet.getOffset(), packet.getLength(),
                            (fecData, fecOffset, fecLen) -> {
                                kcp.input(fecData, fecOffset, fecLen);
                            });
                    } else {
                        kcp.input(packet.getData(), packet.getOffset(), packet.getLength());
                    }
                    // P-KCP: 收到数据后立即 update/flush，确保 ACK 即时发出
                    // 避免等待 updateLoop 下次唤醒（最坏延迟 DEFAULT_UPDATE_INTERVAL ms）
                    kcp.update(System.currentTimeMillis());
                    processReceivedData();
                }

            } catch (IOException e) {
                if (running.get() && listener != null) {
                    listener.onError(e);
                }
                break;
            }
        }
    }

    private void processReceivedData() {
        int size = kcp.peekSize();
        while (size > 0) {
            if (size > dataBuffer.length) {
                byte[] buf = new byte[size];
                int recv = kcp.recv(buf);
                if (recv > 0 && listener != null) {
                    listener.onReceive(buf, 0, recv);
                }
            } else {
                int recv = kcp.recv(dataBuffer);
                if (recv > 0 && listener != null) {
                    listener.onReceive(dataBuffer, 0, recv);
                }
            }
            size = kcp.peekSize();
        }
    }
}
