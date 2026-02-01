#ifndef VIDEO_BUFFER_H
#define VIDEO_BUFFER_H

#include <QMutex>
#include <QWaitCondition>
#include <QObject>
#include <QElapsedTimer>
#include <atomic>
#include <array>
#include <functional>
#include "fpscounter.h"

// forward declarations
typedef struct AVFrame AVFrame;

// ---------------------------------------------------------
// 缓冲区统计信息
// ---------------------------------------------------------
struct BufferStatistics {
    quint64 totalFrames = 0;      // 总帧数
    quint64 droppedFrames = 0;    // 丢弃帧数
    quint64 renderedFrames = 0;   // 渲染帧数
    double avgQueueDepth = 0.0;   // 平均队列深度
    double dropRate() const {     // 丢帧率
        return totalFrames > 0 ? (double)droppedFrames / totalFrames * 100.0 : 0.0;
    }
};

// ---------------------------------------------------------
// 三缓冲视频帧管理
// 解决双缓冲的生产者-消费者阻塞问题
// ---------------------------------------------------------
class VideoBuffer : public QObject
{
    Q_OBJECT
public:
    // 缓冲策略
    enum class BufferMode {
        Double,     // 传统双缓冲（向后兼容）
        Triple      // 三缓冲（新默认）
    };

    VideoBuffer(QObject *parent = Q_NULLPTR);
    virtual ~VideoBuffer();

    bool init();
    void deInit();
    void lock();
    void unLock();
    void setRenderExpiredFrames(bool renderExpiredFrames);

    // 设置缓冲模式
    void setBufferMode(BufferMode mode);
    BufferMode bufferMode() const { return m_bufferMode; }

    AVFrame *decodingFrame();
    // set the decoder frame as ready for rendering
    // this function locks m_mutex during its execution
    // returns true if the previous frame had been consumed
    void offerDecodedFrame(bool &previousFrameSkipped);

    // mark the rendering frame as consumed and return it
    // MUST be called with m_mutex locked!!!
    // the caller is expected to render the returned frame to some texture before
    // unlocking m_mutex
    const AVFrame *consumeRenderedFrame();

    void peekRenderedFrame(std::function<void(int width, int height, uint8_t* dataRGB32)> onFrame);

    // wake up and avoid any blocking call
    void interrupt();

    // 统计信息
    BufferStatistics statistics() const;
    void resetStatistics();

signals:
    void updateFPS(quint32 fps);

private:
    // 双缓冲交换
    void swap();

    // 三缓冲方法
    void tripleBufferOffer(bool &previousFrameSkipped);
    const AVFrame* tripleBufferConsume();
    int nextIndex(int current) const { return (current + 1) % TRIPLE_BUFFER_SIZE; }

private:
    static constexpr int TRIPLE_BUFFER_SIZE = 3;

    // 缓冲模式
    BufferMode m_bufferMode = BufferMode::Triple;

    // ==== 双缓冲模式 (向后兼容) ====
    AVFrame *m_decodingFrame = Q_NULLPTR;
    AVFrame *m_renderingframe = Q_NULLPTR;

    // ==== 三缓冲模式 ====
    std::array<AVFrame*, TRIPLE_BUFFER_SIZE> m_frames = {nullptr, nullptr, nullptr};
    std::atomic<int> m_writeIndex{0};    // 生产者写入索引
    std::atomic<int> m_readIndex{0};     // 消费者读取索引
    std::atomic<int> m_latestIndex{-1};  // 最新完成的帧索引

    // 通用成员
    QMutex m_mutex;
    bool m_renderingFrameConsumed = true;
    FpsCounter m_fpsCounter;

    bool m_renderExpiredFrames = false;
    QWaitCondition m_renderingFrameConsumedCond;

    // interrupted is not used if expired frames are not rendered
    // since offering a frame will never block
    bool m_interrupted = false;

    // 统计信息
    std::atomic<quint64> m_totalFrames{0};
    std::atomic<quint64> m_droppedFrames{0};
    std::atomic<quint64> m_renderedFrames{0};
    double m_queueDepthSum = 0.0;
    quint64 m_queueDepthCount = 0;
};

#endif // VIDEO_BUFFER_H
