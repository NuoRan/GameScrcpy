#include "ScriptRuntime.h"

#include "BlueprintModel.h"
#include "ComponentLibrary.h"
#include "InputController.h"
#include "NativeExtensions.h"
#include "VisionSearch.h"

#include <gs/platform/AtomicFile.h>
#include <gs/script/Permissions.h>
#include <gs/script/Resources.h>
#include <gs/script/ScriptHost.h>
#include <gs/script/TextSearch.h>
#include <gs/scripthost/RemoteLogicHost.h>
#include <gs/vision/Glance.h>
#include <gs/vision/Matcher.h>

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace gs::app {

namespace {

constexpr std::size_t kMostStoreBytes = std::size_t{2} << 20;
constexpr qsizetype kMostWatched = 256;
constexpr qsizetype kMostProblemChars = 160;
constexpr int kSettleMs = 300;

QString pathText(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

Microseconds micros(double ms)
{
    return Microseconds{static_cast<Microseconds::rep>(std::llround(std::max(ms, 0.0) * 1000.0))};
}

bool scriptFile(const std::filesystem::path& path)
{
    std::wstring name = path.filename().wstring();
    std::ranges::transform(name, name.begin(),
                           [](wchar_t c) { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + 32) : c; });
    if (name.ends_with(L".d.ts")) {
        return false;
    }
    return name.ends_with(L".ts") || name.ends_with(L".mts") || name.ends_with(L".js") || name.ends_with(L".mjs");
}

bool skipped(const std::filesystem::path& directory)
{
    const std::wstring name = directory.filename().wstring();
    return name.starts_with(L'.') || name == L"node_modules" || name == L"types" || name == L"tests" ||
           name == L"sessions" || name == L"res";
}

bool truthyOf(const script::VarValue& value)
{
    if (const bool* on = std::get_if<bool>(&value)) {
        return *on;
    }
    if (const double* number = std::get_if<double>(&value)) {
        return *number != 0.0 && !std::isnan(*number);
    }
    return !std::get<std::string>(value).empty();
}

script::VarValue scriptValue(const input::VarValue& value)
{
    return std::visit([](const auto& each) { return script::VarValue(each); }, value);
}

std::string placedSettings(const std::string& settings, const input::Control& control,
                           const input::CoordinateMapper& mapper)
{
    QJsonObject object = QJsonDocument::fromJson(QByteArray::fromStdString(settings)).object();
    const auto ratio = [](double value) { return std::clamp(value, 0.0, 1.0); };
    const input::Vec2 at = mapper.position(control.anchor, control.offset);
    object.insert(QStringLiteral("at"), QJsonArray{ratio(at.x), ratio(at.y)});
    if (!input::componentRef(control).empty()) {
        QJsonObject points;
        for (const input::NamedValue& entry : input::componentProps(control)) {
            if (const auto* offset = std::get_if<input::Vec2>(&entry.value)) {
                const input::Vec2 step = mapper.extent(*offset);
                points.insert(QString::fromStdString(entry.name), QJsonArray{ratio(at.x + step.x), ratio(at.y + step.y)});
            }
        }
        object.insert(QStringLiteral("points"), points);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
}

void applyTuning(input::Control& control, const input::Control& base, const QJsonObject& asked,
                 const input::CoordinateMapper* mapper)
{
    const QString reset = asked.value(QStringLiteral("reset")).toString();
    if (reset == u"all") {
        control = base;
        return;
    }
    if (reset == u"position") {
        control.anchor = base.anchor;
        control.offset = base.offset;
    }
    if (const QJsonArray at = asked.value(QStringLiteral("at")).toArray(); at.size() == 2 && mapper != nullptr) {
        const input::Vec2 offset = mapper->offsetOf(control.anchor, {at[0].toDouble(), at[1].toDouble()});
        const auto tidy = [](double value) { return std::round(std::clamp(value, -2.0, 2.0) * 10000.0) / 10000.0 + 0.0; };
        control.offset = {tidy(offset.x), tidy(offset.y)};
    }
    const auto number = [&asked](const char* key) -> std::optional<double> {
        const QJsonValue value = asked.value(QLatin1StringView(key));
        return value.isDouble() ? std::optional<double>(value.toDouble()) : std::nullopt;
    };
    const auto whole = [](double value, double low, double high) {
        return static_cast<std::uint16_t>(std::lround(std::clamp(value, low, high)));
    };
    const auto radius = [&](double& value, double was) {
        if (reset == u"radius") {
            value = was;
        }
        if (const std::optional<double> given = number("radius")) {
            value = std::clamp(*given, 0.01, 1.0);
        }
    };
    const auto sensitivity = [&](input::Vec2& value, input::Vec2 was) {
        if (reset == u"sensitivity") {
            value = was;
        }
        if (const QJsonArray given = asked.value(QStringLiteral("sensitivity")).toArray(); given.size() == 2) {
            value = {std::clamp(given[0].toDouble(), 0.01, 100.0), std::clamp(given[1].toDouble(), 0.01, 100.0)};
        }
    };
    if (auto* tap = std::get_if<input::TapControl>(&control.kind)) {
        if (reset == u"duration") {
            tap->tapMs = std::get<input::TapControl>(base.kind).tapMs;
        }
        if (const std::optional<double> ms = number("tapMs")) {
            tap->tapMs = whole(*ms, 0.0, 1000.0);
        }
    } else if (auto* longPress = std::get_if<input::LongPressControl>(&control.kind)) {
        if (reset == u"duration") {
            longPress->holdMs = std::get<input::LongPressControl>(base.kind).holdMs;
        }
        if (const std::optional<double> ms = number("holdMs")) {
            longPress->holdMs = whole(*ms, 100.0, 10000.0);
        }
    } else if (auto* turbo = std::get_if<input::TurboControl>(&control.kind)) {
        if (reset == u"rate") {
            turbo->rateHz = std::get<input::TurboControl>(base.kind).rateHz;
        }
        if (const std::optional<double> rate = number("rateHz")) {
            turbo->rateHz = static_cast<std::uint8_t>(whole(*rate, 1.0, 30.0));
        }
    } else if (auto* swipe = std::get_if<input::SwipeControl>(&control.kind)) {
        if (reset == u"duration") {
            swipe->durationMs = std::get<input::SwipeControl>(base.kind).durationMs;
        }
        if (const std::optional<double> ms = number("durationMs")) {
            swipe->durationMs = whole(*ms, 10.0, 10000.0);
        }
    } else if (auto* wasd = std::get_if<input::WasdControl>(&control.kind)) {
        radius(wasd->radius, std::get<input::WasdControl>(base.kind).radius);
    } else if (auto* walker = std::get_if<input::RightWalkControl>(&control.kind)) {
        radius(walker->radius, std::get<input::RightWalkControl>(base.kind).radius);
    } else if (auto* pad = std::get_if<input::PadStickControl>(&control.kind)) {
        radius(pad->radius, std::get<input::PadStickControl>(base.kind).radius);
    } else if (auto* camera = std::get_if<input::CameraControl>(&control.kind)) {
        sensitivity(camera->sensitivity, std::get<input::CameraControl>(base.kind).sensitivity);
    } else if (auto* padCamera = std::get_if<input::PadCameraControl>(&control.kind)) {
        sensitivity(padCamera->sensitivity, std::get<input::PadCameraControl>(base.kind).sensitivity);
    } else if (auto* peek = std::get_if<input::FreeLookControl>(&control.kind)) {
        sensitivity(peek->sensitivity, std::get<input::FreeLookControl>(base.kind).sensitivity);
    } else if (auto* skill = std::get_if<input::SkillControl>(&control.kind)) {
        const auto& was = std::get<input::SkillControl>(base.kind);
        radius(skill->radius, was.radius);
        sensitivity(skill->sensitivity, was.sensitivity);
    }
}

QVariantList tuningChanges(const input::Control& base, const input::Control& tuned, const input::CoordinateMapper* mapper)
{
    QVariantList changes;
    const QString name = QString::fromStdString(base.name.empty() ? base.id : base.name);
    const auto add = [&](const QString& prop, const QString& before, const QString& after) {
        if (before != after) {
            changes.append(QVariantMap{{QStringLiteral("name"), name},
                                       {QStringLiteral("prop"), prop},
                                       {QStringLiteral("before"), before},
                                       {QStringLiteral("after"), after}});
        }
    };
    const auto point = [](input::Vec2 at) {
        return QStringLiteral("%1, %2").arg(at.x, 0, 'f', 2).arg(at.y, 0, 'f', 2);
    };
    const auto pair = [](input::Vec2 value) {
        return value.x == value.y ? QString::number(value.x, 'g', 4)
                                  : QStringLiteral("%1, %2").arg(value.x, 0, 'g', 4).arg(value.y, 0, 'g', 4);
    };
    if (base.anchor != tuned.anchor || base.offset != tuned.offset) {
        add(QStringLiteral("position"),
            mapper != nullptr ? point(mapper->position(base.anchor, base.offset)) : point(base.offset),
            mapper != nullptr ? point(mapper->position(tuned.anchor, tuned.offset)) : point(tuned.offset));
    }
    const auto number = [](double value) { return QString::number(value, 'g', 4); };
    if (const auto* tap = std::get_if<input::TapControl>(&tuned.kind)) {
        add(QStringLiteral("duration"), number(std::get<input::TapControl>(base.kind).tapMs), number(tap->tapMs));
    } else if (const auto* longPress = std::get_if<input::LongPressControl>(&tuned.kind)) {
        add(QStringLiteral("duration"), number(std::get<input::LongPressControl>(base.kind).holdMs),
            number(longPress->holdMs));
    } else if (const auto* turbo = std::get_if<input::TurboControl>(&tuned.kind)) {
        add(QStringLiteral("rate"), number(std::get<input::TurboControl>(base.kind).rateHz), number(turbo->rateHz));
    } else if (const auto* swipe = std::get_if<input::SwipeControl>(&tuned.kind)) {
        add(QStringLiteral("duration"), number(std::get<input::SwipeControl>(base.kind).durationMs),
            number(swipe->durationMs));
    } else if (const auto* wasd = std::get_if<input::WasdControl>(&tuned.kind)) {
        add(QStringLiteral("radius"), number(std::get<input::WasdControl>(base.kind).radius), number(wasd->radius));
    } else if (const auto* walker = std::get_if<input::RightWalkControl>(&tuned.kind)) {
        add(QStringLiteral("radius"), number(std::get<input::RightWalkControl>(base.kind).radius), number(walker->radius));
    } else if (const auto* pad = std::get_if<input::PadStickControl>(&tuned.kind)) {
        add(QStringLiteral("radius"), number(std::get<input::PadStickControl>(base.kind).radius), number(pad->radius));
    } else if (const auto* camera = std::get_if<input::CameraControl>(&tuned.kind)) {
        add(QStringLiteral("sensitivity"), pair(std::get<input::CameraControl>(base.kind).sensitivity),
            pair(camera->sensitivity));
    } else if (const auto* padCamera = std::get_if<input::PadCameraControl>(&tuned.kind)) {
        add(QStringLiteral("sensitivity"), pair(std::get<input::PadCameraControl>(base.kind).sensitivity),
            pair(padCamera->sensitivity));
    } else if (const auto* peek = std::get_if<input::FreeLookControl>(&tuned.kind)) {
        add(QStringLiteral("sensitivity"), pair(std::get<input::FreeLookControl>(base.kind).sensitivity),
            pair(peek->sensitivity));
    } else if (const auto* skill = std::get_if<input::SkillControl>(&tuned.kind)) {
        const auto& was = std::get<input::SkillControl>(base.kind);
        add(QStringLiteral("radius"), number(was.radius), number(skill->radius));
        add(QStringLiteral("sensitivity"), pair(was.sensitivity), pair(skill->sensitivity));
    }
    return changes;
}

std::string statusJson(const input::ControlStatus& status)
{
    return std::format(
        R"({{"touching":{},"stick":{{"x":{},"y":{}}},"aiming":{},"walking":{},"cancelled":{},"cell":{}}})",
        status.touching, status.stick.x, status.stick.y, status.aiming, status.walking, status.cancelled, status.cell);
}

QString drawingKindName(script::DrawingKind kind)
{
    switch (kind) {
    case script::DrawingKind::Box:
        return QStringLiteral("box");
    case script::DrawingKind::Mark:
        return QStringLiteral("mark");
    case script::DrawingKind::Image:
        return QStringLiteral("image");
    case script::DrawingKind::Text:
        break;
    }
    return QStringLiteral("text");
}

class GlobalBoard
{
public:
    using Listener = std::function<void(const std::string&, const std::string&)>;

    static GlobalBoard& instance()
    {
        static GlobalBoard board;
        return board;
    }

    std::optional<std::string> get(const QString& device, const std::string& name)
    {
        const std::scoped_lock lock(m_mutex);
        const auto values = m_values.find(device);
        if (values == m_values.end()) {
            return std::nullopt;
        }
        const auto found = values->second.find(name);
        return found != values->second.end() ? std::optional<std::string>(found->second) : std::nullopt;
    }

    void set(const QString& device, const std::string& name, const std::string& json)
    {
        std::vector<Listener> listeners;
        {
            const std::scoped_lock lock(m_mutex);
            std::string& slot = m_values[device][name];
            if (slot == json) {
                return;
            }
            slot = json;
            for (const auto& [key, entry] : m_listeners) {
                if (entry.first == device) {
                    listeners.push_back(entry.second);
                }
            }
        }
        for (const Listener& listener : listeners) {
            listener(name, json);
        }
    }

    std::uint64_t join(const QString& device, Listener listener)
    {
        const std::scoped_lock lock(m_mutex);
        const std::uint64_t key = ++m_next;
        m_listeners.emplace(key, std::make_pair(device, std::move(listener)));
        return key;
    }

    void leave(std::uint64_t key)
    {
        const std::scoped_lock lock(m_mutex);
        m_listeners.erase(key);
    }

private:
    std::mutex m_mutex;
    std::map<QString, std::map<std::string, std::string>> m_values;
    std::map<std::uint64_t, std::pair<QString, Listener>> m_listeners;
    std::uint64_t m_next = 0;
};

}

void NoPhoneLane::attach(std::shared_ptr<const input::Profile>, std::shared_ptr<LaneObserver> observer)
{
    observer->synced({});
}

void NoPhoneLane::detach(std::shared_ptr<LaneObserver>) {}

void NoPhoneLane::post(std::function<void(InputHost& host, Microseconds now)>) {}

TaskLane::TaskLane(std::shared_ptr<LaneAccess> lane) : m_lane(std::move(lane)) {}

void TaskLane::attach(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer)
{
    observer->synced({});
    m_observer = observer;
    m_lane->watch(std::move(profile), std::move(observer));
}

void TaskLane::detach(std::shared_ptr<LaneObserver> observer)
{
    m_lane->unwatch(std::move(observer));
}

void TaskLane::post(std::function<void(InputHost& host, Microseconds now)> task)
{
    m_lane->post(std::move(task));
}

void TaskLane::claimKeys(std::vector<input::InputSource> keys)
{
    if (std::shared_ptr<LaneObserver> observer = m_observer.lock()) {
        m_lane->watchKeys(std::move(observer), std::move(keys));
    }
}

ControllerLane::ControllerLane(InputController& input, QString serial, std::shared_ptr<ControlChannel> channel)
    : m_input(input)
    , m_serial(std::move(serial))
    , m_channel(std::move(channel))
{
}

void ControllerLane::attach(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer)
{
    m_input.attachScript(m_serial, m_channel, std::move(profile), std::move(observer));
}

void ControllerLane::detach(std::shared_ptr<LaneObserver> observer)
{
    m_input.detachScript(m_channel, observer);
}

void ControllerLane::post(std::function<void(InputHost& host, Microseconds now)> task)
{
    m_input.post(std::move(task));
}

void ControllerLane::claimKeys(std::vector<input::InputSource> keys)
{
    m_input.claimScriptKeys(m_serial, m_channel, std::move(keys));
}

void ControllerLane::handOver(std::vector<std::string> ids)
{
    m_input.handOverScriptControls(m_channel, std::move(ids));
}

void ControllerLane::watch(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer)
{
    m_input.watchScript(m_channel, std::move(profile), std::move(observer));
}

void ControllerLane::unwatch(std::shared_ptr<LaneObserver> observer)
{
    m_input.unwatchScript(m_channel, observer);
}

void ControllerLane::watchKeys(std::shared_ptr<LaneObserver> observer, std::vector<input::InputSource> keys)
{
    m_input.watchScriptKeys(m_channel, observer, std::move(keys));
}

std::optional<input::Vec2> ControllerLane::cursor() const
{
    return m_input.cursorOver(m_serial);
}

bool ControllerLane::held(std::string_view name) const
{
    return m_input.heldOn(m_serial, name);
}

class ScriptRuntime::StoreWriter
{
public:
    explicit StoreWriter(std::function<void()> busy)
        : m_busy(std::move(busy))
        , m_thread([this](const std::stop_token& stop) { run(stop); })
    {
    }

    ~StoreWriter()
    {
        m_thread.request_stop();
        m_wake.notify_all();
    }

    StoreWriter(const StoreWriter&) = delete;
    StoreWriter& operator=(const StoreWriter&) = delete;

    void save(std::filesystem::path path, std::string contents)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_pending = Pending{.path = std::move(path), .contents = std::move(contents)};
            m_unsaved.reset();
        }
        m_wake.notify_all();
    }

    void setAsideFirst(std::filesystem::path path)
    {
        const std::scoped_lock lock(m_mutex);
        m_kept.erase(path);
        m_asides.insert(std::move(path));
    }

    void cancelAside(const std::filesystem::path& path)
    {
        const std::scoped_lock lock(m_mutex);
        m_asides.erase(path);
        m_kept.erase(path);
    }

    [[nodiscard]] std::optional<std::string> latest(const std::filesystem::path& path)
    {
        const std::scoped_lock lock(m_mutex);
        const std::array<const Pending*, 3> candidates{m_pending ? &*m_pending : nullptr, m_writing,
                                                       m_unsaved ? &*m_unsaved : nullptr};
        for (const Pending* each : candidates) {
            if (each != nullptr && each->path == path) {
                return each->contents;
            }
        }
        return std::nullopt;
    }

private:
    struct Pending {
        std::filesystem::path path;
        std::string contents;
    };

    void run(const std::stop_token& stop)
    {
        std::unique_lock lock(m_mutex);
        bool lastChance = false;
        for (;;) {
            m_wake.wait(lock, stop, [this] { return m_pending.has_value(); });
            if (!m_pending) {
                if (!stop.stop_requested() || !m_unsaved || std::exchange(lastChance, true)) {
                    return;
                }
                m_pending = std::exchange(m_unsaved, std::nullopt);
            }
            Pending next = std::move(*m_pending);
            m_pending.reset();
            m_writing = &next;
            const bool aside = m_asides.erase(next.path) > 0;
            bool kept = m_kept.contains(next.path);
            lock.unlock();
            std::error_code error;
            if (aside) {
                kept = !moveAside(next.path);
            }
            if (!kept) {
                std::filesystem::create_directories(next.path.parent_path(), error);
                if (!error) {
                    error = platform::writeFileAtomically(next.path, next.contents);
                }
                const bool busy = platform::fileBusy(error);
                if (error) {
                    qWarning("logic store %s could not be saved%s", qUtf8Printable(pathText(next.path)),
                             busy ? ": another program is using it" : "");
                }
                if (busy && m_busy) {
                    m_busy();
                }
            }
            lock.lock();
            if (kept) {
                m_kept.insert(next.path);
            }
            m_writing = nullptr;
            if (!error) {
                m_unsaved.reset();
            } else if (!m_pending) {
                m_unsaved = std::move(next);
            }
        }
    }

    bool moveAside(const std::filesystem::path& path)
    {
        std::filesystem::path bad = path;
        bad += L".bad";
        const std::error_code moved = platform::replaceFile(path, bad);
        if (!moved) {
            qWarning("logic store %s is kept with .bad added to its name", qUtf8Printable(pathText(path)));
            return true;
        }
        if (platform::fileMissing(moved)) {
            return true;
        }
        qWarning("logic store %s is still used by another program: it stays where it is and is not saved over "
                 "(error %d)",
                 qUtf8Printable(pathText(path)), moved.value());
        if (platform::fileBusy(moved) && m_busy) {
            m_busy();
        }
        return false;
    }

    const std::function<void()> m_busy;
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::optional<Pending> m_pending;
    std::optional<Pending> m_unsaved;
    const Pending* m_writing = nullptr;
    std::set<std::filesystem::path> m_asides;
    std::set<std::filesystem::path> m_kept;
    std::jthread m_thread;
};

class ScriptRuntime::Seer
{
public:
    using Reply = std::function<void(std::uint64_t, std::string)>;

    Seer(std::filesystem::path folder, std::shared_ptr<media::FrameSnapshots> snapshots, Reply answer, Reply refuse)
        : m_folder(std::move(folder))
        , m_snapshots(std::move(snapshots))
        , m_answer(std::move(answer))
        , m_refuse(std::move(refuse))
        , m_shared(std::make_shared<Shared>())
    {
        if (m_snapshots != nullptr) {
            m_snapshots->setListener(
                [shared = std::weak_ptr<Shared>(m_shared)](const std::shared_ptr<const media::LumaSnapshot>& made) {
                    if (const std::shared_ptr<Shared> alive = shared.lock()) {
                        {
                            const std::scoped_lock lock(alive->mutex);
                            alive->fresh = made;
                        }
                        alive->wake.notify_all();
                    }
                });
        }
        m_thread = std::jthread([this](const std::stop_token& stop) { run(stop); });
    }

    ~Seer()
    {
        if (m_snapshots != nullptr) {
            m_snapshots->setListener({});
        }
        m_thread.request_stop();
    }

    Seer(const Seer&) = delete;
    Seer& operator=(const Seer&) = delete;

    void find(std::uint64_t id, const script::FindQuery& query)
    {
        enqueue(id, query, query.match == script::MatchMode::Color);
    }

    void findColor(std::uint64_t id, const script::ColorQuery& query) { enqueue(id, query, true); }

    void pickColor(std::uint64_t id, const script::PixelQuery& query) { enqueue(id, query, true); }

    void capture(std::uint64_t id, const script::CaptureQuery& query) { enqueue(id, query, true); }

    void changed(std::uint64_t id, const script::ChangeQuery& query) { enqueue(id, query, false); }

    void readText(std::uint64_t id, const script::TextQuery& query) { enqueue(id, query, false); }

    void save(std::uint64_t id, std::uint64_t capture, std::string name)
    {
        {
            const std::scoped_lock lock(m_shared->mutex);
            m_shared->saves.push_back(Save{.id = id, .capture = capture, .name = std::move(name)});
        }
        m_shared->wake.notify_all();
    }

    struct Captured {
        QImage picture;
        QByteArray png;
        script::PictureSize source;
        quint64 frames = 0;
    };

    std::optional<Captured> captured(std::uint64_t capture)
    {
        const std::scoped_lock lock(m_capturesMutex);
        const auto found = m_captures.find(capture);
        return found == m_captures.end() ? std::nullopt : std::optional<Captured>(found->second);
    }

    std::uint64_t keepFrame(QImage picture, script::PictureSize source, quint64 frames)
    {
        const std::scoped_lock lock(m_capturesMutex);
        const std::uint64_t capture = ++m_lastCapture;
        m_captures.emplace(capture, Captured{.picture = std::move(picture), .png = {}, .source = source, .frames = frames});
        keepNewest(frames, kMostFrames);
        return capture;
    }

    void forgetFrames(quint64 frames)
    {
        const std::scoped_lock lock(m_capturesMutex);
        std::erase_if(m_captures, [frames](const auto& entry) { return entry.second.frames == frames; });
    }

    [[nodiscard]] const std::filesystem::path& folder() const noexcept { return m_folder; }

    static QByteArray pngOf(const QImage& picture)
    {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !picture.save(&buffer, "PNG")) {
            return {};
        }
        return bytes;
    }

    void cancel(std::uint64_t id)
    {
        {
            const std::scoped_lock lock(m_shared->mutex);
            m_shared->cancelled.push_back(id);
        }
        m_shared->wake.notify_all();
    }

private:
    using Clock = std::chrono::steady_clock;
    using Query = std::variant<script::FindQuery, script::ColorQuery, script::PixelQuery, script::CaptureQuery,
                               script::ChangeQuery, script::TextQuery>;

    static constexpr auto kFreshFrameGrace = std::chrono::milliseconds(250);
    static constexpr std::size_t kMostPatterns = 64;
    static constexpr std::size_t kMostCaptures = script::kMostCaptures;
    static constexpr std::size_t kMostFrames = 2;
    static constexpr std::size_t kMostGlances = script::kMostChangeLooks;

    struct Request {
        std::uint64_t id = 0;
        Query query;
        bool color = false;
        std::uint64_t after = 0;
        Clock::time_point grace;
        Clock::time_point deadline;
        bool tried = false;
    };

    struct Save {
        std::uint64_t id = 0;
        std::uint64_t capture = 0;
        std::string name;
    };

    struct Shared {
        std::mutex mutex;
        std::condition_variable_any wake;
        std::vector<Request> incoming;
        std::vector<std::uint64_t> cancelled;
        std::shared_ptr<const media::LumaSnapshot> fresh;
        std::vector<Save> saves;
    };

    struct Picture {
        std::filesystem::file_time_type written;
        std::uintmax_t size = 0;
        QImage picture;
        std::string problem;
    };

    struct Pattern {
        std::filesystem::file_time_type written;
        std::uintmax_t size = 0;
        std::shared_ptr<const vision::Template> pattern;
        std::string problem;
    };

    using PatternKey = std::tuple<std::string, bool, int, int>;

    static std::string nothing(const Query& query)
    {
        if (const auto* pixel = std::get_if<script::PixelQuery>(&query)) {
            return pixel->match ? "false" : "null";
        }
        if (std::holds_alternative<script::ChangeQuery>(query)) {
            return "false";
        }
        const auto* find = std::get_if<script::FindQuery>(&query);
        return find != nullptr && find->all ? "[]" : "null";
    }

    static std::string labelOf(const script::FindQuery& query)
    {
        return query.file == query.image ? query.image : std::format("{} ({})", query.image, query.file);
    }

    void enqueue(std::uint64_t id, Query query, bool color)
    {
        if (m_snapshots == nullptr) {
            m_answer(id, nothing(query));
            return;
        }
        const double timeoutMs = std::visit([](const auto& asked) { return asked.timeoutMs; }, query);
        const auto now = std::chrono::steady_clock::now();
        const auto wait = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double, std::milli>(std::max(timeoutMs, 0.0)));
        {
            const std::scoped_lock lock(m_shared->mutex);
            m_shared->incoming.push_back(Request{.id = id,
                                                 .query = std::move(query),
                                                 .color = color,
                                                 .after = m_snapshots->offered(),
                                                 .grace = now + std::min<Clock::duration>(wait, kFreshFrameGrace),
                                                 .deadline = now + wait});
        }
        m_snapshots->request(color);
        m_shared->wake.notify_all();
    }

    void run(const std::stop_token& stop)
    {
        std::vector<Request> waiting;
        for (;;) {
            std::vector<Request> arrived;
            std::vector<std::uint64_t> cancelled;
            std::shared_ptr<const media::LumaSnapshot> fresh;
            std::vector<Save> saves;
            {
                std::unique_lock lock(m_shared->mutex);
                const auto ready = [this] {
                    return !m_shared->incoming.empty() || !m_shared->cancelled.empty() || m_shared->fresh != nullptr ||
                           !m_shared->saves.empty();
                };
                std::optional<Clock::time_point> due;
                for (const Request& request : waiting) {
                    const Clock::time_point next = request.tried ? request.deadline : request.grace;
                    due = due ? std::min(*due, next) : next;
                }
                if (due) {
                    m_shared->wake.wait_until(lock, stop, *due, ready);
                } else {
                    m_shared->wake.wait(lock, stop, ready);
                }
                if (stop.stop_requested()) {
                    return;
                }
                arrived = std::exchange(m_shared->incoming, {});
                cancelled = std::exchange(m_shared->cancelled, {});
                fresh = std::exchange(m_shared->fresh, nullptr);
                saves = std::exchange(m_shared->saves, {});
            }
            const auto withdrawn = [&cancelled](const Request& request) {
                return std::ranges::find(cancelled, request.id) != cancelled.end();
            };
            for (const Save& save : saves) {
                if (std::ranges::find(cancelled, save.id) != cancelled.end()) {
                    continue;
                }
                std::string problem = saveNow(save.capture, save.name);
                if (problem.empty()) {
                    m_answer(save.id, "null");
                } else {
                    m_refuse(save.id, std::move(problem));
                }
            }
            std::erase_if(waiting, withdrawn);
            for (Request& request : arrived) {
                if (withdrawn(request)) {
                    continue;
                }
                if (const auto* query = std::get_if<script::FindQuery>(&request.query)) {
                    std::string problem;
                    if (patternFor(*query, 0, 0, problem) == nullptr) {
                        m_refuse(request.id, std::move(problem));
                        continue;
                    }
                }
                waiting.push_back(std::move(request));
            }
            const Clock::time_point now = Clock::now();
            const auto freshFor = [&fresh](const Request& request) {
                return fresh != nullptr && fresh->frame > request.after && (!request.color || fresh->colored());
            };
            const bool colorDue = std::ranges::any_of(waiting, [&](const Request& request) {
                return request.color && !request.tried && now >= request.grace && !freshFor(request);
            });
            std::shared_ptr<const media::LumaSnapshot> current;
            bool captured = false;
            std::erase_if(waiting, [&](Request& request) {
                if (freshFor(request)) {
                    serve(request, *fresh);
                    return true;
                }
                if (!request.tried && now >= request.grace) {
                    if (!std::exchange(captured, true)) {
                        current = m_snapshots->capture(colorDue);
                    }
                    if (current != nullptr) {
                        serve(request, *current);
                        return true;
                    }
                    request.tried = true;
                }
                if (now >= request.deadline) {
                    m_answer(request.id, nothing(request.query));
                    return true;
                }
                return false;
            });
        }
    }

    void serve(const Request& request, const media::LumaSnapshot& shot)
    {
        if (const auto* pixel = std::get_if<script::PixelQuery>(&request.query)) {
            m_answer(request.id, pixelJson(shot, *pixel));
            return;
        }
        if (const auto* query = std::get_if<script::CaptureQuery>(&request.query)) {
            serveCapture(request.id, *query, shot);
            return;
        }
        if (const auto* query = std::get_if<script::TextQuery>(&request.query)) {
            const vision::Rect area = pixelArea(query->region, shot.width, shot.height);
            script::VisionAnswer answer = script::answerText(*query,
                                                             {.plane = shot.luma.data(),
                                                              .stride = shot.width,
                                                              .left = area.x,
                                                              .top = area.y,
                                                              .width = area.width,
                                                              .height = area.height},
                                                             {shot.width, shot.height});
            if (answer.problem.empty()) {
                m_answer(request.id, std::move(answer.json));
            } else {
                m_refuse(request.id, std::move(answer.problem));
            }
            return;
        }
        if (const auto* query = std::get_if<script::ChangeQuery>(&request.query)) {
            const vision::Rect area = pixelArea(query->region, shot.width, shot.height);
            vision::Glance seen =
                area.empty() ? vision::Glance{}
                             : vision::glanceOf(vision::ImageView(shot.luma.data(), shot.width, shot.height, shot.width)
                                                    .crop(area));
            bool changed = false;
            if (const auto found = m_glances.find(query->key); found != m_glances.end()) {
                changed = vision::changedShare(found->second, seen) > query->threshold;
                found->second = std::move(seen);
            } else {
                if (m_glances.size() >= kMostGlances) {
                    m_glances.clear();
                }
                m_glances.emplace(query->key, std::move(seen));
            }
            m_answer(request.id, changed ? "true" : "false");
            return;
        }
        if (const auto* query = std::get_if<script::ColorQuery>(&request.query)) {
            const vision::Rect area = pixelArea(query->region, shot.width, shot.height);
            const std::optional<vision::ColorPatch> patch =
                findColorIn(shot, {query->rgb.red, query->rgb.green, query->rgb.blue}, query->tolerance, area);
            m_answer(request.id,
                     patch ? foundJson(patch->rect, patch->score, shot.width, shot.height) : std::string("null"));
            return;
        }
        const auto& query = std::get<script::FindQuery>(request.query);
        std::string problem;
        const std::shared_ptr<const vision::Template> pattern = patternFor(query, shot.width, shot.height, problem);
        if (pattern == nullptr) {
            m_refuse(request.id, std::move(problem));
            return;
        }
        const vision::Rect area = pixelArea(query.region, shot.width, shot.height);
        const std::vector<vision::Match> found = findTemplate(shot, *pattern, area, query.threshold, query.most, query.all);
        if (!query.all) {
            m_answer(request.id, found.empty() ? std::string("null")
                                               : foundJson(found.front().rect, found.front().score, shot.width,
                                                           shot.height));
            return;
        }
        std::string json = "[";
        for (const vision::Match& match : found) {
            if (json.size() > 1) {
                json += ',';
            }
            json += foundJson(match.rect, match.score, shot.width, shot.height);
        }
        json += ']';
        m_answer(request.id, std::move(json));
    }

    const Picture* pictureFor(const script::FindQuery& query, std::string& problem)
    {
        const std::filesystem::path path = m_folder / QString::fromStdString(query.file).toStdWString();
        std::error_code error;
        const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, error);
        const std::uintmax_t size = error ? 0 : std::filesystem::file_size(path, error);
        if (error) {
            m_pictures.erase(query.file);
            problem = std::format("GS-API-016 {} cannot be read: there is no such file in the profile folder",
                                  labelOf(query));
            return nullptr;
        }
        auto found = m_pictures.find(query.file);
        if (found == m_pictures.end() || found->second.written != written || found->second.size != size) {
            if (m_pictures.size() >= kMostPatterns) {
                m_pictures.clear();
            }
            Picture& made = m_pictures[query.file];
            made = Picture{.written = written, .size = size};
            made.picture = readPicture(pathText(path), labelOf(query), made.problem);
            found = m_pictures.find(query.file);
        }
        if (!found->second.problem.empty()) {
            problem = found->second.problem;
            return nullptr;
        }
        return &found->second;
    }

    void serveCapture(std::uint64_t id, const script::CaptureQuery& query, const media::LumaSnapshot& shot)
    {
        const vision::Rect area = pixelArea(query.region, shot.width, shot.height);
        const std::vector<std::uint8_t> rgb = area.empty() || !shot.colored()
                                                  ? std::vector<std::uint8_t>()
                                                  : media::rgbOf(shot, area.x, area.y, area.width, area.height);
        const auto row = 3 * static_cast<std::size_t>(std::max(area.width, 0));
        if (rgb.size() < row * static_cast<std::size_t>(std::max(area.height, 0)) || rgb.empty()) {
            m_answer(id, "null");
            return;
        }
        QImage picture(area.width, area.height, QImage::Format_RGB888);
        for (int y = 0; y < area.height; ++y) {
            std::memcpy(picture.scanLine(y), rgb.data() + row * static_cast<std::size_t>(y), row);
        }
        QByteArray png = pngOf(picture);
        std::uint64_t capture = 0;
        {
            const std::scoped_lock lock(m_capturesMutex);
            capture = ++m_lastCapture;
            m_captures.emplace(capture, Captured{.picture = std::move(picture),
                                                 .png = std::move(png),
                                                 .source = {shot.width, shot.height}});
            keepNewest(0, kMostCaptures);
        }
        const double w = shot.width;
        const double h = shot.height;
        m_answer(id, std::format(R"({{"capture":{},"x":{},"y":{},"width":{},"height":{},"pixelWidth":{},"pixelHeight":{}}})",
                                 capture, area.x / w, area.y / h, area.width / w, area.height / h, area.width,
                                 area.height));
    }

    void keepNewest(quint64 frames, std::size_t most)
    {
        auto count = static_cast<std::size_t>(
            std::ranges::count_if(m_captures, [frames](const auto& entry) { return entry.second.frames == frames; }));
        for (auto entry = m_captures.begin(); entry != m_captures.end() && count > most;) {
            if (entry->second.frames == frames) {
                entry = m_captures.erase(entry);
                --count;
            } else {
                ++entry;
            }
        }
    }

    std::string saveNow(std::uint64_t capture, const std::string& name)
    {
        const std::optional<Captured> kept = captured(capture);
        if (!kept) {
            return std::format("GS-API-037 captured image {} is no longer kept: a script keeps its {} latest captured "
                               "images",
                               capture, kMostCaptures);
        }
        const QByteArray png = kept->png.isEmpty() ? pngOf(kept->picture) : kept->png;
        if (png.isEmpty() || m_folder.empty()) {
            return "GS-API-038 the image cannot be saved here";
        }
        const std::filesystem::path res = m_folder / L"res";
        const std::filesystem::path index = res / L"resources.json";
        const script::LoadedResources loaded = script::loadResources(index);
        if (!loaded) {
            return std::format("GS-API-020 {} cannot be used: {}", script::kResourcesFile,
                               loaded.error().empty() ? std::string("it is not valid") : loaded.error().front());
        }
        script::Resources resources = *loaded;
        const auto existing = std::ranges::find(resources.templates, name, &script::TemplateResource::name);
        if (existing == resources.templates.end() && resources.templates.size() >= script::kMostResourcesOfAKind) {
            return std::format("GS-API-038 the image cannot be saved: the folder already has {} templates",
                               script::kMostResourcesOfAKind);
        }
        const std::string file =
            existing != resources.templates.end()
                ? existing->file
                : templateFileFor(QString::fromStdString(name), resources.templates, {}, [&res](const std::string& made) {
                      std::error_code error;
                      return std::filesystem::exists(res / QString::fromStdString(made).toStdWString(), error);
                  });
        std::error_code error;
        std::filesystem::create_directories(res, error);
        const std::error_code written =
            error ? error
                  : platform::writeFileAtomically(
                        res / QString::fromStdString(file).toStdWString(),
                        std::string_view(png.constData(), static_cast<std::size_t>(png.size())));
        if (written) {
            return platform::fileBusy(written) ? "GS-API-038 the image cannot be saved: another program is using res/"
                                               : "GS-API-038 the image cannot be saved: res/ cannot be written";
        }
        if (existing != resources.templates.end()) {
            existing->source = kept->source;
        } else {
            resources.templates.push_back({.name = name,
                                           .file = file,
                                           .threshold = script::kNewTemplateThreshold,
                                           .match = script::MatchMode::Color,
                                           .region = {},
                                           .source = kept->source});
        }
        if (script::saveResources(index, resources)) {
            return "GS-API-038 the image cannot be saved: res/resources.json cannot be written";
        }
        return {};
    }

    std::shared_ptr<const vision::Template> capturedPattern(const script::FindQuery& query, int width, int height,
                                                            std::string& problem)
    {
        const std::optional<Captured> kept = captured(query.capture);
        if (!kept) {
            problem = std::format("GS-API-037 {} is no longer kept: a script keeps its {} latest captured images",
                                  query.image, kMostCaptures);
            return nullptr;
        }
        const QImage scaled = scaledPicture(kept->picture, kept->source, width, height);
        const bool color = query.match == script::MatchMode::Color;
        const PatternKey key{query.image, color, scaled.width(), scaled.height()};
        if (const auto found = m_patterns.find(key); found != m_patterns.end()) {
            problem = found->second.problem;
            return found->second.pattern;
        }
        if (m_patterns.size() >= kMostPatterns) {
            m_patterns.clear();
        }
        Pattern& made = m_patterns[key];
        made.pattern = templateOf(scaled, color, query.image, made.problem);
        problem = made.problem;
        return made.pattern;
    }

    std::shared_ptr<const vision::Template> patternFor(const script::FindQuery& query, int width, int height,
                                                       std::string& problem)
    {
        if (query.capture != 0) {
            return capturedPattern(query, width, height, problem);
        }
        const Picture* picture = pictureFor(query, problem);
        if (picture == nullptr) {
            return nullptr;
        }
        const QImage scaled = scaledPicture(picture->picture, query.source, width, height);
        const bool color = query.match == script::MatchMode::Color;
        const PatternKey key{query.file, color, scaled.width(), scaled.height()};
        if (const auto found = m_patterns.find(key);
            found != m_patterns.end() && found->second.written == picture->written && found->second.size == picture->size) {
            problem = found->second.problem;
            return found->second.pattern;
        }
        if (m_patterns.size() >= kMostPatterns) {
            m_patterns.clear();
        }
        Pattern& made = m_patterns[key];
        made = Pattern{.written = picture->written, .size = picture->size};
        made.pattern = templateOf(scaled, color, labelOf(query), made.problem);
        problem = made.problem;
        return made.pattern;
    }

    const std::filesystem::path m_folder;
    const std::shared_ptr<media::FrameSnapshots> m_snapshots;
    const Reply m_answer;
    const Reply m_refuse;
    const std::shared_ptr<Shared> m_shared;
    std::map<std::string, Picture> m_pictures;
    std::map<PatternKey, Pattern> m_patterns;
    std::map<std::string, vision::Glance> m_glances;
    std::uint64_t m_lastCapture = 0;
    std::mutex m_capturesMutex;
    std::map<std::uint64_t, Captured> m_captures;
    std::jthread m_thread;
};

class ScriptRuntime::Link final : public LaneObserver,
                                  public script::ScriptServices,
                                  public std::enable_shared_from_this<ScriptRuntime::Link>
{
public:
    Link(ScriptRuntime& runtime, const LogicSource& source, std::shared_ptr<LaneAccess> lane,
         std::filesystem::path store, StoreWriter* writer)
        : m_runtime(&runtime)
        , m_lane(std::move(lane))
        , m_channel(m_lane->channel())
        , m_store(std::move(store))
        , m_writer(writer)
        , m_deviceKey(source.deviceKey.isEmpty() ? source.device : source.deviceKey)
        , m_task(source.task)
        , m_taskParams(source.task ? source.taskParams : std::string("null"))
        , m_gestureOffset(source.gestureOffset)
        , m_profile(source.profile)
        , m_caller(runtime.m_caller)
        , m_guarded(!source.extension.isEmpty())
        , m_permissions(source.permissions)
        , m_follows(source.followsProfile)
        , m_inputGrant(source.inputGrant)
    {
        m_profileFact = QJsonDocument(QJsonObject{{QStringLiteral("id"), source.id}, {QStringLiteral("name"), source.name}})
                            .toJson(QJsonDocument::Compact)
                            .toStdString();
        m_controls = controlInfosOf(source.profile);
        m_states = stateInfosOf(source.profile);
    }

    ~Link() override { GlobalBoard::instance().leave(m_globals); }

    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    void joinGlobals()
    {
        m_globals = GlobalBoard::instance().join(
            m_deviceKey, [weak = weak_from_this()](const std::string& name, const std::string& json) {
                if (const std::shared_ptr<Link> link = weak.lock()) {
                    link->tell("global", name, json);
                }
            });
    }

    void bind(script::LogicHost* host, Seer* seer)
    {
        const std::scoped_lock lock(m_mutex);
        m_host = host;
        m_seer = seer;
    }

    void setDebugRecording(bool on)
    {
        m_debugRecording.store(on);
        const std::scoped_lock lock(m_debugMutex);
        m_debugEvents.clear();
    }

    void noteDebug(QVariantMap event)
    {
        if (!m_debugRecording.load()) {
            return;
        }
        event.insert(QStringLiteral("at"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
        const std::scoped_lock lock(m_debugMutex);
        if (m_debugEvents.size() < 500) {
            m_debugEvents.push_back(std::move(event));
        }
    }

    [[nodiscard]] QVariantList takeDebugEvents()
    {
        const std::scoped_lock lock(m_debugMutex);
        QVariantList events;
        events.swap(m_debugEvents);
        return events;
    }

    void setScript(script::ScriptId script)
    {
        const std::scoped_lock lock(m_mutex);
        m_script = script;
    }

    void close()
    {
        const std::scoped_lock lock(m_mutex);
        m_runtime = nullptr;
        m_host = nullptr;
        m_seer = nullptr;
        m_script = 0;
    }

    void forgetWriter()
    {
        const std::scoped_lock lock(m_mutex);
        m_writer = nullptr;
    }

    void setStorePath(std::filesystem::path path)
    {
        const std::scoped_lock lock(m_mutex);
        m_store = std::move(path);
    }

    [[nodiscard]] bool wrote(const std::filesystem::path& path)
    {
        const std::scoped_lock lock(m_mutex);
        return m_written.contains(path);
    }

    [[nodiscard]] bool ready()
    {
        const std::scoped_lock lock(m_mutex);
        return m_synced;
    }

    void setDevice(const script::DeviceInfo& device)
    {
        const std::scoped_lock lock(m_mutex);
        const bool turned = m_deviceSet && device.rotation != m_device.rotation;
        m_device = device;
        m_deviceSet = true;
        if (turned && m_host != nullptr && m_script != 0) {
            m_host->event("app", "rotation",
                          QJsonDocument(QJsonObject{{QStringLiteral("model"), QString::fromStdString(device.model)},
                                                    {QStringLiteral("android"), device.android},
                                                    {QStringLiteral("width"), device.width},
                                                    {QStringLiteral("height"), device.height},
                                                    {QStringLiteral("rotation"), device.rotation}})
                              .toJson(QJsonDocument::Compact)
                              .toStdString());
        }
    }

    std::vector<std::pair<std::string, bool>> takeRestores()
    {
        const std::scoped_lock lock(m_mutex);
        std::vector<std::pair<std::string, bool>> restores(m_restore.begin(), m_restore.end());
        for (const auto& [id, on] : restores) {
            if (script::ControlInfo* control = controlOf(id)) {
                control->enabled = on;
            }
        }
        m_restore.clear();
        return restores;
    }

    std::vector<input::Control> takeTunings()
    {
        const std::scoped_lock lock(m_mutex);
        std::vector<input::Control> originals;
        if (m_profile != nullptr) {
            for (const input::Control& control : m_profile->controls) {
                if (m_tuned.contains(control.id)) {
                    originals.push_back(control);
                }
            }
        }
        m_tuned.clear();
        return originals;
    }

    [[nodiscard]] bool takeReveal()
    {
        const std::scoped_lock lock(m_mutex);
        return std::exchange(m_revealed, false);
    }

    [[nodiscard]] std::shared_ptr<const input::Profile> profile()
    {
        const std::scoped_lock lock(m_mutex);
        return m_profile;
    }

    void profileChanged(const std::shared_ptr<const input::Profile>& profile) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            if ((!m_task && !m_follows) || profile == m_profile) {
                return;
            }
            m_profile = profile;
            m_controls = controlInfosOf(profile);
            m_states = stateInfosOf(profile);
            m_tuned.clear();
            m_status.clear();
            m_restore.clear();
        }
        tell("profile", {}, "{}");
    }

    static std::vector<script::ControlInfo> controlInfosOf(const std::shared_ptr<const input::Profile>& profile)
    {
        std::vector<script::ControlInfo> controls;
        if (profile != nullptr) {
            for (const input::Control& control : profile->controls) {
                controls.push_back({control.id, control.name,
                                    std::string(input::kControlTypeNames[control.kind.index()]), control.enabled,
                                    input::controlSettings(control)});
            }
        }
        return controls;
    }

    static std::vector<script::StateInfo> stateInfosOf(const std::shared_ptr<const input::Profile>& profile)
    {
        std::vector<script::StateInfo> states;
        if (profile != nullptr) {
            for (const input::Variable& variable : profile->vars) {
                states.push_back({variable.id, variable.name, scriptValue(variable.initial)});
            }
        }
        return states;
    }

    void resetActions()
    {
        const std::scoped_lock lock(m_mutex);
        m_actions.clear();
        m_warnedFast = false;
    }

    void synced(const LaneValues& values) override
    {
        const std::scoped_lock lock(m_mutex);
        for (const auto& [id, value] : values.states) {
            if (script::StateInfo* state = stateOf(id); state != nullptr && truthyOf(state->value) != value) {
                state->value = value;
            }
        }
        for (const auto& [id, on] : values.enabled) {
            if (script::ControlInfo* control = controlOf(id)) {
                control->enabled = on;
            }
        }
        if (!std::exchange(m_synced, true) && m_runtime != nullptr) {
            QMetaObject::invokeMethod(m_runtime, [runtime = m_runtime] { runtime->load(); }, Qt::QueuedConnection);
        }
    }

    void controlPressed(std::string_view id, bool down) override
    {
        if (!hears("control", id)) {
            return;
        }
        const std::scoped_lock lock(m_mutex);
        if (m_host != nullptr && m_script != 0) {
            m_host->controlEvent(std::string(id), down);
        }
    }

    void stateChanged(std::string_view id, bool value) override
    {
        const std::scoped_lock lock(m_mutex);
        script::StateInfo* state = stateOf(id);
        if (state == nullptr || truthyOf(state->value) == value) {
            return;
        }
        state->value = value;
        if (m_host != nullptr && m_script != 0 && hears("state", id)) {
            m_host->stateEvent(std::string(id), value);
        }
    }

    void controlEnabledChanged(std::string_view id, bool enabled) override
    {
        const std::scoped_lock lock(m_mutex);
        if (script::ControlInfo* control = controlOf(id)) {
            control->enabled = enabled;
        }
        if (m_host != nullptr && m_script != 0 && hears("enabled", id)) {
            m_host->event("enabled", std::string(id), enabled ? "true" : "false");
        }
    }

    void statusChanged(std::string_view id, const input::ControlStatus& status) override
    {
        const std::scoped_lock lock(m_mutex);
        m_status.insert_or_assign(std::string(id), script::ControlStatus{.touching = status.touching,
                                                                         .stick = {status.stick.x, status.stick.y},
                                                                         .aiming = status.aiming,
                                                                         .walking = status.walking,
                                                                         .cancelled = status.cancelled,
                                                                         .cell = status.cell});
        if (m_host != nullptr && m_script != 0 && hears("status", id)) {
            m_host->event("status", std::string(id), statusJson(status));
        }
    }

    void keyEvent(std::string_view name, bool down) override { tell("key", std::string(name), down ? "true" : "false"); }

    void lockChanged(bool locked) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_locked = locked;
        }
        tell("lock", {}, locked ? "true" : "false");
    }

    void tell(std::string_view kind, const std::string& id, const std::string& json)
    {
        if (!hears(kind, id)) {
            return;
        }
        const std::scoped_lock lock(m_mutex);
        if (m_host != nullptr && m_script != 0) {
            m_host->event(std::string(kind), id, json);
        }
    }

    void setFact(std::string_view name, std::string json, bool announce)
    {
        const std::scoped_lock lock(m_mutex);
        if (name == "keymap") {
            m_keymapFact = std::move(json);
            return;
        }
        if (name == "focused") {
            const bool changed = json != m_focusedFact;
            m_focusedFact = std::move(json);
            if (announce && changed && m_host != nullptr && m_script != 0) {
                m_host->event("app", m_focusedFact == "true" ? "focus" : "blur", "{}");
            }
            return;
        }
        if (name == "profile") {
            m_profileFact = std::move(json);
            return;
        }
        m_foregroundFact = json;
        if (announce && m_host != nullptr && m_script != 0 && hears("app", "foreground")) {
            m_host->event("app", "foreground", std::move(json));
        }
    }

    void releaseKeys()
    {
        bool keys = false;
        bool handed = false;
        {
            const std::scoped_lock lock(m_mutex);
            keys = !m_claimed.empty();
            handed = !m_handed.empty();
            m_claimed.clear();
            m_handed.clear();
            if (handed) {
                tellHanded();
            }
        }
        if (keys) {
            m_lane->claimKeys({});
        }
        if (handed) {
            m_lane->handOver({});
        }
    }

    [[nodiscard]] QStringList handed()
    {
        const std::scoped_lock lock(m_mutex);
        return handedList();
    }

    void setConnected(bool on)
    {
        std::vector<std::function<void(InputHost&, Microseconds)>> held;
        std::vector<std::uint64_t> waits;
        script::LogicHost* host = nullptr;
        {
            const std::scoped_lock lock(m_mutex);
            if (on == m_connected) {
                return;
            }
            m_connected = on;
            if (on) {
                held.swap(m_held);
                waits.swap(m_connectionWaits);
            }
            host = m_script != 0 ? m_host : nullptr;
        }
        for (std::function<void(InputHost&, Microseconds)>& task : held) {
            m_lane->post(std::move(task));
        }
        tell("app", on ? "reconnect" : "disconnect", "{}");
        if (host != nullptr) {
            for (const std::uint64_t request : waits) {
                host->answer(request, "null");
            }
        }
    }

    [[nodiscard]] bool connected() override
    {
        const std::scoped_lock lock(m_mutex);
        return m_connected;
    }

    void awaitConnection(std::uint64_t request) override
    {
        script::LogicHost* host = nullptr;
        {
            const std::scoped_lock lock(m_mutex);
            if (!m_connected) {
                m_connectionWaits.push_back(request);
                return;
            }
            host = m_host;
        }
        if (host != nullptr) {
            host->answer(request, "null");
        }
    }

    script::DeviceInfo device() override
    {
        const std::scoped_lock lock(m_mutex);
        return m_device;
    }

    void tap(std::uint64_t gesture, script::Point at, double durationMs) override
    {
        if (!allowed("tap")) {
            return;
        }
        count();
        noteDebug({{QStringLiteral("kind"), QStringLiteral("tap")}, {QStringLiteral("x"), at.x}, {QStringLiteral("y"), at.y}});
        post([channel = m_channel, gesture = gesture + m_gestureOffset, at,
              duration = micros(durationMs)](InputHost& host, Microseconds now) {
            host.tap(channel, gesture, {at.x, at.y}, duration, now);
        });
    }

    void swipe(std::uint64_t gesture, script::Point from, script::Point to, double durationMs) override
    {
        if (!allowed("swipe")) {
            return;
        }
        count();
        noteDebug({{QStringLiteral("kind"), QStringLiteral("swipe")}, {QStringLiteral("x"), from.x}, {QStringLiteral("y"), from.y},
                   {QStringLiteral("x2"), to.x}, {QStringLiteral("y2"), to.y}});
        post([channel = m_channel, gesture = gesture + m_gestureOffset, from, to,
              duration = micros(durationMs)](InputHost& host, Microseconds now) {
            host.swipe(channel, gesture, {from.x, from.y}, {to.x, to.y}, duration, now);
        });
    }

    void lift(std::uint64_t gesture) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_holding.erase(gesture + m_gestureOffset);
        }
        post([channel = m_channel, gesture = gesture + m_gestureOffset](InputHost& host, Microseconds now) {
            host.liftGesture(channel, gesture, now);
        });
    }

    [[nodiscard]] QVariantMap snapshot()
    {
        const std::scoped_lock lock(m_mutex);
        QVariantList components;
        for (const script::ControlInfo& control : m_controls) {
            const auto found = m_status.find(control.id);
            const script::ControlStatus status = found != m_status.end() ? found->second : script::ControlStatus{};
            const bool moving = status.stick.x != 0.0 || status.stick.y != 0.0;
            QVariantMap item{{QStringLiteral("id"), QString::fromStdString(control.id)},
                             {QStringLiteral("name"), QString::fromStdString(control.name.empty() ? control.id : control.name)},
                             {QStringLiteral("type"), QString::fromStdString(control.type)},
                             {QStringLiteral("enabled"), control.enabled},
                             {QStringLiteral("touching"), status.touching},
                             {QStringLiteral("aiming"), status.aiming},
                             {QStringLiteral("walking"), status.walking},
                             {QStringLiteral("moving"), moving}};
            if (moving) {
                const double degrees = std::atan2(status.stick.x, -status.stick.y) * 180.0 / std::numbers::pi;
                item.insert(QStringLiteral("direction"), static_cast<int>(std::lround(std::fmod(degrees + 360.0, 360.0))));
            }
            if (status.cell >= 0) {
                item.insert(QStringLiteral("cell"), status.cell);
            }
            components.append(item);
        }
        QVariantList vars;
        for (const script::StateInfo& state : m_states) {
            QVariant value;
            if (const bool* on = std::get_if<bool>(&state.value)) {
                value = *on;
            } else if (const double* number = std::get_if<double>(&state.value)) {
                value = *number;
            } else {
                value = QString::fromStdString(std::get<std::string>(state.value));
            }
            vars.append(QVariantMap{{QStringLiteral("id"), QString::fromStdString(state.id)},
                                    {QStringLiteral("name"), QString::fromStdString(state.name.empty() ? state.id : state.name)},
                                    {QStringLiteral("value"), value}});
        }
        return {{QStringLiteral("components"), components}, {QStringLiteral("vars"), vars}};
    }

    void liftHeld()
    {
        std::set<std::uint64_t> holding;
        {
            const std::scoped_lock lock(m_mutex);
            holding.swap(m_holding);
        }
        for (const std::uint64_t gesture : holding) {
            post([channel = m_channel, gesture](InputHost& host, Microseconds now) {
                host.liftGesture(channel, gesture, now);
            });
        }
    }

    void key(std::string_view name, bool down) override
    {
        const std::optional<std::uint32_t> keycode = script::androidKeycode(name);
        if (!keycode || !allowed("key")) {
            return;
        }
        if (down) {
            count();
        }
        post([channel = m_channel, code = *keycode, down](InputHost& host, Microseconds) {
            host.androidKey(channel, code, down);
        });
    }

    std::vector<script::ControlInfo> controls() override
    {
        if (!allowed("controls")) {
            return {};
        }
        const std::scoped_lock lock(m_mutex);
        std::vector<script::ControlInfo> list = m_controls;
        if (m_profile == nullptr || m_device.width <= 0 || m_device.height <= 0) {
            return list;
        }
        const input::CoordinateMapper mapper{input::ScreenGeometry{.width = static_cast<double>(m_device.width),
                                                                   .height = static_cast<double>(m_device.height)},
                                             m_profile->screen.scaleMode, m_profile->screen.safeArea};
        for (std::size_t i = 0; i < list.size() && i < m_profile->controls.size(); ++i) {
            const input::Control& control = m_profile->controls[i];
            const auto tuned = m_tuned.find(control.id);
            list[i].props = placedSettings(list[i].props, tuned != m_tuned.end() ? tuned->second : control, mapper);
        }
        return list;
    }

    std::optional<std::string> tuneControl(std::string_view id, std::string_view change) override
    {
        if (!allowed("tuneControl")) {
            return std::nullopt;
        }
        input::Control tuned;
        std::string settings;
        std::shared_ptr<const input::Profile> basis;
        {
            const std::scoped_lock lock(m_mutex);
            if (m_profile == nullptr) {
                return std::nullopt;
            }
            basis = m_profile;
            const auto base = std::ranges::find(m_profile->controls, id, &input::Control::id);
            if (base == m_profile->controls.end()) {
                return std::nullopt;
            }
            const auto found = m_tuned.find(id);
            tuned = found != m_tuned.end() ? found->second : *base;
            const std::optional<input::CoordinateMapper> mapper = mapperLocked();
            applyTuning(tuned, *base,
                        QJsonDocument::fromJson(QByteArray(change.data(), static_cast<qsizetype>(change.size()))).object(),
                        mapper ? &*mapper : nullptr);
            if (tuned == *base) {
                if (found != m_tuned.end()) {
                    m_tuned.erase(found);
                }
            } else {
                m_tuned.insert_or_assign(std::string(id), tuned);
            }
            settings = input::controlSettings(tuned);
            if (script::ControlInfo* control = controlOf(id)) {
                control->props = settings;
            }
            if (mapper) {
                settings = placedSettings(settings, tuned, *mapper);
            }
        }
        m_lane->post([channel = m_channel, profile = std::move(basis), tuned](InputHost& host, Microseconds now) {
            host.retuneControl(channel, profile.get(), tuned, now);
        });
        return settings;
    }

    void saveProfile(std::uint64_t request) override
    {
        if (!allowed("saveProfile")) {
            refuseDenied(request, "saveProfile");
            return;
        }
        script::LogicHost* host = nullptr;
        {
            const std::scoped_lock lock(m_mutex);
            host = m_host;
            if (m_profile != nullptr && !m_tuned.empty() && m_runtime != nullptr) {
                const std::optional<input::CoordinateMapper> mapper = mapperLocked();
                auto saved = std::make_shared<input::Profile>(*m_profile);
                QVariantList changes;
                for (input::Control& control : saved->controls) {
                    if (const auto found = m_tuned.find(control.id); found != m_tuned.end()) {
                        changes.append(tuningChanges(control, found->second, mapper ? &*mapper : nullptr));
                        control = found->second;
                    }
                }
                QMetaObject::invokeMethod(
                    m_runtime,
                    [runtime = m_runtime, request, changes, profile = std::shared_ptr<const input::Profile>(saved)] {
                        emit runtime->saveWanted(request, changes, profile);
                    },
                    Qt::QueuedConnection);
                return;
            }
        }
        if (host != nullptr) {
            host->answer(request, "false");
        }
    }

    bool pressControl(std::string_view id, bool down) override
    {
        if (!allowed("pressControl")) {
            return false;
        }
        {
            const std::scoped_lock lock(m_mutex);
            if (controlOf(id) == nullptr) {
                return false;
            }
        }
        if (down) {
            count();
        }
        post([channel = m_channel, id = std::string(id), down](InputHost& host, Microseconds now) {
            host.pressControl(channel, id, down, now);
        });
        return true;
    }

    bool enableControl(std::string_view id, bool enabled) override
    {
        if (!allowed("enableControl")) {
            return false;
        }
        {
            const std::scoped_lock lock(m_mutex);
            script::ControlInfo* control = controlOf(id);
            if (control == nullptr) {
                return false;
            }
            if (!m_restore.contains(id)) {
                m_restore.emplace(std::string(id), control->enabled);
            }
            control->enabled = enabled;
        }
        m_lane->post([channel = m_channel, id = std::string(id), enabled](InputHost& host, Microseconds now) {
            host.enableControl(channel, id, enabled, now);
        });
        return true;
    }

    std::vector<script::StateInfo> states() override
    {
        if (!allowed("states")) {
            return {};
        }
        const std::scoped_lock lock(m_mutex);
        return m_states;
    }

    std::optional<script::VarValue> stateValue(std::string_view id) override
    {
        if (!allowed("stateValue")) {
            return std::nullopt;
        }
        const std::scoped_lock lock(m_mutex);
        const script::StateInfo* state = stateOf(id);
        return state != nullptr ? std::optional<script::VarValue>(state->value) : std::nullopt;
    }

    bool setState(std::string_view id, const script::VarValue& value) override
    {
        if (!allowed("setState")) {
            return false;
        }
        {
            const std::scoped_lock lock(m_mutex);
            script::StateInfo* state = stateOf(id);
            if (state == nullptr) {
                return false;
            }
            const bool changed = state->value != value;
            state->value = value;
            if (changed && m_host != nullptr && m_script != 0) {
                m_host->event("state", std::string(id), script::valueJson(value));
            }
        }
        m_lane->post([channel = m_channel, id = std::string(id), on = truthyOf(value)](InputHost& host, Microseconds now) {
            host.setState(channel, id, on, now);
        });
        return true;
    }

    bool controlAction(std::string_view id, std::string_view action, script::Point at) override
    {
        if (!allowed("controlAction")) {
            return false;
        }
        using Run = std::function<bool(input::MappingEngine&, Microseconds)>;
        const std::string name(id);
        const input::Vec2 value{at.x, at.y};
        Run run;
        if (action == "lock" || action == "unlock" || action == "toggleLock") {
            const input::Switch change = action == "lock"     ? input::Switch::On
                                         : action == "unlock" ? input::Switch::Off
                                                              : input::Switch::Toggle;
            run = [change](input::MappingEngine& engine, Microseconds now) {
                engine.setCameraLock(change, now);
                return true;
            };
        } else {
            {
                const std::scoped_lock lock(m_mutex);
                if (controlOf(id) == nullptr) {
                    return false;
                }
            }
            if (action == "reset") {
                run = [name](input::MappingEngine& engine, Microseconds now) { return engine.resetControl(name, now); };
            } else if (action == "push") {
                run = [name, value](input::MappingEngine& engine, Microseconds now) {
                    return engine.pushControl(name, value, now);
                };
            } else if (action == "turn") {
                run = [name, value](input::MappingEngine& engine, Microseconds now) {
                    return engine.turnControl(name, value, now);
                };
            } else if (action == "walkTo") {
                run = [name, value](input::MappingEngine& engine, Microseconds now) {
                    return engine.walkControl(name, value, now);
                };
            } else if (action.starts_with("down:") || action.starts_with("up:")) {
                const bool down = action.starts_with("down:");
                const std::optional<input::Slot> slot = input::slotNamed(action.substr(down ? 5 : 3));
                if (!slot) {
                    return false;
                }
                run = [name, slot = *slot, down](input::MappingEngine& engine, Microseconds now) {
                    return engine.pressSlot(name, slot, down, now);
                };
            } else {
                return false;
            }
        }
        count();
        post([channel = m_channel, run = std::move(run)](InputHost& host, Microseconds now) {
            host.withEngine(channel, [&run, now](input::MappingEngine& engine) { return run(engine, now); }, now);
        });
        return true;
    }

    std::optional<script::ControlStatus> controlStatus(std::string_view id) override
    {
        if (!allowed("controlStatus")) {
            return std::nullopt;
        }
        const std::scoped_lock lock(m_mutex);
        if (controlOf(id) == nullptr) {
            return std::nullopt;
        }
        const auto found = m_status.find(id);
        return found != m_status.end() ? found->second : script::ControlStatus{};
    }

    bool cameraLocked() override
    {
        if (!allowed("cameraLocked")) {
            return false;
        }
        const std::scoped_lock lock(m_mutex);
        return m_locked;
    }

    bool claimKey(std::string_view name) override
    {
        const std::optional<input::InputSource> source = input::parseSource(name);
        if (!source || !allowed("claimKey")) {
            return false;
        }
        std::vector<input::InputSource> claimed;
        {
            const std::scoped_lock lock(m_mutex);
            if (std::ranges::find(m_claimed, *source) != m_claimed.end()) {
                return true;
            }
            m_claimed.push_back(*source);
            claimed = m_claimed;
        }
        m_lane->claimKeys(std::move(claimed));
        return true;
    }

    void handOver(std::string_view id, bool on) override
    {
        if (m_task || m_guarded) {
            return;
        }
        std::vector<std::string> handed;
        {
            const std::scoped_lock lock(m_mutex);
            const auto found = std::ranges::find(m_handed, id);
            if (on == (found != m_handed.end())) {
                return;
            }
            if (on) {
                m_handed.emplace_back(id);
            } else {
                m_handed.erase(found);
            }
            handed = m_handed;
            tellHanded();
        }
        m_lane->handOver(std::move(handed));
    }

    [[nodiscard]] QStringList handedList() const
    {
        QStringList controls;
        for (const std::string& each : m_handed) {
            controls.append(QString::fromStdString(each));
        }
        return controls;
    }

    void tellHanded() const
    {
        if (m_runtime != nullptr) {
            QMetaObject::invokeMethod(
                m_runtime, [runtime = m_runtime, controls = handedList()] { emit runtime->handedOverChanged(controls); },
                Qt::QueuedConnection);
        }
    }

    void type(std::string_view text) override
    {
        if (!allowed("type")) {
            return;
        }
        count();
        post([channel = m_channel, text = std::string(text)](InputHost& host, Microseconds) {
            host.typeText(channel, text);
        });
    }

    void hold(std::uint64_t gesture, script::Point at) override
    {
        if (!allowed("hold")) {
            return;
        }
        count();
        noteDebug({{QStringLiteral("kind"), QStringLiteral("hold")}, {QStringLiteral("x"), at.x}, {QStringLiteral("y"), at.y}});
        {
            const std::scoped_lock lock(m_mutex);
            m_holding.insert(gesture + m_gestureOffset);
        }
        post([channel = m_channel, gesture = gesture + m_gestureOffset, at](InputHost& host, Microseconds now) {
            host.holdGesture(channel, gesture, {at.x, at.y}, now);
        });
    }

    void move(std::uint64_t gesture, script::Point to, double durationMs) override
    {
        if (!allowed("move")) {
            return;
        }
        post([channel = m_channel, gesture = gesture + m_gestureOffset, to,
              duration = micros(durationMs)](InputHost& host, Microseconds now) {
            host.moveGesture(channel, gesture, {to.x, to.y}, duration, now);
        });
    }

    void liftAll() override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_holding.clear();
        }
        post([channel = m_channel](InputHost& host, Microseconds now) { host.liftAll(channel, now); });
    }

    std::optional<std::string> globalValue(std::string_view name) override
    {
        if (!allowed("globalValue")) {
            return std::nullopt;
        }
        return GlobalBoard::instance().get(m_deviceKey, std::string(name));
    }

    void setGlobal(std::string_view name, std::string_view json) override
    {
        if (!allowed("setGlobal")) {
            return;
        }
        GlobalBoard::instance().set(m_deviceKey, std::string(name), std::string(json));
    }

    std::string app(std::string_view request, std::string_view json) override
    {
        if (!allowedApp(request)) {
            return request == "notify" ? "false" : "null";
        }
        const std::scoped_lock lock(m_mutex);
        if (request == "keymap" && json == "null") {
            return m_keymapFact;
        }
        if (request == "foreground") {
            return m_foregroundFact;
        }
        if (request == "focused") {
            return m_focusedFact;
        }
        if (request == "profile" && json == "null") {
            return m_profileFact;
        }
        if (request == "taskParams") {
            return m_taskParams;
        }
        if (m_runtime == nullptr) {
            return "false";
        }
        if (request == "notify") {
            const auto now = std::chrono::steady_clock::now();
            if (m_lastNotice && now - *m_lastNotice < std::chrono::seconds(10)) {
                return "false";
            }
            m_lastNotice = now;
        }
        if (request == "taskProgress") {
            const QByteArray wrapped = "[" + QByteArray(json.data(), static_cast<qsizetype>(json.size())) + "]";
            const QString text = QJsonDocument::fromJson(wrapped).array().at(0).toString();
            QMetaObject::invokeMethod(
                m_runtime, [runtime = m_runtime, text] { emit runtime->progressed(text); }, Qt::QueuedConnection);
            return "true";
        }
        if (request == "hints") {
            const QByteArray asked(json.data(), static_cast<qsizetype>(json.size()));
            const QJsonValue reveal = QJsonDocument::fromJson(asked).object().value(QStringLiteral("reveal"));
            if (reveal.isBool()) {
                m_revealed = reveal.toBool();
            }
        }
        QMetaObject::invokeMethod(
            m_runtime,
            [runtime = m_runtime, kind = QString::fromUtf8(request.data(), static_cast<qsizetype>(request.size())),
             value = QString::fromUtf8(json.data(), static_cast<qsizetype>(json.size()))] {
                emit runtime->appRequested(kind, value);
            },
            Qt::QueuedConnection);
        return "true";
    }

    std::optional<std::string> loadStore(std::string_view) override
    {
        std::filesystem::path path;
        {
            const std::scoped_lock lock(m_mutex);
            if (m_store.empty() || m_writer == nullptr) {
                return std::nullopt;
            }
            path = m_store;
            if (std::optional<std::string> latest = m_writer->latest(path)) {
                return latest;
            }
        }
        std::string contents;
        const std::error_code error =
            platform::readFile(path, kMostStoreBytes, contents, platform::WhenBusy::GiveUp);
        if (!error && readable(contents)) {
            keepUntouched(path, false);
            return contents;
        }
        if (platform::fileMissing(error)) {
            keepUntouched(path, false);
            return std::nullopt;
        }
        if (!error || error == std::errc::file_too_large) {
            setAside(path, error ? "is larger than 2 MiB" : "is not a JSON object");
            return std::nullopt;
        }
        qWarning("logic store %s cannot be read (error %d): starting empty and not saving over it",
                 qUtf8Printable(pathText(path)), error.value());
        keepUntouched(path, true);
        if (platform::fileBusy(error)) {
            reportBusy();
        }
        return std::nullopt;
    }

    void saveStore(std::string_view, std::string_view contents) override
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_store.empty() && m_writer != nullptr && m_store != m_untouched) {
            m_written.insert(m_store);
            m_writer->save(m_store, std::string(contents));
        }
    }

    void toast(std::string_view text) override
    {
        const std::scoped_lock lock(m_mutex);
        if (m_runtime != nullptr) {
            QMetaObject::invokeMethod(
                m_runtime,
                [runtime = m_runtime, message = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()))] {
                    emit runtime->toast(message);
                },
                Qt::QueuedConnection);
        }
    }

    void ask(std::uint64_t request, const script::Prompt& prompt) override
    {
        static constexpr const char* kKinds[] = {"confirm", "choose", "ask"};
        QStringList choices;
        for (const std::string& choice : prompt.choices) {
            choices.append(QString::fromStdString(choice));
        }
        const QVariantMap shown{{QStringLiteral("kind"), QString::fromLatin1(kKinds[static_cast<std::size_t>(prompt.kind)])},
                                {QStringLiteral("question"), QString::fromStdString(prompt.question)},
                                {QStringLiteral("choices"), choices},
                                {QStringLiteral("initial"), QString::fromStdString(prompt.initial)},
                                {QStringLiteral("ok"), QString::fromStdString(prompt.ok)},
                                {QStringLiteral("cancel"), QString::fromStdString(prompt.cancel)}};
        const std::scoped_lock lock(m_mutex);
        if (m_runtime != nullptr) {
            QMetaObject::invokeMethod(
                m_runtime, [runtime = m_runtime, request, shown] { emit runtime->prompt(request, shown); },
                Qt::QueuedConnection);
        }
    }

    void withdraw(std::uint64_t request) override
    {
        const std::scoped_lock lock(m_mutex);
        if (m_runtime != nullptr) {
            QMetaObject::invokeMethod(
                m_runtime, [runtime = m_runtime, request] { emit runtime->promptWithdrawn(request); },
                Qt::QueuedConnection);
        }
    }

    std::optional<script::Point> cursor() override
    {
        if (!allowed("cursor")) {
            return std::nullopt;
        }
        const std::optional<input::Vec2> at = m_lane->cursor();
        return at ? std::optional<script::Point>(script::Point{at->x, at->y}) : std::nullopt;
    }

    void draw(std::uint64_t id, const script::Drawing& drawing) override
    {
        QVariantMap shown{
            {QStringLiteral("kind"), drawingKindName(drawing.kind)},
            {QStringLiteral("x"), drawing.at.x},
            {QStringLiteral("y"), drawing.at.y},
            {QStringLiteral("width"), drawing.width},
            {QStringLiteral("height"), drawing.height},
            {QStringLiteral("text"), QString::fromStdString(drawing.text)},
            {QStringLiteral("color"), QString::fromStdString(drawing.color)}};
        const std::scoped_lock lock(m_mutex);
        if (m_runtime == nullptr) {
            return;
        }
        QByteArray png;
        QString file;
        if (drawing.kind == script::DrawingKind::Image) {
            if (m_seer == nullptr) {
                return;
            }
            if (drawing.capture == 0) {
                file = QUrl::fromLocalFile(pathText(m_seer->folder() / QString::fromStdString(drawing.image).toStdWString()))
                           .toString();
            } else if (const std::optional<Seer::Captured> kept = m_seer->captured(drawing.capture); kept) {
                png = kept->png.isEmpty() ? Seer::pngOf(kept->picture) : kept->png;
                if (png.isEmpty()) {
                    return;
                }
            } else {
                return;
            }
        }
        QMetaObject::invokeMethod(
            m_runtime,
            [runtime = m_runtime, id, shown, png, file]() mutable {
                if (!png.isEmpty()) {
                    shown.insert(QStringLiteral("source"),
                                 QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64()));
                } else if (!file.isEmpty()) {
                    shown.insert(QStringLiteral("source"), file);
                }
                emit runtime->drawn(id, shown);
            },
            Qt::QueuedConnection);
    }

    void erase(std::uint64_t id) override
    {
        const std::scoped_lock lock(m_mutex);
        if (m_runtime != nullptr) {
            QMetaObject::invokeMethod(
                m_runtime, [runtime = m_runtime, id] { emit runtime->erased(id); }, Qt::QueuedConnection);
        }
    }

    bool keyDown(std::string_view name) override { return allowed("keyDown") && m_lane->held(name); }

    bool readsInput() override
    {
        if (m_guarded || m_grants == nullptr || m_readAllowed.load() || m_grants->allows(m_inputGrant)) {
            return true;
        }
        if (!m_readAsked.exchange(true)) {
            const std::scoped_lock lock(m_mutex);
            if (m_runtime != nullptr) {
                QMetaObject::invokeMethod(m_runtime, [runtime = m_runtime] { emit runtime->inputWanted(); },
                                          Qt::QueuedConnection);
            }
        }
        return false;
    }

    void allowReading() { m_readAllowed.store(true); }

    void askAgain() { m_readAsked.store(false); }

    void find(std::uint64_t request, const script::FindQuery& query) override
    {
        if (!allowed("find")) {
            refuseDenied(request, "find");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        if (m_seer != nullptr) {
            m_seer->find(request, query);
        }
    }

    void findColor(std::uint64_t request, const script::ColorQuery& query) override
    {
        if (!allowed("findColor")) {
            refuseDenied(request, "findColor");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        if (m_seer != nullptr) {
            m_seer->findColor(request, query);
        }
    }

    void pickColor(std::uint64_t request, const script::PixelQuery& query) override
    {
        if (!allowed("pickColor")) {
            refuseDenied(request, "pickColor");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        if (m_seer != nullptr) {
            m_seer->pickColor(request, query);
        }
    }

    void capture(std::uint64_t request, const script::CaptureQuery& query) override
    {
        if (!allowed("capture")) {
            refuseDenied(request, "capture");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        if (m_seer != nullptr) {
            m_seer->capture(request, query);
        }
    }

    void changed(std::uint64_t request, const script::ChangeQuery& query) override
    {
        if (!allowed("changed")) {
            refuseDenied(request, "changed");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        if (m_seer != nullptr) {
            m_seer->changed(request, query);
        }
    }

    void readText(std::uint64_t request, const script::TextQuery& query) override
    {
        if (!allowed("readText")) {
            refuseDenied(request, "readText");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        if (m_seer != nullptr) {
            m_seer->readText(request, query);
        }
    }

    void saveCapture(std::uint64_t request, std::uint64_t capture, std::string_view name) override
    {
        if (!allowed("saveCapture")) {
            refuseDenied(request, "saveCapture");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        if (m_seer != nullptr) {
            m_seer->save(request, capture, std::string(name));
        }
    }

    void cancelFind(std::uint64_t request) override
    {
        const std::scoped_lock lock(m_mutex);
        if (m_seer != nullptr) {
            m_seer->cancel(request);
        }
    }

    void readClipboard(std::uint64_t request) override
    {
        if (!allowed("readClipboard")) {
            refuseDenied(request, "readClipboard");
            return;
        }
        const std::scoped_lock lock(m_mutex);
        if (m_runtime != nullptr) {
            QMetaObject::invokeMethod(
                m_runtime, [runtime = m_runtime, request] { emit runtime->clipboardWanted(request); },
                Qt::QueuedConnection);
        }
    }

    void writeClipboard(std::uint64_t request, std::string_view text) override
    {
        if (!allowed("writeClipboard")) {
            refuseDenied(request, "writeClipboard");
            return;
        }
        count();
        const std::scoped_lock lock(m_mutex);
        if (m_runtime != nullptr) {
            QMetaObject::invokeMethod(
                m_runtime,
                [runtime = m_runtime, request,
                 given = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()))] {
                    emit runtime->clipboardGiven(request, given);
                },
                Qt::QueuedConnection);
        }
    }

    std::vector<script::NodeInfo> nodes() override
    {
        const NativeExtensions* native = NativeExtensions::instance();
        return native != nullptr ? native->nodes() : std::vector<script::NodeInfo>{};
    }

    script::NodeAnswer callNode(std::string_view extension, std::string_view node,
                                const std::vector<script::NodeValue>& arguments) override
    {
        noteDebug({{QStringLiteral("kind"), QStringLiteral("node")},
                   {QStringLiteral("node"), QString::fromStdString(script::nodePath(extension, node))}});
        const NativeExtensions* native = NativeExtensions::instance();
        if (native == nullptr) {
            return {{}, "GS-API-060", "no extension offers this node now"};
        }
        if (m_guarded) {
            for (const script::NodeInfo& info : native->nodes()) {
                if (info.extension != extension || info.name != node) {
                    continue;
                }
                for (const std::string& permission : info.permissions) {
                    if (!script::permits(m_permissions, permission)) {
                        const std::string problem = script::permissionProblem(permission);
                        return {{}, "GS-API-072", problem.substr(problem.find(' ') + 1)};
                    }
                }
            }
        }
        return native->call(extension, node, arguments);
    }

    bool offersService(std::string_view service) override { return LogicServices::instance().offers(service); }

    script::ServiceAnswer callService(std::string_view service, std::string_view json) override
    {
        noteDebug({{QStringLiteral("kind"), QStringLiteral("service")},
                   {QStringLiteral("service"), QString::fromUtf8(service.data(), static_cast<qsizetype>(service.size()))}});
        return LogicServices::instance().call(m_caller, service, json);
    }

    void requestService(std::uint64_t id, std::string_view service, std::string_view json) override
    {
        const std::scoped_lock lock(m_mutex);
        if (m_runtime == nullptr) {
            return;
        }
        m_services.insert_or_assign(id, std::string(service));
        QMetaObject::invokeMethod(
            m_runtime,
            [runtime = m_runtime, id, name = std::string(service),
             given = QString::fromUtf8(json.data(), static_cast<qsizetype>(json.size()))] {
                runtime->serve(id, name, given);
            },
            Qt::QueuedConnection);
    }

    void dropService(std::uint64_t id) override
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_services.find(id);
        if (found == m_services.end()) {
            return;
        }
        std::string name = std::move(found->second);
        m_services.erase(found);
        if (m_runtime != nullptr) {
            QMetaObject::invokeMethod(
                m_runtime, [runtime = m_runtime, id, name = std::move(name)] { runtime->unserve(id, name); },
                Qt::QueuedConnection);
        }
    }

    void settled(std::uint64_t id)
    {
        const std::scoped_lock lock(m_mutex);
        m_services.erase(id);
    }

private:
    [[nodiscard]] bool allowed(std::string_view method) const
    {
        return !m_guarded || script::permits(m_permissions, script::serviceRule(method).permission);
    }

    [[nodiscard]] bool allowedApp(std::string_view request) const
    {
        return !m_guarded || script::permits(m_permissions, script::appRule(request).permission);
    }

    [[nodiscard]] bool hears(std::string_view kind, std::string_view id) const
    {
        return !m_guarded || script::permits(m_permissions, script::eventRule(kind, id).permission);
    }

    void refuseDenied(std::uint64_t request, std::string_view method)
    {
        script::LogicHost* host = nullptr;
        {
            const std::scoped_lock lock(m_mutex);
            host = m_host;
        }
        if (host != nullptr) {
            host->refuse(request, script::permissionProblem(script::serviceRule(method).permission));
        }
    }

    static bool readable(const std::string& contents)
    {
        QJsonParseError problem;
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(contents), &problem);
        return problem.error == QJsonParseError::NoError && document.isObject();
    }

    void keepUntouched(const std::filesystem::path& path, bool untouched)
    {
        const std::scoped_lock lock(m_mutex);
        if (untouched) {
            m_untouched = path;
            return;
        }
        if (m_untouched == path) {
            m_untouched.clear();
        }
        if (m_writer != nullptr) {
            m_writer->cancelAside(path);
        }
    }

    void setAside(const std::filesystem::path& path, const char* why)
    {
        std::filesystem::path aside = path;
        aside += L".bad";
        std::error_code error;
        std::filesystem::rename(path, aside, error);
        if (platform::fileBusy(error) && setAsideLater(path)) {
            qWarning("logic store %s %s: starting empty; another program is using the file, so .bad is added to its "
                     "name just before the first save",
                     qUtf8Printable(pathText(path)), why);
            return;
        }
        if (error) {
            qWarning("logic store %s %s: starting empty; the file stays where it is and is not saved over (error %d)",
                     qUtf8Printable(pathText(path)), why, error.value());
        } else {
            qWarning("logic store %s %s: starting empty; the file is kept with .bad added to its name",
                     qUtf8Printable(pathText(path)), why);
        }
        keepUntouched(path, static_cast<bool>(error));
    }

    bool setAsideLater(const std::filesystem::path& path)
    {
        const std::scoped_lock lock(m_mutex);
        if (m_writer == nullptr) {
            return false;
        }
        if (m_untouched == path) {
            m_untouched.clear();
        }
        m_writer->setAsideFirst(path);
        return true;
    }

    void reportBusy()
    {
        const std::scoped_lock lock(m_mutex);
        if (m_runtime != nullptr) {
            QMetaObject::invokeMethod(m_runtime, [runtime = m_runtime] { emit runtime->storeBusy(); },
                                      Qt::QueuedConnection);
        }
    }

    script::ControlInfo* controlOf(std::string_view id)
    {
        const auto found = std::ranges::find_if(m_controls, [&](const script::ControlInfo& c) { return c.id == id; });
        return found != m_controls.end() ? &*found : nullptr;
    }

    [[nodiscard]] std::optional<input::CoordinateMapper> mapperLocked() const
    {
        if (m_profile == nullptr || m_device.width <= 0 || m_device.height <= 0) {
            return std::nullopt;
        }
        return input::CoordinateMapper{input::ScreenGeometry{.width = static_cast<double>(m_device.width),
                                                             .height = static_cast<double>(m_device.height)},
                                       m_profile->screen.scaleMode, m_profile->screen.safeArea};
    }

    script::StateInfo* stateOf(std::string_view id)
    {
        const auto found = std::ranges::find_if(m_states, [&](const script::StateInfo& s) { return s.id == id; });
        return found != m_states.end() ? &*found : nullptr;
    }

    void count()
    {
        const auto now = std::chrono::steady_clock::now();
        const std::scoped_lock lock(m_mutex);
        if (m_warnedFast) {
            return;
        }
        m_actions.push_back(now);
        while (!m_actions.empty() && now - m_actions.front() >= std::chrono::seconds(1)) {
            m_actions.pop_front();
        }
        if (m_actions.size() > kFastActions) {
            m_warnedFast = true;
            m_actions.clear();
            if (m_runtime != nullptr) {
                QMetaObject::invokeMethod(m_runtime, [runtime = m_runtime] { emit runtime->tooFast(); },
                                          Qt::QueuedConnection);
            }
        }
    }

    void post(std::function<void(InputHost&, Microseconds)> task)
    {
        {
            const std::scoped_lock lock(m_mutex);
            if (!m_connected) {
                m_held.push_back(std::move(task));
                return;
            }
        }
        m_lane->post(std::move(task));
    }

    std::mutex m_mutex;
    bool m_connected = true;
    std::vector<std::function<void(InputHost&, Microseconds)>> m_held;
    std::vector<std::uint64_t> m_connectionWaits;
    std::mutex m_debugMutex;
    std::atomic<bool> m_debugRecording{false};
    QVariantList m_debugEvents;
    ScriptRuntime* m_runtime = nullptr;
    std::shared_ptr<LaneAccess> m_lane;
    const ControlChannel* const m_channel;
    std::filesystem::path m_store;
    std::filesystem::path m_untouched;
    std::set<std::filesystem::path> m_written;
    StoreWriter* m_writer = nullptr;
    script::LogicHost* m_host = nullptr;
    Seer* m_seer = nullptr;
    script::ScriptId m_script = 0;
    bool m_synced = false;
    script::DeviceInfo m_device;
    bool m_deviceSet = false;
    std::vector<script::ControlInfo> m_controls;
    std::vector<script::StateInfo> m_states;
    std::map<std::string, bool, std::less<>> m_restore;
    std::map<std::string, input::Control, std::less<>> m_tuned;
    std::deque<std::chrono::steady_clock::time_point> m_actions;
    std::optional<std::chrono::steady_clock::time_point> m_lastNotice;
    bool m_revealed = false;
    bool m_warnedFast = false;
    const QString m_deviceKey;
    const bool m_task;
    const std::string m_taskParams;
    const std::uint64_t m_gestureOffset;
    std::shared_ptr<const input::Profile> m_profile;
    std::set<std::uint64_t> m_holding;
    std::uint64_t m_globals = 0;
    std::map<std::string, script::ControlStatus, std::less<>> m_status;
    bool m_locked = false;
    std::vector<input::InputSource> m_claimed;
    std::vector<std::string> m_handed;
    std::string m_keymapFact = "true";
    std::string m_foregroundFact = "null";
    std::string m_focusedFact = "false";
    std::string m_profileFact = "null";
    const LogicCaller m_caller;
    std::map<std::uint64_t, std::string> m_services;
    const bool m_guarded;
    const std::vector<std::string> m_permissions;
    const bool m_follows;
    const InputGrant m_inputGrant;
    InputReadGrants* const m_grants = InputReadGrants::instance();
    std::atomic<bool> m_readAllowed{false};
    std::atomic<bool> m_readAsked{false};
};

ScriptRuntime::ScriptRuntime(LogicSource source, std::shared_ptr<LaneAccess> lane, std::filesystem::path storeFolder,
                             QObject* parent)
    : QObject(parent)
    , m_source(std::move(source))
    , m_lane(std::move(lane))
    , m_storeFolder(std::move(storeFolder))
{
    m_caller = LogicCaller{
        .extension = m_source.extension, .script = m_source.id, .device = m_source.serial, .task = m_source.task};
    m_caller.logic = LogicServices::instance().join(this, m_caller);
    m_writer = std::make_unique<StoreWriter>([this] {
        QMetaObject::invokeMethod(this, [this] { emit storeBusy(); }, Qt::QueuedConnection);
    });
    m_link = std::make_shared<Link>(*this, m_source, m_lane, storePath(), m_writer.get());
    m_link->joinGlobals();
    script::LogicHost::Sink sink = [this](const script::Report& report) {
        QMetaObject::invokeMethod(this, [this, report] { handle(report); }, Qt::QueuedConnection);
    };
    if (m_source.extension.isEmpty()) {
        m_host = std::make_unique<script::ScriptHost>(std::move(sink), script::Limits{}, m_link);
    } else {
        m_sandboxed = true;
        m_host = std::make_unique<scripthost::RemoteLogicHost>(
            std::move(sink), script::Limits{}, m_link,
            scripthost::RemoteOptions{.label = QStringLiteral("%1/%2").arg(m_source.extension, m_source.id).toStdString()});
    }
    if (!m_host->started()) {
        qWarning("logic %s: %s", qUtf8Printable(m_source.name), m_host->problem().c_str());
    }
    m_seer = std::make_unique<Seer>(
        m_source.folder, m_source.snapshots,
        [host = m_host.get(), link = m_link](std::uint64_t request, std::string json) {
            link->noteDebug({{QStringLiteral("kind"), QStringLiteral("vision")},
                             {QStringLiteral("json"), QString::fromStdString(json)}});
            host->answer(request, std::move(json));
        },
        [host = m_host.get()](std::uint64_t request, std::string message) {
            host->refuse(request, std::move(message));
        });
    m_link->bind(m_host.get(), m_seer.get());
    m_settle.setInterval(kSettleMs);
    m_settle.setSingleShot(true); // source-rule-allow R9: a debounce; editors write a file in several steps
    connect(&m_settle, &QTimer::timeout, this, [this] {
        const auto before = m_watchedScripts;
        watch();
        if (m_watchedScripts == before) {
            return;
        }
        qInfo("logic %s: files changed, reloading", qUtf8Printable(m_source.name));
        reload();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_settle.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_settle.start(); });
    if (NativeExtensions* native = NativeExtensions::instance()) {
        connect(native, &NativeExtensions::changed, this, [this] { m_link->tell("nodes", {}, "{}"); });
    }
    m_lane->attach(m_source.profile, m_link);
    if (!m_source.task && !m_sandboxed) {
        watch();
    }
}

ScriptRuntime::~ScriptRuntime()
{
    LogicServices::instance().leave(m_caller.logic);
    m_link->close();
    m_seer.reset();
    m_host.reset();
    m_link->forgetWriter();
    m_link->releaseKeys();
    emit drawingsCleared();
    restore();
    m_link->liftHeld();
    m_lane->detach(m_link);
    if (m_script != 0) {
        qInfo("logic %s on %s: stopped", qUtf8Printable(m_source.name), qUtf8Printable(m_source.device));
    }
}

QVariantMap ScriptRuntime::snapshot() const
{
    return m_link->snapshot();
}

QStringList ScriptRuntime::handedOver() const
{
    return m_link->handed();
}

void ScriptRuntime::setDevice(const script::DeviceInfo& device)
{
    m_link->setDevice(device);
}

void ScriptRuntime::setKeymapOn(bool on)
{
    m_link->setFact("keymap", on ? "true" : "false", false);
}

void ScriptRuntime::setForeground(const QString& package, bool game)
{
    const std::string json =
        package.isEmpty()
            ? std::string("null")
            : QJsonDocument(QJsonObject{{QStringLiteral("package"), package}, {QStringLiteral("game"), game}})
                  .toJson(QJsonDocument::Compact)
                  .toStdString();
    m_link->setFact("foreground", json, true);
}

void ScriptRuntime::setFocused(bool focused)
{
    m_link->setFact("focused", focused ? "true" : "false", true);
}

void ScriptRuntime::setProfileFact(const QString& id, const QString& name)
{
    if (id.isEmpty() && name.isEmpty()) {
        m_link->setFact("profile", "null", false);
        return;
    }
    m_link->setFact("profile",
                    QJsonDocument(QJsonObject{{QStringLiteral("id"), id.isEmpty() ? name : id},
                                              {QStringLiteral("name"), name.isEmpty() ? id : name}})
                        .toJson(QJsonDocument::Compact)
                        .toStdString(),
                    false);
}

void ScriptRuntime::setConnected(bool connected)
{
    m_link->setConnected(connected);
}

void ScriptRuntime::setInputBox(const std::string& json)
{
    m_link->tell("app", "inputBox", json);
}

std::filesystem::path ScriptRuntime::storePath() const
{
    if (m_storeFolder.empty() || m_source.id.isEmpty()) {
        return {};
    }
    const QString key = m_source.deviceKey.isEmpty() ? QStringLiteral("phone") : m_source.deviceKey;
    if (!m_source.extension.isEmpty()) {
        const auto encoded = [](const QString& text) {
            return QString::fromLatin1(QUrl::toPercentEncoding(text)).toStdWString();
        };
        return m_storeFolder / L".extensions" / encoded(m_source.extension) / encoded(m_source.id) /
               (encoded(key) + L".json");
    }
    return m_storeFolder / m_source.id.toStdWString() /
           (QString::fromLatin1(QUrl::toPercentEncoding(key)) + QStringLiteral(".json")).toStdWString();
}

void ScriptRuntime::setDeviceKey(const QString& key)
{
    if (key == m_source.deviceKey) {
        return;
    }
    const std::filesystem::path before = storePath();
    m_source.deviceKey = key;
    const std::filesystem::path after = storePath();
    m_link->setStorePath(after);
    if (m_script == 0 || before.empty() || after.empty()) {
        return;
    }
    std::error_code error;
    if (!std::filesystem::exists(after, error) && m_link->wrote(before)) {
        std::optional<std::string> carried = m_writer->latest(before);
        if (!carried) {
            std::string contents;
            if (!platform::readFile(before, kMostStoreBytes, contents, platform::WhenBusy::GiveUp)) {
                carried = std::move(contents);
            }
        }
        if (carried) {
            m_writer->save(after, std::move(*carried));
        }
    }
    qInfo("logic %s on %s: the phone is now known by its own id, reloading with its store",
          qUtf8Printable(m_source.name), qUtf8Printable(m_source.device));
    reload();
}

void ScriptRuntime::setFrameRate(double hz)
{
    m_host->setFrameRate(hz);
}

void ScriptRuntime::answer(quint64 request, const QString& json)
{
    m_link->settled(request);
    m_host->answer(request, json.toStdString());
}

void ScriptRuntime::refuse(quint64 request, const QString& message)
{
    m_link->settled(request);
    m_host->refuse(request, message.toStdString());
}

void ScriptRuntime::tell(const QString& kind, const QString& id, const QString& json)
{
    m_link->tell(kind.toStdString(), id.toStdString(), json.toStdString());
}

std::uint64_t ScriptRuntime::keepFrame(const QImage& picture, script::PictureSize source, quint64 frames)
{
    return m_seer != nullptr ? m_seer->keepFrame(picture, source, frames) : 0;
}

void ScriptRuntime::forgetFrames(quint64 frames)
{
    if (m_seer != nullptr) {
        m_seer->forgetFrames(frames);
    }
}

void ScriptRuntime::allowInput()
{
    m_link->allowReading();
    if (InputReadGrants* grants = InputReadGrants::instance()) {
        grants->allow(m_source.inputGrant);
    }
}

void ScriptRuntime::serve(quint64 request, const std::string& service, const QString& json)
{
    if (!LogicServices::instance().request(m_caller, service, request, json)) {
        refuse(request, QStringLiteral("GS-API-070 no part of GameScrcpy offers the service %1")
                            .arg(QString::fromStdString(service)));
    }
}

void ScriptRuntime::unserve(quint64 request, const std::string& service)
{
    LogicServices::instance().drop(m_caller, service, request);
}

void ScriptRuntime::reload()
{
    if (m_script != 0) {
        m_host->unload(m_script);
        m_link->setScript(0);
        m_script = 0;
    }
    emit drawingsCleared();
    restore();
    m_link->resetActions();
    m_link->askAgain();
    m_link->releaseKeys();
    m_link->liftHeld();
    load();
}

void ScriptRuntime::setDebugging(bool on)
{
    if (on == m_debugging) {
        return;
    }
    m_debugging = on;
    m_link->setDebugRecording(on);
    reload();
}

void ScriptRuntime::setBreakpoints(const QStringList& places)
{
    m_breakpoints = places;
    std::vector<std::string> list;
    list.reserve(static_cast<std::size_t>(places.size()));
    for (const QString& place : places) {
        list.push_back(place.toStdString());
    }
    m_host->setBreakpoints(std::move(list));
}

void ScriptRuntime::resumeDebug(quint64 pause, int mode)
{
    m_host->resume(pause, static_cast<script::StepMode>(std::clamp(mode, 0, 3)));
}

void ScriptRuntime::debugReport(const script::Report& report)
{
    const QJsonDocument parsed = QJsonDocument::fromJson(QByteArray::fromStdString(report.message));
    if (report.kind == script::ReportKind::DebugHits) {
        QVariantList hits = parsed.array().toVariantList();
        hits.append(m_link->takeDebugEvents());
        emit debugHits(hits);
    } else if (report.kind == script::ReportKind::DebugPaused) {
        emit debugPaused(parsed.object().toVariantMap());
    } else {
        emit debugResumed(static_cast<quint64>(parsed.object().value(QStringLiteral("pause")).toDouble()));
    }
}

void ScriptRuntime::load()
{
    if (m_script != 0 || !m_link->ready()) {
        return;
    }
    script::ScriptSource source{.folder = m_source.folder, .entry = m_source.entry};
    if (m_source.profile != nullptr) {
        source.modules = script::controlModules(m_source.folder);
        std::erase_if(source.modules, [this](const std::string& module) {
            const std::string_view name = std::string_view(module).substr(std::string_view("controls/").size());
            const std::string_view id = name.substr(0, name.rfind('.'));
            return std::ranges::none_of(m_source.profile->controls,
                                        [id](const input::Control& control) { return control.id == id; });
        });
    }
    if (!m_source.extension.isEmpty()) {
        source.permissions = m_source.permissions;
        source.phone = m_lane->channel() != nullptr;
    }
    if (m_debugging) {
        source.transform = [folder = m_source.folder](const std::string& module, const std::string& code) {
            return instrumentModule(folder, module, code);
        };
    }
    if (m_source.profile != nullptr) {
        const QByteArray name = m_source.name.toUtf8();
        const QByteArray device = m_source.device.toUtf8();
        const ComponentLibrary* library = ComponentLibrary::instance();
        const auto listed = [&source](const std::string& ref) {
            return std::ranges::any_of(source.components,
                                       [&](const script::ComponentModule& each) { return each.ref == ref; });
        };
        for (const input::Control& control : m_source.profile->controls) {
            const std::string& ref = input::componentRef(control);
            if (ref.empty() || listed(ref)) {
                continue;
            }
            const ComponentEntry* entry = library != nullptr ? library->find(QString::fromStdString(ref)) : nullptr;
            if (entry == nullptr) {
                qWarning("logic %s on %s: the component %s used by %s is not installed", name.constData(),
                         device.constData(), ref.c_str(), control.id.c_str());
                continue;
            }
            if (!m_sandboxed && !entry->extensionId.isEmpty()) {
                qWarning("logic %s on %s: the component %s used by %s comes with an extension; copy it to your "
                         "components to use it",
                         name.constData(), device.constData(), ref.c_str(), control.id.c_str());
                continue;
            }
            script::ComponentModule placed{.ref = ref,
                                           .folder = library->folderOf(entry->ref),
                                           .entry = ComponentLibrary::entryName().toStdString()};
            for (const ComponentChild& child : entry->children) {
                placed.children.push_back(child.name.toStdString());
            }
            source.components.push_back(std::move(placed));
        }
        if (!source.components.empty()) {
            for (const ComponentEntry& entry : library->entries()) {
                const std::string ref = entry.ref.toStdString();
                if (!listed(ref) && (m_sandboxed || entry.extensionId.isEmpty())) {
                    source.components.push_back(script::ComponentModule{.ref = ref,
                                                                        .folder = library->folderOf(entry.ref),
                                                                        .entry = ComponentLibrary::entryName().toStdString(),
                                                                        .placed = false});
                }
            }
        }
        std::error_code missing;
        if (source.components.empty() && source.modules.empty() && input::usesComponents(*m_source.profile) &&
            !std::filesystem::exists(m_source.folder / m_source.entry, missing)) {
            return;
        }
    }
    m_script = m_host->load(source);
    m_link->setScript(m_script);
    if (m_debugging && !m_breakpoints.isEmpty()) {
        setBreakpoints(m_breakpoints);
    }
}

void ScriptRuntime::restore()
{
    const ControlChannel* channel = m_lane->channel();
    for (auto& [id, on] : m_link->takeRestores()) {
        m_lane->post([channel, id = std::move(id), on](InputHost& host, Microseconds now) {
            host.enableControl(channel, id, on, now);
        });
    }
    for (input::Control& control : m_link->takeTunings()) {
        m_lane->post([channel, profile = m_link->profile(), control = std::move(control)](InputHost& host,
                                                                                         Microseconds now) {
            host.retuneControl(channel, profile.get(), control, now);
        });
    }
    if (m_link->takeReveal()) {
        emit appRequested(QStringLiteral("hints"), QStringLiteral(R"({"reveal":false})"));
    }
}

void ScriptRuntime::watch()
{
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty()) {
        m_watcher.removePaths(watched);
    }
    QStringList paths{pathText(m_source.folder)};
    m_watchedScripts.clear();
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(m_source.folder,
                                                          std::filesystem::directory_options::skip_permission_denied,
                                                          error);
         !error && it != std::filesystem::recursive_directory_iterator() && paths.size() < kMostWatched;
         it.increment(error)) {
        std::error_code kind;
        if (it->is_directory(kind)) {
            if (skipped(it->path())) {
                it.disable_recursion_pending();
            } else {
                paths.append(pathText(it->path()));
            }
        } else if (scriptFile(it->path())) {
            paths.append(pathText(it->path()));
            std::error_code time;
            m_watchedScripts.emplace(it->path(), it->last_write_time(time));
        } else if (it->path().filename() == L"resources.json") {
            std::error_code time;
            m_watchedScripts.emplace(it->path(), it->last_write_time(time));
        }
    }
    m_watcher.addPaths(paths);
}

void ScriptRuntime::handle(const script::Report& report)
{
    if (report.kind == script::ReportKind::DebugHits || report.kind == script::ReportKind::DebugPaused ||
        report.kind == script::ReportKind::DebugResumed) {
        debugReport(report);
        return;
    }
    announce(report);
    const QByteArray name = m_source.name.toUtf8();
    const QByteArray device = m_source.device.toUtf8();
    const std::string text = script::describe(report);
    switch (report.kind) {
    case script::ReportKind::Log:
        if (report.level == log::Level::Error || report.level == log::Level::Warning) {
            qWarning("logic %s on %s: %s", name.constData(), device.constData(), text.c_str());
        } else {
            qInfo("logic %s on %s: %s", name.constData(), device.constData(), text.c_str());
        }
        return;
    case script::ReportKind::ScriptLoaded:
        qInfo("logic %s on %s: %s", name.constData(), device.constData(), text.c_str());
        emit loaded();
        if (m_source.task && m_task == 0) {
            m_task = m_host->start(m_script, m_source.taskHandler);
        }
        return;
    case script::ReportKind::TaskFinished:
    case script::ReportKind::TaskCancelled:
        if (m_source.task && report.task == m_task && !std::exchange(m_ended, true)) {
            emit ended(report.kind == script::ReportKind::TaskFinished ? QStringLiteral("done") : QStringLiteral("stopped"),
                       {});
        }
        return;
    case script::ReportKind::SliceSpike:
    case script::ReportKind::SustainedLoad:
    case script::ReportKind::MemoryWarning:
        qWarning("logic %s on %s: %s", name.constData(), device.constData(), text.c_str());
        return;
    case script::ReportKind::ScriptFailed:
    case script::ReportKind::TaskFailed:
    case script::ReportKind::TaskInterrupted:
    case script::ReportKind::UnhandledRejection:
    case script::ReportKind::MemoryLimit:
        qWarning("logic %s on %s: %s", name.constData(), device.constData(), text.c_str());
        if (!report.stack.empty()) {
            qWarning("%s", report.stack.c_str());
        }
        emit failed(problemOf(report));
        if (m_source.task && (report.kind == script::ReportKind::ScriptFailed || report.task == m_task ||
                              report.kind == script::ReportKind::MemoryLimit) &&
            !std::exchange(m_ended, true)) {
            emit ended(QStringLiteral("failed"), problemOf(report));
        }
        return;
    }
}

QString ScriptRuntime::problemOf(const script::Report& report) const
{
    if (report.kind == script::ReportKind::MemoryLimit) {
        return tr("Logic error: it used too much memory and was stopped");
    }
    if (m_sandboxed && report.kind == script::ReportKind::ScriptFailed && report.message == scripthost::kStoppedText) {
        return tr("The extension's logic stopped unexpectedly");
    }
    const bool tooLong = report.kind == script::ReportKind::TaskInterrupted ||
                         (report.kind == script::ReportKind::ScriptFailed && report.limit.count() > 0);
    QString message = tooLong ? tr("it ran too long without waiting and was stopped")
                              : QString::fromStdString(report.message).simplified();
    if (message.size() > kMostProblemChars) {
        message = message.left(kMostProblemChars - 1) + QChar(0x2026);
    }
    const QString file = QFileInfo(QString::fromStdString(report.location.module)).fileName();
    if (file.isEmpty() || report.location.line <= 0) {
        return tr("Logic error: %1").arg(message);
    }
    return tr("Logic error: %1 line %2: %3").arg(file, QString::number(report.location.line), message);
}

void ScriptRuntime::announce(const script::Report& report)
{
    if (report.kind == script::ReportKind::TaskFinished || report.kind == script::ReportKind::TaskCancelled) {
        return;
    }
    const bool stops = report.kind == script::ReportKind::ScriptFailed || report.kind == script::ReportKind::MemoryLimit;
    const bool failure = stops || report.kind == script::ReportKind::TaskFailed ||
                         report.kind == script::ReportKind::TaskInterrupted ||
                         report.kind == script::ReportKind::UnhandledRejection;
    if (report.kind == script::ReportKind::ScriptLoaded) {
        m_halted = false;
        m_lastError.clear();
    } else if (failure) {
        m_halted = m_halted || stops;
        m_lastError = problemOf(report);
    }
    QString text = QString::fromStdString(script::describe(report));
    const QString module = QString::fromStdString(report.location.module);
    if (!module.isEmpty() && report.kind != script::ReportKind::ScriptLoaded && text.startsWith(module)) {
        if (const qsizetype colon = text.indexOf(QStringLiteral(": "), module.size()); colon >= 0) {
            text.remove(0, colon + 2);
        }
    }
    emit reported(LogicReport{.kind = report.kind,
                              .level = failure ? log::Level::Error : report.level,
                              .text = text,
                              .file = module,
                              .line = report.location.line,
                              .column = report.location.column,
                              .folder = pathText(m_source.folder),
                              .time = QDateTime::currentDateTime()});
}

}
