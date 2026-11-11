#include "MediaSinks.h"

#include "NativeExtensions.h"

#include <gs/media/FrameSnapshot.h>

#include <QJSEngine>
#include <QMetaObject>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

namespace gs::app {

namespace {

constexpr std::chrono::milliseconds kSoundOnlyCheck{200};

QString keyOf(const MediaSinkOffer& offer)
{
    return QString::fromStdString(offer.extension + "/" + offer.name);
}

std::uint64_t steadyMicros()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::uint8_t clamped(float value) noexcept
{
    return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 255.0F) + 0.5F);
}

void sendPictures(const std::stop_token& stop, const std::shared_ptr<exthost::SinkPort>& port,
                  const MediaSinks::Camera& camera, std::uint32_t width, std::uint32_t height, std::uint32_t fps,
                  const std::function<void(const std::string& why)>& ended)
{
    const std::chrono::microseconds period =
        camera ? std::chrono::microseconds(1'000'000 / std::max<std::uint32_t>(fps, 1U))
               : std::chrono::duration_cast<std::chrono::microseconds>(kSoundOnlyCheck);
    std::vector<std::uint8_t> pixels;
    std::mutex mutex;
    std::condition_variable_any wake;
    auto next = std::chrono::steady_clock::now();
    while (!stop.stop_requested()) {
        if (const std::optional<std::string> why = port->ended()) {
            ended(*why);
            return;
        }
        if (camera) {
            if (const std::shared_ptr<const media::LumaSnapshot> shot = camera(); shot != nullptr && shot->colored()) {
                letterboxBgra(*shot, width, height, pixels);
                const exthost::FrameView view{.format = exthost::PixelFormat::Bgra,
                                              .width = width,
                                              .height = height,
                                              .stride = width * 4,
                                              .chromaStride = 0,
                                              .data = pixels.data(),
                                              .chroma = nullptr,
                                              .timeUs = steadyMicros()};
                static_cast<void>(port->frame(view));
            }
        }
        next += period;
        const auto now = std::chrono::steady_clock::now();
        if (next < now) {
            next = now;
        }
        std::unique_lock lock(mutex);
        static_cast<void>(wake.wait_until(lock, stop, next, [] { return false; }));
    }
}

}

void letterboxBgra(const media::LumaSnapshot& shot, std::uint32_t width, std::uint32_t height,
                   std::vector<std::uint8_t>& out)
{
    const std::size_t total = std::size_t{width} * height * 4;
    out.assign(total, 0);
    for (std::size_t at = 3; at < total; at += 4) {
        out[at] = 255;
    }
    const std::size_t chromaRow = std::size_t{2} * static_cast<std::size_t>(shot.chromaWidth());
    if (!shot.colored() || width == 0 || height == 0 || shot.width <= 0 || shot.height <= 0 ||
        shot.luma.size() < static_cast<std::size_t>(shot.width) * static_cast<std::size_t>(shot.height) ||
        shot.chroma.size() < chromaRow * static_cast<std::size_t>(shot.chromaHeight())) {
        return;
    }
    const double scale = std::min(static_cast<double>(width) / shot.width, static_cast<double>(height) / shot.height);
    const auto shownWidth =
        std::clamp(static_cast<std::uint32_t>(std::lround(static_cast<double>(shot.width) * scale)), 1U, width);
    const auto shownHeight =
        std::clamp(static_cast<std::uint32_t>(std::lround(static_cast<double>(shot.height) * scale)), 1U, height);
    const std::uint32_t left = (width - shownWidth) / 2;
    const std::uint32_t top = (height - shownHeight) / 2;
    const auto sourceWidth = static_cast<std::uint64_t>(shot.width);
    const auto sourceHeight = static_cast<std::uint64_t>(shot.height);
    std::vector<std::size_t> columns(shownWidth);
    for (std::uint32_t x = 0; x < shownWidth; ++x) {
        columns[x] = static_cast<std::size_t>(std::min(std::uint64_t{x} * sourceWidth / shownWidth, sourceWidth - 1));
    }
    const auto& rows = shot.matrix.rows;
    for (std::uint32_t y = 0; y < shownHeight; ++y) {
        const auto line = static_cast<std::size_t>(std::min(std::uint64_t{y} * sourceHeight / shownHeight, sourceHeight - 1));
        const std::uint8_t* luma = shot.luma.data() + line * static_cast<std::size_t>(shot.width);
        const std::uint8_t* chroma = shot.chroma.data() + (line / 2) * chromaRow;
        std::uint8_t* pixel = out.data() + (std::size_t{top + y} * width + left) * 4;
        for (std::uint32_t x = 0; x < shownWidth; ++x, pixel += 4) {
            const std::size_t column = columns[x];
            const auto light = static_cast<float>(luma[column]);
            const auto blue = static_cast<float>(chroma[2 * (column / 2)]);
            const auto red = static_cast<float>(chroma[2 * (column / 2) + 1]);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto& row = rows[2 - channel];
                pixel[channel] = clamped(row[0] * light + row[1] * blue + row[2] * red + row[3] * 255.0F);
            }
        }
    }
}

struct MediaSinks::Shared {
    std::mutex mutex;
    MediaSinks* owner = nullptr;

    void ended(std::uint64_t run, const std::string& why)
    {
        const std::scoped_lock lock(mutex);
        if (owner == nullptr) {
            return;
        }
        MediaSinks* target = owner;
        QMetaObject::invokeMethod(
            target, [target, run, text = QString::fromStdString(why)] { target->ended(run, text); },
            Qt::QueuedConnection);
    }
};

struct MediaSinks::Run {
    std::uint64_t id = 0;
    QString serial;
    QString key;
    QString title;
    bool audio = false;
    std::shared_ptr<exthost::SinkPort> port;
    std::unique_ptr<SinkLease> lease;
    std::jthread pump;
};

struct MediaSinks::Listener {
    std::mutex mutex;
    std::vector<std::shared_ptr<exthost::SinkPort>> ports;
};

MediaSinks::MediaSinks(NativeExtensions& extensions, Host host, QObject* parent)
    : QObject(parent)
    , m_extensions(extensions)
    , m_host(std::move(host))
    , m_shared(std::make_shared<Shared>())
{
    m_shared->owner = this;
    if (s_instance == nullptr) {
        s_instance = this;
    }
    connect(&m_extensions, &NativeExtensions::mediaChanged, this, &MediaSinks::offersChanged);
}

MediaSinks::~MediaSinks()
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = nullptr;
    }
    stopAll();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

MediaSinks* MediaSinks::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QVariantList MediaSinks::sinks() const
{
    QVariantList list;
    for (const MediaSinkOffer& offer : m_extensions.mediaSinks()) {
        list.append(QVariantMap{{QStringLiteral("key"), keyOf(offer)},
                                {QStringLiteral("title"), offer.title},
                                {QStringLiteral("extensionName"), offer.extensionName},
                                {QStringLiteral("picture"), offer.width > 0 && offer.height > 0},
                                {QStringLiteral("sound"), offer.audio}});
    }
    return list;
}

QVariantList MediaSinks::running() const
{
    QVariantList list;
    for (const std::unique_ptr<Run>& run : m_runs) {
        list.append(QVariantMap{{QStringLiteral("serial"), run->serial}, {QStringLiteral("key"), run->key}});
    }
    return list;
}

bool MediaSinks::sending(const QString& serial, const QString& key) const
{
    return std::ranges::any_of(m_runs, [&serial, &key](const std::unique_ptr<Run>& run) {
        return run->serial == serial && run->key == key;
    });
}

QString MediaSinks::toggle(const QString& serial, const QString& key)
{
    const auto already = std::ranges::find_if(m_runs, [&serial, &key](const std::unique_ptr<Run>& run) {
        return run->serial == serial && run->key == key;
    });
    if (already != m_runs.end()) {
        stop((*already)->id);
        return {};
    }
    const std::vector<MediaSinkOffer> offers = m_extensions.mediaSinks();
    const auto offer =
        std::ranges::find_if(offers, [&key](const MediaSinkOffer& each) { return keyOf(each) == key; });
    if (offer == offers.end()) {
        return tr("This destination is not available any more");
    }
    bool open = false;
    QString name;
    for (const QVariant& window : m_host.windows ? m_host.windows() : QVariantList()) {
        const QVariantMap fields = window.toMap();
        if (fields.value(QStringLiteral("serial")).toString() == serial) {
            open = true;
            name = fields.value(QStringLiteral("name")).toString();
        }
    }
    if (!open) {
        return tr("The window is closed");
    }
    const bool pictured = offer->width > 0 && offer->height > 0;
    Camera camera = pictured && m_host.camera ? m_host.camera(serial) : Camera();
    if (pictured && !camera) {
        return tr("This window has no picture to send");
    }
    const exthost::SinkStart start{.device = serial.toStdString(),
                                   .name = name.toStdString(),
                                   .width = pictured ? offer->width : 0U,
                                   .height = pictured ? offer->height : 0U,
                                   .fps = pictured ? offer->fps : 0U,
                                   .audio = offer->audio};
    std::unique_ptr<SinkLease> lease = m_extensions.openSink(offer->extension, offer->name, start);
    if (lease == nullptr) {
        return tr("%1 did not start").arg(offer->title);
    }
    auto run = std::make_unique<Run>();
    run->id = ++m_lastRun;
    run->serial = serial;
    run->key = key;
    run->title = offer->title;
    run->audio = offer->audio;
    run->port = lease->sharedPort();
    run->lease = std::move(lease);
    if (run->audio) {
        listen(serial, run->port);
    }
    const std::uint64_t id = run->id;
    std::function<void(const std::string&)> report = [shared = m_shared, id](const std::string& why) {
        shared->ended(id, why);
    };
    run->pump = std::jthread(sendPictures, run->port, std::move(camera), start.width, start.height, start.fps,
                             std::move(report));
    qInfo("sending the window of %s to %s", qUtf8Printable(serial), qUtf8Printable(offer->title));
    m_runs.push_back(std::move(run));
    emit changed();
    return {};
}

void MediaSinks::windowsChanged()
{
    QStringList open;
    for (const QVariant& window : m_host.windows ? m_host.windows() : QVariantList()) {
        open.append(window.toMap().value(QStringLiteral("serial")).toString());
    }
    std::vector<std::uint64_t> closing;
    for (const std::unique_ptr<Run>& run : m_runs) {
        if (!open.contains(run->serial)) {
            closing.push_back(run->id);
        }
    }
    for (const std::uint64_t id : closing) {
        stop(id);
    }
}

void MediaSinks::stopAll()
{
    while (!m_runs.empty()) {
        stop(m_runs.back()->id);
    }
}

void MediaSinks::offersChanged()
{
    const std::vector<MediaSinkOffer> offers = m_extensions.mediaSinks();
    std::vector<std::uint64_t> gone;
    for (const std::unique_ptr<Run>& run : m_runs) {
        if (std::ranges::none_of(offers, [&run](const MediaSinkOffer& offer) { return keyOf(offer) == run->key; })) {
            gone.push_back(run->id);
        }
    }
    for (const std::uint64_t id : gone) {
        stop(id);
    }
    emit changed();
}

void MediaSinks::ended(std::uint64_t run, const QString& why)
{
    const auto found =
        std::ranges::find_if(m_runs, [run](const std::unique_ptr<Run>& each) { return each->id == run; });
    if (found == m_runs.end()) {
        return;
    }
    const QString serial = (*found)->serial;
    const QString title = (*found)->title;
    qWarning("sending the window of %s to %s stopped: %s", qUtf8Printable(serial), qUtf8Printable(title),
             qUtf8Printable(why));
    stop(run);
    emit stopped(serial, tr("Stopped sending to %1: %2").arg(title, why));
}

void MediaSinks::stop(std::uint64_t run)
{
    const auto found =
        std::ranges::find_if(m_runs, [run](const std::unique_ptr<Run>& each) { return each->id == run; });
    if (found == m_runs.end()) {
        return;
    }
    std::unique_ptr<Run> stopping = std::move(*found);
    m_runs.erase(found);
    if (stopping->audio) {
        unlisten(stopping->serial, stopping->port);
    }
    qInfo("no longer sending the window of %s to %s", qUtf8Printable(stopping->serial),
          qUtf8Printable(stopping->title));
    stopping.reset();
    emit changed();
}

void MediaSinks::listen(const QString& serial, const std::shared_ptr<exthost::SinkPort>& port)
{
    std::shared_ptr<Listener>& listener = m_listeners[serial];
    const bool fresh = listener == nullptr;
    if (fresh) {
        listener = std::make_shared<Listener>();
    }
    {
        const std::scoped_lock lock(listener->mutex);
        listener->ports.push_back(port);
    }
    if (!fresh || !m_host.listen) {
        return;
    }
    const std::weak_ptr<Listener> weak = listener;
    const bool heard = m_host.listen(serial, [weak](std::span<const float> samples, int channels, int rate,
                                                    std::int64_t) {
        const std::shared_ptr<Listener> alive = weak.lock();
        if (alive == nullptr || channels < 1 || channels > static_cast<int>(exthost::kMostChannels) ||
            rate != static_cast<int>(exthost::kSoundRate)) {
            return;
        }
        const std::size_t frames = samples.size() / static_cast<std::size_t>(channels);
        const std::uint64_t now = steadyMicros();
        const std::scoped_lock lock(alive->mutex);
        for (std::size_t first = 0; first < frames; first += exthost::kMostSoundFrames) {
            const std::size_t count = std::min<std::size_t>(frames - first, exthost::kMostSoundFrames);
            const exthost::SoundView view{.format = exthost::SampleFormat::F32,
                                          .channels = static_cast<std::uint32_t>(channels),
                                          .rate = static_cast<std::uint32_t>(rate),
                                          .frames = static_cast<std::uint32_t>(count),
                                          .data = samples.data() + first * static_cast<std::size_t>(channels),
                                          .timeUs = now};
            for (const std::shared_ptr<exthost::SinkPort>& each : alive->ports) {
                static_cast<void>(each->sound(view));
            }
        }
    });
    if (!heard) {
        qInfo("the window of %s has no sound to send", qUtf8Printable(serial));
    }
}

void MediaSinks::unlisten(const QString& serial, const std::shared_ptr<exthost::SinkPort>& port)
{
    const auto found = m_listeners.find(serial);
    if (found == m_listeners.end()) {
        return;
    }
    bool empty = false;
    {
        const std::scoped_lock lock(found->second->mutex);
        std::erase(found->second->ports, port);
        empty = found->second->ports.empty();
    }
    if (!empty) {
        return;
    }
    m_listeners.erase(found);
    if (m_host.listen) {
        static_cast<void>(m_host.listen(serial, {}));
    }
}

}
