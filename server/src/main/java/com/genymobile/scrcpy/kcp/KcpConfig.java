package com.genymobile.scrcpy.kcp;

/**
 * KcpConfig - KCP 协议配置常量
 *
 * MERGE: 统一服务端和客户端的 KCP 配置
 *
 * 这些常量必须与客户端 (C++ ikcp.c) 保持一致:
 * - 会话 ID (conv)
 * - 窗口大小
 * - MTU
 * - 模式参数
 */
public final class KcpConfig {

    private KcpConfig() {
        // 不可实例化
    }

    //=========================================================================
    // 会话 ID - 必须与客户端一致
    //=========================================================================

    /** 视频通道会话 ID */
    public static final int CONV_VIDEO = 0x11223344;

    /** 控制通道会话 ID */
    public static final int CONV_CONTROL = 0x22334455;

    //=========================================================================
    // 默认端口
    //=========================================================================

    /** 默认 KCP 视频端口 */
    public static final int DEFAULT_VIDEO_PORT = 27185;

    /** 默认 KCP 控制端口 (视频端口 + 1) */
    public static final int DEFAULT_CONTROL_PORT = 27186;

    //=========================================================================
    // 协议参数 - 必须与客户端一致
    //=========================================================================

    /** 默认 MTU */
    public static final int DEFAULT_MTU = 1400;

    /** KCP 头部开销 */
    public static final int OVERHEAD = 24;

    /** 默认发送窗口 */
    public static final int DEFAULT_SND_WND = 128;

    /** 默认接收窗口 */
    public static final int DEFAULT_RCV_WND = 128;

    /** 默认更新间隔 (ms) */
    public static final int DEFAULT_INTERVAL = 10;

    /** 死链阈值 */
    public static final int DEFAULT_DEAD_LINK = 20;

    //=========================================================================
    // 快速模式参数 (用于投屏)
    //=========================================================================

    /** 快速模式: nodelay */
    public static final int FAST_MODE_NODELAY = 2;

    /** 快速模式: 更新间隔 */
    public static final int FAST_MODE_INTERVAL = 10;

    /** 快速模式: 快速重传 */
    public static final int FAST_MODE_RESEND = 2;

    /** 快速模式: 关闭拥塞控制 */
    public static final int FAST_MODE_NC = 1;

    /** 快速模式: 最小 RTO */
    public static final int FAST_MODE_MIN_RTO = 10;

    //=========================================================================
    // 视频流特定配置
    //=========================================================================

    /** 视频流窗口大小 (根据码率动态计算的基础值) */
    public static final int VIDEO_MIN_WINDOW = 128;

    /** 视频流最大窗口大小 */
    public static final int VIDEO_MAX_WINDOW = 2048;

    /** 视频流缓冲时间 (ms) - 用于计算窗口大小 */
    public static final int VIDEO_BUFFER_MS = 150;

    //=========================================================================
    // 丢帧控制参数
    //=========================================================================

    /** 默认丢帧阈值 (pending 包数) */
    public static final int DEFAULT_DROP_THRESHOLD = 128;

    /** 默认恢复阈值 (pending 包数) */
    public static final int DEFAULT_RESUME_THRESHOLD = 32;

    //=========================================================================
    // Socket 缓冲区大小
    //=========================================================================

    /** UDP 接收缓冲区 */
    public static final int SOCKET_RECV_BUFFER = 2 * 1024 * 1024;

    /** UDP 发送缓冲区 */
    public static final int SOCKET_SEND_BUFFER = 1 * 1024 * 1024;

    //=========================================================================
    // 工具方法
    //=========================================================================

    /**
     * 根据码率计算窗口大小
     *
     * @param bitrateBps 码率 (bps)
     * @return 窗口大小
     */
    public static int calculateWindowSize(int bitrateBps) {
        // 基于缓冲时间计算: (码率 * 缓冲时间) / (MTU - 头部)
        int bytesPerMs = bitrateBps / 8 / 1000;
        int packetSize = DEFAULT_MTU - OVERHEAD;
        int window = (bytesPerMs * VIDEO_BUFFER_MS) / packetSize;

        return Math.max(VIDEO_MIN_WINDOW, Math.min(window, VIDEO_MAX_WINDOW));
    }
}
