#include "StreamManager.h"
#include "demuxer.h"
#include "decoder.h"
#include "interfaces/IVideoChannel.h"
#include "impl/TcpVideoChannel.h"
#include "impl/KcpVideoChannel.h"

namespace qsc {
namespace core {

StreamManager::StreamManager(QObject* parent)
    : QObject(parent)
{
}

StreamManager::~StreamManager()
{
    stop();
}

void StreamManager::setVideoChannel(IVideoChannel* channel)
{
    m_videoChannel = channel;
}

void StreamManager::setDecoder(Decoder* decoder)
{
    m_decoder = decoder;

    if (m_decoder) {
        // 连接 FPS 更新信号
        connect(m_decoder, &Decoder::updateFPS, this, &StreamManager::onDecoderFpsUpdated);
    }
}

void StreamManager::setFrameCallback(FrameCallback callback)
{
    m_frameCallback = std::move(callback);
}

bool StreamManager::start(const QSize& frameSize)
{
    if (m_running) {
        return true;
    }

    if (!m_videoChannel || !m_decoder) {
        return false;
    }

    m_frameSize = frameSize;

    // 创建 Demuxer
    m_demuxer = std::make_unique<Demuxer>();
    m_demuxer->setFrameSize(frameSize);

    // 根据通道类型安装对应的 socket
    if (auto* tcpChannel = dynamic_cast<TcpVideoChannel*>(m_videoChannel)) {
        m_demuxer->installVideoSocket(tcpChannel->socket());
    } else if (auto* kcpChannel = dynamic_cast<KcpVideoChannel*>(m_videoChannel)) {
        m_demuxer->installKcpVideoSocket(kcpChannel->socket());
    } else {
        return false;
    }

    // 连接信号
    connect(m_demuxer.get(), &Demuxer::onStreamStop, this, &StreamManager::onDemuxerStopped);
    connect(m_demuxer.get(), &Demuxer::getFrame, this, &StreamManager::onGetFrame);

    // 启动解码
    if (!m_demuxer->startDecode()) {
        m_demuxer.reset();
        return false;
    }

    m_running = true;

    // 发送解码器信息
    emit decoderInfo(m_decoder->isHardwareAccelerated(), m_decoder->hwDecoderName());

    return true;
}

void StreamManager::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;

    if (m_demuxer) {
        m_demuxer->stopDecode();
        m_demuxer->wait();
        m_demuxer.reset();
    }
}

bool StreamManager::isRunning() const
{
    return m_running;
}

void StreamManager::screenshot(ScreenshotCallback callback)
{
    if (m_decoder && callback) {
        m_decoder->peekFrame(callback);
    }
}

void StreamManager::onDemuxerStopped()
{
    m_running = false;
    emit streamStopped();
}

void StreamManager::onGetFrame(AVPacket* packet)
{
    if (m_decoder && packet) {
        m_decoder->push(packet);
    }
}

void StreamManager::onDecoderFpsUpdated(quint32 fps)
{
    m_currentFps = fps;
    emit fpsUpdated(fps);
}

} // namespace core
} // namespace qsc
