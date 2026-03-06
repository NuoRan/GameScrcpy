package com.genymobile.scrcpy.auxiliary;

import java.io.Closeable;
import java.io.IOException;

/**
 * 辅助通道接口
 *
 * 独立于控制通道的第三条通路，用于视频参数、音频、剪贴板等非输入类指令。
 * TCP 和 UDP/KCP 各有实现。
 */
public interface IAuxChannel extends Closeable {

    /**
     * 辅助通道消息回调
     */
    interface Callback {
        void onVideoParamsChanged(int bitrate, int maxFps, int maxSize);
        void onStreamingChanged(boolean streaming);
    }

    /**
     * 设置回调
     */
    void setCallback(Callback callback);

    /**
     * 启动读取线程
     */
    void start();

    /**
     * 停止并释放资源
     */
    void stop();
}
