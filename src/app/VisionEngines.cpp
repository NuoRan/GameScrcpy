#include "VisionEngines.h"

#include "ExtensionHost.h"
#include "NativeExtensions.h"
#include "VisionSearch.h"

#include <gs/base/JsonNesting.h>
#include <gs/platform/TextReader.h>
#include <gs/script/Permissions.h>

#include <nlohmann/json.hpp>

#include <QByteArray>
#include <QMetaObject>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace gs::app {

namespace {

constexpr std::string_view kRecognize = "vision.recognize";
constexpr std::string_view kEngines = "vision.engines";
constexpr std::size_t kMostLanguageBytes = 35;
constexpr double kEdge = 1e-9;

using Json = nlohmann::ordered_json;

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

bool languageTag(std::string_view tag) noexcept
{
    return !tag.empty() && tag.size() <= kMostLanguageBytes && std::ranges::all_of(tag, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    });
}

bool ratioIn(const nlohmann::json& area, const char* name, double& out)
{
    const auto found = area.find(name);
    if (found == area.end() || !found->is_number()) {
        return false;
    }
    out = found->get<double>();
    return out >= 0.0 && out <= 1.0;
}

struct Asked {
    QString engine;
    VisionAsk ask;
    script::Region region;
};

std::optional<Asked> askedOf(const QString& json, QString& problem)
{
    const std::string text = toUtf8(json);
    if (!jsonNestingWithin(text, kMostJsonNesting)) {
        problem = QStringLiteral("GS-API-172 what is asked is nested more than 64 levels deep");
        return std::nullopt;
    }
    const nlohmann::json body = nlohmann::json::parse(text, nullptr, false);
    if (!body.is_object()) {
        problem = QStringLiteral("GS-API-172 what is asked must be a JSON object");
        return std::nullopt;
    }
    Asked made;
    const auto engine = body.find("engine");
    if (engine == body.end() || !engine->is_string() || engine->get_ref<const std::string&>().empty() ||
        engine->get_ref<const std::string&>().size() > VisionEngines::kMostEngineIdBytes) {
        problem = QStringLiteral("GS-API-172 the engine is the id of one from vision.engines(), such as \"system\"");
        return std::nullopt;
    }
    made.engine = fromUtf8(engine->get_ref<const std::string&>());
    const auto kind = body.find("kind");
    const std::optional<exthost::VisionKind> named =
        kind != body.end() && kind->is_string() ? exthost::visionKindNamed(kind->get_ref<const std::string&>())
                                                : std::nullopt;
    if (!named) {
        problem = QStringLiteral("GS-API-172 kind must be \"ocr\", \"detect\" or \"classify\"");
        return std::nullopt;
    }
    made.ask.kind = *named;
    const auto region = body.find("region");
    if (region != body.end() && !region->is_null()) {
        script::Region read;
        if (!region->is_object() || !ratioIn(*region, "x", read.x) || !ratioIn(*region, "y", read.y) ||
            !ratioIn(*region, "width", read.width) || !ratioIn(*region, "height", read.height) ||
            read.x + read.width > 1.0 + kEdge || read.y + read.height > 1.0 + kEdge) {
            problem = QStringLiteral("GS-API-002 the region must be an object inside the picture");
            return std::nullopt;
        }
        made.region = read;
    }
    const auto params = body.find("params");
    if (params != body.end() && !params->is_null()) {
        if (!params->is_object()) {
            problem = QStringLiteral("GS-API-172 params must be an object");
            return std::nullopt;
        }
        made.ask.params = params->dump();
        if (made.ask.params.size() > exthost::kMostVisionParamBytes) {
            problem = QStringLiteral("GS-API-172 params must be at most 64 KiB as JSON");
            return std::nullopt;
        }
    }
    return made;
}

script::Region ratiosOf(const vision::Rect& area, const media::LumaSnapshot& shot)
{
    const double width = shot.width > 0 ? shot.width : 1.0;
    const double height = shot.height > 0 ? shot.height : 1.0;
    return {area.x / width, area.y / height, area.width / width, area.height / height};
}

bool lumaFits(const media::LumaSnapshot& shot)
{
    return shot.width > 0 && shot.height > 0 &&
           shot.luma.size() >= static_cast<std::size_t>(shot.width) * static_cast<std::size_t>(shot.height);
}

exthost::VisionImage bgraOf(const media::LumaSnapshot& shot, const vision::Rect& area)
{
    const auto width = static_cast<std::size_t>(area.width);
    const auto height = static_cast<std::size_t>(area.height);
    const std::vector<std::uint8_t> rgb = media::rgbOf(shot, area.x, area.y, area.width, area.height);
    const bool colored = rgb.size() >= width * height * 3;
    if (!colored && !lumaFits(shot)) {
        return {};
    }
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(width * height * 4);
    for (std::size_t y = 0; y < height; ++y) {
        const std::uint8_t* luma = colored ? nullptr
                                           : shot.luma.data() +
                                                 (static_cast<std::size_t>(area.y) + y) * static_cast<std::size_t>(shot.width) +
                                                 static_cast<std::size_t>(area.x);
        for (std::size_t x = 0; x < width; ++x) {
            std::uint8_t* pixel = pixels->data() + 4 * (y * width + x);
            if (colored) {
                const std::uint8_t* from = rgb.data() + 3 * (y * width + x);
                pixel[0] = from[2];
                pixel[1] = from[1];
                pixel[2] = from[0];
            } else {
                pixel[0] = luma[x];
                pixel[1] = luma[x];
                pixel[2] = luma[x];
            }
            pixel[3] = 255;
        }
    }
    return {.pixels = std::shared_ptr<const std::uint8_t>(pixels, pixels->data()),
            .width = static_cast<std::uint32_t>(width),
            .height = static_cast<std::uint32_t>(height),
            .stride = static_cast<std::uint32_t>(width * 4)};
}

QString unknownLanguage(const std::string& language)
{
    const std::vector<std::string> installed = platform::textLanguages();
    if (installed.empty()) {
        return QStringLiteral("GS-API-171 this computer has no text recognition: add a language with optical "
                              "character recognition in Windows Settings, Time & language, Language & region");
    }
    QStringList have;
    for (const std::string& tag : installed) {
        have.append(fromUtf8(tag));
    }
    return QStringLiteral("GS-API-171 this computer has no text recognition for \"%1\" (it has %2): add the language "
                          "in Windows Settings, Time & language, Language & region")
        .arg(fromUtf8(language), have.join(QStringLiteral(", ")));
}

QString problemOf(exthost::CallStatus status, const std::string& problem)
{
    const QString text = fromUtf8(problem);
    switch (status) {
    case exthost::CallStatus::Ok:
        return {};
    case exthost::CallStatus::BadArgument:
        return QStringLiteral("GS-API-172 ") + text;
    case exthost::CallStatus::TooSlow:
        return QStringLiteral("GS-API-174 ") + text;
    case exthost::CallStatus::Unavailable:
        return QStringLiteral("GS-API-170 ") + text;
    case exthost::CallStatus::Failed:
    case exthost::CallStatus::Crashed:
        break;
    }
    return QStringLiteral("GS-API-173 ") + text;
}

}

SystemVisionEngine::SystemVisionEngine()
{
    for (const std::string& tag : platform::textLanguages()) {
        m_languages.append(fromUtf8(tag));
    }
}

VisionEngineInfo SystemVisionEngine::info() const
{
    return {.id = fromView(VisionEngines::kSystem),
            .title = QStringLiteral("Windows"),
            .kinds = {fromView(exthost::visionKindName(exthost::VisionKind::Ocr))},
            .languages = m_languages};
}

VisionReply SystemVisionEngine::recognize(const VisionPicture& picture, const VisionAsk& ask)
{
    const nlohmann::json params = nlohmann::json::parse(ask.params, nullptr, false);
    std::string language;
    bool words = false;
    if (params.is_object()) {
        const auto tag = params.find("language");
        if (tag != params.end() && !tag->is_null()) {
            if (!tag->is_string() || !languageTag(tag->get_ref<const std::string&>())) {
                return {.json = {}, .area = {}, .problem = QStringLiteral("GS-API-172 language must be a tag such as "
                                                                          "\"en\" or \"zh-Hans\"")};
            }
            language = tag->get<std::string>();
        }
        const auto each = params.find("words");
        if (each != params.end() && !each->is_null()) {
            if (!each->is_boolean()) {
                return {.json = {}, .area = {}, .problem = QStringLiteral("GS-API-172 words must be true or false")};
            }
            words = each->get<bool>();
        }
    }
    const media::LumaSnapshot& shot = *picture.shot;
    const vision::Rect area = pixelArea(picture.region, shot.width, shot.height);
    VisionReply reply{.json = "[]", .area = ratiosOf(area, shot), .problem = {}};
    if (area.empty() || !lumaFits(shot)) {
        return reply;
    }
    const std::uint8_t* start = shot.luma.data() + static_cast<std::ptrdiff_t>(area.y) * shot.width + area.x;
    const auto read = platform::readText(start, area.width, area.height, shot.width, language);
    if (!read) {
        reply.json.clear();
        reply.problem = read.error() == platform::TextProblem::Failed
                            ? QStringLiteral("GS-API-173 Windows could not read the text in the picture")
                            : unknownLanguage(language);
        return reply;
    }
    const double width = area.width;
    const double height = area.height;
    Json list = Json::array();
    const auto add = [&list, width, height](const std::string& text, const platform::TextBox& box) {
        Json entry = Json::object();
        entry["text"] = text;
        entry["x"] = (box.x + box.width / 2.0) / width;
        entry["y"] = (box.y + box.height / 2.0) / height;
        entry["width"] = box.width / width;
        entry["height"] = box.height / height;
        entry["confidence"] = 1;
        list.push_back(std::move(entry));
    };
    for (const platform::TextLine& line : (*read).lines) {
        if (!words) {
            add(line.text, line.box);
            continue;
        }
        for (const platform::TextWord& word : line.words) {
            add(word.text, word.box);
        }
    }
    reply.json = list.dump(-1, ' ', false, Json::error_handler_t::replace);
    return reply;
}

ExtensionVisionEngine::ExtensionVisionEngine(ExtensionVisionEngineInfo declared, const NativeExtensions& native)
    : m_declared(std::move(declared))
    , m_native(native)
{
}

VisionEngineInfo ExtensionVisionEngine::info() const
{
    return {.id = m_declared.extension + u'/' + m_declared.name,
            .title = m_declared.title,
            .kinds = m_declared.kinds,
            .languages = m_declared.languages};
}

VisionReply ExtensionVisionEngine::recognize(const VisionPicture& picture, const VisionAsk& ask)
{
    const media::LumaSnapshot& shot = *picture.shot;
    const vision::Rect area = pixelArea(picture.region, shot.width, shot.height);
    VisionReply reply{.json = "[]", .area = ratiosOf(area, shot), .problem = {}};
    if (area.empty()) {
        return reply;
    }
    reply.json.clear();
    if (area.width > static_cast<int>(exthost::kMostImageSide) || area.height > static_cast<int>(exthost::kMostImageSide)) {
        reply.problem = QStringLiteral("GS-API-172 the part of the picture is larger than %1 pixels on a side: pass a "
                                       "smaller region")
                            .arg(exthost::kMostImageSide);
        return reply;
    }
    const exthost::VisionImage image = bgraOf(shot, area);
    if (!exthost::validImage(image)) {
        reply.problem = QStringLiteral("GS-API-173 the picture could not be read");
        return reply;
    }
    const exthost::CallResult result =
        m_native.recognize(toUtf8(m_declared.extension), toUtf8(m_declared.name), ask.kind, image, ask.params);
    if (result.ok()) {
        reply.json = result.value.text;
    } else {
        reply.problem = problemOf(result.status, result.problem);
    }
    return reply;
}

VisionReply visionResult(exthost::VisionKind kind, const VisionReply& reply)
{
    if (!reply.problem.isEmpty()) {
        return reply;
    }
    const auto bad = [](const QString& why) {
        return VisionReply{.json = {}, .area = {}, .problem = QStringLiteral("GS-API-173 the engine's result is not "
                                                                            "valid: ") + why};
    };
    const nlohmann::json list = nlohmann::json::parse(reply.json, nullptr, false);
    if (!list.is_array()) {
        return bad(QStringLiteral("it is not a list"));
    }
    if (list.size() > VisionEngines::kMostResults) {
        return bad(QStringLiteral("it has more than %1 entries").arg(VisionEngines::kMostResults));
    }
    const bool text = kind == exthost::VisionKind::Ocr;
    const char* const named = text ? "text" : "label";
    const std::size_t most = text ? VisionEngines::kMostTextBytes : VisionEngines::kMostLabelBytes;
    const script::Region& area = reply.area;
    Json made = Json::array();
    for (std::size_t index = 0; index < list.size(); ++index) {
        const nlohmann::json& item = list[index];
        const QString which = QStringLiteral("entry %1 ").arg(index + 1);
        if (!item.is_object()) {
            return bad(which + QStringLiteral("is not an object"));
        }
        const auto said = item.find(named);
        if (said == item.end() || !said->is_string() || said->get_ref<const std::string&>().size() > most) {
            return bad(which + QStringLiteral("has no %1 of at most %2 bytes").arg(QLatin1String(named)).arg(most));
        }
        Json entry = Json::object();
        entry[named] = said->get<std::string>();
        if (kind != exthost::VisionKind::Classify) {
            constexpr std::array<const char*, 4> kBox{"x", "y", "width", "height"};
            std::array<double, 4> box{};
            for (std::size_t field = 0; field < kBox.size(); ++field) {
                const auto value = item.find(kBox[field]);
                if (value == item.end() || !value->is_number()) {
                    return bad(which + QStringLiteral("has no number %1").arg(QLatin1String(kBox[field])));
                }
                box[field] = std::clamp(value->get<double>(), 0.0, 1.0);
            }
            entry["x"] = area.x + box[0] * area.width;
            entry["y"] = area.y + box[1] * area.height;
            entry["width"] = box[2] * area.width;
            entry["height"] = box[3] * area.height;
        }
        const auto confidence = item.find("confidence");
        if (confidence == item.end() || !confidence->is_number()) {
            return bad(which + QStringLiteral("has no number confidence"));
        }
        entry["confidence"] = std::clamp(confidence->get<double>(), 0.0, 1.0);
        made.push_back(std::move(entry));
    }
    return {.json = made.dump(-1, ' ', false, Json::error_handler_t::replace), .area = area, .problem = {}};
}

struct VisionEngines::Shared {
    mutable std::mutex mutex;
    VisionEngines* owner = nullptr;
    std::vector<VisionEngineInfo> engines;

    void post(std::function<void(VisionEngines&)> work) const
    {
        const std::scoped_lock lock(mutex);
        if (owner == nullptr) {
            return;
        }
        VisionEngines* target = owner;
        QMetaObject::invokeMethod(target, [target, work = std::move(work)] { work(*target); }, Qt::QueuedConnection);
    }
};

class VisionEngines::Worker
{
public:
    struct Job {
        Key key;
        std::shared_ptr<VisionEngine> engine;
        VisionPicture picture;
        VisionAsk ask;
    };

    explicit Worker(std::shared_ptr<Shared> shared)
        : m_shared(std::move(shared))
        , m_thread([this] { run(); })
    {
    }

    ~Worker()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_all();
    }

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void push(Job job)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_jobs.push_back(std::move(job));
        }
        m_wake.notify_one();
    }

    void forget(const std::function<bool(const Key&)>& matches)
    {
        const std::scoped_lock lock(m_mutex);
        std::erase_if(m_jobs, [&matches](const Job& job) { return matches(job.key); });
    }

private:
    void run()
    {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
                if (m_stopping) {
                    return;
                }
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            const Key key = job.key;
            VisionReply reply = visionResult(job.ask.kind, job.engine->recognize(job.picture, job.ask));
            job = {};
            m_shared->post([key, reply = std::move(reply)](VisionEngines& self) { self.finished(key, reply); });
        }
    }

    std::shared_ptr<Shared> m_shared;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Job> m_jobs;
    bool m_stopping = false;
    std::jthread m_thread;
};

VisionEngines::VisionEngines(QObject* parent)
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
    };
    m_frames = {
        .grab = [](const LogicCaller& caller, quint64 request, bool color, LogicServices::FrameTaken taken) {
            return LogicServices::instance().grab(caller.logic, request, color, std::move(taken));
        },
        .ungrab = [](const LogicCaller& caller,
                     quint64 request) { LogicServices::instance().ungrab(caller.logic, request); },
    };
}

VisionEngines::~VisionEngines()
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = nullptr;
    }
    if (m_offered) {
        for (const std::string_view name : {kRecognize, kEngines}) {
            LogicServices::instance().withdraw(name);
        }
    }
    m_workers.clear();
    m_pending.clear();
    m_engines.clear();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void VisionEngines::offerServices()
{
    LogicServices& services = LogicServices::instance();
    services.offerCall(std::string(kEngines), [shared = m_shared](const LogicCaller&, std::string_view) {
        return callWith(shared, kEngines);
    });
    services.offerRequest(
        std::string(kRecognize),
        [this](const LogicCaller& caller, quint64 id, const QString& json) { request(caller, kRecognize, id, json); },
        [this](const LogicCaller& caller, quint64 id) { dropRequest(caller, id); });
    connect(&services, &LogicServices::logicEnded, this, [this](quint64 logic) { endLogic(logic); });
    m_offered = true;
}

void VisionEngines::setReplies(VisionReplies replies)
{
    m_replies = std::move(replies);
}

void VisionEngines::setFrames(VisionFrames frames)
{
    m_frames = std::move(frames);
}

void VisionEngines::setPermitted(const QStringList& extensions)
{
    m_permitted = std::set<QString>(extensions.begin(), extensions.end());
}

void VisionEngines::addEngine(std::shared_ptr<VisionEngine> engine)
{
    const QString id = engine->info().id;
    removeEngine(id);
    m_engines.emplace(id, std::move(engine));
    publish();
}

void VisionEngines::removeEngine(const QString& id)
{
    const auto found = m_engines.find(id);
    if (found == m_engines.end()) {
        return;
    }
    m_engines.erase(found);
    std::vector<Key> gone;
    for (const auto& [key, pending] : m_pending) {
        if (pending.engine == id) {
            gone.push_back(key);
        }
    }
    for (const Key& key : gone) {
        forget(key);
        if (m_replies.refuse) {
            m_replies.refuse(key.first, key.second, QStringLiteral("GS-API-170 the recognition engine %1 went away").arg(id));
        }
    }
    publish();
}

void VisionEngines::follow(ExtensionHost& extensions, const NativeExtensions& native)
{
    const auto update = [this, &extensions, &native] {
        QStringList permitted;
        std::map<QString, ExtensionVisionEngineInfo> wanted;
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
            for (const ext::NativeVisionEngine& engine : manifest.contributes.native->visionEngines) {
                QStringList kinds;
                for (const std::string& kind : engine.kinds) {
                    kinds.append(QString::fromStdString(kind));
                }
                QStringList languages;
                for (const std::string& language : engine.languages) {
                    languages.append(QString::fromStdString(language));
                }
                ExtensionVisionEngineInfo declared{
                    .extension = id,
                    .name = QString::fromStdString(engine.name),
                    .title = extensions.textOf(manifest, "visionEngines." + engine.name + ".title", engine.title),
                    .kinds = kinds,
                    .languages = languages};
                wanted.emplace(id + u'/' + declared.name, std::move(declared));
            }
        }
        std::vector<QString> stale;
        for (const auto& [id, engine] : m_engines) {
            const auto* provided = dynamic_cast<const ExtensionVisionEngine*>(engine.get());
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
                addEngine(std::make_shared<ExtensionVisionEngine>(std::move(declared), native));
            }
        }
        setPermitted(permitted);
    };
    update();
    connect(&extensions, &ExtensionHost::extensionsChanged, this, update);
}

void VisionEngines::request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json)
{
    const Key key{caller.logic, id};
    if (service != kRecognize) {
        if (m_replies.refuse) {
            m_replies.refuse(caller.logic, id,
                             QStringLiteral("GS-API-070 no part of GameScrcpy offers the service %1").arg(fromView(service)));
        }
        return;
    }
    QString problem;
    if (!caller.extension.isEmpty() && !m_permitted.contains(caller.extension)) {
        problem = QString::fromStdString(script::permissionProblem(kPermission));
    } else if (caller.device.isEmpty()) {
        problem = QString::fromStdString(script::phoneProblem());
    }
    std::optional<Asked> asked;
    if (problem.isEmpty()) {
        asked = askedOf(json, problem);
    }
    const auto found = asked ? m_engines.find(asked->engine) : m_engines.end();
    if (asked && found == m_engines.end()) {
        problem = QStringLiteral("GS-API-170 there is no recognition engine %1; vision.engines() lists them")
                      .arg(asked->engine);
    } else if (asked) {
        const QString kind = fromView(exthost::visionKindName(asked->ask.kind));
        const auto waiting = std::ranges::count_if(
            m_pending, [&asked](const auto& entry) { return entry.second.engine == asked->engine; });
        if (!found->second->info().kinds.contains(kind)) {
            problem = QStringLiteral("GS-API-171 the engine %1 does not recognize %2").arg(asked->engine, kind);
        } else if (static_cast<std::size_t>(waiting) >= kMostWaiting) {
            problem = QStringLiteral("GS-API-174 the engine %1 has %2 pictures waiting already")
                          .arg(asked->engine)
                          .arg(kMostWaiting);
        }
    }
    if (!problem.isEmpty()) {
        if (m_replies.refuse) {
            m_replies.refuse(caller.logic, id, problem);
        }
        return;
    }
    m_pending.insert_or_assign(key, Pending{.caller = caller,
                                            .engine = asked->engine,
                                            .ask = asked->ask,
                                            .region = asked->region,
                                            .stage = Stage::Framing});
    const bool color = asked->engine != fromView(kSystem);
    const bool grabbing =
        m_frames.grab && m_frames.grab(caller, id, color, [shared = m_shared, key](std::shared_ptr<const media::LumaSnapshot> shot) {
            shared->post([key, shot = std::move(shot)](VisionEngines& self) { self.framed(key, shot); });
        });
    if (!grabbing) {
        answer(key, QStringLiteral("null"));
    }
}

void VisionEngines::dropRequest(const LogicCaller& caller, quint64 id)
{
    forget(Key{caller.logic, id});
}

script::ServiceAnswer VisionEngines::call(const LogicCaller&, std::string_view service, std::string_view)
{
    return callWith(m_shared, service);
}

script::ServiceAnswer VisionEngines::callWith(const std::shared_ptr<Shared>& shared, std::string_view service)
{
    if (service != kEngines) {
        return {"null", "GS-API-070", "no part of GameScrcpy answers the service " + std::string(service) + " at once"};
    }
    Json list = Json::array();
    {
        const std::scoped_lock lock(shared->mutex);
        for (const VisionEngineInfo& info : shared->engines) {
            Json kinds = Json::array();
            for (const QString& kind : info.kinds) {
                kinds.push_back(toUtf8(kind));
            }
            Json languages = Json::array();
            for (const QString& language : info.languages) {
                languages.push_back(toUtf8(language));
            }
            Json entry = Json::object();
            entry["id"] = toUtf8(info.id);
            entry["title"] = toUtf8(info.title);
            entry["kinds"] = std::move(kinds);
            entry["languages"] = std::move(languages);
            list.push_back(std::move(entry));
        }
    }
    return {list.dump(-1, ' ', false, Json::error_handler_t::replace), {}, {}};
}

void VisionEngines::endLogic(std::uint64_t logic)
{
    std::vector<Key> gone;
    for (const auto& [key, pending] : m_pending) {
        if (key.first == logic) {
            gone.push_back(key);
        }
    }
    for (const Key& key : gone) {
        forget(key);
    }
}

std::vector<VisionEngineInfo> VisionEngines::engines() const
{
    const std::scoped_lock lock(m_shared->mutex);
    return m_shared->engines;
}

void VisionEngines::framed(const Key& key, std::shared_ptr<const media::LumaSnapshot> shot)
{
    const auto found = m_pending.find(key);
    if (found == m_pending.end() || found->second.stage != Stage::Framing) {
        return;
    }
    if (shot == nullptr) {
        answer(key, QStringLiteral("null"));
        return;
    }
    const auto engine = m_engines.find(found->second.engine);
    if (engine == m_engines.end()) {
        refuse(key, QStringLiteral("GS-API-170 the recognition engine %1 went away").arg(found->second.engine));
        return;
    }
    found->second.stage = Stage::Queued;
    workerFor(found->second.engine)
        .push(Worker::Job{.key = key,
                          .engine = engine->second,
                          .picture = {.shot = std::move(shot), .region = found->second.region},
                          .ask = found->second.ask});
}

void VisionEngines::finished(const Key& key, const VisionReply& reply)
{
    if (!m_pending.contains(key)) {
        return;
    }
    if (!reply.problem.isEmpty()) {
        refuse(key, reply.problem);
    } else {
        answer(key, fromUtf8(reply.json));
    }
}

void VisionEngines::forget(const Key& key)
{
    const auto found = m_pending.find(key);
    if (found == m_pending.end()) {
        return;
    }
    const Pending gone = std::move(found->second);
    m_pending.erase(found);
    if (gone.stage == Stage::Framing) {
        if (m_frames.ungrab) {
            m_frames.ungrab(gone.caller, key.second);
        }
        return;
    }
    if (const auto worker = m_workers.find(gone.engine); worker != m_workers.end()) {
        worker->second->forget([&key](const Key& each) { return each == key; });
    }
}

void VisionEngines::answer(const Key& key, const QString& json)
{
    m_pending.erase(key);
    if (m_replies.answer) {
        m_replies.answer(key.first, key.second, json);
    }
}

void VisionEngines::refuse(const Key& key, const QString& message)
{
    m_pending.erase(key);
    if (m_replies.refuse) {
        m_replies.refuse(key.first, key.second, message);
    }
}

void VisionEngines::publish()
{
    std::vector<VisionEngineInfo> list;
    for (const auto& [id, engine] : m_engines) {
        if (id == fromView(kSystem)) {
            list.insert(list.begin(), engine->info());
        } else {
            list.push_back(engine->info());
        }
    }
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->engines = std::move(list);
    }
    emit enginesChanged();
}

VisionEngines::Worker& VisionEngines::workerFor(const QString& engine)
{
    auto found = m_workers.find(engine);
    if (found == m_workers.end()) {
        found = m_workers.emplace(engine, std::make_unique<Worker>(m_shared)).first;
    }
    return *found->second;
}

}
