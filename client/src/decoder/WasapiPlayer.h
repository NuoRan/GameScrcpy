#pragma once
/**
 * @file WasapiPlayer.h
 * @brief WASAPI 共享模式音频播放器
 *
 * 替代 QAudioSink，零 Qt 依赖。
 * 使用独立 feed 线程轮询 WASAPI 缓冲区，通过回调拉取 PCM 数据。
 * COM 生命周期完全封装在 feed 线程内。
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <thread>
#include <atomic>
#include <functional>
#include <cstdint>

class WasapiPlayer {
public:
    /// 拉取回调: 写入 maxSize 字节 PCM 到 data, 返回实际写入字节数
    using PullCallback = std::function<int64_t(char* data, int64_t maxSize)>;

    WasapiPlayer();
    ~WasapiPlayer();

    WasapiPlayer(const WasapiPlayer&) = delete;
    WasapiPlayer& operator=(const WasapiPlayer&) = delete;

    /**
     * @brief 初始化 WASAPI 并开始播放
     * @param sampleRate 采样率 (48000)
     * @param channels   声道数 (2)
     * @param bitsPerSample 位深度 (16)
     * @param pullCb 数据拉取回调 (在 feed 线程调用)
     * @return true=成功
     *
     * 内部启动 feed 线程，阻塞等待初始化完成再返回。
     */
    bool initialize(int sampleRate, int channels, int bitsPerSample, PullCallback pullCb);

    /**
     * @brief 停止播放并释放所有资源
     */
    void shutdown();

private:
    void feedThreadFunc();
    void releaseResources();

    PullCallback m_callback;
    int m_sampleRate = 0;
    int m_channels = 0;
    int m_bitsPerSample = 0;

    IMMDeviceEnumerator* m_enumerator = nullptr;
    IMMDevice*           m_device = nullptr;
    IAudioClient*        m_audioClient = nullptr;
    IAudioRenderClient*  m_renderClient = nullptr;

    UINT32 m_bufferFrames = 0;
    UINT32 m_frameSize = 0;

    HANDLE m_readyEvent = nullptr;
    HANDLE m_stopEvent = nullptr;
    std::thread m_feedThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_initOk{false};
};
