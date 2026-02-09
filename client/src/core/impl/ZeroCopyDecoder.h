#ifndef CORE_ZEROCOPYDECODER_H
#define CORE_ZEROCOPYDECODER_H

#include "../interfaces/IDecoder.h"
#include "../infra/FrameQueue.h"
#include <memory>
#include <QString>
#include <QObject>
#include <QMutex>
#include <QElapsedTimer>
#include <atomic>

// FFmpeg 前向声明（必须在 namespace 外部）
struct AVCodecContext;
struct AVFrame;
struct AVBufferRef;
struct AVPacket;
struct AVCodec;

namespace qsc {
namespace core {

/**
 * @brief 零拷贝 FFmpeg 解码器 / Zero-Copy FFmpeg Decoder
 *
 * 直接将解码数据写入 FramePool 管理的内存，通过 FrameQueue 传递给渲染器。
 * Writes decoded data directly into FramePool-managed memory, passes to renderer via FrameQueue.
 *
 * 特性 / Features:
 * - 零拷贝：解码数据直接写入预分配帧 / Zero-copy: decode into pre-allocated frames
 * - 硬件加速：支持 D3D11VA/VideoToolbox/VAAPI / HW accel support
 * - H.264/H.265 双编码支持 / Dual codec support
 * - 线程安全 / Thread-safe
 */
class ZeroCopyDecoder : public QObject, public IDecoder {
    Q_OBJECT

public:
    explicit ZeroCopyDecoder(QObject* parent = nullptr);
    ~ZeroCopyDecoder() override;

    // IDecoder 接口实现
    bool open(int codecId) override;
    void close() override;
    bool decode(const uint8_t* data, int size, int64_t pts) override;
    void setFrameCallback(FrameCallback callback) override;
    bool isHardwareAccelerated() const override;
    const char* name() const override { return "ZeroCopyFFmpeg"; }

    /**
     * @brief 设置帧队列（用于零拷贝输出）
     * @param queue 帧队列指针（生命周期由外部管理）
     */
    void setFrameQueue(FrameQueue* queue);

    /**
     * @brief 获取硬件解码器名称
     */
    QString hwDecoderName() const { return m_hwDecoderName; }

    /**
     * @brief 获取当前帧用于截图
     */
    void peekFrame(std::function<void(int width, int height, uint8_t* dataRGB32)> callback);

signals:
    /**
     * @brief FPS 更新信号
     */
    void fpsUpdated(quint32 fps);

    /**
     * @brief 新帧可用信号
     */
    void frameReady();

private:
    bool initHardwareDecoder(const AVCodec* codec);
    bool transferHwFrame(AVFrame* hwFrame, AVFrame* swFrame);
    void processDecodedFrame(AVFrame* frame);
    void updateFps();

private:
    AVCodecContext* m_codecCtx = nullptr;
    AVBufferRef* m_hwDeviceCtx = nullptr;
    AVFrame* m_hwFrame = nullptr;       // 硬件帧（GPU内存）
    AVFrame* m_swFrame = nullptr;       // 软件帧（用于硬解转换）
    AVFrame* m_decodeFrame = nullptr;   // 解码输出帧
    AVPacket* m_packet = nullptr;       // 复用的 AVPacket

    int m_hwPixFmt = -1;  // AVPixelFormat 值
    bool m_isOpen = false;
    QString m_hwDecoderName;
    int m_codecId = 0;

    // 零拷贝输出
    FrameQueue* m_frameQueue = nullptr;
    FrameCallback m_frameCallback;

    // FPS 统计
    std::atomic<quint32> m_frameCount{0};
    std::atomic<quint32> m_currentFps{0};
    QElapsedTimer m_fpsTimer;  // monotonic clock，无系统调用开销

    // 截图缓存：保留最后一帧 AVFrame 的引用，避免每帧拷贝
    QMutex m_screenshotMutex;
    AVFrame* m_lastAVFrame = nullptr;  // 引用计数的 AVFrame 副本
    std::vector<uint8_t> m_lastFrameY;  // 懒转换缓存
    std::vector<uint8_t> m_lastFrameU;
    std::vector<uint8_t> m_lastFrameV;
    int m_lastWidth = 0;
    int m_lastHeight = 0;
    bool m_screenshotCacheStale = true;  // 截图缓存是否过期

    // 分辨率变化检测（仅用于日志）
    int m_decodedWidth = 0;
    int m_decodedHeight = 0;
};

} // namespace core
} // namespace qsc

#endif // CORE_ZEROCOPYDECODER_H
