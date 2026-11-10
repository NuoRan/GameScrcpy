#include "ExtensionLogic.h"

#include <gs/script/Report.h>
#include <gs/scripthost/RemoteLogicHost.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <utility>

namespace gs::app {

namespace {

constexpr int kGestureShift = 48;
constexpr std::uint64_t kGestureSlots = 255;
constexpr quint64 kShownIds = (quint64{1} << 32) - 1;
constexpr quint64 kFirstShown = (quint64{1} << 53) - (quint64{1} << 32);
constexpr std::size_t kMostStops = 3;
constexpr Microseconds kStopWindow = std::chrono::minutes{1};
constexpr auto kRestartDelay = std::chrono::milliseconds{1000};

bool listed(const std::vector<ExtensionScript>& scripts, const ExtensionScript& script)
{
    return std::ranges::find(scripts, script) != scripts.end();
}

QString pathText(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

bool sandboxStopped(const LogicReport& report)
{
    return report.kind == script::ReportKind::ScriptFailed &&
           report.text == QLatin1StringView(scripthost::kStoppedText.data(),
                                            static_cast<qsizetype>(scripthost::kStoppedText.size()));
}

QString tagged(const QString& extension, const QString& text)
{
    return text.isEmpty() ? extension : QStringLiteral("%1 \u00B7 %2").arg(extension, text);
}

QString titled(const QString& json, const QString& extension)
{
    const QJsonDocument parsed = QJsonDocument::fromJson(json.toUtf8());
    if (!parsed.isObject()) {
        return json;
    }
    QJsonObject shown = parsed.object();
    shown.insert(QStringLiteral("title"), tagged(extension, shown.value(QStringLiteral("title")).toString()));
    return QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Compact));
}

QString cancelled(const QString& kind)
{
    return kind == u"confirm" || kind == u"save" ? QStringLiteral("false") : QStringLiteral("null");
}

const Clock& clockOf(const Clock* clock)
{
    static const SteadyClock steady{};
    return clock != nullptr ? *clock : steady;
}

template <typename Entries>
std::vector<std::pair<QString, quint64>> takeRun(Entries& entries, quint64 run)
{
    std::vector<std::pair<QString, quint64>> taken;
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->second.run == run) {
            taken.emplace_back(it->second.serial, it->first);
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
    return taken;
}

}

ExtensionLogic::ExtensionLogic(std::filesystem::path storeFolder, QObject* parent)
    : QObject(parent)
    , m_storeFolder(std::move(storeFolder))
{
}

ExtensionLogic::~ExtensionLogic()
{
    endSessions();
    std::vector<Running> apps = std::exchange(m_apps, {});
    apps.clear();
}

void ExtensionLogic::setScripts(std::vector<ExtensionScript> scripts)
{
    if (scripts == m_scripts) {
        return;
    }
    m_scripts = std::move(scripts);
    const auto gone = [this](const QString& id) {
        return std::ranges::none_of(m_scripts, [&id](const ExtensionScript& script) { return script.extensionId == id; });
    };
    std::erase_if(m_stopped, gone);
    std::erase_if(m_crashes, [&gone](const auto& entry) { return gone(entry.first); });
    syncApps();
    for (auto& [serial, session] : m_sessions) {
        syncSession(session);
    }
}

void ExtensionLogic::startSession(Phone phone)
{
    if (phone.serial.isEmpty() || phone.channel == nullptr || !phone.lane || runs(phone.serial, phone.channel)) {
        return;
    }
    endSession(phone.serial);
    const QString serial = phone.serial;
    Session& session = m_sessions[serial];
    session.phone = std::move(phone);
    syncSession(session);
}

void ExtensionLogic::updateSession(const QString& serial, const script::DeviceInfo& device, const QString& deviceKey)
{
    const auto found = m_sessions.find(serial);
    if (found == m_sessions.end()) {
        return;
    }
    found->second.phone.device = device;
    found->second.phone.deviceKey = deviceKey;
    for (const Running& running : found->second.running) {
        running.runtime->setDevice(device);
        running.runtime->setDeviceKey(deviceKey);
    }
}

void ExtensionLogic::endSession(const QString& serial)
{
    cancelPromptsOn(serial);
    const auto found = m_sessions.find(serial);
    if (found == m_sessions.end()) {
        return;
    }
    Session ended = std::move(found->second);
    m_sessions.erase(found);
    ended.running.clear();
}

void ExtensionLogic::endSessions()
{
    std::set<QString> placed;
    for (const auto& [id, prompt] : m_prompts) {
        placed.insert(prompt.serial);
    }
    for (const QString& serial : placed) {
        cancelPromptsOn(serial);
    }
    std::map<QString, Session> ended = std::exchange(m_sessions, {});
    ended.clear();
}

bool ExtensionLogic::answerPrompt(quint64 id, const QString& json)
{
    const auto found = m_prompts.find(id);
    if (found == m_prompts.end()) {
        return false;
    }
    const PendingPrompt prompt = std::move(found->second);
    m_prompts.erase(found);
    if (prompt.save == nullptr) {
        if (prompt.runtime != nullptr) {
            prompt.runtime->answer(prompt.request, json);
        }
        return true;
    }
    const bool saved = json == u"true" && prompt.runtime != nullptr && m_saver && m_saver(prompt.serial, *prompt.save);
    if (prompt.runtime != nullptr) {
        prompt.runtime->answer(prompt.request, saved ? QStringLiteral("true") : QStringLiteral("false"));
    }
    return true;
}

bool ExtensionLogic::cancelPrompt(quint64 id)
{
    const auto found = m_prompts.find(id);
    if (found == m_prompts.end()) {
        return false;
    }
    const QString answer = cancelled(found->second.kind);
    return answerPrompt(id, answer);
}

bool ExtensionLogic::runs(const QString& serial, const ControlChannel* channel) const
{
    const auto found = m_sessions.find(serial);
    return found != m_sessions.end() && found->second.phone.channel == channel;
}

std::vector<ScriptRuntime*> ExtensionLogic::appRuntimes() const
{
    std::vector<ScriptRuntime*> runtimes;
    runtimes.reserve(m_apps.size());
    for (const Running& running : m_apps) {
        runtimes.push_back(running.runtime.get());
    }
    return runtimes;
}

std::vector<ScriptRuntime*> ExtensionLogic::sessionRuntimes(const QString& serial) const
{
    std::vector<ScriptRuntime*> runtimes;
    if (const auto found = m_sessions.find(serial); found != m_sessions.end()) {
        for (const Running& running : found->second.running) {
            runtimes.push_back(running.runtime.get());
        }
    }
    return runtimes;
}

void ExtensionLogic::syncApps()
{
    std::erase_if(m_apps, [this](const Running& running) { return !listed(m_scripts, running.script); });
    for (const ExtensionScript& script : m_scripts) {
        if (!script.app || m_stopped.contains(script.extensionId) ||
            std::ranges::any_of(m_apps, [&script](const Running& running) { return running.script == script; })) {
            continue;
        }
        m_apps.push_back(Running{.script = script, .runtime = startScript(script, nullptr)});
    }
}

void ExtensionLogic::syncSession(Session& session)
{
    std::erase_if(session.running, [this](const Running& running) { return !listed(m_scripts, running.script); });
    for (const ExtensionScript& script : m_scripts) {
        if (script.app || m_stopped.contains(script.extensionId) ||
            std::ranges::any_of(session.running, [&script](const Running& running) { return running.script == script; })) {
            continue;
        }
        session.running.push_back(Running{.script = script, .runtime = startScript(script, &session.phone)});
        emit sessionStarted(session.phone.serial, session.running.back().runtime.get());
    }
}

std::unique_ptr<ScriptRuntime> ExtensionLogic::startScript(const ExtensionScript& script, const Phone* phone)
{
    LogicSource source;
    source.folder = script.folder;
    source.entry = script.entry;
    source.id = script.name;
    source.name = QStringLiteral("%1 \u00B7 %2").arg(script.extensionName, script.name);
    source.extension = script.extensionId;
    source.permissions = script.permissions;
    std::shared_ptr<LaneAccess> lane;
    if (phone != nullptr) {
        source.profile = phone->profile;
        source.device = phone->name;
        source.deviceKey = phone->deviceKey;
        source.serial = phone->serial;
        source.snapshots = phone->snapshots;
        source.followsProfile = true;
        source.gestureOffset = nextGestureOffset();
        lane = phone->lane();
    } else {
        source.device = QStringLiteral("GameScrcpy");
        lane = std::make_shared<NoPhoneLane>();
    }
    const QString serial = phone != nullptr ? phone->serial : QString();
    const QString label = source.name;
    qInfo("extension script %s on %s: starting %s in the logic sandbox", qUtf8Printable(source.name),
          qUtf8Printable(source.device), qUtf8Printable(pathText(source.folder / source.entry)));
    auto runtime = std::make_unique<ScriptRuntime>(std::move(source), std::move(lane), m_storeFolder);
    ScriptRuntime* made = runtime.get();
    if (phone != nullptr) {
        made->setDevice(phone->device);
        made->setFrameRate(phone->frameRate > 0.0 ? phone->frameRate : 60.0);
    }
    const quint64 run = ++m_lastRun;
    const bool app = phone == nullptr;
    const QString extension = script.extensionName;
    const QString origin = QStringLiteral("ext:%1/%2").arg(script.extensionId, script.name);
    connect(made, &ScriptRuntime::toast, this, [this, serial](const QString& text) { emit toast(serial, text); });
    connect(made, &ScriptRuntime::appRequested, this,
            [this, serial, origin, extension](const QString& request, const QString& json) {
                if (request == u"progress" || request == u"notify") {
                    emit requested(serial, origin, request, titled(json, extension));
                } else if (request != u"machineState") {
                    emit requested(serial, origin, request, json);
                }
            });
    connect(made, &QObject::destroyed, this, [this, serial, origin, run] {
        dropRun(run);
        emit requested(serial, origin, QStringLiteral("progress"), QStringLiteral("null"));
    });
    connect(made, &ScriptRuntime::failed, this, [this, serial, label](const QString& message) {
        emit failed(serial, label + QStringLiteral(": ") + message);
    });
    connect(made, &ScriptRuntime::reported, this, [this, serial, app, script, made](const LogicReport& report) {
        emit reported(serial, report);
        if (app && sandboxStopped(report)) {
            appStopped(script, *made);
        }
    });
    wireAsking(*made, run, serial, extension);
    if (!app) {
        wireDrawing(*made, run, serial, extension);
    }
    return runtime;
}

void ExtensionLogic::wireAsking(ScriptRuntime& runtime, quint64 run, const QString& serial, const QString& extension)
{
    ScriptRuntime* made = &runtime;
    connect(made, &ScriptRuntime::prompt, this,
            [this, made, run, serial, extension](quint64 request, const QVariantMap& shown) {
                const QString target = serial.isEmpty() && m_front ? m_front() : serial;
                const quint64 id = nextShown();
                m_prompts.insert_or_assign(id, PendingPrompt{.run = run,
                                                             .serial = target,
                                                             .runtime = QPointer<ScriptRuntime>(made),
                                                             .request = request,
                                                             .kind = shown.value(QStringLiteral("kind")).toString()});
                if (target.isEmpty()) {
                    qInfo("extension script %s: no mirror window is open to ask in, so the question was cancelled",
                          qUtf8Printable(made->source().name));
                    cancelPrompt(id);
                    return;
                }
                QVariantMap prompt = shown;
                prompt.insert(QStringLiteral("extension"), extension);
                emit prompted(target, id, prompt);
            });
    connect(made, &ScriptRuntime::promptWithdrawn, this, [this, run](quint64 request) {
        const auto found = std::ranges::find_if(m_prompts, [run, request](const auto& entry) {
            return entry.second.run == run && entry.second.request == request;
        });
        if (found == m_prompts.end()) {
            return;
        }
        const quint64 id = found->first;
        const QString target = found->second.serial;
        m_prompts.erase(found);
        emit promptWithdrawn(target, id);
    });
    connect(made, &ScriptRuntime::saveWanted, this,
            [this, made, run, serial, extension](quint64 request, const QVariantList& changes,
                                                 std::shared_ptr<const input::Profile> profile) {
                const bool asking = std::ranges::any_of(m_prompts, [&serial](const auto& entry) {
                    return entry.second.save != nullptr && entry.second.serial == serial;
                });
                if (serial.isEmpty() || profile == nullptr || asking) {
                    made->answer(request, QStringLiteral("false"));
                    return;
                }
                const quint64 id = nextShown();
                m_prompts.insert_or_assign(id, PendingPrompt{.run = run,
                                                             .serial = serial,
                                                             .runtime = QPointer<ScriptRuntime>(made),
                                                             .request = request,
                                                             .kind = QStringLiteral("save"),
                                                             .save = std::move(profile)});
                emit saveWanted(serial, id,
                                QVariantMap{{QStringLiteral("kind"), QStringLiteral("save")},
                                            {QStringLiteral("extension"), extension},
                                            {QStringLiteral("changes"), changes}});
            });
    connect(made, &ScriptRuntime::clipboardWanted, this, [this, made, serial](quint64 request) {
        if (serial.isEmpty()) {
            made->answer(request, QStringLiteral("null"));
            return;
        }
        emit clipboardWanted(serial, made, request);
    });
    connect(made, &ScriptRuntime::clipboardGiven, this, [this, made, serial](quint64 request, const QString& text) {
        if (serial.isEmpty()) {
            made->answer(request, QStringLiteral("false"));
            return;
        }
        emit clipboardGiven(serial, made, request, text);
    });
}

void ExtensionLogic::wireDrawing(ScriptRuntime& runtime, quint64 run, const QString& serial, const QString& extension)
{
    connect(&runtime, &ScriptRuntime::drawn, this,
            [this, run, serial, extension](quint64 own, const QVariantMap& drawing) {
                const auto found = std::ranges::find_if(m_drawings, [run, own](const auto& entry) {
                    return entry.second.run == run && entry.second.own == own;
                });
                const quint64 id = found != m_drawings.end() ? found->first : nextShown();
                m_drawings.insert_or_assign(id, ShownDrawing{.run = run, .own = own, .serial = serial});
                QVariantMap shown = drawing;
                shown.insert(QStringLiteral("extension"), extension);
                emit drawn(serial, id, shown);
            });
    connect(&runtime, &ScriptRuntime::erased, this, [this, run](quint64 own) {
        const auto found = std::ranges::find_if(m_drawings, [run, own](const auto& entry) {
            return entry.second.run == run && entry.second.own == own;
        });
        if (found == m_drawings.end()) {
            return;
        }
        const quint64 id = found->first;
        const QString target = found->second.serial;
        m_drawings.erase(found);
        emit erased(target, id);
    });
    connect(&runtime, &ScriptRuntime::drawingsCleared, this, [this, run] { eraseDrawings(run); });
}

void ExtensionLogic::cancelPromptsOn(const QString& serial)
{
    std::vector<quint64> placed;
    for (const auto& [id, prompt] : m_prompts) {
        if (prompt.serial == serial) {
            placed.push_back(id);
        }
    }
    for (const quint64 id : placed) {
        if (cancelPrompt(id)) {
            emit promptWithdrawn(serial, id);
        }
    }
}

void ExtensionLogic::eraseDrawings(quint64 run)
{
    for (const auto& [serial, id] : takeRun(m_drawings, run)) {
        emit erased(serial, id);
    }
}

void ExtensionLogic::dropRun(quint64 run)
{
    for (const auto& [serial, id] : takeRun(m_prompts, run)) {
        emit promptWithdrawn(serial, id);
    }
    eraseDrawings(run);
}

void ExtensionLogic::appStopped(const ExtensionScript& script, ScriptRuntime& runtime)
{
    if (m_stopped.contains(script.extensionId)) {
        return;
    }
    const Microseconds now = clockOf(m_clock).now();
    std::vector<Microseconds>& times = m_crashes[script.extensionId];
    times.push_back(now);
    if (times.size() > kMostStops) {
        times.erase(times.begin());
    }
    if (times.size() == kMostStops && now - times.front() < kStopWindow) {
        qWarning("extension %s: its logic stopped working three times within a minute, turning it off",
                 qUtf8Printable(script.extensionId));
        m_stopped.insert(script.extensionId);
        QMetaObject::invokeMethod(
            this, [this, id = script.extensionId, name = script.extensionName] { stopExtension(id, name); },
            Qt::QueuedConnection);
        return;
    }
    qInfo("extension script %s: its logic sandbox stopped, starting it again in a second",
          qUtf8Printable(runtime.source().name));
    const QPointer<ScriptRuntime> crashed(&runtime);
    auto restart = [this, script, crashed] { restartApp(script, crashed); };
    QTimer::singleShot(kRestartDelay, this, std::move(restart)); // source-rule-allow R9: the pause before an app script starts again is the behaviour
}

void ExtensionLogic::restartApp(const ExtensionScript& script, const QPointer<ScriptRuntime>& crashed)
{
    const auto holds = [&crashed](const Running& running) {
        return crashed != nullptr && running.runtime.get() == crashed.data();
    };
    if (m_stopped.contains(script.extensionId) || std::ranges::none_of(m_apps, holds)) {
        return;
    }
    qInfo("extension script %s: starting again after its logic sandbox stopped", qUtf8Printable(crashed->source().name));
    std::unique_ptr<ScriptRuntime> started = startScript(script, nullptr);
    if (const auto found = std::ranges::find_if(m_apps, holds); found != m_apps.end()) {
        found->runtime.swap(started);
    }
}

void ExtensionLogic::stopExtension(const QString& id, const QString& name)
{
    if (!m_stopped.contains(id)) {
        return;
    }
    const auto from = [&id](const Running& running) { return running.script.extensionId == id; };
    std::erase_if(m_apps, from);
    for (auto& [serial, session] : m_sessions) {
        std::erase_if(session.running, from);
    }
    emit stoppedWorking(id, name);
}

std::uint64_t ExtensionLogic::nextGestureOffset()
{
    m_lastSlot = m_lastSlot % kGestureSlots + 1;
    return m_lastSlot << kGestureShift;
}

quint64 ExtensionLogic::nextShown()
{
    m_lastShown = m_lastShown % kShownIds + 1;
    return kFirstShown + m_lastShown;
}

}
