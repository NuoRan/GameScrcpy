#ifndef CORE_STREAMMANAGER_H
#define CORE_STREAMMANAGER_H

#include <QObject>
#include <QSize>
#include <memory>
#include <functional>

// 前向声明
class Demuxer;
class Decoder;
struct AVPacket;

namespace qsc {
namespace core {

// 前向声明
class IVideoChannel;
class IDecoder;
class IRenderer;
struct FrameData;

/**
 * @brief 流管理器 / Stream Manager
 *
 * 管理视频流的接收、解码和渲染流程 / Manages video stream receive, decode, and render pipeline:
 * VideoChannel -> Demuxer -> Decoder -> Renderer
 *
 * 职责 / Responsibilities:
 * - 协调视频通道、解码器、渲染器的生命周期 / Coordinate lifecycles of video channel, decoder, renderer
 * - 处理 Scrcpy 协议（解析头部、提取编码信息）/ Handle Scrcpy protocol (parse header, extract codec info)
 * - 管理 FPS 统计和性能监控 / Manage FPS statistics and performance monitoring
 */
class StreamManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 帧回调类型（渲染用）
     */
    using FrameCallback = std::function<void(int width, int height,
                                             uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
                                             int linesizeY, int linesizeU, int linesizeV)>;

    explicit StreamManager(QObject* parent = nullptr);
    ~StreamManager() override;

    /**
     * @brief 设置视频通道（依赖注入）
     */
    void setVideoChannel(IVideoChannel* channel);

    /**
     * @brief 设置解码器（依赖注入）
     */
    void setDecoder(Decoder* decoder);

    /**
     * @brief 设置帧回调
     */
    void setFrameCallback(FrameCallback callback);

    /**
     * @brief 启动流处理
     * @param frameSize 预期帧尺寸
     * @return 成功返回 true
     */
    bool start(const QSize& frameSize);

    /**
     * @brief 停止流处理
     */
    void stop();

    /**
     * @brief 是否正在运行
     */
    bool isRunning() const;

    /**
     * @brief 获取当前帧尺寸
     */
    QSize frameSize() const { return m_frameSize; }

    /**
     * @brief 获取当前 FPS
     */
    quint32 fps() const { return m_currentFps; }

    /**
     * @brief 截图（获取当前帧的 RGB 数据）
     */
    using ScreenshotCallback = std::function<void(int width, int height, uint8_t* dataRGB32)>;
    void screenshot(ScreenshotCallback callback);

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
     * @brief 视频尺寸变化信号
     */
    void frameSizeChanged(const QSize& size);

    /**
     * @brief 解码器信息信号
     */
    void decoderInfo(bool hardwareAccelerated, const QString& decoderName);

private slots:
    void onDemuxerStopped();
    void onGetFrame(AVPacket* packet);
    void onDecoderFpsUpdated(quint32 fps);

private:
    // 使用现有的 Demuxer 和 Decoder
    std::unique_ptr<Demuxer> m_demuxer;
    Decoder* m_decoder = nullptr;  // 外部管理
    IVideoChannel* m_videoChannel = nullptr;  // 外部管理

    FrameCallback m_frameCallback;
    QSize m_frameSize;
    quint32 m_currentFps = 0;
    bool m_running = false;
};

} // namespace core
} // namespace qsc

#endif // CORE_STREAMMANAGER_H
