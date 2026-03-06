#ifndef VIDEO_BUFFER_H
#define VIDEO_BUFFER_H

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <array>
#include <functional>
#include <cstdint>
#include "fpscounter.h"

// forward declarations
typedef struct AVFrame AVFrame;

// ---------------------------------------------------------
// 缓冲区统计信息 / Buffer Statistics
// ---------------------------------------------------------
struct BufferStatistics {
    uint64_t totalFrames = 0;      // 总帧数 / Total frames
    uint64_t droppedFrames = 0;    // 丢弃帧数 / Dropped frames
    uint64_t renderedFrames = 0;   // 渲染帧数 / Rendered frames
    double avgQueueDepth = 0.0;   // 平均队列深度 / Average queue depth
    double dropRate() const {     // 丢帧率 / Drop rate (%)
        return totalFrames > 0 ? (double)droppedFrames / totalFrames * 100.0 : 0.0;
    }
};

// ---------------------------------------------------------
// 三缓冲视频帧管理 / Triple-Buffered Video Frame Manager
// 解决双缓冲的生产者-消费者阻塞问题 / Solves producer-consumer blocking of double-buffering
// 纯 C++ 实现，无 QObject 依赖。
// ---------------------------------------------------------
class VideoBuffer
{
public:
    // 缓冲策略 / Buffer strategy
    enum class BufferMode {
        Double,     // 传统双缓冲 / Traditional double-buffering (backward compat)
        Triple      // 三缓冲 / Triple-buffering (new default)
    };

    VideoBuffer();
    ~VideoBuffer();

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

    /// 轮询 FPS（GUI 层每秒调用一次）
    /// Poll FPS — GUI layer calls once per second
    uint32_t pollFps() { return m_fpsCounter.pollFps(); }
    uint32_t lastFps() const { return m_fpsCounter.lastFps(); }

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
    AVFrame *m_decodingFrame = nullptr;
    AVFrame *m_renderingframe = nullptr;

    // ==== 三缓冲模式 ====
    std::array<AVFrame*, TRIPLE_BUFFER_SIZE> m_frames = {nullptr, nullptr, nullptr};
    std::atomic<int> m_writeIndex{0};    // 生产者写入索引
    std::atomic<int> m_readIndex{0};     // 消费者读取索引
    std::atomic<int> m_latestIndex{-1};  // 最新完成的帧索引

    // 通用成员
    std::mutex m_mutex;
    bool m_renderingFrameConsumed = true;
    FpsCounter m_fpsCounter;

    bool m_renderExpiredFrames = false;
    std::condition_variable m_renderingFrameConsumedCond;

    // interrupted is not used if expired frames are not rendered
    // since offering a frame will never block
    bool m_interrupted = false;

    // 统计信息
    std::atomic<uint64_t> m_totalFrames{0};
    std::atomic<uint64_t> m_droppedFrames{0};
    std::atomic<uint64_t> m_renderedFrames{0};
    double m_queueDepthSum = 0.0;
    uint64_t m_queueDepthCount = 0;
};

#endif // VIDEO_BUFFER_H
