#ifndef CORE_ZEROCOPYSTREAMMANAGER_H
#define CORE_ZEROCOPYSTREAMMANAGER_H

#include <QObject>
#include <QSize>
#include <memory>
#include <functional>

class Demuxer;
class VideoSocket;
class KcpVideoSocket;
struct AVPacket;

namespace qsc {
namespace core {

class ZeroCopyDecoder;
class ZeroCopyRenderer;
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
class ZeroCopyStreamManager : public QObject {
    Q_OBJECT

public:
    explicit ZeroCopyStreamManager(QObject* parent = nullptr);
    ~ZeroCopyStreamManager() override;

    /**
     * @brief 【依赖注入】设置自定义解码器
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
     * @brief 【新架构】安装视频通道（通过接口）
     * @param channel 视频通道（不持有所有权）
     */
    void installVideoChannel(IVideoChannel* channel);

    /**
     * @brief 设置帧尺寸
     */
    void setFrameSize(const QSize& size);

    /**
     * @brief 获取渲染器控件
     * @return 渲染器指针（用于嵌入到 UI）
     */
    ZeroCopyRenderer* renderer() const { return m_renderer.get(); }

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
    quint32 fps() const { return m_currentFps; }

    /**
     * @brief 是否使用硬件加速
     */
    bool isHardwareAccelerated() const;

    /**
     * @brief 获取解码器名称
     */
    QString decoderName() const;

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

signals:
    /**
     * @brief FPS 更新信号
     */
    void fpsUpdated(quint32 fps);

    /**
     * @brief 流停止信号
     */
    void streamStopped();

    /**
     * @brief 帧尺寸变化信号
     */
    void frameSizeChanged(const QSize& size);

    /**
     * @brief 解码器信息信号
     */
    void decoderInfo(bool hardwareAccelerated, const QString& decoderName);

    /**
     * @brief 新帧可用信号（事件驱动通知）
     *
     * 信号发出时，帧已在 FrameQueue 中，消费者调用 consumeFrame() 获取。
     */
    void frameReady();

    /**
     * @brief 新帧可用信号（带数据，兼容旧接口）
     * @deprecated 使用 frameReady() + consumeFrame() 替代
     */
    void frameReadyWithData(int width, int height,
                           uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
                           int linesizeY, int linesizeU, int linesizeV);

private slots:
    void onDemuxerStopped();
    void onDemuxerGetFrame(AVPacket* packet);
    void onDecoderFpsUpdated(quint32 fps);

private:
    bool openDecoder();

private:
    std::unique_ptr<Demuxer> m_demuxer;
    std::unique_ptr<ZeroCopyDecoder> m_decoder;
    std::unique_ptr<FrameQueue> m_frameQueue;
    std::unique_ptr<ZeroCopyRenderer> m_renderer;

    VideoSocket* m_videoSocket = nullptr;
    KcpVideoSocket* m_kcpVideoSocket = nullptr;
    IVideoChannel* m_videoChannel = nullptr;  // 新架构接口

    QSize m_frameSize;
    quint32 m_currentFps = 0;
    bool m_running = false;
    bool m_decoderOpened = false;
    bool m_decoderInjected = false;  // 是否使用注入的解码器
};

} // namespace core
} // namespace qsc

#endif // CORE_ZEROCOPYSTREAMMANAGER_H
