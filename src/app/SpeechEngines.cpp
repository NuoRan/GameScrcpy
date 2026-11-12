#include "SpeechEngines.h"

#include "ExtensionHost.h"
#include "NativeExtensions.h"
#include "NativeJobs.h"

#include <gs/platform/Speech.h>

#include <nlohmann/json.hpp>

#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaObject>
#include <QVariantMap>

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>

namespace gs::app {

namespace {

constexpr std::string_view kSay = "speech.say";
constexpr std::string_view kStop = "speech.stop";
constexpr std::string_view kVoices = "speech.voices";
constexpr std::string_view kEngines = "speech.engines";
constexpr std::string_view kListen = "speech.listen";
constexpr std::string_view kClose = "speech.close";
constexpr std::size_t kMostVoices = 256;

QString fromView(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString fromUtf8(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string toUtf8(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString compactJson(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString compactJson(const QJsonArray& array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QJsonObject objectOf(const QByteArray& json)
{
    QJsonParseError problem;
    const QJsonDocument document = QJsonDocument::fromJson(json, &problem);
    return problem.error == QJsonParseError::NoError && document.isObject() ? document.object() : QJsonObject();
}

QString engineProblem(const QString& problem)
{
    if (problem.startsWith(QStringLiteral("GS-API-"))) {
        return problem;
    }
    return QStringLiteral("GS-API-112 ") + (problem.isEmpty() ? QStringLiteral("the speech engine failed") : problem);
}

std::uint64_t jobIdOf(const std::string& json)
{
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
    if (!parsed.is_object() || !parsed.contains("id") || !parsed["id"].is_string()) {
        return 0;
    }
    std::uint64_t id = 0;
    for (const char c : parsed["id"].get_ref<const std::string&>()) {
        if (c < '0' || c > '9') {
            return 0;
        }
        id = id * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return id;
}

std::string textIn(const nlohmann::json& object, const char* key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

QJsonObject engineJson(const SpeechEngineInfo& info)
{
    return QJsonObject{{QStringLiteral("id"), info.id},
                       {QStringLiteral("title"), info.title},
                       {QStringLiteral("say"), info.say},
                       {QStringLiteral("listen"), info.listen},
                       {QStringLiteral("dictation"), info.dictation},
                       {QStringLiteral("languages"), QJsonArray::fromStringList(info.languages)}};
}

QVariantMap voiceMap(const SpeechVoiceInfo& voice)
{
    return QVariantMap{{QStringLiteral("id"), voice.id},
                       {QStringLiteral("name"), voice.name},
                       {QStringLiteral("language"), voice.language}};
}

QString languageIn(const QStringList& languages, const QString& wanted)
{
    for (const QString& language : languages) {
        if (language.compare(wanted, Qt::CaseInsensitive) == 0) {
            return language;
        }
    }
    for (const QString& language : languages) {
        if (language.startsWith(wanted + u'-', Qt::CaseInsensitive)) {
            return language;
        }
    }
    return {};
}

bool optionalNumber(const QJsonValue& value, double low, double high)
{
    return value.isUndefined() || value.isNull() ||
           (value.isDouble() && value.toDouble() >= low && value.toDouble() <= high);
}

}

SystemSpeechEngine::SystemSpeechEngine(platform::SpeechOutput output)
    : m_speech(std::make_unique<platform::SystemSpeech>(output))
{
    for (const platform::SpeechVoice& voice : m_speech->voices()) {
        m_voices.push_back({.id = fromUtf8(voice.id), .name = fromUtf8(voice.name), .language = fromUtf8(voice.language)});
    }
    for (const std::string& language : m_speech->languages()) {
        m_languages.append(fromUtf8(language));
    }
}

SystemSpeechEngine::~SystemSpeechEngine() = default;

SpeechEngineInfo SystemSpeechEngine::info() const
{
    return {.id = fromView(SpeechEngines::kSystem),
            .title = QStringLiteral("Windows"),
            .say = !m_voices.empty(),
            .listen = !m_languages.isEmpty(),
            .dictation = false,
            .languages = m_languages};
}

void SystemSpeechEngine::voices(Voices done)
{
    std::vector<SpeechVoiceInfo> found;
    for (const platform::SpeechVoice& voice : m_speech->voices()) {
        found.push_back({.id = fromUtf8(voice.id), .name = fromUtf8(voice.name), .language = fromUtf8(voice.language)});
    }
    if (!found.empty()) {
        m_voices = found;
    }
    done(std::move(found), {});
}

std::uint64_t SystemSpeechEngine::say(const SpeechLineRequest& line, Spoken spoken)
{
    QString voice;
    if (!line.voice.isEmpty()) {
        const auto found = std::ranges::find_if(m_voices, [&line](const SpeechVoiceInfo& known) {
            return known.id.compare(line.voice, Qt::CaseInsensitive) == 0 ||
                   known.name.compare(line.voice, Qt::CaseInsensitive) == 0;
        });
        if (found == m_voices.end()) {
            spoken(false, QStringLiteral("GS-API-110 Windows has no voice %1").arg(line.voice));
            return 0;
        }
        voice = found->id;
    }
    return m_speech->say({.text = toUtf8(line.text), .voice = toUtf8(voice), .rate = line.rate, .volume = line.volume},
                         [spoken = std::move(spoken)](bool finished, std::string problem) {
                             spoken(finished, fromUtf8(problem));
                         });
}

void SystemSpeechEngine::stop(std::uint64_t token)
{
    if (token != 0) {
        m_speech->stop(token);
    }
}

std::uint64_t SystemSpeechEngine::listen(const SpeechListenRequest& wanted, Heard heard, Ended ended)
{
    if (!wanted.phrases) {
        ended(QStringLiteral("GS-API-110 Windows only listens for a list of commands"));
        return 0;
    }
    platform::SpeechCommands commands{.phrases = {}, .language = toUtf8(wanted.language)};
    for (const QString& phrase : *wanted.phrases) {
        commands.phrases.push_back(toUtf8(phrase));
    }
    return m_speech->listen(
        std::move(commands),
        [heard = std::move(heard)](platform::SpeechHeard got) {
            heard({.phrase = fromUtf8(got.phrase), .text = fromUtf8(got.text), .confidence = got.confidence});
        },
        [ended = std::move(ended)](std::string problem) { ended(fromUtf8(problem)); });
}

void SystemSpeechEngine::unlisten(std::uint64_t token)
{
    if (token != 0) {
        m_speech->unlisten(token);
    }
}

ExtensionSpeechEngine::ExtensionSpeechEngine(ExtensionSpeechEngineInfo declared, NativeJobs& jobs)
    : m_declared(std::move(declared))
    , m_jobs(jobs)
{
}

SpeechEngineInfo ExtensionSpeechEngine::info() const
{
    return {.id = m_declared.extension + u'/' + m_declared.name,
            .title = m_declared.title,
            .say = !m_declared.say.isEmpty(),
            .listen = !m_declared.listen.isEmpty(),
            .dictation = m_declared.dictation && !m_declared.listen.isEmpty(),
            .languages = m_declared.languages};
}

void ExtensionSpeechEngine::voices(Voices done)
{
    if (m_declared.voices.isEmpty()) {
        done({}, {});
        return;
    }
    const NativeExtensions* native = NativeExtensions::instance();
    if (native == nullptr) {
        done({}, QStringLiteral("the extension %1 is not running").arg(m_declared.extension));
        return;
    }
    const script::NodeAnswer answer = native->call(toUtf8(m_declared.extension), toUtf8(m_declared.voices), {});
    if (!answer.code.empty()) {
        done({}, fromUtf8(answer.problem));
        return;
    }
    const nlohmann::json list = nlohmann::json::parse(answer.value.text, nullptr, false);
    std::vector<SpeechVoiceInfo> voices;
    if (list.is_array()) {
        for (const nlohmann::json& item : list) {
            const std::string id = item.is_object() ? textIn(item, "id") : std::string();
            if (id.empty()) {
                continue;
            }
            const std::string name = textIn(item, "name");
            voices.push_back({.id = fromUtf8(id),
                              .name = fromUtf8(name.empty() ? id : name),
                              .language = fromUtf8(textIn(item, "language"))});
            if (voices.size() == kMostVoices) {
                break;
            }
        }
    }
    done(std::move(voices), {});
}

std::uint64_t ExtensionSpeechEngine::say(const SpeechLineRequest& line, Spoken spoken)
{
    const nlohmann::json request{{"extension", toUtf8(m_declared.extension)},
                                 {"job", toUtf8(m_declared.say)},
                                 {"args",
                                  {{"text", toUtf8(line.text)},
                                   {"voice", toUtf8(line.voice)},
                                   {"rate", line.rate},
                                   {"volume", line.volume}}}};
    const script::ServiceAnswer started = m_jobs.startForHost(
        request.dump(), HostJobEvents{.progress = {},
                                      .done = [spoken](bool worked,
                                                       const QString& text) { spoken(worked, worked ? QString() : text); },
                                      .output = {}});
    if (!started.code.empty()) {
        spoken(false, QStringLiteral("GS-API-112 ") + fromUtf8(started.problem));
        return 0;
    }
    return jobIdOf(started.json);
}

void ExtensionSpeechEngine::stop(std::uint64_t token)
{
    if (token != 0) {
        m_jobs.cancelForHost(token);
    }
}

std::uint64_t ExtensionSpeechEngine::listen(const SpeechListenRequest& wanted, Heard heard, Ended ended)
{
    nlohmann::json phrases = nullptr;
    if (wanted.phrases) {
        phrases = nlohmann::json::array();
        for (const QString& phrase : *wanted.phrases) {
            phrases.push_back(toUtf8(phrase));
        }
    }
    const nlohmann::json request{
        {"extension", toUtf8(m_declared.extension)},
        {"job", toUtf8(m_declared.listen)},
        {"args", {{"phrases", phrases}, {"language", toUtf8(wanted.language)}, {"confidence", wanted.confidence}}}};
    const script::ServiceAnswer started = m_jobs.startForHost(
        request.dump(),
        HostJobEvents{.progress = {},
                      .done = [ended](bool worked, const QString& text) { ended(worked ? QString() : text); },
                      .output =
                          [heard](const QString& json) {
                              const nlohmann::json value = nlohmann::json::parse(toUtf8(json), nullptr, false);
                              if (!value.is_object()) {
                                  return;
                              }
                              SpeechHeardInfo info;
                              const auto phrase = value.find("phrase");
                              if (phrase != value.end() && phrase->is_string()) {
                                  info.phrase = fromUtf8(phrase->get<std::string>());
                              }
                              info.text = fromUtf8(textIn(value, "text"));
                              const auto confidence = value.find("confidence");
                              info.confidence = confidence != value.end() && confidence->is_number()
                                                    ? std::clamp(confidence->get<double>(), 0.0, 1.0)
                                                    : 1.0;
                              heard(std::move(info));
                          }});
    if (!started.code.empty()) {
        ended(QStringLiteral("GS-API-112 ") + fromUtf8(started.problem));
        return 0;
    }
    return jobIdOf(started.json);
}

void ExtensionSpeechEngine::unlisten(std::uint64_t token)
{
    if (token != 0) {
        m_jobs.cancelForHost(token);
    }
}

struct SpeechEngines::Shared {
    mutable std::mutex mutex;
    SpeechEngines* owner = nullptr;
    std::vector<SpeechEngineInfo> engines;

    void post(std::function<void(SpeechEngines&)> work) const
    {
        const std::scoped_lock lock(mutex);
        if (owner == nullptr) {
            return;
        }
        SpeechEngines* target = owner;
        QMetaObject::invokeMethod(target, [target, work = std::move(work)] { work(*target); }, Qt::QueuedConnection);
    }
};

SpeechEngines::SpeechEngines(QObject* parent)
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
    };
}

SpeechEngines::~SpeechEngines()
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = nullptr;
    }
    if (m_offered) {
        for (const std::string_view name : {kSay, kStop, kVoices, kEngines, kListen, kClose}) {
            LogicServices::instance().withdraw(name);
        }
    }
    for (const auto& [handle, listener] : m_listeners) {
        if (const auto engine = m_engines.find(listener.engine); engine != m_engines.end()) {
            engine->second->unlisten(listener.token);
        }
    }
    m_listeners.clear();
    m_queues.clear();
    m_engines.clear();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

SpeechEngines* SpeechEngines::create(QQmlEngine*, QJSEngine*)
{
    if (s_instance == nullptr) {
        auto* made = new SpeechEngines(nullptr);
        QJSEngine::setObjectOwnership(made, QJSEngine::JavaScriptOwnership);
        return made;
    }
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void SpeechEngines::offerServices()
{
    LogicServices& services = LogicServices::instance();
    for (const std::string_view name : {kStop, kEngines, kClose}) {
        services.offerCall(std::string(name), [shared = m_shared, name](const LogicCaller& caller, std::string_view json) {
            return callWith(shared, caller, name, json);
        });
    }
    for (const std::string_view name : {kSay, kVoices, kListen}) {
        services.offerRequest(
            std::string(name),
            [this, name](const LogicCaller& caller, quint64 id, const QString& json) { request(caller, name, id, json); },
            [this](const LogicCaller& caller, quint64 id) { dropRequest(caller, id); });
    }
    connect(&services, &LogicServices::logicEnded, this, [this](quint64 logic) { endLogic(logic); });
    m_offered = true;
}

void SpeechEngines::setReplies(SpeechReplies replies)
{
    m_replies = std::move(replies);
}

void SpeechEngines::setDefaults(std::function<SpeechDefaults()> defaults)
{
    m_defaults = std::move(defaults);
}

void SpeechEngines::setPermitted(const QStringList& extensions)
{
    m_permitted = std::set<QString>(extensions.begin(), extensions.end());
    std::vector<QString> closing;
    for (const auto& [handle, listener] : m_listeners) {
        if (!listener.owner.isEmpty() && !m_permitted.contains(listener.owner)) {
            closing.push_back(handle);
        }
    }
    for (const QString& handle : closing) {
        drop(handle, QStringLiteral("forbidden"), {});
    }
}

void SpeechEngines::addEngine(std::unique_ptr<SpeechEngine> engine)
{
    const QString id = engine->info().id;
    removeEngine(id);
    m_engines.emplace(id, std::move(engine));
    publish();
}

void SpeechEngines::removeEngine(const QString& id)
{
    const auto found = m_engines.find(id);
    if (found == m_engines.end()) {
        return;
    }
    std::vector<QString> closing;
    for (const auto& [handle, listener] : m_listeners) {
        if (listener.engine == id) {
            closing.push_back(handle);
        }
    }
    for (const QString& handle : closing) {
        drop(handle, QStringLiteral("engine"), QStringLiteral("the speech engine %1 went away").arg(id));
    }
    if (const auto queue = m_queues.find(id); queue != m_queues.end()) {
        Queue left = std::move(queue->second);
        m_queues.erase(queue);
        for (const Line& line : left.waiting) {
            settle(line, false, {});
        }
        if (left.current) {
            found->second->stop(left.token);
            settle(*left.current, false, {});
        }
    }
    m_engines.erase(found);
    publish();
}

void SpeechEngines::follow(ExtensionHost& extensions, NativeJobs& jobs)
{
    const auto update = [this, &extensions, &jobs] {
        QStringList permitted;
        std::map<QString, ExtensionSpeechEngineInfo> wanted;
        for (const ExtensionHost::Extension& extension : extensions.extensions()) {
            if (!extension.active || !extension.manifest) {
                continue;
            }
            const ext::Manifest& manifest = *extension.manifest;
            const QString id = QString::fromStdString(manifest.id);
            const std::vector<std::string> granted = extensions.grantedPermissions(manifest.id);
            if (std::ranges::find(granted, kPermission) != granted.end()) {
                permitted.append(id);
            }
            if (!manifest.contributes.native) {
                continue;
            }
            for (const ext::NativeSpeechEngine& engine : manifest.contributes.native->speechEngines) {
                QStringList languages;
                for (const std::string& language : engine.languages) {
                    languages.append(QString::fromStdString(language));
                }
                ExtensionSpeechEngineInfo declared{.extension = id,
                                                   .name = QString::fromStdString(engine.name),
                                                   .title = extensions.textOf(manifest,
                                                                              "speechEngines." + engine.name + ".title",
                                                                              engine.title),
                                                   .say = QString::fromStdString(engine.say),
                                                   .voices = QString::fromStdString(engine.voices),
                                                   .listen = QString::fromStdString(engine.listen),
                                                   .dictation = engine.dictation,
                                                   .languages = languages};
                wanted.emplace(id + u'/' + declared.name, std::move(declared));
            }
        }
        std::vector<QString> stale;
        for (const auto& [id, engine] : m_engines) {
            const auto* provided = dynamic_cast<const ExtensionSpeechEngine*>(engine.get());
            const auto still = wanted.find(id);
            if (provided != nullptr && (still == wanted.end() || !(still->second == provided->declared()))) {
                stale.push_back(id);
            }
        }
        for (const QString& id : stale) {
            removeEngine(id);
        }
        for (auto& [id, declared] : wanted) {
            if (!m_engines.contains(id)) {
                addEngine(std::make_unique<ExtensionSpeechEngine>(std::move(declared), jobs));
            }
        }
        setPermitted(permitted);
    };
    update();
    connect(&extensions, &ExtensionHost::extensionsChanged, this, update);
}

void SpeechEngines::request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json)
{
    if (service == kSay) {
        say(caller, id, json);
    } else if (service == kVoices) {
        listVoices(caller, id, json);
    } else if (service == kListen) {
        listen(caller, id, json);
    } else if (m_replies.refuse) {
        m_replies.refuse(caller.logic, id,
                         QStringLiteral("GS-API-070 no part of GameScrcpy offers the service %1").arg(fromView(service)));
    }
}

void SpeechEngines::dropRequest(const LogicCaller& caller, quint64 id)
{
    stopWhere([&caller, id](const Line& line) { return !line.preview && line.logic == caller.logic && line.request == id; });
}

script::ServiceAnswer SpeechEngines::call(const LogicCaller& caller, std::string_view service, std::string_view json)
{
    return callWith(m_shared, caller, service, json);
}

script::ServiceAnswer SpeechEngines::callWith(const std::shared_ptr<Shared>& shared, const LogicCaller& caller,
                                              std::string_view service, std::string_view json)
{
    if (service == kEngines) {
        QJsonArray list;
        {
            const std::scoped_lock lock(shared->mutex);
            for (const SpeechEngineInfo& info : shared->engines) {
                list.append(engineJson(info));
            }
        }
        return {toUtf8(compactJson(list)), {}, {}};
    }
    if (service == kStop) {
        shared->post([logic = caller.logic](SpeechEngines& self) {
            self.stopWhere([logic](const Line& line) { return !line.preview && line.logic == logic; });
        });
        return {"null", {}, {}};
    }
    if (service == kClose) {
        const QString handle =
            objectOf(QByteArray(json.data(), static_cast<qsizetype>(json.size()))).value(u"handle").toString();
        shared->post([logic = caller.logic, handle](SpeechEngines& self) { self.close(logic, handle); });
        return {"null", {}, {}};
    }
    return {"null", "GS-API-070", "no part of GameScrcpy answers the service " + std::string(service) + " at once"};
}

void SpeechEngines::say(const LogicCaller& caller, quint64 id, const QString& json)
{
    const auto refuse = [this, &caller, id](const QString& message) {
        if (m_replies.refuse) {
            m_replies.refuse(caller.logic, id, message);
        }
    };
    const QJsonObject body = objectOf(json.toUtf8());
    const QJsonValue text = body.value(u"text");
    if (!text.isString() || text.toString().trimmed().isEmpty() || text.toString().size() > kMostTextCharacters) {
        refuse(QStringLiteral("GS-API-111 the text to read is 1 to 1000 characters"));
        return;
    }
    const QJsonValue rate = body.value(u"rate");
    const QJsonValue volume = body.value(u"volume");
    const QJsonValue voice = body.value(u"voice");
    const QJsonValue chosen = body.value(u"engine");
    const bool interrupt = body.value(u"interrupt").toBool(false);
    if (!optionalNumber(rate, 0.5, 2.0) || !optionalNumber(volume, 0.0, 1.0) ||
        !(voice.isUndefined() || voice.isNull() || voice.isString()) ||
        !(chosen.isUndefined() || chosen.isNull() || chosen.isString())) {
        refuse(QStringLiteral("GS-API-111 rate is from 0.5 to 2, volume from 0 to 1, and voice and engine are names"));
        return;
    }
    const SpeechDefaults wanted = defaults();
    const QString settled = wanted.engine.isEmpty() ? fromView(kSystem) : wanted.engine;
    const QString engine = chosen.isString() && !chosen.toString().isEmpty() ? chosen.toString() : settled;
    const auto found = m_engines.find(engine);
    if (found == m_engines.end() || !found->second->info().say) {
        refuse(QStringLiteral("GS-API-110 there is no speech engine %1 that reads aloud").arg(engine));
        return;
    }
    std::size_t lines = 0;
    std::size_t dropped = 0;
    for (const auto& [name, queue] : m_queues) {
        const auto queued = static_cast<std::size_t>(std::ranges::count_if(
            queue.waiting, [&caller](const Line& line) { return !line.preview && line.logic == caller.logic; }));
        lines += queued;
        if (name == engine) {
            dropped = queued;
        }
    }
    const std::size_t waiting = interrupt ? lines - dropped : lines;
    if (waiting >= kMostWaitingLines) {
        refuse(QStringLiteral("GS-API-111 a logic has at most 32 lines waiting to be read"));
        return;
    }
    Line line{.key = ++m_lastKey,
              .logic = caller.logic,
              .request = id,
              .preview = false,
              .line = {.text = text.toString(),
                       .voice = voice.isString() ? voice.toString() : (engine == settled ? wanted.voice : QString()),
                       .rate = rate.isDouble() ? rate.toDouble() : wanted.rate,
                       .volume = volume.isDouble() ? volume.toDouble() : wanted.volume}};
    enqueue(engine, std::move(line), interrupt);
}

void SpeechEngines::listVoices(const LogicCaller& caller, quint64 id, const QString& json)
{
    const QJsonObject body = objectOf(json.toUtf8());
    const SpeechDefaults wanted = defaults();
    const QString settled = wanted.engine.isEmpty() ? fromView(kSystem) : wanted.engine;
    const QString engine = body.value(u"engine").isString() && !body.value(u"engine").toString().isEmpty()
                               ? body.value(u"engine").toString()
                               : settled;
    const auto found = m_engines.find(engine);
    if (found == m_engines.end()) {
        if (m_replies.refuse) {
            m_replies.refuse(caller.logic, id, QStringLiteral("GS-API-110 there is no speech engine %1").arg(engine));
        }
        return;
    }
    found->second->voices([shared = m_shared, logic = caller.logic, id](std::vector<SpeechVoiceInfo> voices,
                                                                        QString problem) {
        shared->post([logic, id, voices = std::move(voices), problem = std::move(problem)](SpeechEngines& self) {
            if (!problem.isEmpty()) {
                if (self.m_replies.refuse) {
                    self.m_replies.refuse(logic, id, engineProblem(problem));
                }
                return;
            }
            QJsonArray list;
            for (const SpeechVoiceInfo& voice : voices) {
                list.append(QJsonObject{{QStringLiteral("id"), voice.id},
                                        {QStringLiteral("name"), voice.name},
                                        {QStringLiteral("language"), voice.language}});
            }
            if (self.m_replies.answer) {
                self.m_replies.answer(logic, id, compactJson(list));
            }
        });
    });
}

void SpeechEngines::listen(const LogicCaller& caller, quint64 id, const QString& json)
{
    const auto refuse = [this, &caller, id](const QString& message) {
        if (m_replies.refuse) {
            m_replies.refuse(caller.logic, id, message);
        }
    };
    if (!caller.extension.isEmpty() && !m_permitted.contains(caller.extension)) {
        refuse(QStringLiteral("GS-API-113 this extension may not listen: its manifest does not ask for the permission "
                              "\"speech.listen\", or it was not allowed"));
        return;
    }
    const QJsonObject body = objectOf(json.toUtf8());
    std::optional<QStringList> phrases;
    const QJsonValue given = body.value(u"phrases");
    if (given.isArray()) {
        const QJsonArray array = given.toArray();
        bool fine = !array.isEmpty() && static_cast<std::size_t>(array.size()) <= kMostPhrases;
        QStringList list;
        for (const QJsonValue& item : array) {
            const QString phrase = item.toString().trimmed();
            fine = fine && item.isString() && !phrase.isEmpty() && phrase.size() <= kMostPhraseCharacters;
            list.append(phrase);
        }
        if (!fine) {
            refuse(QStringLiteral("GS-API-111 listen for 1 to 256 commands of 1 to 64 characters each"));
            return;
        }
        list.removeDuplicates();
        phrases = list;
    } else if (!given.isNull() && !given.isUndefined()) {
        refuse(QStringLiteral("GS-API-111 listen for 1 to 256 commands of 1 to 64 characters each"));
        return;
    }
    const QJsonValue confidence = body.value(u"confidence");
    const QJsonValue chosen = body.value(u"engine");
    const QJsonValue spoken = body.value(u"language");
    if (!optionalNumber(confidence, 0.0, 1.0) || !(chosen.isUndefined() || chosen.isNull() || chosen.isString()) ||
        !(spoken.isUndefined() || spoken.isNull() || spoken.isString())) {
        refuse(QStringLiteral("GS-API-111 confidence is from 0 to 1, and engine and language are names"));
        return;
    }
    const SpeechDefaults wanted = defaults();
    const QString settled = wanted.listenEngine.isEmpty() ? fromView(kSystem) : wanted.listenEngine;
    const QString engine = chosen.isString() && !chosen.toString().isEmpty() ? chosen.toString() : settled;
    const auto found = m_engines.find(engine);
    if (found == m_engines.end() || !found->second->info().listen) {
        refuse(QStringLiteral("GS-API-110 there is no speech engine %1 that listens").arg(engine));
        return;
    }
    const SpeechEngineInfo info = found->second->info();
    if (!phrases && !info.dictation) {
        refuse(QStringLiteral("GS-API-110 the speech engine %1 only listens for a list of commands").arg(engine));
        return;
    }
    QString language = spoken.toString();
    if (language.isEmpty()) {
        for (const QString& candidate : {wanted.language, wanted.interfaceLanguage}) {
            if (candidate.isEmpty()) {
                continue;
            }
            language = info.languages.isEmpty() ? candidate : languageIn(info.languages, candidate);
            if (!language.isEmpty()) {
                break;
            }
        }
        if (language.isEmpty() && !info.languages.isEmpty()) {
            language = info.languages.front();
        }
    } else if (!info.languages.isEmpty()) {
        const QString known = languageIn(info.languages, language);
        if (known.isEmpty()) {
            refuse(QStringLiteral("GS-API-110 the speech engine %1 does not know the language %2").arg(engine, language));
            return;
        }
        language = known;
    }
    const auto listening = std::ranges::count_if(
        m_listeners, [&caller](const auto& entry) { return entry.second.logic == caller.logic; });
    if (static_cast<std::size_t>(listening) >= kMostListeners) {
        refuse(QStringLiteral("GS-API-111 a logic listens with at most 8 listeners at once"));
        return;
    }
    const double least = confidence.isDouble() ? confidence.toDouble() : 0.6;
    const QString handle = QStringLiteral("s%1").arg(++m_lastHandle);
    m_listeners.emplace(handle, Listener{.logic = caller.logic,
                                         .owner = caller.extension,
                                         .engine = engine,
                                         .token = 0,
                                         .confidence = least});
    const std::uint64_t token = found->second->listen(
        SpeechListenRequest{.phrases = phrases, .language = language, .confidence = least},
        [shared = m_shared, handle](SpeechHeardInfo heard) {
            shared->post([handle, heard = std::move(heard)](SpeechEngines& self) { self.heard(handle, heard); });
        },
        [shared = m_shared, handle](QString problem) {
            shared->post([handle, problem = std::move(problem)](SpeechEngines& self) { self.ended(handle, problem); });
        });
    m_listeners.at(handle).token = token;
    if (m_replies.answer) {
        m_replies.answer(caller.logic, id,
                         compactJson(QJsonObject{{QStringLiteral("handle"), handle},
                                                 {QStringLiteral("engine"), engine},
                                                 {QStringLiteral("language"), language}}));
    }
}

void SpeechEngines::enqueue(const QString& engine, Line line, bool interrupt)
{
    Queue& queue = m_queues[engine];
    if (interrupt) {
        std::deque<Line> dropped;
        dropped.swap(queue.waiting);
        for (const Line& old : dropped) {
            settle(old, false, {});
        }
        if (queue.current && !queue.stopping) {
            queue.stopping = true;
            m_engines.at(engine)->stop(queue.token);
        }
    }
    queue.waiting.push_back(std::move(line));
    startNext(engine);
}

void SpeechEngines::startNext(const QString& engine)
{
    const auto found = m_engines.find(engine);
    Queue& queue = m_queues[engine];
    if (found == m_engines.end() || queue.current || queue.waiting.empty()) {
        return;
    }
    queue.current = std::move(queue.waiting.front());
    queue.waiting.pop_front();
    queue.stopping = false;
    queue.token = 0;
    const quint64 key = queue.current->key;
    const std::uint64_t token =
        found->second->say(queue.current->line, [shared = m_shared, engine, key](bool finished, QString problem) {
            shared->post([engine, key, finished, problem = std::move(problem)](SpeechEngines& self) {
                self.spoken(engine, key, finished, problem);
            });
        });
    queue.token = token;
}

void SpeechEngines::spoken(const QString& engine, quint64 key, bool finished, const QString& problem)
{
    const auto found = m_queues.find(engine);
    if (found == m_queues.end() || !found->second.current || found->second.current->key != key) {
        return;
    }
    Queue& queue = found->second;
    const Line line = std::move(*queue.current);
    const bool stopped = queue.stopping;
    queue.current.reset();
    queue.token = 0;
    queue.stopping = false;
    settle(line, finished && !stopped, stopped ? QString() : problem);
    startNext(engine);
}

void SpeechEngines::settle(const Line& line, bool finished, const QString& problem)
{
    if (line.preview) {
        return;
    }
    if (finished || problem.isEmpty()) {
        if (m_replies.answer) {
            m_replies.answer(line.logic, line.request, finished ? QStringLiteral("true") : QStringLiteral("false"));
        }
    } else if (m_replies.refuse) {
        m_replies.refuse(line.logic, line.request, engineProblem(problem));
    }
}

void SpeechEngines::stopWhere(const std::function<bool(const Line&)>& matches)
{
    for (auto& [engine, queue] : m_queues) {
        std::deque<Line> kept;
        std::vector<Line> dropped;
        for (Line& line : queue.waiting) {
            if (matches(line)) {
                dropped.push_back(std::move(line));
            } else {
                kept.push_back(std::move(line));
            }
        }
        queue.waiting.swap(kept);
        for (const Line& line : dropped) {
            settle(line, false, {});
        }
        if (queue.current && matches(*queue.current) && !queue.stopping) {
            queue.stopping = true;
            if (const auto found = m_engines.find(engine); found != m_engines.end()) {
                found->second->stop(queue.token);
            }
        }
    }
}

void SpeechEngines::heard(const QString& handle, const SpeechHeardInfo& heard)
{
    const auto found = m_listeners.find(handle);
    if (found == m_listeners.end() || heard.confidence < found->second.confidence || !m_replies.tell) {
        return;
    }
    m_replies.tell(found->second.logic, fromView(kEventKind), handle,
                   compactJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("heard")},
                                           {QStringLiteral("phrase"), heard.phrase ? QJsonValue(*heard.phrase)
                                                                                   : QJsonValue(QJsonValue::Null)},
                                           {QStringLiteral("text"), heard.text},
                                           {QStringLiteral("confidence"), heard.confidence}}));
}

void SpeechEngines::ended(const QString& handle, const QString& problem)
{
    const auto found = m_listeners.find(handle);
    if (found == m_listeners.end()) {
        return;
    }
    const std::uint64_t logic = found->second.logic;
    m_listeners.erase(found);
    if (m_replies.tell) {
        m_replies.tell(logic, fromView(kEventKind), handle,
                       compactJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("closed")},
                                               {QStringLiteral("reason"), QStringLiteral("engine")},
                                               {QStringLiteral("problem"), problem}}));
    }
}

void SpeechEngines::close(std::uint64_t logic, const QString& handle)
{
    const auto found = m_listeners.find(handle);
    if (found == m_listeners.end() || found->second.logic != logic) {
        return;
    }
    const Listener listener = found->second;
    m_listeners.erase(found);
    if (const auto engine = m_engines.find(listener.engine); engine != m_engines.end()) {
        engine->second->unlisten(listener.token);
    }
}

void SpeechEngines::drop(const QString& handle, const QString& reason, const QString& problem)
{
    const auto found = m_listeners.find(handle);
    if (found == m_listeners.end()) {
        return;
    }
    const Listener listener = found->second;
    m_listeners.erase(found);
    if (const auto engine = m_engines.find(listener.engine); engine != m_engines.end()) {
        engine->second->unlisten(listener.token);
    }
    if (m_replies.tell) {
        m_replies.tell(listener.logic, fromView(kEventKind), handle,
                       compactJson(QJsonObject{{QStringLiteral("type"), QStringLiteral("closed")},
                                               {QStringLiteral("reason"), reason},
                                               {QStringLiteral("problem"), problem}}));
    }
}

void SpeechEngines::endLogic(std::uint64_t logic)
{
    stopWhere([logic](const Line& line) { return !line.preview && line.logic == logic; });
    std::vector<QString> closing;
    for (const auto& [handle, listener] : m_listeners) {
        if (listener.logic == logic) {
            closing.push_back(handle);
        }
    }
    for (const QString& handle : closing) {
        close(logic, handle);
    }
}

void SpeechEngines::publish()
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->engines.clear();
        for (const auto& [id, engine] : m_engines) {
            m_shared->engines.push_back(engine->info());
        }
    }
    emit enginesChanged();
}

SpeechDefaults SpeechEngines::defaults() const
{
    return m_defaults ? m_defaults() : SpeechDefaults{};
}

QVariantList SpeechEngines::engineList() const
{
    QVariantList list;
    for (const auto& [id, engine] : m_engines) {
        const SpeechEngineInfo info = engine->info();
        QVariantMap entry{{QStringLiteral("id"), info.id},
                          {QStringLiteral("title"), info.title},
                          {QStringLiteral("say"), info.say},
                          {QStringLiteral("listen"), info.listen},
                          {QStringLiteral("dictation"), info.dictation},
                          {QStringLiteral("languages"), info.languages}};
        if (id == fromView(kSystem)) {
            list.prepend(entry);
        } else {
            list.append(entry);
        }
    }
    return list;
}

void SpeechEngines::loadVoices(const QString& engine)
{
    const SpeechDefaults wanted = defaults();
    const QString id = engine.isEmpty() ? (wanted.engine.isEmpty() ? fromView(kSystem) : wanted.engine) : engine;
    const auto found = m_engines.find(id);
    if (found == m_engines.end()) {
        emit voicesLoaded(id, {}, tr("This engine is not available now."));
        return;
    }
    found->second->voices([shared = m_shared, id](std::vector<SpeechVoiceInfo> voices, QString problem) {
        shared->post([id, voices = std::move(voices), problem = std::move(problem)](SpeechEngines& self) {
            QVariantList list;
            for (const SpeechVoiceInfo& voice : voices) {
                list.append(voiceMap(voice));
            }
            emit self.voicesLoaded(id, list, problem);
        });
    });
}

bool SpeechEngines::preview()
{
    const SpeechDefaults wanted = defaults();
    const QString engine = wanted.engine.isEmpty() ? fromView(kSystem) : wanted.engine;
    const auto found = m_engines.find(engine);
    if (found == m_engines.end() || !found->second->info().say) {
        return false;
    }
    stopWhere([](const Line& line) { return line.preview; });
    enqueue(engine,
            Line{.key = ++m_lastKey,
                 .logic = 0,
                 .request = 0,
                 .preview = true,
                 .line = {.text = tr("This is how logic reads text aloud."),
                          .voice = wanted.voice,
                          .rate = wanted.rate,
                          .volume = wanted.volume}},
            false);
    return true;
}

}
