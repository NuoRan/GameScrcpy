package com.genymobile.scrcpy.auxiliary;

/**
 * 辅助通道消息定义
 *
 * 辅助通道 (AuxChannel) 独立于控制通道，专门传输非输入类命令：
 * - 视频参数调整（码率/帧率/分辨率）
 * - 视频流暂停/恢复
 * - 未来：音频、剪贴板等
 *
 * 消息格式: [type:1B] [payload:变长]
 */
public final class AuxMessage {

    public static final int TYPE_SET_VIDEO_PARAMS    = 0x01;  // bitrate(4)+maxFps(2)+maxSize(2) = 8B
    public static final int TYPE_SET_VIDEO_STREAMING = 0x02;  // mode(1) = 1B

    // 未来扩展预留
    // public static final int TYPE_SET_AUDIO_PARAMS = 0x10;
    // public static final int TYPE_SET_CLIPBOARD    = 0x20;

    private int type;

    // video params
    private int videoBitRate;
    private int videoMaxFps;
    private int videoMaxSize;

    // video streaming
    private boolean videoStreamingOn;

    public int getType() { return type; }

    public int getVideoBitRate() { return videoBitRate; }
    public int getVideoMaxFps() { return videoMaxFps; }
    public int getVideoMaxSize() { return videoMaxSize; }
    public boolean isVideoStreamingOn() { return videoStreamingOn; }

    public static AuxMessage createSetVideoParams(int bitRate, int maxFps, int maxSize) {
        AuxMessage msg = new AuxMessage();
        msg.type = TYPE_SET_VIDEO_PARAMS;
        msg.videoBitRate = bitRate;
        msg.videoMaxFps = maxFps;
        msg.videoMaxSize = maxSize;
        return msg;
    }

    public static AuxMessage createSetVideoStreaming(boolean on) {
        AuxMessage msg = new AuxMessage();
        msg.type = TYPE_SET_VIDEO_STREAMING;
        msg.videoStreamingOn = on;
        return msg;
    }
}
