#include "ZeroCopyStreamManager.h"
#include "infra/FrameQueue.h"
#include "impl/ZeroCopyDecoder.h"
#include "impl/ZeroCopyRenderer.h"
#include "interfaces/IVideoChannel.h"
#include "demuxer.h"
#include "videosocket.h"
#include "kcpvideosocket.h"

extern "C" {
#include "libavcodec/avcodec.h"
}

#include <QDebug>

namespace qsc {
namespace core {

ZeroCopyStreamManager::ZeroCopyStreamManager(QObject* parent)
    : QObject(parent)
    , m_frameQueue(std::make_unique<FrameQueue>())
    , m_renderer(std::make_unique<ZeroCopyRenderer>())
{
    qInfo("[ZeroCopyStreamManager] Created (zero-copy pipeline)");
}

ZeroCopyStreamManager::~ZeroCopyStreamManager()
{
    stop();
    qInfo("[ZeroCopyStreamManager] Destroyed");
}

void ZeroCopyStreamManager::setDecoder(std::unique_ptr<ZeroCopyDecoder> decoder)
{
    if (m_running) {
        qWarning("[ZeroCopyStreamManager] Cannot set decoder while running");
        return;
    }
    m_decoder = std::move(decoder);
    m_decoderInjected = true;
    qInfo("[ZeroCopyStreamManager] Custom decoder injected");
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

void ZeroCopyStreamManager::setFrameSize(const QSize& size)
{
    m_frameSize = size;
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
        qWarning("[ZeroCopyStreamManager] No video socket installed");
        return false;
    }

    // 设置帧尺寸
    m_demuxer->setFrameSize(m_frameSize);

    // 连接信号
    // 【重要】必须使用 DirectConnection，因为 Demuxer 在子线程运行
    // 如果用 QueuedConnection，slot 执行时 packet 已经被 unref 了
    connect(m_demuxer.get(), &Demuxer::onStreamStop,
            this, &ZeroCopyStreamManager::onDemuxerStopped,
            Qt::QueuedConnection);
    connect(m_demuxer.get(), &Demuxer::getFrame,
            this, &ZeroCopyStreamManager::onDemuxerGetFrame,
            Qt::DirectConnection);

    // 启动 Demuxer（必须用 startDecode 而不是 start，否则停止标志不会重置）
    if (!m_demuxer->startDecode()) {
        qWarning("[ZeroCopyStreamManager] Failed to start demuxer");
        m_demuxer.reset();
        return false;
    }
    m_running = true;

    qInfo("[ZeroCopyStreamManager] Started");

    return true;
}

void ZeroCopyStreamManager::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;

    // 停止 Demuxer（必须等待线程结束再删除！）
    if (m_demuxer) {
        m_demuxer->stopDecode();
        // 等待 Demuxer 线程完全结束，最多等待 3 秒
        if (!m_demuxer->wait(3000)) {
            qWarning("[ZeroCopyStreamManager] Demuxer thread did not stop in time, terminating");
            m_demuxer->terminate();
            m_demuxer->wait(1000);
        }
        m_demuxer.reset();
    }

    // 停止解码器（Demuxer 已完全停止，不会再调用 decode()）
    if (m_decoder) {
        m_decoder->close();
        m_decoder.reset();
    }

    m_decoderOpened = false;

    qInfo("[ZeroCopyStreamManager] Stopped");
}

bool ZeroCopyStreamManager::isHardwareAccelerated() const
{
    return m_decoder ? m_decoder->isHardwareAccelerated() : false;
}

QString ZeroCopyStreamManager::decoderName() const
{
    return m_decoder ? m_decoder->hwDecoderName() : QString();
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
        qInfo("[ZeroCopyStreamManager] Using default ZeroCopyDecoder");
    }

    // 设置帧队列
    m_decoder->setFrameQueue(m_frameQueue.get());

    // 连接信号
    connect(m_decoder.get(), &ZeroCopyDecoder::frameReady,
            this, &ZeroCopyStreamManager::frameReady);
    connect(m_decoder.get(), &ZeroCopyDecoder::fpsUpdated,
            this, &ZeroCopyStreamManager::onDecoderFpsUpdated);

    // 打开解码器（使用 H.264 编码）
    if (!m_decoder->open(AV_CODEC_ID_H264)) {
        qWarning("[ZeroCopyStreamManager] Failed to open decoder");
        return false;
    }

    m_decoderOpened = true;

    qInfo("[ZeroCopyStreamManager] Decoder opened: %s (H.264)%s",
          m_decoder->isHardwareAccelerated() ? qPrintable(m_decoder->hwDecoderName()) : "software",
          m_decoderInjected ? " [injected]" : "");

    emit decoderInfo(m_decoder->isHardwareAccelerated(), m_decoder->hwDecoderName());

    return true;
}

void ZeroCopyStreamManager::onDemuxerStopped()
{
    qInfo("[ZeroCopyStreamManager] Demuxer stopped");
    stop();
    emit streamStopped();
}

void ZeroCopyStreamManager::onDemuxerGetFrame(AVPacket* packet)
{
    if (!packet) {
        return;
    }

    if (!openDecoder()) {
        qWarning("[ZeroCopyStreamManager] Decoder not initialized");
        return;
    }

    m_decoder->decode(packet->data, packet->size, packet->pts);
}

void ZeroCopyStreamManager::onDecoderFpsUpdated(quint32 fps)
{
    m_currentFps = fps;
    emit fpsUpdated(fps);
}

} // namespace core
} // namespace qsc
