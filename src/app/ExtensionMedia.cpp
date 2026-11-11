#include "ExtensionMedia.h"

#include <gs/capture/CaptureDevices.h>
#include <gs/capture/CaptureFormat.h>

#include <nlohmann/json.hpp>

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace gs::app {

namespace {

constexpr QStringView kSourcePrefix = u"source-";

std::int64_t steadyMicros()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}

QString extensionSourceSerial(const MediaSourceDevice& source)
{
    const QString key = QString::fromStdString(source.extension + "/" + source.source + "/" + source.id);
    return kSourcePrefix.toString() + QString::fromStdString(capture::shortHash(key.toStdWString()));
}

bool isExtensionSourceSerial(QStringView serial) noexcept
{
    return serial.startsWith(kSourcePrefix);
}

std::optional<MediaSourceDevice> pairedSound(const std::vector<MediaSourceDevice>& sources,
                                             const MediaSourceDevice& video)
{
    const auto found = std::ranges::find_if(sources, [&video](const MediaSourceDevice& source) {
        return source.kind == exthost::MediaKind::Audio && source.extension == video.extension && source.id == video.id;
    });
    return found != sources.end() ? std::optional<MediaSourceDevice>(*found) : std::nullopt;
}

SourcePlan planExtensionSources(const std::vector<MediaSourceDevice>& sources, const std::vector<QString>& shown)
{
    SourcePlan plan;
    std::vector<QString> listed;
    for (const MediaSourceDevice& source : sources) {
        if (source.kind != exthost::MediaKind::Video) {
            continue;
        }
        const QString serial = extensionSourceSerial(source);
        if (std::ranges::find(listed, serial) != listed.end()) {
            continue;
        }
        listed.push_back(serial);
        if (std::ranges::find(shown, serial) == shown.end()) {
            plan.added.push_back(source);
        }
    }
    for (const QString& serial : shown) {
        if (std::ranges::find(listed, serial) == listed.end()) {
            plan.removed.push_back(serial);
        }
    }
    return plan;
}

ExtensionVideoSource::ExtensionVideoSource(Opener open, Phone phone)
    : m_open(std::move(open))
    , m_phone(std::move(phone))
{
}

ExtensionVideoSource::~ExtensionVideoSource()
{
    stop();
}

bool ExtensionVideoSource::start(capture::SampleSink& sink, const std::string&)
{
    stop();
    const std::string params = nlohmann::json{{"phone", m_phone ? m_phone() : std::string()}}.dump();
    std::unique_ptr<SourceLease> pictures = m_open ? m_open(exthost::MediaKind::Video, params) : nullptr;
    if (pictures == nullptr) {
        const std::scoped_lock lock(m_mutex);
        m_problem = QCoreApplication::translate("ExtensionMedia", "The extension's picture could not be opened")
                        .toStdString();
        return false;
    }
    std::unique_ptr<SourceLease> sound = m_open(exthost::MediaKind::Audio, params);
    {
        const std::scoped_lock lock(m_mutex);
        m_problem.clear();
        m_givesSound = sound != nullptr;
    }
    m_pictures = std::move(pictures);
    m_sound = std::move(sound);
    exthost::MediaStream& shown = m_pictures->stream();
    m_picturePump = std::jthread([this, &sink, &shown](const std::stop_token& stop) { showPictures(stop, sink, shown); });
    if (m_sound != nullptr) {
        exthost::MediaStream& heard = m_sound->stream();
        m_soundPump = std::jthread([this, &sink, &heard](const std::stop_token& stop) { playSound(stop, sink, heard); });
    }
    return true;
}

void ExtensionVideoSource::stop()
{
    m_picturePump = std::jthread();
    m_soundPump = std::jthread();
    m_pictures.reset();
    m_sound.reset();
}

std::string ExtensionVideoSource::problem() const
{
    const std::scoped_lock lock(m_mutex);
    return m_problem;
}

bool ExtensionVideoSource::givesSound() const
{
    const std::scoped_lock lock(m_mutex);
    return m_givesSound;
}

void ExtensionVideoSource::showPictures(const std::stop_token& stop, capture::SampleSink& sink,
                                        exthost::MediaStream& stream)
{
    std::optional<capture::CaptureFormat> offered;
    while (stream.wait(stop)) {
        if (const std::optional<exthost::FrameView> frame = stream.frame()) {
            const bool planar = frame->format == exthost::PixelFormat::Nv12;
            const capture::CaptureFormat format{.width = static_cast<int>(frame->width),
                                                .height = static_cast<int>(frame->height),
                                                .rateNumerator = 0,
                                                .rateDenominator = 1,
                                                .subtype = capture::Subtype::Nv12,
                                                .index = 0};
            if (!offered || offered->width != format.width || offered->height != format.height) {
                offered = format;
                sink.offered({}, format);
            }
            const std::size_t rows = std::size_t{frame->stride} * frame->height;
            const std::size_t bytes = planar ? rows + std::size_t{frame->chromaStride} * (frame->height / 2) : rows;
            capture::CaptureSample sample;
            sample.kind = planar ? capture::SampleKind::Nv12 : capture::SampleKind::Bgra;
            sample.width = static_cast<int>(frame->width);
            sample.height = static_cast<int>(frame->height);
            sample.bytes = std::span<const std::uint8_t>(frame->data, bytes);
            sample.pitch = static_cast<std::ptrdiff_t>(frame->stride);
            sample.chromaOffset = planar ? frame->chroma - frame->data : 0;
            sample.ptsUs = static_cast<std::int64_t>(frame->timeUs);
            sample.receivedUs = steadyMicros();
            sample.color = {.matrix = media::VideoMatrix::Bt709, .fullRange = false};
            sink.sample(sample);
        }
        if (const std::optional<std::string> why = stream.ended()) {
            {
                const std::scoped_lock lock(m_mutex);
                m_problem = *why;
            }
            sink.ended(*why);
            return;
        }
    }
}

void ExtensionVideoSource::playSound(const std::stop_token& stop, capture::SampleSink& sink,
                                     exthost::MediaStream& stream)
{
    while (stream.wait(stop)) {
        while (const std::optional<exthost::SoundBlock> block = stream.sound()) {
            sink.heard(block->samples, static_cast<int>(block->channels), static_cast<int>(block->rate),
                       static_cast<std::int64_t>(block->timeUs));
        }
        if (stream.ended()) {
            return;
        }
    }
}

}
