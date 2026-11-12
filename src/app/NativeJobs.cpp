#include "NativeJobs.h"

#include "NativeExtensions.h"

#include <gs/exthost/Module.h>

#include <nlohmann/json.hpp>

#include <QMetaObject>

#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace gs::app {

namespace {

constexpr std::string_view kStart = "native.jobStart";
constexpr std::string_view kCancel = "native.jobCancel";
constexpr std::string_view kResult = "native.jobResult";

bool numeric(const nlohmann::json& value, std::string_view key)
{
    const auto found = value.find(std::string(key));
    return found != value.end() && found->is_number();
}

bool fits(std::string_view type, const nlohmann::json& value)
{
    if (type == "boolean") {
        return value.is_boolean();
    }
    if (type == "number" || type == "duration") {
        return value.is_number();
    }
    if (type == "string") {
        return value.is_string();
    }
    if (type == "color") {
        return value.is_string() && exthost::validColor(value.get_ref<const std::string&>());
    }
    if (type == "point") {
        return value.is_object() && numeric(value, "x") && numeric(value, "y");
    }
    if (type == "region") {
        return value.is_object() && numeric(value, "x") && numeric(value, "y") && numeric(value, "width") &&
               numeric(value, "height");
    }
    return true;
}

std::string argumentProblem(const ext::NativeJob& declared, const nlohmann::json& args)
{
    if (!args.is_object()) {
        return "the arguments have to be an object with a value for each parameter";
    }
    for (const auto& [key, value] : args.items()) {
        if (std::ranges::find(declared.params, key, &ext::NativeParam::name) == declared.params.end()) {
            return "it takes no " + key;
        }
    }
    for (const ext::NativeParam& param : declared.params) {
        const auto found = args.find(param.name);
        if (found == args.end()) {
            return "it needs " + param.name;
        }
        if (!fits(param.type, *found)) {
            return param.name + " has to be a " + param.type;
        }
    }
    return {};
}

std::pair<bool, std::string> checkedResult(const std::string& returns, bool worked, std::string text)
{
    if (!worked) {
        return {false, std::move(text)};
    }
    if (!exthost::jsonDepthWithin(text, exthost::kMostJsonDepth)) {
        return {false, "the job gave a result nested more than 64 levels deep"};
    }
    const nlohmann::json value = nlohmann::json::parse(text, nullptr, false);
    if (value.is_discarded()) {
        return {false, "the job gave a result that is not JSON"};
    }
    if (returns.empty()) {
        return {true, "null"};
    }
    if (!fits(returns, value)) {
        return {false, "the job gave a result that is not a " + returns};
    }
    return {true, value.dump()};
}

std::optional<std::uint64_t> jobIdOf(std::string_view json)
{
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
    if (!parsed.is_object() || !parsed.contains("id") || !parsed["id"].is_string()) {
        return std::nullopt;
    }
    const std::string& text = parsed["id"].get_ref<const std::string&>();
    std::uint64_t id = 0;
    for (const char c : text) {
        if (c < '0' || c > '9' || id > (UINT64_MAX - 9) / 10) {
            return std::nullopt;
        }
        id = id * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return text.empty() ? std::nullopt : std::optional<std::uint64_t>(id);
}

using RunningJobs = std::vector<std::pair<std::weak_ptr<exthost::Module>, std::uint64_t>>;

void cancelAll(const RunningJobs& running)
{
    for (const auto& [weak, job] : running) {
        if (const std::shared_ptr<exthost::Module> module = weak.lock()) {
            module->cancelJob(job);
        }
    }
}

}

struct NativeJobs::Shared {
    struct Job {
        std::uint64_t logic = 0;
        std::string extension;
        std::string name;
        std::weak_ptr<exthost::Module> module;
        std::uint64_t running = 0;
        std::optional<quint64> waiting;
        std::optional<std::pair<bool, std::string>> ended;
        bool cancelled = false;
        std::optional<HostJobEvents> host;
    };

    void post(std::function<void(NativeJobs&)> work)
    {
        const std::scoped_lock lock(mutex);
        if (owner == nullptr) {
            return;
        }
        NativeJobs* target = owner;
        QMetaObject::invokeMethod(target, [target, work = std::move(work)] { work(*target); }, Qt::QueuedConnection);
    }

    mutable std::mutex mutex;
    NativeJobs* owner = nullptr;
    std::map<std::uint64_t, Job> jobs;
    std::uint64_t next = 0;
};

NativeJobs::NativeJobs(const NativeExtensions& extensions, QObject* parent)
    : QObject(parent)
    , m_extensions(extensions)
    , m_shared(std::make_shared<Shared>())
{
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

NativeJobs::~NativeJobs()
{
    RunningJobs running;
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = nullptr;
        for (const auto& [id, job] : m_shared->jobs) {
            if (job.running != 0) {
                running.emplace_back(job.module, job.running);
            }
        }
        m_shared->jobs.clear();
    }
    cancelAll(running);
    if (m_offered) {
        for (const std::string_view name : {kStart, kCancel, kResult}) {
            LogicServices::instance().withdraw(name);
        }
    }
}

void NativeJobs::offerServices()
{
    LogicServices& services = LogicServices::instance();
    services.offerCall(std::string(kStart),
                       [this](const LogicCaller& caller, std::string_view json) { return start(caller, json); });
    services.offerCall(std::string(kCancel),
                       [this](const LogicCaller& caller, std::string_view json) { return cancel(caller, json); });
    services.offerRequest(
        std::string(kResult),
        [this](const LogicCaller& caller, quint64 request, const QString& json) { result(caller, request, json); },
        [this](const LogicCaller& caller, quint64 request) { dropResult(caller, request); });
    connect(&services, &LogicServices::logicEnded, this, [this](quint64 logic) { endLogic(logic); });
    m_offered = true;
}

void NativeJobs::setReplies(JobReplies replies)
{
    m_replies = std::move(replies);
}

script::ServiceAnswer NativeJobs::start(const LogicCaller& caller, std::string_view json)
{
    return begin(caller.logic, json, std::nullopt);
}

script::ServiceAnswer NativeJobs::startForHost(std::string_view json, HostJobEvents events)
{
    return begin(0, json, std::move(events));
}

void NativeJobs::cancelForHost(std::uint64_t id)
{
    std::shared_ptr<exthost::Module> module;
    std::uint64_t running = 0;
    {
        const std::scoped_lock lock(m_shared->mutex);
        const auto found = m_shared->jobs.find(id);
        if (found == m_shared->jobs.end() || !found->second.host || found->second.ended) {
            return;
        }
        found->second.cancelled = true;
        module = found->second.module.lock();
        running = found->second.running;
    }
    if (module != nullptr && running != 0) {
        module->cancelJob(running);
    }
}

script::ServiceAnswer NativeJobs::begin(std::uint64_t logic, std::string_view json, std::optional<HostJobEvents> host)
{
    const nlohmann::json request = nlohmann::json::parse(json, nullptr, false);
    if (!request.is_object() || !request.contains("extension") || !request["extension"].is_string() ||
        !request.contains("job") || !request["job"].is_string()) {
        return {.json = "null", .code = "GS-API-061", .problem = "a job is started with its extension and its name"};
    }
    const std::string extension = request["extension"].get<std::string>();
    const std::string name = request["job"].get<std::string>();
    const NativeJobTarget target = m_extensions.jobTarget(extension, name);
    if (!target.code.empty()) {
        return {.json = "null", .code = target.code, .problem = target.problem};
    }
    const nlohmann::json args = request.contains("args") ? request["args"] : nlohmann::json::object();
    if (const std::string problem = argumentProblem(target.declared, args); !problem.empty()) {
        return {.json = "null", .code = "GS-API-061", .problem = problem};
    }
    const std::int64_t most = target.declared.most.value_or(ext::kMostRunningNativeJobs);
    std::uint64_t id = 0;
    {
        const std::scoped_lock lock(m_shared->mutex);
        const auto same = std::ranges::count_if(m_shared->jobs, [&](const auto& entry) {
            return entry.second.extension == extension && entry.second.name == name && !entry.second.ended;
        });
        if (same >= most) {
            return {.json = "null",
                    .code = "GS-API-067",
                    .problem = target.extensionName + " runs " + std::to_string(most) + " of this job at most"};
        }
        id = ++m_shared->next;
        m_shared->jobs.emplace(id, Shared::Job{.logic = logic,
                                               .extension = extension,
                                               .name = name,
                                               .module = target.module,
                                               .running = 0,
                                               .waiting = std::nullopt,
                                               .ended = std::nullopt,
                                               .cancelled = false,
                                               .host = std::move(host)});
    }
    const std::weak_ptr<Shared> weak = m_shared;
    exthost::JobEvents events{
        .progress =
            [weak, id](double fraction, std::string text) {
                if (const std::shared_ptr<Shared> shared = weak.lock()) {
                    shared->post([id, fraction, text = std::move(text)](NativeJobs& self) {
                        self.progressed(id, fraction, text);
                    });
                }
            },
        .log =
            [weak, id](int level, std::string text) {
                if (const std::shared_ptr<Shared> shared = weak.lock()) {
                    shared->post(
                        [id, level, text = std::move(text)](NativeJobs& self) { self.logged(id, level, text); });
                }
            },
        .done =
            [weak, id, returns = target.declared.returns](bool worked, std::string text) {
                if (const std::shared_ptr<Shared> shared = weak.lock()) {
                    auto [fine, said] = checkedResult(returns, worked, std::move(text));
                    shared->post([id, fine, said = std::move(said)](NativeJobs& self) { self.ended(id, fine, said); });
                }
            },
        .output =
            [weak, id](std::string json) {
                if (const std::shared_ptr<Shared> shared = weak.lock()) {
                    shared->post([id, json = std::move(json)](NativeJobs& self) { self.outputted(id, json); });
                }
            }};
    const std::uint64_t running = target.module->startJob(target.index, args.dump(), std::move(events));
    const std::scoped_lock lock(m_shared->mutex);
    const auto found = m_shared->jobs.find(id);
    if (running == 0) {
        if (found != m_shared->jobs.end()) {
            m_shared->jobs.erase(found);
        }
        return {.json = "null",
                .code = "GS-API-067",
                .problem = target.extensionName + " runs as many of its jobs as it may; wait for one to end"};
    }
    if (found != m_shared->jobs.end()) {
        found->second.running = running;
    }
    return {.json = nlohmann::json{{"id", std::to_string(id)}}.dump(), .code = {}, .problem = {}};
}

script::ServiceAnswer NativeJobs::cancel(const LogicCaller& caller, std::string_view json)
{
    const std::optional<std::uint64_t> id = jobIdOf(json);
    std::shared_ptr<exthost::Module> module;
    std::uint64_t running = 0;
    {
        const std::scoped_lock lock(m_shared->mutex);
        const auto found = id ? m_shared->jobs.find(*id) : m_shared->jobs.end();
        if (found == m_shared->jobs.end() || found->second.logic != caller.logic || found->second.host ||
            found->second.ended) {
            return {};
        }
        found->second.cancelled = true;
        module = found->second.module.lock();
        running = found->second.running;
    }
    if (module != nullptr && running != 0) {
        module->cancelJob(running);
    }
    return {};
}

void NativeJobs::result(const LogicCaller& caller, quint64 request, const QString& json)
{
    const std::optional<std::uint64_t> id = jobIdOf(json.toStdString());
    std::optional<std::pair<bool, std::string>> ended;
    bool cancelled = false;
    {
        const std::scoped_lock lock(m_shared->mutex);
        const auto found = id ? m_shared->jobs.find(*id) : m_shared->jobs.end();
        if (found == m_shared->jobs.end() || found->second.logic != caller.logic || found->second.host) {
            if (m_replies.refuse) {
                m_replies.refuse(caller.logic, request, QStringLiteral("GS-API-060 there is no such job"));
            }
            return;
        }
        if (!found->second.ended) {
            found->second.waiting = request;
            return;
        }
        ended = std::move(found->second.ended);
        cancelled = found->second.cancelled;
        m_shared->jobs.erase(found);
    }
    if (ended->first) {
        if (m_replies.answer) {
            m_replies.answer(caller.logic, request, QString::fromStdString(ended->second));
        }
    } else if (m_replies.refuse) {
        m_replies.refuse(caller.logic, request,
                         QString::fromStdString((cancelled ? "GS-API-066 " : "GS-API-065 ") + ended->second));
    }
}

void NativeJobs::dropResult(const LogicCaller& caller, quint64 request)
{
    std::shared_ptr<exthost::Module> module;
    std::uint64_t running = 0;
    {
        const std::scoped_lock lock(m_shared->mutex);
        const auto found = std::ranges::find_if(m_shared->jobs, [&](const auto& entry) {
            return entry.second.logic == caller.logic && entry.second.waiting == request;
        });
        if (found == m_shared->jobs.end()) {
            return;
        }
        module = found->second.module.lock();
        running = found->second.running;
        m_shared->jobs.erase(found);
    }
    if (module != nullptr && running != 0) {
        module->cancelJob(running);
    }
}

void NativeJobs::endLogic(std::uint64_t logic)
{
    RunningJobs running;
    {
        const std::scoped_lock lock(m_shared->mutex);
        for (auto entry = m_shared->jobs.begin(); entry != m_shared->jobs.end();) {
            if (entry->second.logic != logic || entry->second.host) {
                ++entry;
                continue;
            }
            if (entry->second.running != 0 && !entry->second.ended) {
                running.emplace_back(entry->second.module, entry->second.running);
            }
            entry = m_shared->jobs.erase(entry);
        }
    }
    cancelAll(running);
}

std::size_t NativeJobs::running() const
{
    const std::scoped_lock lock(m_shared->mutex);
    return m_shared->jobs.size();
}

void NativeJobs::progressed(std::uint64_t id, double fraction, const std::string& text)
{
    std::uint64_t logic = 0;
    bool forHost = false;
    std::function<void(double, const QString&)> progress;
    {
        const std::scoped_lock lock(m_shared->mutex);
        const auto found = m_shared->jobs.find(id);
        if (found == m_shared->jobs.end() || found->second.ended) {
            return;
        }
        logic = found->second.logic;
        forHost = found->second.host.has_value();
        if (forHost) {
            progress = found->second.host->progress;
        }
    }
    if (forHost) {
        if (progress) {
            progress(fraction, QString::fromStdString(text));
        }
        return;
    }
    if (m_replies.tell) {
        m_replies.tell(logic, QStringLiteral("job"), QString::number(id),
                       QString::fromStdString(
                           nlohmann::json{{"kind", "progress"}, {"fraction", fraction}, {"text", text}}.dump()));
    }
}

void NativeJobs::logged(std::uint64_t id, int level, const std::string& text)
{
    std::uint64_t logic = 0;
    std::string extension;
    bool forHost = false;
    {
        const std::scoped_lock lock(m_shared->mutex);
        const auto found = m_shared->jobs.find(id);
        if (found == m_shared->jobs.end() || found->second.ended) {
            return;
        }
        logic = found->second.logic;
        extension = found->second.extension;
        forHost = found->second.host.has_value();
    }
    if (forHost) {
        qInfo("[extensions] %s: %s", extension.c_str(), text.c_str());
        return;
    }
    if (m_replies.tell) {
        m_replies.tell(logic, QStringLiteral("job"), QString::number(id),
                       QString::fromStdString(nlohmann::json{{"kind", "log"}, {"level", level}, {"text", text}}.dump()));
    }
}

void NativeJobs::outputted(std::uint64_t id, const std::string& json)
{
    if (!exthost::jsonDepthWithin(json, exthost::kMostJsonDepth) || !nlohmann::json::accept(json)) {
        return;
    }
    std::uint64_t logic = 0;
    std::function<void(const QString&)> output;
    bool forHost = false;
    {
        const std::scoped_lock lock(m_shared->mutex);
        const auto found = m_shared->jobs.find(id);
        if (found == m_shared->jobs.end() || found->second.ended) {
            return;
        }
        logic = found->second.logic;
        forHost = found->second.host.has_value();
        if (forHost) {
            output = found->second.host->output;
        }
    }
    if (forHost) {
        if (output) {
            output(QString::fromStdString(json));
        }
        return;
    }
    if (m_replies.tell) {
        m_replies.tell(logic, QStringLiteral("job"), QString::number(id),
                       QString::fromStdString(R"({"kind":"output","value":)" + json + '}'));
    }
}

void NativeJobs::ended(std::uint64_t id, bool worked, const std::string& text)
{
    std::uint64_t logic = 0;
    std::optional<quint64> waiting;
    bool cancelled = false;
    std::optional<HostJobEvents> host;
    {
        const std::scoped_lock lock(m_shared->mutex);
        const auto found = m_shared->jobs.find(id);
        if (found == m_shared->jobs.end()) {
            return;
        }
        if (found->second.host) {
            host = std::move(found->second.host);
            m_shared->jobs.erase(found);
        } else if (!found->second.waiting) {
            found->second.ended.emplace(worked, text);
            return;
        } else {
            logic = found->second.logic;
            waiting = found->second.waiting;
            cancelled = found->second.cancelled;
            m_shared->jobs.erase(found);
        }
    }
    if (host) {
        if (host->done) {
            host->done(worked, QString::fromStdString(text));
        }
        return;
    }
    if (worked) {
        if (m_replies.answer) {
            m_replies.answer(logic, *waiting, QString::fromStdString(text));
        }
    } else if (m_replies.refuse) {
        m_replies.refuse(logic, *waiting, QString::fromStdString((cancelled ? "GS-API-066 " : "GS-API-065 ") + text));
    }
}

}
