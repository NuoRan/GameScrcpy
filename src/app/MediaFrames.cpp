#include "MediaFrames.h"

#include "AppSettings.h"
#include "ExtensionHost.h"
#include "VisionSearch.h"

#include <gs/base/Log.h>
#include <gs/script/Permissions.h>

#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <thread>

namespace gs::app {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::string_view kFrames = "media.frames";
constexpr std::string_view kDone = "media.done";
constexpr std::string_view kClose = "media.close";
constexpr std::string_view kCategory = "extensions";
constexpr double kEdge = 1e-9;

QString fromView(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString compactJson(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject objectOf(const QByteArray& json)
{
    QJsonParseError problem;
    const QJsonDocument document = QJsonDocument::fromJson(json, &problem);
    return problem.error == QJsonParseError::NoError && document.isObject() ? document.object() : QJsonObject();
}

QString handleText(quint64 handle)
{
    return QStringLiteral("f%1").arg(handle);
}

quint64 handleOf(const QString& text)
{
    bool number = false;
    const quint64 handle = text.startsWith(u'f') ? text.mid(1).toULongLong(&number) : 0;
    return number ? handle : 0;
}

QString invalidWith(const QString& what)
{
    return QStringLiteral("GS-API-120 ") + what;
}

QString groupOf(const std::string& key)
{
    return QStringLiteral("tap:") + QString::fromStdString(key);
}

std::string keyOf(const FrameTapSource& tap)
{
    return tap.extension + "/" + tap.name;
}

Clock::duration intervalOf(double fps)
{
    return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / std::max(fps, 0.01)));
}

double ratio(double value)
{
    return std::clamp(value, -1.0, 2.0);
}

script::ServiceAnswer refused(const QString& message)
{
    const std::string full = message.toStdString();
    const std::size_t space = full.find(' ');
    return {"null", full.substr(0, space), space == std::string::npos ? std::string() : full.substr(space + 1)};
}

}

std::vector<std::uint8_t> framePixels(const media::LumaSnapshot& shot, int left, int top, int width, int height,
                                      int outWidth, int outHeight)
{
    const std::size_t chromaRow = std::size_t{2} * static_cast<std::size_t>(shot.chromaWidth());
    if (!shot.colored() || outWidth <= 0 || outHeight <= 0 || width < outWidth || height < outHeight || left < 0 ||
        top < 0 || left > shot.width - width || top > shot.height - height ||
        shot.luma.size() < static_cast<std::size_t>(shot.width) * static_cast<std::size_t>(shot.height) ||
        shot.chroma.size() < chromaRow * static_cast<std::size_t>(shot.chromaHeight())) {
        return {};
    }
    const auto spans = [](int start, int length, int count) {
        std::vector<std::pair<int, int>> made(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            const int from = start + static_cast<int>(static_cast<std::int64_t>(index) * length / count);
            const int to = start + static_cast<int>(static_cast<std::int64_t>(index + 1) * length / count);
            made[static_cast<std::size_t>(index)] = {from, std::max(to, from + 1)};
        }
        return made;
    };
    const std::vector<std::pair<int, int>> columns = spans(left, width, outWidth);
    const std::vector<std::pair<int, int>> rows = spans(top, height, outHeight);
    const auto& matrix = shot.matrix.rows;
    std::vector<std::uint8_t> pixels(std::size_t{4} * static_cast<std::size_t>(outWidth) *
                                     static_cast<std::size_t>(outHeight));
    for (int y = 0; y < outHeight; ++y) {
        const auto [rowFrom, rowTo] = rows[static_cast<std::size_t>(y)];
        for (int x = 0; x < outWidth; ++x) {
            const auto [columnFrom, columnTo] = columns[static_cast<std::size_t>(x)];
            std::uint32_t lightSum = 0;
            for (int row = rowFrom; row < rowTo; ++row) {
                const std::uint8_t* line =
                    shot.luma.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(shot.width);
                for (int column = columnFrom; column < columnTo; ++column) {
                    lightSum += line[column];
                }
            }
            std::uint32_t blueSum = 0;
            std::uint32_t redSum = 0;
            std::uint32_t chromaCount = 0;
            for (int row = rowFrom / 2; row <= (rowTo - 1) / 2; ++row) {
                const std::uint8_t* line = shot.chroma.data() + static_cast<std::size_t>(row) * chromaRow;
                for (int column = columnFrom / 2; column <= (columnTo - 1) / 2; ++column) {
                    blueSum += line[2 * column];
                    redSum += line[2 * column + 1];
                    ++chromaCount;
                }
            }
            const auto area = static_cast<float>((rowTo - rowFrom) * (columnTo - columnFrom));
            const float light = static_cast<float>(lightSum) / area;
            const float blue = static_cast<float>(blueSum) / static_cast<float>(chromaCount);
            const float red = static_cast<float>(redSum) / static_cast<float>(chromaCount);
            std::uint8_t* pixel = pixels.data() + std::size_t{4} * (static_cast<std::size_t>(y) *
                                                                       static_cast<std::size_t>(outWidth) +
                                                                   static_cast<std::size_t>(x));
            for (std::size_t channel = 0; channel < matrix.size(); ++channel) {
                const auto& weights = matrix[channel];
                const float value = weights[0] * light + weights[1] * blue + weights[2] * red + weights[3] * 255.0F;
                pixel[2 - channel] = static_cast<std::uint8_t>(std::clamp(value, 0.0F, 255.0F) + 0.5F);
            }
            pixel[3] = 255;
        }
    }
    return pixels;
}

QVariantMap overlayShape(const exthost::Shape& shape, const QString& extension)
{
    constexpr std::array<std::string_view, 5> kTones{"accent", "good", "warn", "bad", "white"};
    const double x = ratio(shape.x);
    const double y = ratio(shape.y);
    const double x2 = ratio(shape.x2);
    const double y2 = ratio(shape.y2);
    QVariantMap made{{QStringLiteral("id"), QString::number(shape.id)},
                     {QStringLiteral("color"), fromView(kTones[shape.color < kTones.size() ? shape.color : 0])},
                     {QStringLiteral("text"), QString::fromStdString(shape.text)},
                     {QStringLiteral("extension"), extension},
                     {QStringLiteral("x"), x},
                     {QStringLiteral("y"), y},
                     {QStringLiteral("width"), 0.0},
                     {QStringLiteral("height"), 0.0}};
    switch (shape.kind) {
    case exthost::ShapeKind::Rect:
        made.insert(QStringLiteral("kind"), QStringLiteral("box"));
        made.insert(QStringLiteral("x"), std::min(x, x2));
        made.insert(QStringLiteral("y"), std::min(y, y2));
        made.insert(QStringLiteral("width"), std::abs(x2 - x));
        made.insert(QStringLiteral("height"), std::abs(y2 - y));
        break;
    case exthost::ShapeKind::Line:
        made.insert(QStringLiteral("kind"), QStringLiteral("line"));
        made.insert(QStringLiteral("width"), x2 - x);
        made.insert(QStringLiteral("height"), y2 - y);
        made.insert(QStringLiteral("text"), QString());
        break;
    case exthost::ShapeKind::Point:
        made.insert(QStringLiteral("kind"), QStringLiteral("mark"));
        break;
    case exthost::ShapeKind::Text:
        made.insert(QStringLiteral("kind"), QStringLiteral("text"));
        break;
    }
    return made;
}

struct MediaFrames::Shared {
    mutable std::mutex mutex;
    MediaFrames* owner = nullptr;
    std::set<QString> permitted;

    void post(std::function<void(MediaFrames&)> work) const
    {
        const std::scoped_lock lock(mutex);
        if (owner == nullptr) {
            return;
        }
        MediaFrames* target = owner;
        QMetaObject::invokeMethod(target, [target, work = std::move(work)] { work(*target); }, Qt::QueuedConnection);
    }

    [[nodiscard]] bool allowed(const QString& extension) const
    {
        const std::scoped_lock lock(mutex);
        return extension.isEmpty() || permitted.contains(extension);
    }
};

struct MediaFrames::Feed {
    struct Want {
        quint64 handle = 0;
        double fps = kDefaultFps;
        int size = kDefaultSize;
        script::Region region;
        bool busy = false;
        bool tried = false;
        std::uint64_t last = 0;
        Clock::time_point due{};
    };

    struct Tap {
        std::string key;
        quint64 instance = 0;
        std::weak_ptr<exthost::Module> module;
        std::size_t index = 0;
        int fps = 1;
        int size = kLeastSize;
        std::uint64_t opened = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        int rotation = 0;
        bool failed = false;
        bool tried = false;
        std::uint64_t last = 0;
        Clock::time_point due{};
    };

    Feed(QString phone, std::shared_ptr<media::FrameSnapshots> source, std::weak_ptr<Shared> owner, int turned)
        : serial(std::move(phone))
        , snapshots(std::move(source))
        , shared(std::move(owner))
        , rotation(turned)
    {
        watcher = snapshots->watch([this] { nudge(); });
        thread = std::jthread([this](const std::stop_token& stop) { run(stop); });
    }

    ~Feed()
    {
        snapshots->unwatch(watcher);
        thread.request_stop();
        if (thread.joinable()) {
            thread.join();
        }
    }

    Feed(const Feed&) = delete;
    Feed& operator=(const Feed&) = delete;

    void nudge()
    {
        {
            const std::scoped_lock lock(mutex);
            woken = true;
        }
        wake.notify_all();
    }

    void addWant(Want want)
    {
        {
            const std::scoped_lock lock(mutex);
            wants.insert_or_assign(want.handle, want);
            woken = true;
        }
        wake.notify_all();
    }

    void dropWant(quint64 handle)
    {
        const std::scoped_lock lock(mutex);
        wants.erase(handle);
    }

    void readyFor(quint64 handle)
    {
        {
            const std::scoped_lock lock(mutex);
            if (const auto found = wants.find(handle); found != wants.end()) {
                found->second.busy = false;
            }
            woken = true;
        }
        wake.notify_all();
    }

    void addTap(std::shared_ptr<Tap> tap)
    {
        {
            const std::scoped_lock lock(mutex);
            if (const auto found = taps.find(tap->key); found != taps.end()) {
                closing.push_back(found->second);
            }
            taps.insert_or_assign(tap->key, std::move(tap));
            woken = true;
        }
        wake.notify_all();
    }

    void removeTap(const std::string& key)
    {
        {
            const std::scoped_lock lock(mutex);
            const auto found = taps.find(key);
            if (found == taps.end()) {
                return;
            }
            closing.push_back(found->second);
            taps.erase(found);
            woken = true;
        }
        wake.notify_all();
    }

    void failTap(const std::string& key, quint64 instance)
    {
        const std::scoped_lock lock(mutex);
        if (const auto found = taps.find(key); found != taps.end() && found->second->instance == instance) {
            found->second->failed = true;
        }
    }

    void turn(int turned)
    {
        {
            const std::scoped_lock lock(mutex);
            if (rotation == turned) {
                return;
            }
            rotation = turned;
            woken = true;
        }
        wake.notify_all();
    }

    [[nodiscard]] bool idle()
    {
        const std::scoped_lock lock(mutex);
        return wants.empty() && taps.empty();
    }

    void run(const std::stop_token& stop)
    {
        for (;;) {
            std::vector<std::shared_ptr<Tap>> ending;
            std::vector<Want> wanted;
            std::vector<std::shared_ptr<Tap>> tapped;
            int turned = 0;
            {
                std::unique_lock lock(mutex);
                for (;;) {
                    ending = std::exchange(closing, {});
                    if (stop.stop_requested()) {
                        for (const auto& [key, tap] : taps) {
                            ending.push_back(tap);
                        }
                        taps.clear();
                        break;
                    }
                    if (!ending.empty()) {
                        break;
                    }
                    const Clock::time_point now = Clock::now();
                    const std::uint64_t offered = snapshots->offered();
                    std::optional<Clock::time_point> next;
                    const auto due = [&](bool tried, std::uint64_t last, Clock::time_point at) {
                        if (tried && offered <= last) {
                            return false;
                        }
                        if (now < at) {
                            next = next ? std::min(*next, at) : at;
                            return false;
                        }
                        return true;
                    };
                    for (auto& [handle, want] : wants) {
                        if (!want.busy && due(want.tried, want.last, want.due)) {
                            want.busy = true;
                            wanted.push_back(want);
                        }
                    }
                    for (const auto& [key, tap] : taps) {
                        if (!tap->failed && due(tap->tried, tap->last, tap->due)) {
                            tapped.push_back(tap);
                        }
                    }
                    if (!wanted.empty() || !tapped.empty()) {
                        turned = rotation;
                        break;
                    }
                    woken = false;
                    if (next) {
                        wake.wait_until(lock, stop, *next, [this] { return woken; });
                    } else {
                        wake.wait(lock, stop, [this] { return woken; });
                    }
                }
            }
            for (const std::shared_ptr<Tap>& tap : ending) {
                shut(*tap);
            }
            if (stop.stop_requested()) {
                return;
            }
            if (!wanted.empty() || !tapped.empty()) {
                serve(wanted, tapped, turned);
            }
        }
    }

    void shut(Tap& tap) const
    {
        if (tap.opened == 0) {
            return;
        }
        if (const std::shared_ptr<exthost::Module> module = tap.module.lock()) {
            module->closeTap(tap.opened);
        }
        tap.opened = 0;
    }

    void serve(const std::vector<Want>& wanted, const std::vector<std::shared_ptr<Tap>>& tapped, int turned)
    {
        const std::uint64_t offered = snapshots->offered();
        const std::shared_ptr<const media::LumaSnapshot> shot = snapshots->capture(true);
        const Clock::time_point now = Clock::now();
        const bool usable = shot != nullptr && shot->colored() && shot->width > 0 && shot->height > 0;
        const std::uint64_t seen = usable ? shot->frame : offered;
        std::vector<quint64> sent;
        for (const Want& want : wanted) {
            if (usable && send(*shot, want)) {
                sent.push_back(want.handle);
            }
        }
        for (const std::shared_ptr<Tap>& tap : tapped) {
            Tap state;
            {
                const std::scoped_lock lock(mutex);
                state = *tap;
            }
            if (usable) {
                feed(state, *shot, turned);
            }
            const std::scoped_lock lock(mutex);
            tap->opened = state.opened;
            tap->width = state.width;
            tap->height = state.height;
            tap->rotation = state.rotation;
            tap->failed = tap->failed || state.failed;
            tap->tried = true;
            tap->last = seen;
            tap->due = now + intervalOf(state.fps);
        }
        const std::scoped_lock lock(mutex);
        for (const Want& want : wanted) {
            const auto found = wants.find(want.handle);
            if (found == wants.end()) {
                continue;
            }
            found->second.tried = true;
            found->second.last = seen;
            found->second.due = now + intervalOf(found->second.fps);
            if (std::ranges::find(sent, want.handle) == sent.end()) {
                found->second.busy = false;
            }
        }
    }

    [[nodiscard]] bool send(const media::LumaSnapshot& shot, const Want& want) const
    {
        const vision::Rect area = pixelArea(want.region, shot.width, shot.height);
        if (area.empty()) {
            return false;
        }
        const double scale = std::min(1.0, static_cast<double>(want.size) / std::max(area.width, area.height));
        const int width = std::max(1, static_cast<int>(std::lround(area.width * scale)));
        const int height = std::max(1, static_cast<int>(std::lround(area.height * scale)));
        const std::vector<std::uint8_t> pixels =
            framePixels(shot, area.x, area.y, area.width, area.height, width, height);
        const std::shared_ptr<Shared> alive = shared.lock();
        if (pixels.empty() || alive == nullptr) {
            return false;
        }
        QImage picture(width, height, QImage::Format_RGB32);
        const std::size_t row = std::size_t{4} * static_cast<std::size_t>(width);
        for (int line = 0; line < height; ++line) {
            std::memcpy(picture.scanLine(line), pixels.data() + row * static_cast<std::size_t>(line), row);
        }
        const script::PictureSize source{std::max(1, static_cast<int>(std::lround(shot.width * scale))),
                                         std::max(1, static_cast<int>(std::lround(shot.height * scale)))};
        const script::Region region{static_cast<double>(area.x) / shot.width, static_cast<double>(area.y) / shot.height,
                                    static_cast<double>(area.width) / shot.width,
                                    static_cast<double>(area.height) / shot.height};
        const double time = static_cast<double>(shot.ptsUs) / 1000.0;
        alive->post([handle = want.handle, picture, region, source, time](MediaFrames& owner) {
            owner.deliver(handle, picture, region, source, time);
        });
        return true;
    }

    void feed(Tap& tap, const media::LumaSnapshot& shot, int turned) const
    {
        const std::shared_ptr<exthost::Module> module = tap.module.lock();
        if (module == nullptr || !module->working()) {
            tap.failed = true;
            tap.opened = 0;
            return;
        }
        const double scale = std::min(1.0, static_cast<double>(tap.size) / std::max(shot.width, shot.height));
        const auto width = static_cast<std::uint32_t>(std::max(1L, std::lround(shot.width * scale)));
        const auto height = static_cast<std::uint32_t>(std::max(1L, std::lround(shot.height * scale)));
        if (tap.opened != 0 && (tap.width != width || tap.height != height || tap.rotation != turned)) {
            module->closeTap(tap.opened);
            tap.opened = 0;
        }
        if (tap.opened == 0) {
            tap.width = width;
            tap.height = height;
            tap.rotation = turned;
            tap.opened = module->openTap(tap.index,
                                         {.device = serial.toStdString(),
                                          .width = width,
                                          .height = height,
                                          .rotation = static_cast<std::uint32_t>(turned & 3)},
                                         eventsOf(tap));
            if (tap.opened == 0) {
                tap.failed = true;
                report(tap, QStringLiteral("the frame tap did not start"));
                return;
            }
        }
        const std::vector<std::uint8_t> pixels =
            framePixels(shot, 0, 0, shot.width, shot.height, static_cast<int>(width), static_cast<int>(height));
        if (pixels.empty()) {
            return;
        }
        static_cast<void>(module->offerFrame(tap.opened, {.pixels = pixels.data(),
                                                          .stride = 4 * width,
                                                          .width = width,
                                                          .height = height,
                                                          .timeUs = static_cast<std::uint64_t>(
                                                              std::max<std::int64_t>(shot.ptsUs, 0))}));
    }

    [[nodiscard]] exthost::TapEvents eventsOf(const Tap& tap) const
    {
        exthost::TapEvents events;
        events.overlay = [owner = shared, phone = serial, key = tap.key,
                          instance = tap.instance](std::vector<exthost::Shape> shapes) {
            if (const std::shared_ptr<Shared> alive = owner.lock()) {
                alive->post([phone, key, instance, shapes = std::move(shapes)](MediaFrames& self) {
                    self.showOverlay(phone, key, instance, shapes);
                });
            }
        };
        events.failed = [owner = shared, phone = serial, key = tap.key, instance = tap.instance](std::string problem) {
            if (const std::shared_ptr<Shared> alive = owner.lock()) {
                alive->post([phone, key, instance, why = QString::fromStdString(problem)](MediaFrames& self) {
                    self.tapFailed(phone, key, instance, why);
                });
            }
        };
        return events;
    }

    void report(const Tap& tap, const QString& problem) const
    {
        if (const std::shared_ptr<Shared> alive = shared.lock()) {
            alive->post([phone = serial, key = tap.key, instance = tap.instance, problem](MediaFrames& self) {
                self.tapFailed(phone, key, instance, problem);
            });
        }
    }

    const QString serial;
    const std::shared_ptr<media::FrameSnapshots> snapshots;
    const std::weak_ptr<Shared> shared;
    std::mutex mutex;
    std::condition_variable_any wake;
    bool woken = false;
    int rotation = 0;
    std::map<quint64, Want> wants;
    std::map<std::string, std::shared_ptr<Tap>, std::less<>> taps;
    std::vector<std::shared_ptr<Tap>> closing;
    std::uint64_t watcher = 0;
    std::jthread thread;
};

MediaFrames::MediaFrames(QObject* parent)
    : QObject(parent)
    , m_shared(std::make_shared<Shared>())
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    m_shared->owner = this;
    m_replies = {
        .answer = [](std::uint64_t logic, quint64 request,
                     const QString& json) { static_cast<void>(LogicServices::instance().answer(logic, request, json)); },
        .refuse = [](std::uint64_t logic, quint64 request,
                     const QString& message) { static_cast<void>(LogicServices::instance().refuse(logic, request, message)); },
        .tell = [](std::uint64_t logic, const QString& kind, const QString& id,
                   const QString& json) { static_cast<void>(LogicServices::instance().tell(logic, kind, id, json)); },
        .keep = [](std::uint64_t logic, const QImage& picture, script::PictureSize source,
                   quint64 frames) { return LogicServices::instance().keepFrame(logic, picture, source, frames); },
        .forget = [](std::uint64_t logic, quint64 frames) { LogicServices::instance().forgetFrames(logic, frames); },
    };
}

MediaFrames::~MediaFrames()
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = nullptr;
    }
    if (m_offered) {
        for (const std::string_view name : {kDone, kClose, kFrames}) {
            LogicServices::instance().withdraw(name);
        }
    }
    m_feeds.clear();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

MediaFrames* MediaFrames::create(QQmlEngine*, QJSEngine*)
{
    if (s_instance == nullptr) {
        auto* made = new MediaFrames(nullptr);
        made->setSettings(AppSettings::instance());
        QJSEngine::setObjectOwnership(made, QJSEngine::JavaScriptOwnership);
        return made;
    }
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void MediaFrames::offerServices()
{
    LogicServices& services = LogicServices::instance();
    for (const std::string_view name : {kDone, kClose}) {
        services.offerCall(std::string(name), [shared = m_shared, name](const LogicCaller& caller, std::string_view json) {
            return callWith(shared, caller, name, json);
        });
    }
    services.offerRequest(
        std::string(kFrames),
        [this](const LogicCaller& caller, quint64 id, const QString& json) { request(caller, kFrames, id, json); },
        [](const LogicCaller&, quint64) {});
    connect(&services, &LogicServices::logicEnded, this, [this](quint64 logic) { endLogic(logic); });
    m_offered = true;
}

void MediaFrames::setSettings(AppSettings* settings)
{
    if (m_settings != nullptr) {
        disconnect(m_settings, nullptr, this, nullptr);
    }
    m_settings = settings;
    if (m_settings != nullptr) {
        connect(m_settings, &AppSettings::devicesChanged, this, [this] {
            syncTaps();
            refreshList();
        });
    }
    syncTaps();
    refreshList();
}

void MediaFrames::setReplies(MediaReplies replies)
{
    m_replies = std::move(replies);
}

void MediaFrames::setSources(MediaSources sources)
{
    m_sources = std::move(sources);
    sync();
}

void MediaFrames::setPermitted(const QStringList& extensions)
{
    const std::set<QString> next(extensions.begin(), extensions.end());
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->permitted = next;
    }
    std::vector<std::pair<std::uint64_t, quint64>> closing;
    for (const auto& [handle, subscription] : m_subscriptions) {
        if (!subscription.owner.isEmpty() && !next.contains(subscription.owner)) {
            closing.emplace_back(subscription.logic, handle);
        }
    }
    for (const auto& [logic, handle] : closing) {
        close(logic, handle, QStringLiteral("forbidden"));
    }
}

void MediaFrames::follow(ExtensionHost& extensions)
{
    const auto update = [this, &extensions] {
        QStringList permitted;
        for (const ExtensionHost::Extension& extension : extensions.extensions()) {
            if (!extension.active || !extension.manifest) {
                continue;
            }
            const std::vector<std::string> granted = extensions.grantedPermissions(extension.manifest->id);
            if (std::ranges::find(granted, kPermission) != granted.end()) {
                permitted.append(QString::fromStdString(extension.manifest->id));
            }
        }
        setPermitted(permitted);
        sync();
    };
    update();
    connect(&extensions, &ExtensionHost::extensionsChanged, this, update);
}

void MediaFrames::sync()
{
    std::map<QString, FramePhone> phones;
    if (m_sources.phones) {
        for (FramePhone& phone : m_sources.phones()) {
            if (phone.snapshots != nullptr && !phone.serial.isEmpty()) {
                const QString serial = phone.serial;
                phones.insert_or_assign(serial, std::move(phone));
            }
        }
    }
    m_available = m_sources.taps ? m_sources.taps() : std::vector<FrameTapSource>();
    std::vector<QString> gone;
    for (const auto& [serial, feed] : m_feeds) {
        const auto found = phones.find(serial);
        if (found == phones.end() || found->second.snapshots != feed->snapshots) {
            gone.push_back(serial);
        }
    }
    for (const QString& serial : gone) {
        std::vector<std::pair<std::uint64_t, quint64>> ended;
        for (const auto& [handle, subscription] : m_subscriptions) {
            if (subscription.serial == serial) {
                ended.emplace_back(subscription.logic, handle);
            }
        }
        for (const auto& [logic, handle] : ended) {
            close(logic, handle, QStringLiteral("ended"));
        }
        std::erase_if(m_running, [&serial](const auto& entry) { return entry.first.first == serial; });
        m_feeds.erase(serial);
    }
    m_phones = std::move(phones);
    for (const auto& [serial, feed] : m_feeds) {
        feed->turn(m_phones.at(serial).rotation);
    }
    syncTaps();
    refreshList();
}

MediaFrames::Feed* MediaFrames::feedOf(const QString& serial)
{
    if (const auto found = m_feeds.find(serial); found != m_feeds.end()) {
        return found->second.get();
    }
    const auto phone = m_phones.find(serial);
    if (phone == m_phones.end()) {
        return nullptr;
    }
    auto made = std::make_unique<Feed>(serial, phone->second.snapshots, m_shared, phone->second.rotation);
    Feed* feed = made.get();
    m_feeds.emplace(serial, std::move(made));
    return feed;
}

MediaFrames::Feed* MediaFrames::feedIf(const QString& serial) const
{
    const auto found = m_feeds.find(serial);
    return found != m_feeds.end() ? found->second.get() : nullptr;
}

void MediaFrames::prune(const QString& serial)
{
    if (const auto found = m_feeds.find(serial); found != m_feeds.end() && found->second->idle()) {
        m_feeds.erase(found);
    }
}

std::vector<std::string> MediaFrames::chosenOn(const QString& deviceKey) const
{
    if (deviceKey.isEmpty()) {
        return {};
    }
    if (m_settings != nullptr) {
        return m_settings->device(deviceKey.toStdString()).frameTaps;
    }
    const auto found = m_chosen.find(deviceKey);
    return found != m_chosen.end() ? found->second : std::vector<std::string>();
}

bool MediaFrames::choose(const QString& deviceKey, std::vector<std::string> chosen)
{
    if (deviceKey.isEmpty() || chosen.size() > settings::DeviceSettings::kMaxFrameTaps) {
        return false;
    }
    if (m_settings == nullptr) {
        m_chosen.insert_or_assign(deviceKey, std::move(chosen));
        return true;
    }
    settings::DeviceSettings device = m_settings->device(deviceKey.toStdString());
    device.frameTaps = std::move(chosen);
    return m_settings->setDevice(deviceKey.toStdString(), device);
}

void MediaFrames::syncTaps()
{
    std::map<std::pair<QString, std::string>, const FrameTapSource*> wanted;
    for (const auto& [serial, phone] : m_phones) {
        const std::vector<std::string> chosen = chosenOn(phone.deviceKey);
        for (const FrameTapSource& tap : m_available) {
            if (std::ranges::find(chosen, keyOf(tap)) != chosen.end()) {
                wanted.emplace(std::pair(serial, keyOf(tap)), &tap);
            }
        }
    }
    std::vector<std::pair<QString, std::string>> stopping;
    for (const auto& [where, running] : m_running) {
        const auto found = wanted.find(where);
        if (found == wanted.end() || found->second->identity != running.identity || found->second->fps != running.fps ||
            found->second->size != running.size) {
            stopping.push_back(where);
        }
    }
    for (const auto& [serial, key] : stopping) {
        m_running.erase(std::pair(serial, key));
        if (Feed* feed = feedIf(serial)) {
            feed->removeTap(key);
        }
        if (m_sources.overlay) {
            m_sources.overlay(serial, groupOf(key), {});
        }
    }
    for (const auto& [where, tap] : wanted) {
        if (m_running.contains(where)) {
            continue;
        }
        Feed* feed = feedOf(where.first);
        if (feed == nullptr) {
            continue;
        }
        const quint64 instance = ++m_lastInstance;
        m_running.emplace(where, Running{.instance = instance,
                                         .identity = tap->identity,
                                         .fps = tap->fps,
                                         .size = tap->size,
                                         .extensionName = tap->extensionName});
        auto made = std::make_shared<Feed::Tap>();
        made->key = where.second;
        made->instance = instance;
        made->module = tap->module;
        made->index = tap->index;
        made->fps = std::clamp(tap->fps, 1, static_cast<int>(ext::kMostTapFps));
        made->size = std::clamp(tap->size, static_cast<int>(ext::kLeastTapSize), static_cast<int>(ext::kMostTapSize));
        feed->addTap(std::move(made));
    }
    std::vector<QString> idle;
    for (const auto& [serial, feed] : m_feeds) {
        if (feed->idle()) {
            idle.push_back(serial);
        }
    }
    for (const QString& serial : idle) {
        m_feeds.erase(serial);
    }
}

void MediaFrames::refreshList()
{
    QVariantList list;
    for (const auto& [serial, phone] : m_phones) {
        const std::vector<std::string> chosen = chosenOn(phone.deviceKey);
        for (const FrameTapSource& tap : m_available) {
            const std::string key = keyOf(tap);
            list.append(QVariantMap{{QStringLiteral("device"), serial},
                                    {QStringLiteral("key"), QString::fromStdString(key)},
                                    {QStringLiteral("extension"), QString::fromStdString(tap.extension)},
                                    {QStringLiteral("extensionName"), tap.extensionName},
                                    {QStringLiteral("name"), QString::fromStdString(tap.name)},
                                    {QStringLiteral("title"), tap.title},
                                    {QStringLiteral("on"), std::ranges::find(chosen, key) != chosen.end()}});
        }
    }
    if (list != m_tapList) {
        m_tapList = std::move(list);
        emit tapsChanged();
    }
}

bool MediaFrames::setTapOn(const QString& serial, const QString& key, bool on)
{
    const auto phone = m_phones.find(serial);
    const std::string wanted = key.toStdString();
    if (phone == m_phones.end() ||
        std::ranges::none_of(m_available, [&wanted](const FrameTapSource& tap) { return keyOf(tap) == wanted; })) {
        return false;
    }
    std::vector<std::string> chosen = chosenOn(phone->second.deviceKey);
    const bool was = std::ranges::find(chosen, wanted) != chosen.end();
    if (was == on) {
        return true;
    }
    if (on) {
        chosen.push_back(wanted);
    } else {
        std::erase(chosen, wanted);
    }
    if (!choose(phone->second.deviceKey, std::move(chosen))) {
        return false;
    }
    syncTaps();
    refreshList();
    return true;
}

void MediaFrames::request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json)
{
    const auto refuse = [this, &caller, id](const QString& message) {
        if (m_replies.refuse) {
            m_replies.refuse(caller.logic, id, message);
        }
    };
    if (service != kFrames) {
        refuse(QStringLiteral("GS-API-070 no part of GameScrcpy offers the service %1").arg(fromView(service)));
        return;
    }
    if (caller.device.isEmpty()) {
        refuse(QString::fromStdString(script::phoneProblem()));
        return;
    }
    if (!m_shared->allowed(caller.extension)) {
        refuse(QString::fromStdString(script::permissionProblem(kPermission)));
        return;
    }
    const QJsonObject body = objectOf(json.toUtf8());
    const double fps = body.value(u"fps").toDouble(kDefaultFps);
    if (!(fps >= kLeastFps - kEdge && fps <= kMostFps + kEdge)) {
        refuse(invalidWith(QStringLiteral("fps is a number from 0.1 to 10")));
        return;
    }
    const double size = body.value(u"size").toDouble(kDefaultSize);
    if (!(size >= kLeastSize && size <= kMostSize) || std::floor(size) != size) {
        refuse(invalidWith(QStringLiteral("size is a whole number of pixels from 64 to 1920")));
        return;
    }
    script::Region region;
    if (const QJsonValue given = body.value(u"region"); given.isObject()) {
        const QJsonObject area = given.toObject();
        region = {area.value(u"x").toDouble(-1.0), area.value(u"y").toDouble(-1.0), area.value(u"width").toDouble(-1.0),
                  area.value(u"height").toDouble(-1.0)};
        if (!(region.x >= 0.0 && region.y >= 0.0 && region.width > 0.0 && region.height > 0.0 &&
              region.x + region.width <= 1.0 + kEdge && region.y + region.height <= 1.0 + kEdge)) {
            refuse(invalidWith(QStringLiteral("the region is a part of the picture, in ratios from 0 to 1")));
            return;
        }
    } else if (!given.isUndefined() && !given.isNull()) {
        refuse(invalidWith(QStringLiteral("the region is a part of the picture, in ratios from 0 to 1")));
        return;
    }
    if (!m_phones.contains(caller.device)) {
        sync();
    }
    if (!m_phones.contains(caller.device)) {
        refuse(QStringLiteral("GS-API-121 the phone shows no picture to take frames from"));
        return;
    }
    const auto mine = std::ranges::count_if(
        m_subscriptions, [&caller](const auto& entry) { return entry.second.logic == caller.logic; });
    if (static_cast<std::size_t>(mine) >= kMostSubscriptions) {
        refuse(QStringLiteral("GS-API-122 a logic takes frames with at most 4 subscriptions at once"));
        return;
    }
    Feed* feed = feedOf(caller.device);
    if (feed == nullptr) {
        refuse(QStringLiteral("GS-API-121 the phone shows no picture to take frames from"));
        return;
    }
    const quint64 handle = ++m_lastHandle;
    m_subscriptions.emplace(handle, Subscription{.logic = caller.logic, .owner = caller.extension, .serial = caller.device});
    feed->addWant(Feed::Want{.handle = handle, .fps = fps, .size = static_cast<int>(size), .region = region});
    if (m_replies.answer) {
        m_replies.answer(caller.logic, id, compactJson(QJsonObject{{QStringLiteral("handle"), handleText(handle)}}));
    }
}

script::ServiceAnswer MediaFrames::call(const LogicCaller& caller, std::string_view service, std::string_view json)
{
    return callWith(m_shared, caller, service, json);
}

script::ServiceAnswer MediaFrames::callWith(const std::shared_ptr<Shared>& shared, const LogicCaller& caller,
                                            std::string_view service, std::string_view json)
{
    if (service != kDone && service != kClose) {
        return {"null", "GS-API-070", "no part of GameScrcpy answers the service " + std::string(service) + " at once"};
    }
    const QJsonObject body = objectOf(QByteArray(json.data(), static_cast<qsizetype>(json.size())));
    const quint64 handle = handleOf(body.value(u"handle").toString());
    if (handle == 0) {
        return refused(invalidWith(QStringLiteral("the handle is not one of media.frames")));
    }
    const std::uint64_t logic = caller.logic;
    if (service == kDone) {
        shared->post([logic, handle](MediaFrames& self) { self.ready(logic, handle); });
    } else {
        shared->post([logic, handle](MediaFrames& self) { self.close(logic, handle, {}); });
    }
    return {"null", {}, {}};
}

void MediaFrames::endLogic(std::uint64_t logic)
{
    std::set<QString> serials;
    for (auto entry = m_subscriptions.begin(); entry != m_subscriptions.end();) {
        if (entry->second.logic != logic) {
            ++entry;
            continue;
        }
        if (Feed* feed = feedIf(entry->second.serial)) {
            feed->dropWant(entry->first);
        }
        serials.insert(entry->second.serial);
        entry = m_subscriptions.erase(entry);
    }
    for (const QString& serial : serials) {
        prune(serial);
    }
}

void MediaFrames::deliver(quint64 handle, const QImage& picture, script::Region region, script::PictureSize source,
                          double time)
{
    const auto found = m_subscriptions.find(handle);
    if (found == m_subscriptions.end()) {
        return;
    }
    const Subscription subscription = found->second;
    const std::uint64_t capture = m_replies.keep ? m_replies.keep(subscription.logic, picture, source, handle) : 0;
    if (capture == 0 || !m_replies.tell) {
        ready(subscription.logic, handle);
        return;
    }
    const QJsonObject image{{QStringLiteral("capture"), static_cast<double>(capture)},
                            {QStringLiteral("x"), region.x},
                            {QStringLiteral("y"), region.y},
                            {QStringLiteral("width"), region.width},
                            {QStringLiteral("height"), region.height},
                            {QStringLiteral("pixelWidth"), picture.width()},
                            {QStringLiteral("pixelHeight"), picture.height()}};
    m_replies.tell(subscription.logic, fromView(kEventKind), handleText(handle),
                   compactJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("frame")},
                                           {QStringLiteral("time"), time},
                                           {QStringLiteral("image"), image}}));
}

void MediaFrames::ready(std::uint64_t logic, quint64 handle)
{
    const auto found = m_subscriptions.find(handle);
    if (found == m_subscriptions.end() || found->second.logic != logic) {
        return;
    }
    if (Feed* feed = feedIf(found->second.serial)) {
        feed->readyFor(handle);
    }
}

void MediaFrames::close(std::uint64_t logic, quint64 handle, const QString& reason)
{
    const auto found = m_subscriptions.find(handle);
    if (found == m_subscriptions.end() || found->second.logic != logic) {
        return;
    }
    const QString serial = found->second.serial;
    m_subscriptions.erase(found);
    if (Feed* feed = feedIf(serial)) {
        feed->dropWant(handle);
    }
    if (m_replies.forget) {
        m_replies.forget(logic, handle);
    }
    if (!reason.isEmpty() && m_replies.tell) {
        m_replies.tell(logic, fromView(kEventKind), handleText(handle),
                       compactJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("closed")},
                                               {QStringLiteral("reason"), reason}}));
    }
    prune(serial);
}

void MediaFrames::showOverlay(const QString& serial, const std::string& key, quint64 instance,
                              const std::vector<exthost::Shape>& shapes)
{
    const auto found = m_running.find(std::pair(serial, key));
    if (found == m_running.end() || found->second.instance != instance || !m_sources.overlay) {
        return;
    }
    QVariantList list;
    for (const exthost::Shape& shape : shapes) {
        list.append(overlayShape(shape, found->second.extensionName));
    }
    m_sources.overlay(serial, groupOf(key), list);
}

void MediaFrames::tapFailed(const QString& serial, const std::string& key, quint64 instance, const QString& problem)
{
    const auto found = m_running.find(std::pair(serial, key));
    if (found == m_running.end() || found->second.instance != instance) {
        return;
    }
    log::warning(kCategory, "frame tap {} on {}: {}", key, serial.toStdString(), problem.toStdString());
    if (Feed* feed = feedIf(serial)) {
        feed->failTap(key, instance);
    }
    if (m_sources.overlay) {
        m_sources.overlay(serial, groupOf(key), {});
    }
}

}
