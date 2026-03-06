#ifndef CORE_ZEROCOPYSTREAMMANAGER_H
#define CORE_ZEROCOPYSTREAMMANAGER_H

#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include "GameSignal.h"
#include "GameTypes.h"

class Demuxer;
class VideoSocket;
class KcpVideoSocket;
struct AVPacket;

namespace qsc {
namespace core {

class ZeroCopyDecoder;
class FrameQueue;
struct FrameData;
class IVideoChannel;

/**
 * @brief 零拷贝流管理器 / Zero-Copy Stream Manager
 *
 * 管理完整的零拷贝视频管线 / Manages the complete zero-copy video pipeline:
 * VideoSocket → Demuxer → IDecoder → FrameQueue → ZeroCopyRenderer
 *
 * 特性 / Features:
 * - 端到端零拷贝 / End-to-end zero-copy (decode to render)
 * - 预分配帧池，无运行时内存分配 / Pre-allocated frame pool, no runtime allocation
 * - 无锁帧传递 / Lock-free frame passing
 * - 支持硬件加速和解码器依赖注入 / HW accel + decoder DI
 */
class ZeroCopyStreamManager {
public:
    ZeroCopyStreamManager();
    ~ZeroCopyStreamManager();

    /**
     * @brief 依赖注入：设置自定义解码器
     * @param decoder 解码器实例（转移所有权）
     *
     * 必须在 start() 之前调用。如果不调用，将使用默认的 ZeroCopyDecoder。
     */
    void setDecoder(std::unique_ptr<ZeroCopyDecoder> decoder);

    /**
     * @brief 安装 TCP 视频 Socket
     */
    void installVideoSocket(VideoSocket* socket);

    /**
     * @brief 安装 KCP 视频 Socket
     */
    void installKcpVideoSocket(KcpVideoSocket* socket);

    /**
     * @brief 安装视频通道（通过接口）
     * @param channel 视频通道（不持有所有权）
     */
    void installVideoChannel(IVideoChannel* channel);

    /**
     * @brief 设置帧尺寸
     */
    void setFrameSize(const Size& size);

    /**
     * @brief 设置视频编解码器
     * @param codec "h264"
     */
    void setVideoCodec(const std::string& codec);



    /**
     * @brief 启动流处理
     * @return 成功返回 true
     */
    bool start();

    /**
     * @brief 停止流处理
     */
    void stop();

    /**
     * @brief 是否正在运行
     */
    bool isRunning() const { return m_running; }

    /**
     * @brief 获取当前 FPS
     */
    uint32_t fps() const { return m_currentFps; }

    /**
     * @brief 是否使用硬件加速
     */
    bool isHardwareAccelerated() const;

    /**
     * @brief 获取解码器名称
     */
    std::string decoderName() const;

    /**
     * @brief 截图
     */
    using ScreenshotCallback = std::function<void(int width, int height, uint8_t* dataRGB32)>;
    void screenshot(ScreenshotCallback callback);

    /**
     * @brief 获取帧队列（供外部消费者直接使用）
     * @return 帧队列指针
     */
    FrameQueue* frameQueue() const { return m_frameQueue.get(); }

    /**
     * @brief 消费一帧（从 FrameQueue 获取）
     * @return 帧数据指针，使用完后必须调用 releaseFrame()
     */
    FrameData* consumeFrame();

    /**
     * @brief 归还帧到池中
     * @param frame 要归还的帧
     */
    void releaseFrame(FrameData* frame);

    // Signals (Signal<>)
    Signal<uint32_t> fpsUpdated;
    Signal<> streamStopped;
    Signal<const Size&> frameSizeChanged;
    Signal<bool, const std::string&> decoderInfo;
    Signal<> frameReady;
    Signal<int, int, uint8_t*, uint8_t*, uint8_t*, int, int, int> frameReadyWithData;

private:
    void onDemuxerStopped();
    void onDemuxerGetFrame(AVPacket* packet);
    void onDecoderFpsUpdated(uint32_t fps);

private:
    bool openDecoder();

private:
    std::unique_ptr<Demuxer> m_demuxer;
    std::unique_ptr<ZeroCopyDecoder> m_decoder;
    std::unique_ptr<FrameQueue> m_frameQueue;


    VideoSocket* m_videoSocket = nullptr;
    KcpVideoSocket* m_kcpVideoSocket = nullptr;
    IVideoChannel* m_videoChannel = nullptr;  // 新架构接口

    Size m_frameSize;
    std::string m_videoCodec = "h264";
    uint32_t m_currentFps = 0;
    bool m_running = false;
    bool m_decoderOpened = false;
    bool m_decoderInjected = false;  // 是否使用注入的解码器
};

} // namespace core
} // namespace qsc

#endif // CORE_ZEROCOPYSTREAMMANAGER_H
