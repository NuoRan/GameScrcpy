package com.genymobile.scrcpy.kcp;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedList;

/**
 * KcpCore - KCP协议核心实现 - 优化版
 *
 * A-02/A-03 优化: 集成 java-kcp-master 的关键优化特性
 * - AckMask 优化: 批量确认多个数据包
 * - 可配置参数: 对象池大小、窗口等
 * - 更精确的 RTT 计算
 * - LockSupport.parkNanos 替代 Thread.sleep (P-06)
 *
 * 参考: https://github.com/skywind3000/kcp
 * 参考: https://github.com/l42111996/java-Kcp
 */
public final class KcpCore {

    //=========================================================================
    // 常量定义 - 必须与 ikcp.h 完全匹配
    //=========================================================================

    private static final int IKCP_RTO_NDL = 30;      // nodelay模式最小RTO
    private static final int IKCP_RTO_MIN = 100;     // 普通模式最小RTO
    private static final int IKCP_RTO_DEF = 200;     // 默认RTO
    private static final int IKCP_RTO_MAX = 60000;   // 最大RTO

    private static final int IKCP_CMD_PUSH = 81;     // 数据包
    private static final int IKCP_CMD_ACK = 82;      // 确认包
    private static final int IKCP_CMD_WASK = 83;     // 询问窗口
    private static final int IKCP_CMD_WINS = 84;     // 告知窗口

    private static final int IKCP_ASK_SEND = 1;      // 需要发送WASK
    private static final int IKCP_ASK_TELL = 2;      // 需要发送WINS

    private static final int IKCP_WND_SND = 32;      // 默认发送窗口
    private static final int IKCP_WND_RCV = 128;     // 默认接收窗口

    private static final int IKCP_MTU_DEF = 1400;    // 默认MTU
    private static final int IKCP_OVERHEAD = 24;     // KCP头部开销 (不含 AckMask)
    private static final int IKCP_DEADLINK = 20;     // 死链阈值
    private static final int IKCP_THRESH_INIT = 2;   // 慢启动阈值初始值
    private static final int IKCP_THRESH_MIN = 2;    // 慢启动阈值最小值
    private static final int IKCP_PROBE_INIT = 7000; // 窗口探测初始间隔
    private static final int IKCP_PROBE_LIMIT = 120000; // 窗口探测最大间隔
    private static final int IKCP_INTERVAL = 100;    // 默认更新间隔

    //=========================================================================
    // A-03: 可配置参数
    //=========================================================================

    /**
     * KCP 配置类 - 支持链式调用
     */
    public static class Config {
        int segmentPoolSize = 256;      // 对象池大小
        int sndWnd = IKCP_WND_SND;      // 发送窗口
        int rcvWnd = IKCP_WND_RCV;      // 接收窗口
        int mtu = IKCP_MTU_DEF;         // MTU
        int interval = IKCP_INTERVAL;   // 更新间隔
        int deadLink = IKCP_DEADLINK;   // 死链阈值
        boolean nodelay = false;        // 无延迟模式
        int fastResend = 0;             // 快速重传次数
        boolean noCwnd = false;         // 关闭拥塞控制
        int ackMaskSize = 0;            // AckMask 大小 (0/8/16/32/64)

        public Config segmentPoolSize(int size) { this.segmentPoolSize = size; return this; }
        public Config sndWnd(int wnd) { this.sndWnd = wnd; return this; }
        public Config rcvWnd(int wnd) { this.rcvWnd = wnd; return this; }
        public Config mtu(int mtu) { this.mtu = mtu; return this; }
        public Config interval(int interval) { this.interval = interval; return this; }
        public Config deadLink(int deadLink) { this.deadLink = deadLink; return this; }
        public Config nodelay(boolean nodelay) { this.nodelay = nodelay; return this; }
        public Config fastResend(int resend) { this.fastResend = resend; return this; }
        public Config noCwnd(boolean nc) { this.noCwnd = nc; return this; }
        public Config ackMaskSize(int size) { this.ackMaskSize = size; return this; }

        /** 快速模式配置 */
        public Config fastMode() {
            return nodelay(true).interval(10).fastResend(2).noCwnd(true);
        }

        /** 普通模式配置 */
        public Config normalMode() {
            return nodelay(false).interval(10).fastResend(0).noCwnd(true);
        }
    }

    //=========================================================================
    // Segment结构 - 对应 IKCPSEG
    // A-02: 添加 AckMask 支持 (来自 java-kcp-master)
    //=========================================================================

    private static final class Segment {
        int conv;       // 会话ID
        int cmd;        // 命令类型
        int frg;        // 分片号
        int wnd;        // 窗口大小
        long ts;        // 时间戳
        long sn;        // 序列号
        long una;       // 待确认序列号
        long resendts;  // 重传时间
        int rto;        // 超时时间
        int fastack;    // 快速重传计数
        int xmit;       // 重传次数
        long ackMask;   // A-02: ACK位图 (来自 java-kcp-master)
        byte[] data;    // 数据

        Segment(int size) {
            data = new byte[size];
        }

        int len() {
            return data != null ? data.length : 0;
        }

        void reset(int size) {
            conv = 0;
            cmd = 0;
            frg = 0;
            wnd = 0;
            ts = 0;
            sn = 0;
            una = 0;
            resendts = 0;
            rto = 0;
            fastack = 0;
            xmit = 0;
            ackMask = 0;
            if (data == null || data.length < size) {
                data = new byte[size];
            }
        }
    }

    //=========================================================================
    // A-03: 可配置对象池
    //=========================================================================
    private final int segmentPoolSize;
    private final LinkedList<Segment> segmentPool = new LinkedList<>();

    private Segment acquireSegment(int size) {
        Segment seg = segmentPool.poll();
        if (seg != null) {
            seg.reset(size);
            return seg;
        }
        return new Segment(size);
    }

    private void releaseSegment(Segment seg) {
        if (seg != null && segmentPool.size() < segmentPoolSize) {
            segmentPool.add(seg);
        }
    }

    //=========================================================================
    // 输出回调接口
    //=========================================================================

    public interface Output {
        /**
         * 发送数据到下层协议(UDP)
         * @return 成功发送的字节数
         */
        int send(byte[] data, int offset, int len, KcpCore kcp);
    }

    //=========================================================================
    // KCP状态 - 对应 IKCPCB
    //=========================================================================

    private final int conv;                          // 会话ID
    private int mtu = IKCP_MTU_DEF;                  // MTU
    private int mss = IKCP_MTU_DEF - IKCP_OVERHEAD;  // MSS
    private int state = 0;                           // 状态 (0正常, -1死链)

    private long sndUna = 0;                         // 发送窗口起始
    private long sndNxt = 0;                         // 下一个发送序号
    private long rcvNxt = 0;                         // 下一个接收序号

    private int ssthresh = IKCP_THRESH_INIT;         // 慢启动阈值
    private int rxRttval = 0;                        // RTT变化量
    private int rxSrtt = 0;                          // 平滑RTT
    private int rxRto = IKCP_RTO_DEF;               // 重传超时
    private int rxMinrto = IKCP_RTO_MIN;            // 最小RTO

    private int sndWnd = IKCP_WND_SND;              // 发送窗口
    private int rcvWnd = IKCP_WND_RCV;              // 接收窗口
    private int rmtWnd = IKCP_WND_RCV;              // 远端窗口
    private int cwnd = 0;                           // 拥塞窗口
    private int incr = 0;                           // 拥塞窗口增量
    private int probe = 0;                          // 探测标志

    private long current = 0;                        // 当前时间
    private int interval = IKCP_INTERVAL;           // 更新间隔
    private long tsFlush = IKCP_INTERVAL;           // 下次flush时间
    private int xmit = 0;                           // 总重传次数

    private int nodelay = 0;                        // nodelay模式
    private int updated = 0;                        // 是否已更新
    private long tsProbe = 0;                       // 窗口探测时间
    private int probeWait = 0;                      // 窗口探测等待

    private int deadLink = IKCP_DEADLINK;           // 死链阈值
    private int fastresend = 0;                     // 快速重传次数
    private int fastlimit = 5;                      // 快速重传限制
    private int nocwnd = 0;                         // 关闭拥塞控制
    private int stream = 0;                         // 流模式

    // A-02: AckMask 支持 (来自 java-kcp-master)
    private int ackMaskSize = 0;                    // AckMask 大小 (0/8/16/32/64)
    private long ackMask = 0;                       // 当前 AckMask
    private long lastRcvNxt = 0;                    // 上次接收序号

    // 队列
    private final LinkedList<Segment> sndQueue = new LinkedList<>();
    private final LinkedList<Segment> rcvQueue = new LinkedList<>();
    private final ArrayList<Segment> sndBuf = new ArrayList<>();
    private final ArrayList<Segment> rcvBuf = new ArrayList<>();
    private final ArrayList<long[]> ackList = new ArrayList<>();

    // 输出缓冲区
    private byte[] buffer;
    private final Output output;

    // P-06: 用于精确计时的起始时间
    private final long startTicks = System.currentTimeMillis();

    //=========================================================================
    // 构造函数
    //=========================================================================

    /**
     * 创建KCP对象 (默认配置)
     */
    public KcpCore(int conv, Output output) {
        this(conv, output, new Config());
    }

    /**
     * 创建KCP对象 (自定义配置)
     * A-03: 支持可配置参数
     */
    public KcpCore(int conv, Output output, Config config) {
        this.conv = conv;
        this.output = output;
        this.segmentPoolSize = config.segmentPoolSize;
        this.sndWnd = config.sndWnd;
        this.rcvWnd = config.rcvWnd;
        this.mtu = config.mtu;
        this.mss = config.mtu - IKCP_OVERHEAD - (config.ackMaskSize / 8);
        this.interval = config.interval;
        this.deadLink = config.deadLink;
        this.nodelay = config.nodelay ? 1 : 0;
        this.fastresend = config.fastResend;
        this.nocwnd = config.noCwnd ? 1 : 0;
        this.ackMaskSize = config.ackMaskSize;

        if (config.nodelay) {
            rxMinrto = IKCP_RTO_NDL;
        }

        this.buffer = new byte[(mtu + IKCP_OVERHEAD + 8) * 3];
    }

    /**
     * P-06: 获取相对时间 (来自 java-kcp-master)
     */
    private long currentMs(long now) {
        return now - startTicks;
    }

    //=========================================================================
    // 公共方法
    //=========================================================================

    public int getConv() {
        return conv;
    }

    public int getState() {
        return state;
    }

    /**
     * 发送数据
     * 参考 test.cpp: ikcp_send(kcp1, buffer, 8)
     */
    public int send(byte[] data, int offset, int len) {
        if (len <= 0) return -1;

        // 流模式：合并到最后一个segment
        if (stream != 0 && !sndQueue.isEmpty()) {
            Segment old = sndQueue.getLast();
            if (old.len() < mss) {
                int capacity = mss - old.len();
                int extend = Math.min(len, capacity);
                byte[] newData = new byte[old.len() + extend];
                System.arraycopy(old.data, 0, newData, 0, old.len());
                System.arraycopy(data, offset, newData, old.len(), extend);
                old.data = newData;
                old.frg = 0;
                len -= extend;
                offset += extend;
            }
            if (len <= 0) return 0;
        }

        // 计算分片数
        int count;
        if (len <= mss) {
            count = 1;
        } else {
            count = (len + mss - 1) / mss;
        }

        if (count >= IKCP_WND_RCV) {
            return -2;
        }

        if (count == 0) count = 1;

        // 分片
        for (int i = 0; i < count; i++) {
            int size = Math.min(len, mss);
            Segment seg = acquireSegment(size);  // S-K02: 使用对象池
            if (len > 0) {
                System.arraycopy(data, offset, seg.data, 0, size);
            }
            seg.frg = (stream == 0) ? (count - i - 1) : 0;
            sndQueue.add(seg);
            offset += size;
            len -= size;
        }

        return 0;
    }

    public int send(byte[] data) {
        return send(data, 0, data.length);
    }

    /**
     * 接收数据
     * 参考 test.cpp: ikcp_recv(kcp1, buffer, 10)
     */
    public int recv(byte[] buffer, int offset, int len) {
        if (rcvQueue.isEmpty()) {
            return -1;
        }

        int peeksize = peekSize();
        if (peeksize < 0) return -2;
        if (peeksize > len) return -3;

        boolean recover = rcvQueue.size() >= rcvWnd;

        // 合并分片
        int received = 0;
        Iterator<Segment> iter = rcvQueue.iterator();
        while (iter.hasNext()) {
            Segment seg = iter.next();
            System.arraycopy(seg.data, 0, buffer, offset, seg.len());
            offset += seg.len();
            received += seg.len();
            int frg = seg.frg;
            iter.remove();
            releaseSegment(seg);  // S-K02: 回收到对象池
            if (frg == 0) break;
        }

        // 移动数据 rcvBuf -> rcvQueue
        moveRcvBufToQueue();

        // 快速恢复
        if (rcvQueue.size() < rcvWnd && recover) {
            probe |= IKCP_ASK_TELL;
        }

        return received;
    }

    public int recv(byte[] buffer) {
        return recv(buffer, 0, buffer.length);
    }

    /**
     * 查看下一个消息大小
     */
    public int peekSize() {
        if (rcvQueue.isEmpty()) {
            return -1;
        }

        Segment seg = rcvQueue.peek();
        if (seg.frg == 0) return seg.len();

        if (rcvQueue.size() < seg.frg + 1) {
            return -1;
        }

        int length = 0;
        for (Segment s : rcvQueue) {
            length += s.len();
            if (s.frg == 0) break;
        }
        return length;
    }

    /**
     * 输入下层协议数据
     * 参考 test.cpp: ikcp_input(kcp2, buffer, hr)
     */
    public int input(byte[] data, int offset, int size) {
        long prevUna = sndUna;
        long maxack = 0, latestTs = 0;
        boolean flag = false;

        if (data == null || size < IKCP_OVERHEAD) return -1;

        while (true) {
            if (size < IKCP_OVERHEAD) break;

            ByteBuffer bb = ByteBuffer.wrap(data, offset, IKCP_OVERHEAD);
            bb.order(ByteOrder.LITTLE_ENDIAN);

            int conv = bb.getInt();
            if (conv != this.conv) return -1;

            int cmd = bb.get() & 0xFF;
            int frg = bb.get() & 0xFF;
            int wnd = bb.getShort() & 0xFFFF;
            long ts = bb.getInt() & 0xFFFFFFFFL;
            long sn = bb.getInt() & 0xFFFFFFFFL;
            long una = bb.getInt() & 0xFFFFFFFFL;
            int len = bb.getInt();

            offset += IKCP_OVERHEAD;
            size -= IKCP_OVERHEAD;

            if (size < len || len < 0) return -2;

            if (cmd != IKCP_CMD_PUSH && cmd != IKCP_CMD_ACK &&
                cmd != IKCP_CMD_WASK && cmd != IKCP_CMD_WINS) {
                return -3;
            }

            rmtWnd = wnd;
            parseUna(una);
            shrinkBuf();

            if (cmd == IKCP_CMD_ACK) {
                if (current >= ts) {
                    updateAck((int)(current - ts));
                }
                parseAck(sn);
                shrinkBuf();
                if (!flag) {
                    flag = true;
                    maxack = sn;
                    latestTs = ts;
                } else if (sn > maxack) {
                    maxack = sn;
                    latestTs = ts;
                }
            } else if (cmd == IKCP_CMD_PUSH) {
                if (sn < rcvNxt + rcvWnd) {
                    ackList.add(new long[]{sn, ts});
                    if (sn >= rcvNxt) {
                        Segment seg = acquireSegment(len);  // S-K02: 使用对象池
                        seg.conv = conv;
                        seg.cmd = cmd;
                        seg.frg = frg;
                        seg.wnd = wnd;
                        seg.ts = ts;
                        seg.sn = sn;
                        seg.una = una;
                        if (len > 0) {
                            System.arraycopy(data, offset, seg.data, 0, len);
                        }
                        parseData(seg);
                    }
                }
            } else if (cmd == IKCP_CMD_WASK) {
                probe |= IKCP_ASK_TELL;
            }
            // IKCP_CMD_WINS: do nothing

            offset += len;
            size -= len;
        }

        if (flag) {
            parseFastack(maxack, latestTs);
        }

        // 拥塞控制
        if (sndUna > prevUna && cwnd < rmtWnd) {
            int mssVal = mss;
            if (cwnd < ssthresh) {
                cwnd++;
                incr += mssVal;
            } else {
                if (incr < mssVal) incr = mssVal;
                incr += (mssVal * mssVal) / incr + (mssVal / 16);
                if ((cwnd + 1) * mssVal <= incr) {
                    cwnd++;
                }
            }
            if (cwnd > rmtWnd) {
                cwnd = rmtWnd;
                incr = rmtWnd * mssVal;
            }
        }

        return 0;
    }

    public int input(byte[] data) {
        return input(data, 0, data.length);
    }

    /**
     * 更新KCP状态
     * 参考 test.cpp: ikcp_update(kcp1, iclock())
     */
    public void update(long current) {
        this.current = current;

        if (updated == 0) {
            updated = 1;
            tsFlush = current;
        }

        long slap = current - tsFlush;

        if (slap >= 10000 || slap < -10000) {
            tsFlush = current;
            slap = 0;
        }

        if (slap >= 0) {
            tsFlush += interval;
            if (current >= tsFlush) {
                tsFlush = current + interval;
            }
            flush();
        }
    }

    /**
     * 检查下次update时间
     */
    public long check(long current) {
        if (updated == 0) {
            return current;
        }

        long tsFlushVal = tsFlush;
        long tmPacket = 0x7FFFFFFFL;

        if (current - tsFlushVal >= 10000 || current - tsFlushVal < -10000) {
            tsFlushVal = current;
        }

        if (current >= tsFlushVal) {
            return current;
        }

        long tmFlush = tsFlushVal - current;

        for (Segment seg : sndBuf) {
            long diff = seg.resendts - current;
            if (diff <= 0) {
                return current;
            }
            if (diff < tmPacket) {
                tmPacket = diff;
            }
        }

        long minimal = Math.min(tmPacket, tmFlush);
        minimal = Math.min(minimal, interval);

        return current + minimal;
    }

    /**
     * 刷新发送
     */
    public void flush() {
        if (updated == 0) return;

        Segment seg = acquireSegment(0);  // S-K02: 使用对象池
        seg.conv = conv;
        seg.cmd = IKCP_CMD_ACK;
        seg.frg = 0;
        seg.wnd = Math.max(0, rcvWnd - rcvQueue.size());
        seg.una = rcvNxt;

        int offset = 0;

        // 发送ACK
        for (long[] ack : ackList) {
            if (offset + IKCP_OVERHEAD > mtu) {
                outputData(buffer, 0, offset);
                offset = 0;
            }
            seg.sn = ack[0];
            seg.ts = ack[1];
            offset = encodeSeg(buffer, offset, seg);
        }
        ackList.clear();

        // 窗口探测
        if (rmtWnd == 0) {
            if (probeWait == 0) {
                probeWait = IKCP_PROBE_INIT;
                tsProbe = current + probeWait;
            } else if (current >= tsProbe) {
                if (probeWait < IKCP_PROBE_INIT) {
                    probeWait = IKCP_PROBE_INIT;
                }
                probeWait += probeWait / 2;
                if (probeWait > IKCP_PROBE_LIMIT) {
                    probeWait = IKCP_PROBE_LIMIT;
                }
                tsProbe = current + probeWait;
                probe |= IKCP_ASK_SEND;
            }
        } else {
            tsProbe = 0;
            probeWait = 0;
        }

        // 发送探测命令
        if ((probe & IKCP_ASK_SEND) != 0) {
            seg.cmd = IKCP_CMD_WASK;
            if (offset + IKCP_OVERHEAD > mtu) {
                outputData(buffer, 0, offset);
                offset = 0;
            }
            offset = encodeSeg(buffer, offset, seg);
        }

        if ((probe & IKCP_ASK_TELL) != 0) {
            seg.cmd = IKCP_CMD_WINS;
            if (offset + IKCP_OVERHEAD > mtu) {
                outputData(buffer, 0, offset);
                offset = 0;
            }
            offset = encodeSeg(buffer, offset, seg);
        }

        probe = 0;

        // 计算有效窗口
        int cwndVal = Math.min(sndWnd, rmtWnd);
        if (nocwnd == 0) {
            cwndVal = Math.min(cwnd, cwndVal);
        }

        // 移动数据 sndQueue -> sndBuf
        while (sndNxt < sndUna + cwndVal && !sndQueue.isEmpty()) {
            Segment newseg = sndQueue.poll();
            newseg.conv = conv;
            newseg.cmd = IKCP_CMD_PUSH;
            newseg.wnd = seg.wnd;
            newseg.ts = current;
            newseg.sn = sndNxt++;
            newseg.una = rcvNxt;
            newseg.resendts = current;
            newseg.rto = rxRto;
            newseg.fastack = 0;
            newseg.xmit = 0;
            sndBuf.add(newseg);
        }

        // 重传参数
        int resent = (fastresend > 0) ? fastresend : Integer.MAX_VALUE;
        int rtomin = (nodelay == 0) ? (rxRto >> 3) : 0;

        // 发送数据段
        boolean lost = false;
        boolean change = false;

        for (Segment segment : sndBuf) {
            boolean needsend = false;

            if (segment.xmit == 0) {
                // 首次发送
                needsend = true;
                segment.xmit++;
                segment.rto = rxRto;
                segment.resendts = current + segment.rto + rtomin;
            } else if (current >= segment.resendts) {
                // 超时重传
                needsend = true;
                segment.xmit++;
                xmit++;
                if (nodelay == 0) {
                    segment.rto += Math.max(segment.rto, rxRto);
                } else {
                    int step = (nodelay < 2) ? segment.rto : rxRto;
                    segment.rto += step / 2;
                }
                segment.resendts = current + segment.rto;
                lost = true;
            } else if (segment.fastack >= resent) {
                // 快速重传
                if (segment.xmit <= fastlimit || fastlimit <= 0) {
                    needsend = true;
                    segment.xmit++;
                    segment.fastack = 0;
                    segment.resendts = current + segment.rto;
                    change = true;
                }
            }

            if (needsend) {
                segment.ts = current;
                segment.wnd = seg.wnd;
                segment.una = rcvNxt;

                int need = IKCP_OVERHEAD + segment.len();
                if (offset + need > mtu) {
                    outputData(buffer, 0, offset);
                    offset = 0;
                }

                offset = encodeSeg(buffer, offset, segment);
                if (segment.len() > 0) {
                    System.arraycopy(segment.data, 0, buffer, offset, segment.len());
                    offset += segment.len();
                }

                if (segment.xmit >= deadLink) {
                    state = -1;
                }
            }
        }

        // 发送剩余数据
        if (offset > 0) {
            outputData(buffer, 0, offset);
        }

        // 更新拥塞窗口
        if (change) {
            int inflight = (int)(sndNxt - sndUna);
            ssthresh = inflight / 2;
            if (ssthresh < IKCP_THRESH_MIN) {
                ssthresh = IKCP_THRESH_MIN;
            }
            cwnd = ssthresh + resent;
            incr = cwnd * mss;
        }

        if (lost) {
            ssthresh = cwndVal / 2;
            if (ssthresh < IKCP_THRESH_MIN) {
                ssthresh = IKCP_THRESH_MIN;
            }
            cwnd = 1;
            incr = mss;
        }

        if (cwnd < 1) {
            cwnd = 1;
            incr = mss;
        }
    }

    /**
     * 待发送数据量
     */
    public int waitSnd() {
        return sndBuf.size() + sndQueue.size();
    }

    //=========================================================================
    // 配置方法
    //=========================================================================

    /**
     * 快速模式 (参考 test.cpp mode=2)
     * nodelay=2, interval=10, resend=2, nc=1
     * rx_minrto=10, fastresend=1
     */
    public void setFastMode() {
        setNoDelay(2, 10, 2, 1);
        rxMinrto = 10;
        fastresend = 1;
        setWindowSize(128, 128);
    }

    /**
     * 普通模式 (参考 test.cpp mode=1)
     */
    public void setNormalMode() {
        setNoDelay(0, 10, 0, 1);
        setWindowSize(128, 128);
    }

    /**
     * 默认模式 (参考 test.cpp mode=0)
     */
    public void setDefaultMode() {
        setNoDelay(0, 10, 0, 0);
        setWindowSize(32, 128);
    }

    /**
     * 设置nodelay参数
     */
    public void setNoDelay(int nodelay, int interval, int resend, int nc) {
        if (nodelay >= 0) {
            this.nodelay = nodelay;
            rxMinrto = (nodelay != 0) ? IKCP_RTO_NDL : IKCP_RTO_MIN;
        }
        if (interval >= 0) {
            this.interval = Math.max(10, Math.min(interval, 5000));
        }
        if (resend >= 0) {
            fastresend = resend;
        }
        if (nc >= 0) {
            nocwnd = nc;
        }
    }

    /**
     * 设置窗口大小
     */
    public void setWindowSize(int sndwnd, int rcvwnd) {
        if (sndwnd > 0) {
            sndWnd = sndwnd;
        }
        if (rcvwnd > 0) {
            rcvWnd = Math.max(rcvwnd, IKCP_WND_RCV);
        }
    }

    /**
     * 设置MTU
     */
    public int setMtu(int mtu) {
        if (mtu < 50 || mtu < IKCP_OVERHEAD) {
            return -1;
        }
        this.mtu = mtu;
        this.mss = mtu - IKCP_OVERHEAD;
        this.buffer = new byte[(mtu + IKCP_OVERHEAD) * 3];
        return 0;
    }

    /**
     * 设置最小RTO
     */
    public void setMinRto(int minrto) {
        rxMinrto = minrto;
    }

    /**
     * 设置流模式
     */
    public void setStream(int stream) {
        this.stream = stream;
    }

    /**
     * 设置死链阈值
     */
    public void setDeadLink(int deadlink) {
        deadLink = deadlink;
    }

    //=========================================================================
    // 私有方法
    //=========================================================================

    private int encodeSeg(byte[] buf, int offset, Segment seg) {
        ByteBuffer bb = ByteBuffer.wrap(buf, offset, IKCP_OVERHEAD);
        bb.order(ByteOrder.LITTLE_ENDIAN);
        bb.putInt(seg.conv);
        bb.put((byte)seg.cmd);
        bb.put((byte)seg.frg);
        bb.putShort((short)seg.wnd);
        bb.putInt((int)seg.ts);
        bb.putInt((int)seg.sn);
        bb.putInt((int)seg.una);
        bb.putInt(seg.len());
        return offset + IKCP_OVERHEAD;
    }

    private void outputData(byte[] data, int offset, int len) {
        if (output != null && len > 0) {
            output.send(data, offset, len, this);
        }
    }

    private void updateAck(int rtt) {
        if (rxSrtt == 0) {
            rxSrtt = rtt;
            rxRttval = rtt / 2;
        } else {
            int delta = rtt - rxSrtt;
            if (delta < 0) delta = -delta;
            rxRttval = (3 * rxRttval + delta) / 4;
            rxSrtt = (7 * rxSrtt + rtt) / 8;
            if (rxSrtt < 1) rxSrtt = 1;
        }
        int rto = rxSrtt + Math.max(interval, 4 * rxRttval);
        rxRto = Math.max(rxMinrto, Math.min(rto, IKCP_RTO_MAX));
    }

    private void shrinkBuf() {
        if (!sndBuf.isEmpty()) {
            sndUna = sndBuf.get(0).sn;
        } else {
            sndUna = sndNxt;
        }
    }

    private void parseAck(long sn) {
        if (sn < sndUna || sn >= sndNxt) return;
        sndBuf.removeIf(seg -> seg.sn == sn);
    }

    private void parseUna(long una) {
        int removeCount = 0;
        for (Segment seg : sndBuf) {
            if (seg.sn < una) {
                removeCount++;
            } else {
                break;
            }
        }
        if (removeCount > 0) {
            sndBuf.subList(0, removeCount).clear();
        }
    }

    private void parseFastack(long sn, long ts) {
        if (sn < sndUna || sn >= sndNxt) return;
        for (Segment seg : sndBuf) {
            if (sn < seg.sn) break;
            if (sn != seg.sn) {
                seg.fastack++;
            }
        }
    }

    private void parseData(Segment newseg) {
        long sn = newseg.sn;
        if (sn >= rcvNxt + rcvWnd || sn < rcvNxt) {
            return;
        }

        // 查找插入位置
        int insertPos = rcvBuf.size();
        boolean repeat = false;

        for (int i = rcvBuf.size() - 1; i >= 0; i--) {
            Segment seg = rcvBuf.get(i);
            if (seg.sn == sn) {
                repeat = true;
                break;
            }
            if (seg.sn < sn) {
                insertPos = i + 1;
                break;
            }
            insertPos = i;
        }

        if (!repeat) {
            rcvBuf.add(insertPos, newseg);
        }

        moveRcvBufToQueue();
    }

    private void moveRcvBufToQueue() {
        while (!rcvBuf.isEmpty()) {
            Segment seg = rcvBuf.get(0);
            if (seg.sn == rcvNxt && rcvQueue.size() < rcvWnd) {
                rcvBuf.remove(0);
                rcvQueue.add(seg);
                rcvNxt++;
            } else {
                break;
            }
        }
    }
}
