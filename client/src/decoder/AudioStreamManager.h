#ifndef AUDIOSTREAMMANAGER_H
#define AUDIOSTREAMMANAGER_H

#include <mutex>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "GameSignal.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"
}

class NativeTcpSocket;
class QIODevice;
class WasapiPlayer;

/**
 * AudioStreamManager - 音频流管理器 (基于 scrcpy 官方 audio_regulator 架构)
 *
 * 核心设计 (解决原版同步写入导致的音频卡顿):
 *   - 接收线程 (std::thread): TCP recv → FFmpeg 解码 → 重采样 → 写入 SPSC 环形缓冲
 *   - 播放端 (WASAPI feed 线程): 从环形缓冲拉取 PCM 数据
 *   - 解耦: 环形缓冲吸收网络抖动，音频硬件按固定速率消费
 *   - 补偿: swr_set_compensation() 动态微调重采样率，维持目标缓冲水位 (~50ms)
 *   - 预缓冲: 攒够 target_buffering 再开始播放，避免启动时卡顿
 *
 * 协议格式:
 *   4B codec_id (OPUS/AAC/FLAC/RAW)
 *   然后每个包: 8B pts_and_flags + 4B packet_size + NB data
 */
class AudioStreamManager
{
public:
    explicit AudioStreamManager();
    ~AudioStreamManager();

    void installSocket(NativeTcpSocket *socket);
    void setMuted(bool muted);
    bool isMuted() const;
    void stopStream();
    void startStream();  // 启动接收线程

    /// QAudioSink pull 模式接口: 自定义 QIODevice 调用此方法拉取 PCM 数据
    /// 始终返回 maxSize 字节 (不足时用静音填充)
    int64_t pullAudio(char *data, int64_t maxSize);

    // 信号 / Signals
    Signal<const std::string&> audioStreamStarted;
    Signal<> audioStreamStopped;
    Signal<const std::string&> audioStreamError;

private:
    void run();

private:
    // ─── 网络 I/O (原生 socket, 避免 QTcpSocket 跨线程问题) ───
    intptr_t m_socketDescriptor = -1;
    std::vector<uint8_t> m_pendingData;   // 缓冲区残余数据 (installSocket 时排空)
    int m_pendingOffset = 0;
    int32_t recvData(uint8_t *buf, int32_t size);

    // ─── 状态 ───
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_muted{true};
    std::thread m_thread;  // 工作线程 (替代 QThread 继承)

    // ─── FFmpeg 解码 ───
    AVCodecContext *m_codecCtx = nullptr;
    AVFrame *m_frame = nullptr;
    SwrContext *m_swrCtx = nullptr;
    uint8_t *m_resampleBuf = nullptr;
    int m_resampleBufSize = 0;
    bool m_isRaw = false;

    bool initDecoder(AVCodecID codecId);
    void cleanupDecoder();
    std::string codecIdToName(uint32_t id);
    AVCodecID scrcpyCodecToFFmpeg(uint32_t codecId);

    // ─── SPSC 环形缓冲 (接收线程写, 音频线程读) ───
    static constexpr int AUDIO_SAMPLE_RATE = 48000;
    static constexpr int AUDIO_CHANNELS = 2;
    static constexpr int BYTES_PER_SAMPLE = 2;  // Int16
    static constexpr int SAMPLE_SIZE = AUDIO_CHANNELS * BYTES_PER_SAMPLE; // 4
    static constexpr uint32_t TARGET_BUFFERING = AUDIO_SAMPLE_RATE * 30 / 1000; // 30ms
    static constexpr uint32_t RING_CAPACITY = TARGET_BUFFERING + AUDIO_SAMPLE_RATE; // ~1.05s

    uint8_t *m_ring = nullptr;
    uint32_t m_ringAllocSamples = 0; // RING_CAPACITY + 1
    std::atomic<uint32_t> m_ringHead{0}; // 写指针 (sample 单位)
    std::atomic<uint32_t> m_ringTail{0}; // 读指针 (sample 单位)

    uint32_t ringCanRead() const;
    uint32_t ringCanWrite() const;
    void ringWrite(const uint8_t *data, uint32_t samples);
    uint32_t ringRead(uint8_t *data, uint32_t samples);
    void ringWriteSilence(uint32_t samples);

    std::mutex m_ringMutex; // 仅溢出时用

    // ─── 播放 (WasapiPlayer, feed 线程拉取 PCM 数据) ───
    WasapiPlayer *m_wasapiPlayer = nullptr;
    bool setupPlayback();
    void cleanupPlayback();

    // ─── 缓冲管理与补偿 (移植自 scrcpy audio_regulator) ───
    std::atomic<bool> m_prebufferingDone{false};
    std::atomic<bool> m_received{false};
    std::atomic<bool> m_played{false};
    std::atomic<uint32_t> m_underflow{0};

    // 补偿 (仅接收线程)
    float m_avgBuffering = 0.0f;
    int m_avgCount = 0;
    static constexpr int AVG_RANGE = 128;
    uint32_t m_samplesSinceResync = 0;
    bool m_compensationActive = false;
    uint32_t m_underflowReport = 0;
    int64_t m_nextExpectedPts = 0;

    void pushAvgBuffering(float value);
    float getAvgBuffering() const;
    void applyCompensation(uint32_t writtenSamples, uint32_t inputSamples,
                           uint32_t skippedSamples);
};

#endif // AUDIOSTREAMMANAGER_H
