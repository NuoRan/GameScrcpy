#include "videobuffer.h"
#include "avframeconvert.h"
#include <QDebug>

extern "C"
{
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/imgutils.h"
}

VideoBuffer::VideoBuffer(QObject *parent) : QObject(parent) {
    connect(&m_fpsCounter, &FpsCounter::updateFPS, this, &VideoBuffer::updateFPS);
}

VideoBuffer::~VideoBuffer() {}

bool VideoBuffer::init()
{
    if (m_bufferMode == BufferMode::Triple) {
        // 三缓冲模式
        for (int i = 0; i < TRIPLE_BUFFER_SIZE; ++i) {
            m_frames[i] = av_frame_alloc();
            if (!m_frames[i]) {
                goto error;
            }
        }
        m_writeIndex = 0;
        m_readIndex = 0;
        m_latestIndex = -1;
        // 三缓冲模式初始化完成
    } else {
        // 双缓冲模式（向后兼容）
        m_decodingFrame = av_frame_alloc();
        if (!m_decodingFrame) {
            goto error;
        }

        m_renderingframe = av_frame_alloc();
        if (!m_renderingframe) {
            goto error;
        }
        // 双缓冲模式初始化完成
    }

    // there is initially no rendering frame, so consider it has already been
    // consumed
    m_renderingFrameConsumed = true;

    m_fpsCounter.start();
    return true;

error:
    deInit();
    return false;
}

void VideoBuffer::deInit()
{
    if (m_bufferMode == BufferMode::Triple) {
        for (int i = 0; i < TRIPLE_BUFFER_SIZE; ++i) {
            if (m_frames[i]) {
                av_frame_free(&m_frames[i]);
                m_frames[i] = nullptr;
            }
        }
    } else {
        if (m_decodingFrame) {
            av_frame_free(&m_decodingFrame);
            m_decodingFrame = Q_NULLPTR;
        }
        if (m_renderingframe) {
            av_frame_free(&m_renderingframe);
            m_renderingframe = Q_NULLPTR;
        }
    }
    m_fpsCounter.stop();
}

void VideoBuffer::lock()
{
    m_mutex.lock();
}

void VideoBuffer::unLock()
{
    m_mutex.unlock();
}

void VideoBuffer::setRenderExpiredFrames(bool renderExpiredFrames)
{
    m_renderExpiredFrames = renderExpiredFrames;
}

void VideoBuffer::setBufferMode(BufferMode mode)
{
    if (m_bufferMode != mode) {
        // 只能在初始化前更改
        m_bufferMode = mode;
    }
}

AVFrame *VideoBuffer::decodingFrame()
{
    if (m_bufferMode == BufferMode::Triple) {
        return m_frames[m_writeIndex.load()];
    }
    return m_decodingFrame;
}

void VideoBuffer::offerDecodedFrame(bool &previousFrameSkipped)
{
    m_totalFrames++;

    if (m_bufferMode == BufferMode::Triple) {
        tripleBufferOffer(previousFrameSkipped);
        return;
    }

    // 原双缓冲逻辑
    m_mutex.lock();

    if (m_renderExpiredFrames) {
        // if m_renderExpiredFrames is enable, then the decoder must wait for the current
        // frame to be consumed
        while (!m_renderingFrameConsumed && !m_interrupted) {
            m_renderingFrameConsumedCond.wait(&m_mutex);
        }
    } else {
        if (m_fpsCounter.isStarted() && !m_renderingFrameConsumed) {
            m_fpsCounter.addSkippedFrame();
            m_droppedFrames++;
        }
    }

    swap();
    previousFrameSkipped = !m_renderingFrameConsumed;
    m_renderingFrameConsumed = false;
    m_mutex.unlock();
}

void VideoBuffer::tripleBufferOffer(bool &previousFrameSkipped)
{
    // 三缓冲：生产者永不阻塞
    int current = m_writeIndex.load();
    int latest = m_latestIndex.load();

    // 检查是否跳过了之前的帧
    previousFrameSkipped = (latest >= 0 && latest != m_readIndex.load());
    if (previousFrameSkipped && m_fpsCounter.isStarted()) {
        m_fpsCounter.addSkippedFrame();
        m_droppedFrames++;
    }

    // 将当前帧标记为最新完成的帧
    m_latestIndex.store(current);

    // 移动到下一个写入槽位
    // 确保不覆盖正在读取的帧
    int next = nextIndex(current);
    int readIdx = m_readIndex.load();

    if (next == readIdx) {
        // 如果下一个位置正在被读取，跳过它
        next = nextIndex(next);
    }

    m_writeIndex.store(next);

    // 更新队列深度统计
    int depth = 0;
    if (latest >= 0) {
        if (latest >= readIdx) {
            depth = latest - readIdx;
        } else {
            depth = TRIPLE_BUFFER_SIZE - readIdx + latest;
        }
    }
    m_queueDepthSum += depth;
    m_queueDepthCount++;
}

const AVFrame* VideoBuffer::tripleBufferConsume()
{
    int latest = m_latestIndex.load();

    if (latest < 0) {
        // 没有可用的帧
        return nullptr;
    }

    // 更新读取索引
    m_readIndex.store(latest);

    if (m_fpsCounter.isStarted()) {
        m_fpsCounter.addRenderedFrame();
    }
    m_renderedFrames++;

    return m_frames[latest];
}

const AVFrame *VideoBuffer::consumeRenderedFrame()
{
    if (m_bufferMode == BufferMode::Triple) {
        return tripleBufferConsume();
    }

    // 原双缓冲逻辑
    Q_ASSERT(!m_renderingFrameConsumed);
    m_renderingFrameConsumed = true;
    if (m_fpsCounter.isStarted()) {
        m_fpsCounter.addRenderedFrame();
    }
    m_renderedFrames++;

    if (m_renderExpiredFrames) {
        // if m_renderExpiredFrames is enable, then notify the decoder the current frame is
        // consumed, so that it may push a new one
        m_renderingFrameConsumedCond.wakeOne();
    }
    return m_renderingframe;
}

void VideoBuffer::peekRenderedFrame(std::function<void(int width, int height, uint8_t* dataRGB32)> onFrame)
{
    if (!onFrame) {
        return;
    }

    lock();

    AVFrame* frame = nullptr;
    if (m_bufferMode == BufferMode::Triple) {
        int latest = m_latestIndex.load();
        if (latest >= 0) {
            frame = m_frames[latest];
        }
    } else {
        frame = m_renderingframe;
    }

    if (!frame || frame->width <= 0 || frame->height <= 0) {
        unLock();
        return;
    }

    int width = frame->width;
    int height = frame->height;
    int linesize = frame->linesize[0];

    // create buffer
    uint8_t* rgbBuffer = new uint8_t[linesize * height * 4];
    AVFrame *rgbFrame = av_frame_alloc();
    if (!rgbFrame) {
        delete [] rgbBuffer;
        unLock();
        return;
    }

    // bind buffer to AVFrame
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgbBuffer, AV_PIX_FMT_RGB32, width, height, 4);

    // convert
    AVFrameConvert convert;
    convert.setSrcFrameInfo(width, height, AV_PIX_FMT_YUV420P);
    convert.setDstFrameInfo(width, height, AV_PIX_FMT_RGB32);
    bool ret = false;
    ret = convert.init();
    if (!ret) {
        delete [] rgbBuffer;
        av_free(rgbFrame);
        unLock();
        return;
    }
    ret = convert.convert(frame, rgbFrame);
    if (!ret) {
        delete [] rgbBuffer;
        av_free(rgbFrame);
        unLock();
        return;
    }
    convert.deInit();
    av_free(rgbFrame);
    unLock();

    onFrame(width, height, rgbBuffer);
    delete [] rgbBuffer;
}

void VideoBuffer::interrupt()
{
    if (m_renderExpiredFrames) {
        m_mutex.lock();
        m_interrupted = true;
        m_mutex.unlock();
        // wake up blocking wait
        m_renderingFrameConsumedCond.wakeOne();
    }
}

void VideoBuffer::swap()
{
    AVFrame *tmp = m_decodingFrame;
    m_decodingFrame = m_renderingframe;
    m_renderingframe = tmp;
}

BufferStatistics VideoBuffer::statistics() const
{
    BufferStatistics stats;
    stats.totalFrames = m_totalFrames.load();
    stats.droppedFrames = m_droppedFrames.load();
    stats.renderedFrames = m_renderedFrames.load();

    if (m_queueDepthCount > 0) {
        stats.avgQueueDepth = m_queueDepthSum / m_queueDepthCount;
    }

    return stats;
}

void VideoBuffer::resetStatistics()
{
    m_totalFrames = 0;
    m_droppedFrames = 0;
    m_renderedFrames = 0;
    m_queueDepthSum = 0.0;
    m_queueDepthCount = 0;
}

