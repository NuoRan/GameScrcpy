#include "ZeroCopyStreamManager.h"
#include "ThreadDispatcher.h"
#include "infra/FrameQueue.h"
#include "impl/ZeroCopyDecoder.h"

#include "interfaces/IVideoChannel.h"
#include "demuxer.h"
#include "videosocket.h"
#include "kcpvideosocket.h"

extern "C" {
#include "libavcodec/avcodec.h"
}

#define LOG_TAG "StreamManager"
#include "Logger.h"

namespace qsc {
namespace core {

ZeroCopyStreamManager::ZeroCopyStreamManager()
    : m_frameQueue(std::make_unique<FrameQueue>())
{
    LOG_I("[ZeroCopyStreamManager] Created (zero-copy pipeline)");
}

ZeroCopyStreamManager::~ZeroCopyStreamManager()
{
    stop();
    LOG_I("[ZeroCopyStreamManager] Destroyed");
}

void ZeroCopyStreamManager::setDecoder(std::unique_ptr<ZeroCopyDecoder> decoder)
{
    if (m_running) {
        LOG_W("[ZeroCopyStreamManager] Cannot set decoder while running");
        return;
    }
    m_decoder = std::move(decoder);
    m_decoderInjected = true;
    LOG_I("[ZeroCopyStreamManager] Custom decoder injected");
}

void ZeroCopyStreamManager::installVideoSocket(VideoSocket* socket)
{
    m_videoSocket = socket;
}

void ZeroCopyStreamManager::installKcpVideoSocket(KcpVideoSocket* socket)
{
    m_kcpVideoSocket = socket;
}

void ZeroCopyStreamManager::installVideoChannel(IVideoChannel* channel)
{
    m_videoChannel = channel;
}

void ZeroCopyStreamManager::setFrameSize(const Size& size)
{
    m_frameSize = size;
}

void ZeroCopyStreamManager::setVideoCodec(const std::string& codec)
{
    m_videoCodec = codec;
    LOG_I("[ZeroCopyStreamManager] Video codec set to: %s", codec.c_str());
}

bool ZeroCopyStreamManager::start()
{
    if (m_running) {
        return true;
    }

    // 创建 Demuxer
    m_demuxer = std::make_unique<Demuxer>();

    // 安装视频 Socket
    if (m_kcpVideoSocket) {
        m_demuxer->installKcpVideoSocket(m_kcpVideoSocket);
    } else if (m_videoSocket) {
        m_demuxer->installVideoSocket(m_videoSocket);
    } else {
        LOG_W("[ZeroCopyStreamManager] No video socket installed");
        return false;
    }

    // 设置帧尺寸和编解码器
    m_demuxer->setFrameSize(m_frameSize);
    m_demuxer->setVideoCodec(m_videoCodec);

    // 连接信号
    // onStreamStop 从工作线程发出，需要投递到主线程
    m_demuxer->onStreamStop.connect([this]() {
        dispatch::postToMain([this]() { onDemuxerStopped(); });
    });
    // getFrame 必须同步调用（类似 DirectConnection），因为 packet 在下一帧时会被重用
    m_demuxer->getFrame.connect([this](AVPacket* packet) {
        onDemuxerGetFrame(packet);
    });

    // 启动 Demuxer（必须用 startDecode 而不是 start，否则停止标志不会重置）
    if (!m_demuxer->startDecode()) {
        LOG_W("[ZeroCopyStreamManager] Failed to start demuxer");
        m_demuxer.reset();
        return false;
    }
    m_running = true;

    LOG_I("[ZeroCopyStreamManager] Started");

    return true;
}

void ZeroCopyStreamManager::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;

    // 停止 Demuxer（stopDecode 内部已等待线程结束）
    if (m_demuxer) {
        m_demuxer->stopDecode();
        m_demuxer.reset();
    }

    // 停止解码器（Demuxer 已完全停止，不会再调用 decode()）
    if (m_decoder) {
        m_decoder->close();
        m_decoder.reset();
    }

    m_decoderOpened = false;

    LOG_I("[ZeroCopyStreamManager] Stopped");
}

bool ZeroCopyStreamManager::isHardwareAccelerated() const
{
    return m_decoder ? m_decoder->isHardwareAccelerated() : false;
}

std::string ZeroCopyStreamManager::decoderName() const
{
    return m_decoder ? m_decoder->hwDecoderName() : std::string();
}

void ZeroCopyStreamManager::screenshot(ScreenshotCallback callback)
{
    if (m_decoder) {
        m_decoder->peekFrame(callback);
    }
}

FrameData* ZeroCopyStreamManager::consumeFrame()
{
    if (!m_frameQueue) return nullptr;
    return m_frameQueue->popFrame();
}

void ZeroCopyStreamManager::releaseFrame(FrameData* frame)
{
    if (m_frameQueue && frame) {
        m_frameQueue->releaseFrame(frame);
    }
}

bool ZeroCopyStreamManager::openDecoder()
{
    if (m_decoderOpened) {
        return true;
    }

    // 如果没有注入解码器，创建默认的零拷贝解码器
    if (!m_decoder) {
        m_decoder = std::make_unique<ZeroCopyDecoder>();
        LOG_I("[ZeroCopyStreamManager] Using default ZeroCopyDecoder");
    }

    // 设置帧队列
    m_decoder->setFrameQueue(m_frameQueue.get());

    // 连接信号 (ZeroCopyDecoder uses Signal<>, not Qt signals)
    m_decoder->frameReady.connect([this]() { frameReady.fire(); });
    m_decoder->fpsUpdated.connect([this](uint32_t fps) { onDecoderFpsUpdated(fps); });

    // 根据配置确定解码器 codec ID
    AVCodecID codecId = AV_CODEC_ID_H264;
    const char* codecLabel = "H.264";
    if (m_videoCodec == "h265") {
        codecId = AV_CODEC_ID_HEVC;
        codecLabel = "H.265";
    }

    // 打开解码器
    if (!m_decoder->open(codecId)) {
        LOG_W("[ZeroCopyStreamManager] Failed to open decoder");
        return false;
    }

    m_decoderOpened = true;

    LOG_I("[ZeroCopyStreamManager] Decoder opened: %s (%s)%s",
          m_decoder->isHardwareAccelerated() ? m_decoder->hwDecoderName().c_str() : "software",
          codecLabel,
          m_decoderInjected ? " [injected]" : "");

    decoderInfo.fire(m_decoder->isHardwareAccelerated(), m_decoder->hwDecoderName());

    return true;
}

void ZeroCopyStreamManager::onDemuxerStopped()
{
    LOG_I("[ZeroCopyStreamManager] Demuxer stopped");
    stop();
    streamStopped.fire();
}

void ZeroCopyStreamManager::onDemuxerGetFrame(AVPacket* packet)
{
    if (!packet) {
        return;
    }

    if (!openDecoder()) {
        LOG_W("[ZeroCopyStreamManager] Decoder not initialized");
        return;
    }

    static int s_frameCount = 0;
    if (s_frameCount < 1) {
        LOG_I("[ZeroCopyStreamManager] onDemuxerGetFrame #%d: size=%d pts=%lld flags=0x%x",
              s_frameCount, packet->size, (long long)packet->pts, packet->flags);
    }

    m_decoder->decode(packet->data, packet->size, packet->pts, packet->flags);

    if (s_frameCount < 1) {
        LOG_I("[ZeroCopyStreamManager] decode #%d done", s_frameCount);
        s_frameCount++;
    }
}

void ZeroCopyStreamManager::onDecoderFpsUpdated(uint32_t fps)
{
    m_currentFps = fps;
    fpsUpdated.fire(fps);
}

} // namespace core
} // namespace qsc
