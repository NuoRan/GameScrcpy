/**
 * AudioStreamManager - 完整音频管道 (移植自 scrcpy 官方 audio_regulator 架构)
 *
 * 架构:
 *   接收线程 (run): TCP recv  FFmpeg decode  swr_convert  ringWrite
 *   WASAPI feed 线程 (WasapiPlayer): ringRead  硬件输出
 *
 * 关键设计决策:
 *   1. 使用原生 socket recv (避免 QTcpSocket 跨线程 QSocketNotifier 问题)
 *   2. SPSC 无锁环形缓冲 (生产者=接收线程, 消费者=WASAPI feed 线程)
 *   3. WASAPI 共享模式直接写入硬件缓冲 (无 Qt Multimedia 依赖)
 *   4. 预缓冲: 攒够 target_buffering 才开始播放
 *   5. 漂移补偿: swr_set_compensation 每秒重新计算
 *   6. 欠载处理: 填充静音，让补偿机制自动恢复
 */
#include "AudioStreamManager.h"
#include "WasapiPlayer.h"
#include "NativeTcpSocket.h"
#define LOG_TAG "AudioStream"
#include "Logger.h"
#include "ByteOrder.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <limits>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#endif

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
}

// scrcpy 音频协议常量
static constexpr uint32_t CODEC_ID_OPUS = 0x6f707573;
static constexpr uint32_t CODEC_ID_AAC  = 0x00616163;
static constexpr uint32_t CODEC_ID_FLAC = 0x666c6163;
static constexpr uint32_t CODEC_ID_RAW  = 0x00726177;

static constexpr uint64_t PACKET_FLAG_CONFIG    = 1ULL << 63;
static constexpr uint64_t PACKET_FLAG_KEY_FRAME = 1ULL << 62;
static constexpr uint64_t PACKET_PTS_MASK       = PACKET_FLAG_KEY_FRAME - 1;

//
// 构造 / 析构
//
AudioStreamManager::AudioStreamManager()
{
}

AudioStreamManager::~AudioStreamManager()
{
    stopStream();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    // run() 已结束, 在主线程安全清理播放设备
    cleanupPlayback();
    delete[] m_ring;
    m_ring = nullptr;
}

void AudioStreamManager::startStream()
{
    m_stopRequested.store(false);
    m_thread = std::thread(&AudioStreamManager::run, this);
}

//
// 公开接口
//
void AudioStreamManager::installSocket(NativeTcpSocket *socket)
{
    if (socket) {
        m_socketDescriptor = static_cast<intptr_t>(socket->handle());
        // NativeTcpSocket is blocking-mode with no internal buffer,
        // so no need to drain like with QTcpSocket.
        m_pendingData.clear();
        m_pendingOffset = 0;

        LOGD() << "AudioStreamManager: Installed native socket descriptor:" << m_socketDescriptor;
    }
}

void AudioStreamManager::setMuted(bool muted)
{
    m_muted.store(muted);
}

bool AudioStreamManager::isMuted() const
{
    return m_muted.load();
}

void AudioStreamManager::stopStream()
{
    m_stopRequested.store(true);
    if (m_socketDescriptor != -1) {
#ifdef _WIN32
        ::shutdown((SOCKET)m_socketDescriptor, SD_BOTH);
#else
        ::shutdown((int)m_socketDescriptor, SHUT_RDWR);
#endif
    }
}

//
// 环形缓冲 (无锁 SPSC)
//   head = 写指针 (接收线程拥有)
//   tail = 读指针 (音频线程拥有)
//   单位: sample (每个 sample = SAMPLE_SIZE 字节)
//   空: head == tail
//   满: (head + 1) % alloc == tail
//
uint32_t AudioStreamManager::ringCanRead() const
{
    uint32_t head = m_ringHead.load(std::memory_order_acquire);
    uint32_t tail = m_ringTail.load(std::memory_order_acquire);
    return (m_ringAllocSamples + head - tail) % m_ringAllocSamples;
}

uint32_t AudioStreamManager::ringCanWrite() const
{
    uint32_t head = m_ringHead.load(std::memory_order_relaxed);
    uint32_t tail = m_ringTail.load(std::memory_order_acquire);
    return (m_ringAllocSamples + tail - head - 1) % m_ringAllocSamples;
}

void AudioStreamManager::ringWrite(const uint8_t *data, uint32_t samples)
{
    if (!samples || !m_ring) return;

    uint32_t head = m_ringHead.load(std::memory_order_relaxed);
    uint32_t canWrite = ringCanWrite();
    if (samples > canWrite) samples = canWrite;
    if (!samples) return;

    uint32_t rightCount = m_ringAllocSamples - head;
    if (rightCount > samples) rightCount = samples;

    memcpy(m_ring + head * SAMPLE_SIZE, data, rightCount * SAMPLE_SIZE);
    if (samples > rightCount) {
        uint32_t leftCount = samples - rightCount;
        memcpy(m_ring, data + rightCount * SAMPLE_SIZE, leftCount * SAMPLE_SIZE);
    }

    uint32_t newHead = (head + samples) % m_ringAllocSamples;
    m_ringHead.store(newHead, std::memory_order_release);
}

uint32_t AudioStreamManager::ringRead(uint8_t *data, uint32_t samples)
{
    if (!samples || !m_ring) return 0;

    uint32_t tail = m_ringTail.load(std::memory_order_relaxed);
    uint32_t head = m_ringHead.load(std::memory_order_acquire);
    uint32_t canRead = (m_ringAllocSamples + head - tail) % m_ringAllocSamples;
    if (!canRead) return 0;
    if (samples > canRead) samples = canRead;

    if (data) {
        uint32_t rightCount = m_ringAllocSamples - tail;
        if (rightCount > samples) rightCount = samples;
        memcpy(data, m_ring + tail * SAMPLE_SIZE, rightCount * SAMPLE_SIZE);
        if (samples > rightCount) {
            uint32_t leftCount = samples - rightCount;
            memcpy(data + rightCount * SAMPLE_SIZE, m_ring, leftCount * SAMPLE_SIZE);
        }
    }

    uint32_t newTail = (tail + samples) % m_ringAllocSamples;
    m_ringTail.store(newTail, std::memory_order_release);
    return samples;
}

void AudioStreamManager::ringWriteSilence(uint32_t samples)
{
    if (!samples || !m_ring) return;
    uint32_t head = m_ringHead.load(std::memory_order_relaxed);
    uint32_t canWrite = ringCanWrite();
    if (samples > canWrite) samples = canWrite;
    if (!samples) return;

    uint32_t rightCount = m_ringAllocSamples - head;
    if (rightCount > samples) rightCount = samples;
    memset(m_ring + head * SAMPLE_SIZE, 0, rightCount * SAMPLE_SIZE);
    if (samples > rightCount) {
        memset(m_ring, 0, (samples - rightCount) * SAMPLE_SIZE);
    }

    uint32_t newHead = (head + samples) % m_ringAllocSamples;
    m_ringHead.store(newHead, std::memory_order_release);
}

//
// pullAudio: WASAPI feed 线程拉取数据
//   - 始终返回 maxSize 字节 (不足时填充静音)
//   - 实现预缓冲、静音、下溢处理
//
int64_t AudioStreamManager::pullAudio(char *data, int64_t maxSize)
{
    if (!data || maxSize <= 0) {
        return maxSize;
    }

    if (!m_ring) {
        memset(data, 0, static_cast<size_t>(maxSize));
        return maxSize;
    }

    uint32_t requestedSamples = static_cast<uint32_t>(maxSize) / SAMPLE_SIZE;
    if (requestedSamples == 0) {
        memset(data, 0, static_cast<size_t>(maxSize));
        return maxSize;
    }

    std::lock_guard<std::mutex> lock(m_ringMutex);

    bool played = m_played.load(std::memory_order_relaxed);
    if (!played) {
        uint32_t buffered = ringCanRead();
        if (buffered < TARGET_BUFFERING) {
            memset(data, 0, static_cast<size_t>(maxSize));
            return maxSize;
        }
        m_played.store(true, std::memory_order_relaxed);
    }

    uint32_t readSamples = ringRead(reinterpret_cast<uint8_t*>(data), requestedSamples);

    if (readSamples < requestedSamples) {
        uint32_t silence = requestedSamples - readSamples;
        memset(data + readSamples * SAMPLE_SIZE, 0, silence * SAMPLE_SIZE);

        bool received = m_received.load(std::memory_order_relaxed);
        if (received) {
            m_underflow.fetch_add(silence, std::memory_order_relaxed);
        }
    }

    if (m_muted.load(std::memory_order_relaxed)) {
        memset(data, 0, static_cast<size_t>(maxSize));
    }

    return maxSize;
}

//
// 播放设备管理 (WASAPI)
//
bool AudioStreamManager::setupPlayback()
{
    m_wasapiPlayer = new WasapiPlayer();
    bool ok = m_wasapiPlayer->initialize(
        AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, 16,
        [this](char* data, int64_t maxSize) -> int64_t {
            return pullAudio(data, static_cast<int64_t>(maxSize));
        });
    if (!ok) {
        LOGW() << "AudioStreamManager: WASAPI playback setup failed";
        delete m_wasapiPlayer;
        m_wasapiPlayer = nullptr;
        return false;
    }
    LOGI() << "AudioStreamManager: WASAPI playback started";
    return true;
}

void AudioStreamManager::cleanupPlayback()
{
    if (m_wasapiPlayer) {
        m_wasapiPlayer->shutdown();
        delete m_wasapiPlayer;
        m_wasapiPlayer = nullptr;
    }
}

//
// 网络 I/O (原生 socket)
//
int32_t AudioStreamManager::recvData(uint8_t *buf, int32_t size)
{
    if (m_socketDescriptor == -1 || m_stopRequested.load()) return -1;

    int32_t received = 0;

    // 先从缓冲残余数据中取 (installSocket 时排空的)
    if (!m_pendingData.empty()) {
        int available = static_cast<int>(m_pendingData.size()) - m_pendingOffset;
        int toCopy = (std::min)(available, (int)size);
        memcpy(buf, m_pendingData.data() + m_pendingOffset, toCopy);
        m_pendingOffset += toCopy;
        received += toCopy;
        if (m_pendingOffset >= static_cast<int>(m_pendingData.size())) {
            m_pendingData.clear();
            m_pendingOffset = 0;
        }
        if (received >= size) return received;
    }

    while (received < size && !m_stopRequested.load()) {
#ifdef _WIN32
        int ret = ::recv((SOCKET)m_socketDescriptor,
                         reinterpret_cast<char*>(buf + received),
                         size - received, 0);
        if (ret == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) { Sleep(1); continue; }
            return -1;
        }
        if (ret == 0) return -1;
#else
        ssize_t ret = ::recv((int)m_socketDescriptor, buf + received, size - received, 0);
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { usleep(1000); continue; }
            return -1;
        }
        if (ret == 0) return -1;
#endif
        received += static_cast<int32_t>(ret);
    }
    return m_stopRequested.load() ? -1 : received;
}

//
// FFmpeg 解码器
//
AVCodecID AudioStreamManager::scrcpyCodecToFFmpeg(uint32_t codecId)
{
    switch (codecId) {
    case CODEC_ID_OPUS: return AV_CODEC_ID_OPUS;
    case CODEC_ID_AAC:  return AV_CODEC_ID_AAC;
    case CODEC_ID_FLAC: return AV_CODEC_ID_FLAC;
    case CODEC_ID_RAW:  return AV_CODEC_ID_NONE;
    default: return AV_CODEC_ID_NONE;
    }
}

std::string AudioStreamManager::codecIdToName(uint32_t id)
{
    switch (id) {
    case CODEC_ID_OPUS: return "opus";
    case CODEC_ID_AAC:  return "aac";
    case CODEC_ID_FLAC: return "flac";
    case CODEC_ID_RAW:  return "raw";
    default: return "unknown";
    }
}

bool AudioStreamManager::initDecoder(AVCodecID codecId)
{
    const AVCodec *codec = avcodec_find_decoder(codecId);
    if (!codec) {
        LOGW() << "AudioStreamManager: Decoder not found:" << avcodec_get_name(codecId);
        return false;
    }
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) return false;

    m_codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecCtx->sample_rate = AUDIO_SAMPLE_RATE;
    m_codecCtx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;

    if (codecId == AV_CODEC_ID_FLAC) {
        // The sample_fmt is not set by the FLAC decoder
        m_codecCtx->sample_fmt = AV_SAMPLE_FMT_S16;
    }

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        LOGW() << "AudioStreamManager: Could not open codec";
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    m_frame = av_frame_alloc();
    if (!m_frame) { avcodec_free_context(&m_codecCtx); return false; }
    return true;
}

void AudioStreamManager::cleanupDecoder()
{
    if (m_swrCtx) swr_free(&m_swrCtx);
    if (m_resampleBuf) { av_free(m_resampleBuf); m_resampleBuf = nullptr; m_resampleBufSize = 0; }
    if (m_frame) av_frame_free(&m_frame);
    if (m_codecCtx) avcodec_free_context(&m_codecCtx);
}

//
// 漂移补偿 (移植自 scrcpy audio_regulator)
//
void AudioStreamManager::pushAvgBuffering(float value)
{
    m_avgCount++;
    if (m_avgCount > AVG_RANGE) m_avgCount = AVG_RANGE;
    float alpha = 1.0f / m_avgCount;
    m_avgBuffering = m_avgBuffering * (1.0f - alpha) + value * alpha;
}

float AudioStreamManager::getAvgBuffering() const
{
    return m_avgBuffering;
}

void AudioStreamManager::applyCompensation(uint32_t writtenSamples,
                                            uint32_t inputSamples,
                                            uint32_t skippedSamples)
{
    if (!m_played.load(std::memory_order_relaxed)) return;
    if (!m_swrCtx) return;

    uint32_t underflow = m_underflow.exchange(0, std::memory_order_relaxed);
    m_underflowReport += underflow;

    int32_t instantComp = (int32_t)writtenSamples - (int32_t)inputSamples;
    int32_t insertedSilence = (int32_t)underflow;
    int32_t dropped = (int32_t)skippedSamples;

    m_avgBuffering += instantComp + insertedSilence - dropped;
    if (m_avgBuffering < 0) m_avgBuffering = 0;

    uint32_t canRead = ringCanRead();
    pushAvgBuffering((float)canRead);

    m_samplesSinceResync += writtenSamples;
    if (m_samplesSinceResync >= (uint32_t)AUDIO_SAMPLE_RATE) {
        m_samplesSinceResync = 0;

        float avg = getAvgBuffering();
        int diff = (int)TARGET_BUFFERING - (int)avg;

        int threshold = m_compensationActive
                      ? AUDIO_SAMPLE_RATE / 1000      // 1ms
                      : AUDIO_SAMPLE_RATE * 4 / 1000; // 4ms

        if (std::abs(diff) < threshold) {
            diff = 0;
        } else if (diff < 0 && canRead < TARGET_BUFFERING) {
            diff = 0;
        }

        int distance = 4 * AUDIO_SAMPLE_RATE;
        int absMaxDiff = distance / 50;
        diff = std::clamp(diff, -absMaxDiff, absMaxDiff);

        int ret = swr_set_compensation(m_swrCtx, diff, distance);
        if (ret < 0) {
            LOG_W("AudioStreamManager: swr_set_compensation failed: %d", ret);
        } else {
            m_compensationActive = (diff != 0);
        }

        if (diff != 0 || m_underflowReport > 0) {
            LOG_D_THROTTLE(5000, "AudioStreamManager: target=%u avg=%.0f cur=%u comp=%d underflow=%u",
                   TARGET_BUFFERING, avg, canRead, diff, m_underflowReport);
        }
        m_underflowReport = 0;
    }
}

//
// 主循环 (接收线程)
//
void AudioStreamManager::run()
{
    LOGI() << "AudioStreamManager: Thread started";

    // 1. 读取 codec header
    uint8_t headerBuf[4];
    if (recvData(headerBuf, 4) != 4) {
        LOGW() << "AudioStreamManager: Failed to read codec header";
        audioStreamError.fire("Failed to read codec header");
        return;
    }

    uint32_t codecId = qsc::readBigEndian32(headerBuf);

    if (codecId == 0) {
        LOGI() << "AudioStreamManager: Audio disabled by server";
        audioStreamStopped.fire();
        return;
    }
    if (codecId == 1) {
        LOGW() << "AudioStreamManager: Server audio error";
        audioStreamError.fire("Server audio configuration error");
        return;
    }

    std::string codecName = codecIdToName(codecId);
    {
        char hexBuf[16];
        snprintf(hexBuf, sizeof(hexBuf), "(0x%08x)", codecId);
        LOGI() << "AudioStreamManager: Audio codec:" << codecName << hexBuf;
    }

    m_isRaw = (codecId == CODEC_ID_RAW);

    // 2. 初始化解码器
    if (!m_isRaw) {
        AVCodecID ffmpegCodecId = scrcpyCodecToFFmpeg(codecId);
        if (ffmpegCodecId == AV_CODEC_ID_NONE) {
            audioStreamError.fire("Unsupported audio codec: " + codecName);
            return;
        }
        if (!initDecoder(ffmpegCodecId)) {
            audioStreamError.fire("Failed to init audio decoder");
            return;
        }
    }

    // 3. 初始化环形缓冲
    m_ringAllocSamples = RING_CAPACITY + 1;
    m_ring = new (std::nothrow) uint8_t[m_ringAllocSamples * SAMPLE_SIZE]();
    if (!m_ring) {
        cleanupDecoder();
        audioStreamError.fire("Failed to allocate audio ring buffer");
        return;
    }
    m_ringHead.store(0);
    m_ringTail.store(0);
    m_prebufferingDone.store(false);
    m_received.store(false);
    m_played.store(false);
    m_underflow.store(0);
    m_avgBuffering = (float)TARGET_BUFFERING;
    m_avgCount = 0;
    m_samplesSinceResync = 0;
    m_compensationActive = false;
    m_underflowReport = 0;
    m_nextExpectedPts = 0;

    // 4. 初始化 WASAPI 播放 (可在任意线程直接调用, 无需跨线程)
    bool playbackOk = setupPlayback();

    if (!playbackOk) {
        LOGW() << "AudioStreamManager: Playback setup failed";
        delete[] m_ring; m_ring = nullptr;
        cleanupDecoder();
        audioStreamError.fire("Failed to start audio playback");
        return;
    }

    audioStreamStarted.fire(codecName);
    LOGI() << "AudioStreamManager: Pipeline started (target buffering:"
            << TARGET_BUFFERING << "samples =" << (TARGET_BUFFERING * 1000 / AUDIO_SAMPLE_RATE) << "ms)";

    // 5. 接收-解码-推送主循环
    AVPacket *pkt = av_packet_alloc();

    while (!m_stopRequested.load()) {
        uint8_t frameMeta[12];
        if (recvData(frameMeta, 12) != 12) break;

        uint64_t ptsAndFlags = qsc::readBigEndian64(frameMeta);
        uint32_t packetSize = qsc::readBigEndian32(frameMeta + 8);

        if (packetSize == 0 || packetSize > 1024 * 1024) {
            LOGW() << "AudioStreamManager: Invalid packet size" << packetSize;
            break;
        }

        // Allocate ref-counted packet buffer (matching scrcpy av_new_packet usage)
        if (av_new_packet(pkt, packetSize) < 0) {
            LOGW() << "AudioStreamManager: Failed to allocate packet";
            break;
        }
        if (recvData(pkt->data, packetSize) != static_cast<int32_t>(packetSize)) {
            av_packet_unref(pkt);
            break;
        }

        bool isConfig = (ptsAndFlags & PACKET_FLAG_CONFIG) != 0;
        bool isKeyFrame = (ptsAndFlags & PACKET_FLAG_KEY_FRAME) != 0;
        int64_t pts = (int64_t)(ptsAndFlags & PACKET_PTS_MASK);

        // Skip config packets for audio (scrcpy decoder skips when pts==AV_NOPTS_VALUE)
        if (isConfig) {
            LOG_D("AudioStreamManager: Skipping config packet (%u bytes)", packetSize);
            av_packet_unref(pkt);
            continue;
        }

        // Set packet metadata (matching scrcpy demuxer)
        pkt->pts = pts;
        pkt->dts = pts;
        if (isKeyFrame) {
            pkt->flags |= AV_PKT_FLAG_KEY;
        }

        // 不连续检测
        if (m_nextExpectedPts && (pts - m_nextExpectedPts) > 100000) {
            uint32_t canRead = ringCanRead();
            uint32_t inputSamplesEst = packetSize / SAMPLE_SIZE;
            if (inputSamplesEst + canRead < TARGET_BUFFERING) {
                uint32_t silence = TARGET_BUFFERING - canRead - inputSamplesEst;
                ringWriteSilence(silence);
            }
            m_avgBuffering = (float)TARGET_BUFFERING;
            if (m_swrCtx) swr_set_compensation(m_swrCtx, 0, 0);
            m_compensationActive = false;
            m_samplesSinceResync = 0;
            m_underflow.store(0, std::memory_order_relaxed);
        }

        // 解码并写入环形缓冲
        uint32_t totalWrittenSamples = 0;
        uint32_t totalInputSamples = 0;
        uint32_t totalSkipped = 0;

        if (m_isRaw) {
            uint32_t rawSamples = packetSize / SAMPLE_SIZE;
            ringWrite(pkt->data, rawSamples);
            totalWrittenSamples = rawSamples;
            totalInputSamples = rawSamples;
            int64_t duration = (int64_t)rawSamples * 1000000LL / AUDIO_SAMPLE_RATE;
            m_nextExpectedPts = pts + duration;
        } else {
            int ret = avcodec_send_packet(m_codecCtx, pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                LOG_W("AudioStreamManager: avcodec_send_packet error: %d", ret);
                av_packet_unref(pkt);
                continue;
            }

            for (;;) {
                ret = avcodec_receive_frame(m_codecCtx, m_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;

                totalInputSamples += m_frame->nb_samples;

                int64_t duration = (int64_t)m_frame->nb_samples * 1000000LL / AUDIO_SAMPLE_RATE;
                m_nextExpectedPts = pts + duration;

                // 初始化重采样器
                if (!m_swrCtx) {
                    int r = swr_alloc_set_opts2(&m_swrCtx,
                        &m_codecCtx->ch_layout, AV_SAMPLE_FMT_S16, AUDIO_SAMPLE_RATE,
                        &m_codecCtx->ch_layout, m_codecCtx->sample_fmt, m_codecCtx->sample_rate,
                        0, nullptr);
                    if (r < 0 || swr_init(m_swrCtx) < 0) {
                        LOGW() << "AudioStreamManager: swr init failed";
                        swr_free(&m_swrCtx); m_swrCtx = nullptr;
                        continue;
                    }
                }

                // 重采样
                int64_t swrDelay = swr_get_delay(m_swrCtx, AUDIO_SAMPLE_RATE);
                int dstSamples = (int)(swrDelay + m_frame->nb_samples + 256);
                int dstBufSize = dstSamples * SAMPLE_SIZE;

                if (dstBufSize > m_resampleBufSize) {
                    if (m_resampleBuf) av_free(m_resampleBuf);
                    m_resampleBuf = static_cast<uint8_t*>(av_malloc(dstBufSize + 4096));
                    m_resampleBufSize = dstBufSize + 4096;
                }

                int converted = swr_convert(m_swrCtx,
                    &m_resampleBuf, dstSamples,
                    const_cast<const uint8_t**>(m_frame->extended_data),
                    m_frame->nb_samples);

                if (converted > 0) {
                    uint32_t samples = (uint32_t)converted;

                    // 限制最大缓冲
                    bool played = m_played.load(std::memory_order_relaxed);
                    uint32_t maxBuffered;
                    if (played) {
                        maxBuffered = TARGET_BUFFERING * 11 / 10
                                    + 60 * AUDIO_SAMPLE_RATE / 1000;
                    } else {
                        maxBuffered = TARGET_BUFFERING
                                    + 10 * AUDIO_SAMPLE_RATE / 1000;
                    }

                    {
                        std::lock_guard<std::mutex> lock(m_ringMutex);
                        ringWrite(m_resampleBuf, samples);
                        totalWrittenSamples += samples;

                        uint32_t canRead = ringCanRead();
                        if (canRead > maxBuffered) {
                            uint32_t toSkip = canRead - maxBuffered;
                            ringRead(nullptr, toSkip);
                            totalSkipped += toSkip;
                            LOG_D_THROTTLE(5000, "AudioStreamManager: Buffer overflow, skipped %u samples", toSkip);
                        }
                    }

                    m_received.store(true, std::memory_order_relaxed);
                }
            }
        }

        if (totalWrittenSamples > 0) {
            applyCompensation(totalWrittenSamples, totalInputSamples, totalSkipped);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    cleanupDecoder();

    // 清理播放设备 (在工作线程中直接清理，避免 BlockingQueuedConnection 死锁)
    cleanupPlayback();

    LOGI() << "AudioStreamManager: Thread stopped";
    audioStreamStopped.fire();
}
