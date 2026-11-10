#include "CaptureFeed.h"

#include <gs/capture/AudioCapture.h>
#include <gs/capture/AudioPairing.h>
#include <gs/capture/Encoders.h>
#include <gs/capture/FrameBuilder.h>
#include <gs/record/MediaFile.h>
#include <gs/record/Recorder.h>

#include <QCoreApplication>
#include <QtLogging>

#include <algorithm>
#include <cmath>
#include <utility>

namespace gs::app {

namespace {

constexpr auto kRateWindow = std::chrono::seconds(1);
constexpr auto kLookEvery = std::chrono::milliseconds(500);
constexpr auto kThumbnailEvery = std::chrono::milliseconds(500);
constexpr int kThumbnailLongSide = 160;

std::uint16_t nextSession(std::uint16_t session) noexcept
{
    return static_cast<std::uint16_t>(session % 0xFFFFU + 1U);
}

}

CaptureFeed::CaptureFeed(ID3D11Device* device, std::unique_ptr<capture::CaptureSource> source)
    : m_device(device), m_source(std::move(source)), m_snapshots(std::make_shared<media::FrameSnapshots>(device)),
      m_whole(std::make_shared<media::FrameSnapshots>(device)), m_thumbnailer(std::make_unique<media::Thumbnailer>(device))
{
    m_builder = std::make_unique<capture::FrameBuilder>(device,
                                                        [this](media::VideoFrame built) { publish(std::move(built)); });
    m_whole->setListener([this](const std::shared_ptr<const media::LumaSnapshot>& shot) {
        if (shot == nullptr || shot->luma.empty()) {
            return;
        }
        const capture::LumaPlane plane{shot->luma.data(), shot->width, shot->height, shot->width};
        if (m_detector.feed(plane, Clock::now()) && m_detector.box()) {
            cropFound(*m_detector.box());
        }
    });
}

CaptureFeed::~CaptureFeed()
{
    setRecorder(nullptr);
    stop();
    m_whole->setListener({});
}

bool CaptureFeed::start(const CaptureOptions& options)
{
    stop();
    {
        const std::scoped_lock lock(m_mutex);
        m_options = options;
        m_manualCrop = options.manualCrop;
        m_autoCrop.reset();
        m_problem.clear();
        m_formats.clear();
        m_chosen.reset();
        m_frameWidth = 0;
        m_frameHeight = 0;
        m_touchSession = nextSession(m_touchSession);
    }
    m_detector.reset();
    m_nextLook = {};
    applyCrop();
    m_running.store(true, std::memory_order_release);
    if (m_source == nullptr || !m_source->start(*this, options.format)) {
        m_running.store(false, std::memory_order_release);
        const std::scoped_lock lock(m_mutex);
        m_problem = m_source != nullptr ? QString::fromStdString(m_source->problem()) : QString();
        if (m_problem.isEmpty()) {
            m_problem = QCoreApplication::translate("CaptureFeed", "The capture device could not be started");
        }
        return false;
    }
    if (options.sound) {
        startSound();
    }
    return true;
}

void CaptureFeed::stop()
{
    m_running.store(false, std::memory_order_release);
    std::unique_ptr<capture::AudioCapture> capture;
    {
        const std::scoped_lock lock(m_soundMutex);
        capture = std::move(m_capture);
    }
    capture.reset();
    {
        const std::scoped_lock lock(m_soundMutex);
        if (m_player != nullptr) {
            m_player->close();
        }
        m_player.reset();
    }
    if (m_source != nullptr) {
        m_source->stop();
    }
    m_builder->reset();
    m_snapshots->forget();
    m_whole->forget();
    m_framesPerSecond.store(0.0, std::memory_order_relaxed);
}

void CaptureFeed::setListener(Listener listener)
{
    const std::scoped_lock lock(m_mutex);
    m_listener = std::move(listener);
}

void CaptureFeed::setRecorder(std::shared_ptr<record::Recorder> recorder)
{
    std::unique_ptr<capture::VideoEncoder> oldVideo;
    std::unique_ptr<capture::AudioEncoder> oldAudio;
    {
        const std::scoped_lock lock(m_recordMutex);
        oldVideo = std::move(m_videoEncoder);
        oldAudio = std::move(m_audioEncoder);
        m_recorder = recorder;
        m_soundSession = 0;
        if (recorder != nullptr) {
            m_videoEncoder = std::make_unique<capture::VideoEncoder>(
                m_device, [recorder](const protocol::MediaPacket& packet) { recorder->video(packet); });
            m_audioEncoder = std::make_unique<capture::AudioEncoder>(
                [this, recorder](const capture::EncodedSound& sound) { recordSound(*recorder, sound); });
        }
    }
    oldVideo.reset();
    oldAudio.reset();
}

void CaptureFeed::setManualCrop(std::optional<capture::CropBox> crop)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (crop == m_manualCrop) {
            return;
        }
        m_manualCrop = crop;
        m_touchSession = nextSession(m_touchSession);
    }
    applyCrop();
    notify(CaptureEvent::Cropped);
}

void CaptureFeed::setMuted(bool muted)
{
    const std::scoped_lock lock(m_soundMutex);
    m_muted = muted;
    if (m_player != nullptr) {
        m_player->setMuted(muted);
    }
}

void CaptureFeed::setVolume(double volume)
{
    const std::scoped_lock lock(m_soundMutex);
    m_volume = std::clamp(volume, 0.0, 1.0);
    if (m_player != nullptr) {
        m_player->setVolume(static_cast<float>(m_volume));
    }
}

QString CaptureFeed::problem() const
{
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_problem.isEmpty()) {
            return m_problem;
        }
    }
    return QString::fromStdString(m_builder->problem());
}

double CaptureFeed::framesPerSecond() const noexcept
{
    return m_framesPerSecond.load(std::memory_order_relaxed);
}

std::optional<media::Thumbnail> CaptureFeed::takeThumbnail()
{
    const std::scoped_lock lock(m_mutex);
    return std::exchange(m_thumbnail, std::nullopt);
}

capture::CropBox CaptureFeed::crop() const
{
    const std::scoped_lock lock(m_mutex);
    return m_manualCrop.value_or(m_autoCrop.value_or(capture::CropBox{}));
}

bool CaptureFeed::manualCrop() const
{
    const std::scoped_lock lock(m_mutex);
    return m_manualCrop.has_value();
}

QSize CaptureFeed::frameSize() const
{
    const std::scoped_lock lock(m_mutex);
    return {m_frameWidth, m_frameHeight};
}

QSize CaptureFeed::cropSize() const
{
    const std::scoped_lock lock(m_mutex);
    if (m_frameWidth < 2 || m_frameHeight < 2) {
        return {m_frameWidth, m_frameHeight};
    }
    const capture::PixelBox box = capture::pixelBoxOf(m_manualCrop.value_or(m_autoCrop.value_or(capture::CropBox{})),
                                                      m_frameWidth, m_frameHeight);
    return {box.width, box.height};
}

std::uint16_t CaptureFeed::touchSession() const
{
    const std::scoped_lock lock(m_mutex);
    return m_touchSession;
}

QString CaptureFeed::formatText() const
{
    const std::scoped_lock lock(m_mutex);
    if (!m_chosen) {
        return {};
    }
    const double rate = std::round(m_chosen->framesPerSecond() * 100.0) / 100.0;
    return QCoreApplication::translate("CaptureFeed", "%1 × %2 · %3 fps · %4")
        .arg(m_chosen->width)
        .arg(m_chosen->height)
        .arg(rate, 0, 'g', 4)
        .arg(QString::fromLatin1(capture::subtypeName(m_chosen->subtype).data(),
                                 static_cast<qsizetype>(capture::subtypeName(m_chosen->subtype).size())));
}

QStringList CaptureFeed::formats() const
{
    const std::scoped_lock lock(m_mutex);
    QStringList names;
    for (const capture::CaptureFormat& format : m_formats) {
        if (!capture::decodable(format.subtype)) {
            continue;
        }
        const QString name = QString::fromStdString(capture::formatName(format));
        if (!names.contains(name)) {
            names.append(name);
        }
    }
    return names;
}

bool CaptureFeed::sound() const
{
    const std::scoped_lock lock(m_soundMutex);
    return m_player != nullptr;
}

media::AudioStats CaptureFeed::audioStats() const
{
    const std::scoped_lock lock(m_soundMutex);
    return m_player != nullptr ? m_player->stats() : media::AudioStats{};
}

void CaptureFeed::offered(const std::vector<capture::CaptureFormat>& formats,
                          const std::optional<capture::CaptureFormat>& chosen)
{
    {
        const std::scoped_lock lock(m_mutex);
        m_formats = formats;
        m_chosen = chosen;
    }
    if (chosen) {
        notify(CaptureEvent::Opened);
    }
}

void CaptureFeed::sample(const capture::CaptureSample& sample)
{
    if (m_running.load(std::memory_order_acquire)) {
        m_builder->build(sample);
    }
}

void CaptureFeed::frame(media::VideoFrame frame)
{
    publish(std::move(frame));
}

void CaptureFeed::ended(const std::string& problem)
{
    {
        const std::scoped_lock lock(m_mutex);
        m_problem = QString::fromStdString(problem);
    }
    qWarning("capture card: %s", problem.c_str());
    notify(CaptureEvent::Failed);
}

void CaptureFeed::feedSound(std::span<const float> interleaved, int channels, int sampleRate, std::int64_t ptsUs)
{
    {
        const std::scoped_lock lock(m_soundMutex);
        if (m_player != nullptr) {
            static_cast<void>(m_player->pushPcm(interleaved, channels, sampleRate, ptsUs));
        }
    }
    const std::scoped_lock lock(m_recordMutex);
    if (m_audioEncoder != nullptr) {
        m_audioEncoder->encode(interleaved, channels, sampleRate, ptsUs);
    }
}

void CaptureFeed::publish(media::VideoFrame frame)
{
    if (!frame || !m_running.load(std::memory_order_acquire)) {
        return;
    }
    const Clock::time_point now = Clock::now();
    bool resized = false;
    capture::PixelBox region;
    {
        const std::scoped_lock lock(m_mutex);
        if (frame.width() != m_frameWidth || frame.height() != m_frameHeight) {
            m_frameWidth = frame.width();
            m_frameHeight = frame.height();
            m_autoCrop.reset();
            m_touchSession = nextSession(m_touchSession);
            resized = true;
        }
        region = capture::pixelBoxOf(m_manualCrop.value_or(m_autoCrop.value_or(capture::CropBox{})), m_frameWidth,
                                     m_frameHeight);
    }
    if (resized) {
        applyCrop();
        notify(CaptureEvent::Cropped);
    }
    if (now >= m_nextLook) {
        m_nextLook = now + kLookEvery;
        m_whole->request();
    }
    m_whole->offer(frame);
    m_whole->poll();
    m_snapshots->offer(frame);
    m_snapshots->poll();
    if (std::optional<media::Thumbnail> made = m_thumbnailer->collect()) {
        {
            const std::scoped_lock lock(m_mutex);
            m_thumbnail = std::move(made);
        }
        notify(CaptureEvent::Thumbnailed);
    }
    if (now >= m_nextThumbnail) {
        m_nextThumbnail = now + kThumbnailEvery;
        m_thumbnailer->request(frame, kThumbnailLongSide);
    }
    {
        const std::scoped_lock lock(m_recordMutex);
        if (m_videoEncoder != nullptr) {
            m_videoEncoder->encode(frame, region, frame.receivedUs());
        }
    }
    if (m_benchFed.load(std::memory_order_relaxed)) {
        m_bench->put(frame.share());
    }
    m_slot->put(std::move(frame));
    m_published.fetch_add(1, std::memory_order_relaxed);
    ++m_windowFrames;
    measure(now);
}

void CaptureFeed::measure(Clock::time_point now)
{
    const Clock::duration elapsed = now - m_windowStart;
    if (elapsed < kRateWindow) {
        return;
    }
    m_framesPerSecond.store(static_cast<double>(m_windowFrames) / std::chrono::duration<double>(elapsed).count(),
                            std::memory_order_relaxed);
    m_windowFrames = 0;
    m_windowStart = now;
}

void CaptureFeed::cropFound(const capture::CropBox& box)
{
    bool changed = false;
    {
        const std::scoped_lock lock(m_mutex);
        if (m_autoCrop != box) {
            m_autoCrop = box;
            if (!m_manualCrop) {
                m_touchSession = nextSession(m_touchSession);
                changed = true;
            }
        }
    }
    if (changed) {
        qInfo("capture card: the phone screen is at %.3f, %.3f, %.3f × %.3f of the picture", box.x, box.y, box.width,
              box.height);
        applyCrop();
        notify(CaptureEvent::Cropped);
    }
}

void CaptureFeed::applyCrop()
{
    const capture::CropBox box = crop();
    if (box.whole()) {
        m_snapshots->clearCrop();
    } else {
        m_snapshots->setCrop(box.x, box.y, box.width, box.height);
    }
}

void CaptureFeed::startSound()
{
    const std::wstring container = m_source->container();
    if (container.empty()) {
        return;
    }
    const std::vector<capture::AudioEndpoint> endpoints = capture::listCaptureEndpoints();
    const std::optional<std::size_t> paired = capture::pairedEndpoint(container, endpoints);
    if (!paired) {
        qInfo("capture card: no sound device of its own was found");
        return;
    }
    auto player = std::make_unique<media::AudioPlayer>();
    {
        const std::scoped_lock lock(m_soundMutex);
        player->setMuted(m_muted);
        player->setVolume(static_cast<float>(m_volume));
        m_player = std::move(player);
    }
    auto capture = std::make_unique<capture::AudioCapture>(
        endpoints[*paired].id, [this](std::span<const float> samples, int channels, int sampleRate, std::int64_t ptsUs) {
            feedSound(samples, channels, sampleRate, ptsUs);
        });
    {
        const std::scoped_lock lock(m_soundMutex);
        m_capture = std::move(capture);
    }
    qInfo("capture card: playing the sound of %s", endpoints[*paired].name.c_str());
}

void CaptureFeed::notify(CaptureEvent event)
{
    Listener listener;
    {
        const std::scoped_lock lock(m_mutex);
        listener = m_listener;
    }
    if (listener) {
        listener(event);
    }
}

void CaptureFeed::recordSound(record::Recorder& recorder, const capture::EncodedSound& sound)
{
    if (sound.config) {
        m_soundSession = nextSession(m_soundSession);
        const std::vector<std::uint8_t> payload = record::aacSessionPayload(sound.sampleRate, sound.channels);
        protocol::MediaPacket described;
        described.header.kind = protocol::MediaKind::Session;
        described.header.session = m_soundSession;
        described.header.size = static_cast<std::uint32_t>(payload.size());
        described.session = record::aacSessionInfo(sound.sampleRate, sound.channels);
        described.payload = payload;
        recorder.audio(described);
        protocol::MediaPacket configured;
        configured.header.kind = protocol::MediaKind::Config;
        configured.header.session = m_soundSession;
        configured.header.size = static_cast<std::uint32_t>(sound.bytes.size());
        configured.payload = sound.bytes;
        recorder.audio(configured);
        return;
    }
    if (m_soundSession == 0) {
        return;
    }
    protocol::MediaPacket clip;
    clip.header.kind = protocol::MediaKind::Frame;
    clip.header.keyFrame = true;
    clip.header.session = m_soundSession;
    clip.header.ptsUs = sound.ptsUs;
    clip.header.size = static_cast<std::uint32_t>(sound.bytes.size());
    clip.payload = sound.bytes;
    recorder.audio(clip);
}

}
