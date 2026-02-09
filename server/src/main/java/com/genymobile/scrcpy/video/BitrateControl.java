package com.genymobile.scrcpy.video;

/**
 * 动态码率反馈接口。
 * <p>
 * 由传输层实现，编码器在每帧编码后调用以获取建议码率，
 * 实现基于网络拥塞的自适应码率控制。
 */
public interface BitrateControl {

    /**
     * 获取建议码率。
     *
     * @param baseBitrate 原始目标码率 (bps)
     * @return 建议使用的码率 (bps)，等于 baseBitrate 表示无需调整
     */
    int getSuggestedBitrate(int baseBitrate);
}
