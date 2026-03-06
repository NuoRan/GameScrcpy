#include "StreamManager.h"
#include "ThreadDispatcher.h"
#include "demuxer.h"
#include "decoder.h"
#include "interfaces/IVideoChannel.h"
#include "impl/TcpVideoChannel.h"
#include "impl/KcpVideoChannel.h"

namespace qsc {
namespace core {

StreamManager::StreamManager()
{
    m_fpsTimer.setInterval(1000);
    m_fpsTimer.setCallback([this]() { pollDecoderFps(); });
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
}

void StreamManager::setFrameCallback(FrameCallback callback)
{
    m_frameCallback = std::move(callback);
}

bool StreamManager::start(const Size& frameSize)
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
    m_demuxer->onStreamStop.connect([this]() {
        dispatch::postToMain([this]() { onDemuxerStopped(); });
    });
    m_demuxer->getFrame.connect([this](AVPacket* packet) {
        onGetFrame(packet);
    });

    // 启动解码
    if (!m_demuxer->startDecode()) {
        m_demuxer.reset();
        return false;
    }

    m_running = true;

    // 启动 FPS 轮询定时器
    m_fpsTimer.start();

    // 发送解码器信息
    decoderInfo.fire(m_decoder->isHardwareAccelerated(), m_decoder->hwDecoderName());

    return true;
}

void StreamManager::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;
    m_fpsTimer.stop();

    if (m_demuxer) {
        m_demuxer->stopDecode();
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
    streamStopped.fire();
}

void StreamManager::onGetFrame(AVPacket* packet)
{
    if (m_decoder && packet) {
        m_decoder->push(packet);
    }
}

void StreamManager::pollDecoderFps()
{
    if (!m_decoder) return;
    uint32_t fps = m_decoder->pollFps();
    m_currentFps = fps;
    fpsUpdated.fire(fps);
}

} // namespace core
} // namespace qsc
