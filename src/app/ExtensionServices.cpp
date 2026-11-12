#include "ExtensionServices.h"

#include "ExtensionHost.h"
#include "NativeExtensions.h"

#include <gs/ext/Manifest.h>
#include <gs/ext/Schema.h>

#include <nlohmann/json.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <condition_variable>
#include <utility>

namespace gs::app {

namespace {

using Json = nlohmann::json;

constexpr std::string_view kCall = "ext.call";
constexpr std::string_view kProvide = "ext.provide";
constexpr std::string_view kWithdraw = "ext.withdraw";
constexpr std::string_view kAnswer = "ext.answer";
constexpr std::string_view kFail = "ext.fail";
constexpr std::string_view kNotProvided = "GS-API-145";
constexpr std::string_view kMissing = "GS-API-146";
constexpr std::string_view kNotDependency = "GS-API-147";
constexpr std::string_view kMismatch = "GS-API-148";
constexpr std::string_view kFailed = "GS-API-149";
constexpr int kSweepMs = 250;
constexpr std::size_t kMostMessageBytes = 1000;
constexpr std::chrono::seconds kNativeMargin{2};

std::string jsonString(const std::string& text)
{
    return Json(text).dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::string listed(const std::vector<std::string>& names)
{
    std::string out = "[";
    for (const std::string& name : names) {
        out += (out.size() > 1 ? "," : "") + jsonString(name);
    }
    return out + "]";
}

std::string textIn(const Json& body, const char* key)
{
    const auto found = body.find(key);
    return found != body.end() && found->is_string() ? found->get<std::string>() : std::string();
}

std::optional<quint64> callIn(const Json& body)
{
    const auto found = body.find("call");
    if (found == body.end()) {
        return std::nullopt;
    }
    if (found->is_number_unsigned()) {
        return found->get<std::uint64_t>();
    }
    if (!found->is_string()) {
        return std::nullopt;
    }
    bool number = false;
    const quint64 id = QString::fromStdString(found->get<std::string>()).toULongLong(&number);
    return number ? std::optional<quint64>(id) : std::nullopt;
}

exthost::ServiceStatus statusOf(std::string_view code)
{
    if (code.empty()) {
        return exthost::ServiceStatus::Ok;
    }
    if (code == kMissing) {
        return exthost::ServiceStatus::Unsupported;
    }
    if (code == kNotDependency) {
        return exthost::ServiceStatus::Denied;
    }
    if (code == kMismatch) {
        return exthost::ServiceStatus::BadArgument;
    }
    return exthost::ServiceStatus::Failed;
}

std::string shortened(std::string text)
{
    if (text.size() <= kMostMessageBytes) {
        return text;
    }
    std::size_t end = kMostMessageBytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    text.resize(end);
    return text;
}

script::ServiceAnswer accepted()
{
    return {"null", {}, {}};
}

void keepOnly(std::vector<std::string>& permissions, const std::vector<std::string>& allowed)
{
    std::erase_if(permissions, [&allowed](const std::string& permission) {
        return std::ranges::find(allowed, permission) == allowed.end();
    });
}

}

struct ExtensionServices::Shared {
    mutable std::mutex mutex;
    ExtensionServices* owner = nullptr;
    std::vector<ServiceExtension> extensions;
    std::map<std::pair<std::string, std::string>, std::uint64_t> provided;
    std::map<std::uint64_t, Done> natives;
    std::uint64_t lastNative = 0;

    void post(std::function<void(ExtensionServices&)> work) const
    {
        const std::scoped_lock lock(mutex);
        if (owner == nullptr) {
            return;
        }
        ExtensionServices* target = owner;
        QMetaObject::invokeMethod(target, [target, work = std::move(work)] { work(*target); }, Qt::QueuedConnection);
    }

    [[nodiscard]] const ServiceExtension* find(std::string_view id) const
    {
        const auto found = std::ranges::find(extensions, id, &ServiceExtension::id);
        return found != extensions.end() ? &*found : nullptr;
    }

    [[nodiscard]] const OfferedService* offered(std::string_view id, std::string_view name) const
    {
        const ServiceExtension* extension = find(id);
        if (extension == nullptr) {
            return nullptr;
        }
        const auto found = std::ranges::find(extension->services, name, &OfferedService::name);
        return found != extension->services.end() ? &*found : nullptr;
    }
};

struct ExtensionServices::Pending {
    std::uint64_t logic = 0;
    quint64 request = 0;
    std::uint64_t native = 0;
    std::string caller;
    std::string extension;
    std::string name;
    std::string target;
    std::string output;
    std::string args;
    std::vector<std::string> permissions;
    std::uint64_t provider = 0;
    std::chrono::steady_clock::time_point deadline{};
};

ExtensionServices::ExtensionServices(QObject* parent)
    : QObject(parent)
    , m_shared(std::make_shared<Shared>())
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = this;
    }
    {
        const std::scoped_lock lock(s_instanceMutex);
        if (s_instance == nullptr) {
            s_instance = this;
        }
    }
    m_replies = {
        .answer = [](std::uint64_t logic, quint64 request,
                     const QString& json) { static_cast<void>(LogicServices::instance().answer(logic, request, json)); },
        .refuse = [](std::uint64_t logic, quint64 request,
                     const QString& message) { static_cast<void>(LogicServices::instance().refuse(logic, request, message)); },
        .tell = [](std::uint64_t logic, const QString& kind, const QString& id,
                   const QString& json) { static_cast<void>(LogicServices::instance().tell(logic, kind, id, json)); },
        .node = [](std::string_view extension, std::string_view node, const std::vector<script::NodeValue>& values) {
            const NativeExtensions* native = NativeExtensions::instance();
            return native != nullptr
                       ? native->call(extension, node, values)
                       : script::NodeAnswer{{}, "GS-API-060", "the extension that adds it is off or did not load"};
        },
    };
    m_sweep.setInterval(kSweepMs);
    connect(&m_sweep, &QTimer::timeout, this, &ExtensionServices::sweep);
}

ExtensionServices::~ExtensionServices()
{
    {
        const std::scoped_lock lock(s_instanceMutex);
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }
    std::map<std::uint64_t, Done> natives;
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = nullptr;
        natives.swap(m_shared->natives);
    }
    for (auto& entry : natives) {
        if (entry.second) {
            entry.second({exthost::ServiceStatus::Failed, std::string(kFailed) + " GameScrcpy is closing"});
        }
    }
    if (m_offered) {
        for (const std::string_view name : {kCall, kProvide, kWithdraw, kAnswer, kFail}) {
            LogicServices::instance().withdraw(name);
        }
    }
}

ExtensionServices* ExtensionServices::instance()
{
    const std::scoped_lock lock(s_instanceMutex);
    return s_instance;
}

void ExtensionServices::offerServices()
{
    LogicServices& services = LogicServices::instance();
    for (const std::string_view name : {kProvide, kWithdraw, kAnswer, kFail}) {
        services.offerCall(std::string(name), [shared = m_shared, name](const LogicCaller& caller, std::string_view json) {
            return callWith(shared, caller, name, json);
        });
    }
    services.offerRequest(
        std::string(kCall),
        [this](const LogicCaller& caller, quint64 id, const QString& json) { request(caller, kCall, id, json); },
        [this](const LogicCaller& caller, quint64 id) { drop(caller, id); });
    connect(&services, &LogicServices::logicEnded, this, [this](quint64 logic) { endLogic(logic); });
    m_offered = true;
}

void ExtensionServices::setReplies(ServiceReplies replies)
{
    m_replies = std::move(replies);
}

void ExtensionServices::setTimeout(std::chrono::milliseconds limit)
{
    m_timeout = limit;
}

void ExtensionServices::setExtensions(std::vector<ServiceExtension> extensions)
{
    std::vector<quint64> gone;
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->extensions = std::move(extensions);
        std::erase_if(m_shared->provided, [this](const auto& entry) {
            return m_shared->offered(entry.first.first, entry.first.second) == nullptr;
        });
        for (const auto& [id, pending] : m_calls) {
            if (m_shared->offered(pending.extension, pending.name) == nullptr) {
                gone.push_back(id);
            }
        }
    }
    for (const quint64 id : gone) {
        const std::string target = m_calls.at(id).target;
        cancelProvider(id);
        finish(id, std::string(kMissing), target + " is no longer offered: the extension that offers it was turned off");
    }
}

void ExtensionServices::follow(ExtensionHost& extensions)
{
    const auto update = [this, &extensions] {
        std::vector<ServiceExtension> next;
        for (const ExtensionHost::Extension& extension : extensions.extensions()) {
            if (!extension.active || !extension.manifest) {
                continue;
            }
            const ext::Manifest& manifest = *extension.manifest;
            ServiceExtension made{.id = manifest.id,
                                  .name = manifest.name,
                                  .dependencies = {},
                                  .granted = extensions.grantedPermissions(manifest.id),
                                  .services = {}};
            for (const auto& dependency : manifest.dependencies) {
                made.dependencies.push_back(dependency.first);
            }
            for (const ext::ServiceContribution& service : manifest.contributes.services) {
                made.services.push_back({.name = service.name,
                                         .script = service.script,
                                         .node = service.node,
                                         .input = service.input,
                                         .output = service.output});
            }
            next.push_back(std::move(made));
        }
        setExtensions(std::move(next));
    };
    update();
    connect(&extensions, &ExtensionHost::extensionsChanged, this, update);
}

void ExtensionServices::request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json)
{
    if (service != kCall) {
        if (m_replies.refuse) {
            m_replies.refuse(caller.logic, id,
                             QStringLiteral("GS-API-070 no part of GameScrcpy offers the service %1")
                                 .arg(QString::fromUtf8(service.data(), static_cast<qsizetype>(service.size()))));
        }
        return;
    }
    const QByteArray bytes = json.toUtf8();
    const Json body = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    std::optional<std::vector<std::string>> limit;
    if (body.is_object()) {
        if (const auto found = body.find("limit"); found != body.end() && found->is_array()) {
            limit.emplace();
            for (const Json& each : *found) {
                if (each.is_string()) {
                    limit->push_back(each.get<std::string>());
                }
            }
        }
    }
    Pending pending;
    pending.logic = caller.logic;
    pending.request = id;
    pending.caller = caller.extension.toStdString();
    const std::string args = body.is_object() && body.contains("args") ? textIn(body, "args") : std::string("null");
    begin(std::move(pending), body.is_object() ? textIn(body, "service") : std::string(), args, limit);
}

void ExtensionServices::drop(const LogicCaller& caller, quint64 id)
{
    const auto found = std::ranges::find_if(m_calls, [&caller, id](const auto& entry) {
        return entry.second.native == 0 && entry.second.logic == caller.logic && entry.second.request == id;
    });
    if (found == m_calls.end()) {
        return;
    }
    const quint64 key = found->first;
    cancelProvider(key);
    m_calls.erase(key);
    if (m_calls.empty()) {
        m_sweep.stop();
    }
}

script::ServiceAnswer ExtensionServices::call(const LogicCaller& caller, std::string_view service,
                                              std::string_view json)
{
    return callWith(m_shared, caller, service, json);
}

script::ServiceAnswer ExtensionServices::callWith(const std::shared_ptr<Shared>& shared, const LogicCaller& caller,
                                                  std::string_view service, std::string_view json)
{
    const Json body = Json::parse(json.begin(), json.end(), nullptr, false);
    const bool readable = body.is_object();
    const std::string extension = caller.extension.toStdString();
    if (service == kProvide) {
        const std::string name = readable ? textIn(body, "name") : std::string();
        const std::string script = caller.script.toStdString();
        std::string problem;
        {
            const std::scoped_lock lock(shared->mutex);
            const OfferedService* offered = shared->offered(extension, name);
            if (extension.empty()) {
                problem = "only the app script of an extension provides services; your own logic can call them with "
                          "ext.call";
            } else if (offered == nullptr) {
                problem = name + " is not a service of " + extension + ": declare it in contributes.services";
            } else if (!offered->node.empty()) {
                problem = name + " is answered by the native node " + offered->node + ", not by logic";
            } else if (offered->script != script || !caller.device.isEmpty()) {
                problem = name + " is answered by the app script " + offered->script + ", not by " + script;
            } else {
                shared->provided.insert_or_assign(std::pair(extension, name), caller.logic);
            }
        }
        if (!problem.empty()) {
            return {"null", std::string(kNotProvided), problem};
        }
        shared->post([extension, name](ExtensionServices& self) { self.provided(extension, name); });
        return accepted();
    }
    if (service == kWithdraw) {
        const std::string name = readable ? textIn(body, "name") : std::string();
        const std::scoped_lock lock(shared->mutex);
        const auto found = shared->provided.find(std::pair(extension, name));
        if (found != shared->provided.end() && found->second == caller.logic) {
            shared->provided.erase(found);
        }
        return accepted();
    }
    if (service == kAnswer || service == kFail) {
        const std::optional<quint64> id = readable ? callIn(body) : std::nullopt;
        if (!id) {
            return accepted();
        }
        const bool failed = service == kFail;
        const auto output = readable ? body.find("output") : body.end();
        const bool ofOutput = failed && output != body.end() && output->is_boolean() && output->get<bool>();
        std::string text = textIn(body, failed ? "message" : "result");
        shared->post([logic = caller.logic, which = *id, failed, ofOutput, text = std::move(text)](ExtensionServices& self) {
            self.settle(logic, which, failed, ofOutput, text);
        });
        return accepted();
    }
    return {"null", "GS-API-070", "no part of GameScrcpy answers the service " + std::string(service) + " at once"};
}

void ExtensionServices::begin(Pending pending, const std::string& service, const std::string& args,
                              const std::optional<std::vector<std::string>>& limit)
{
    const quint64 id = ++m_lastCall;
    pending.target = service;
    pending.args = args;
    Pending& made = m_calls.insert_or_assign(id, std::move(pending)).first->second;
    const std::size_t slash = service.rfind('/');
    const std::string extension = slash == std::string::npos ? std::string() : service.substr(0, slash);
    const std::string name = slash == std::string::npos ? std::string() : service.substr(slash + 1);
    std::optional<ServiceExtension> provider;
    std::optional<ServiceExtension> caller;
    {
        const std::scoped_lock lock(m_shared->mutex);
        if (const ServiceExtension* found = m_shared->find(extension)) {
            provider = *found;
        }
        if (const ServiceExtension* found = m_shared->find(made.caller); found != nullptr && !made.caller.empty()) {
            caller = *found;
        }
    }
    if (!provider) {
        finish(id, std::string(kMissing),
               "no extension that is on offers " + service + "; turn on the extension that adds it");
        return;
    }
    const auto offered = std::ranges::find(provider->services, name, &OfferedService::name);
    if (offered == provider->services.end()) {
        finish(id, std::string(kMissing), provider->name + " offers no service named " + name);
        return;
    }
    if (!made.caller.empty() && made.caller != provider->id &&
        (!caller || std::ranges::find(caller->dependencies, provider->id) == caller->dependencies.end())) {
        finish(id, std::string(kNotDependency),
               made.caller + " may call " + service + " only when it lists " + provider->id + " in its dependencies");
        return;
    }
    if (args.size() > exthost::kMostServiceBytes) {
        finish(id, std::string(kMismatch), "the arguments of " + service + " are larger than 252 KiB");
        return;
    }
    if (const std::string problem = ext::valueProblem(offered->input, args); !problem.empty()) {
        finish(id, std::string(kMismatch), "the arguments do not match the input of " + service + ": " + problem);
        return;
    }
    made.extension = provider->id;
    made.name = name;
    made.output = offered->output;
    made.permissions = provider->granted;
    if (caller) {
        keepOnly(made.permissions, caller->granted);
    }
    if (limit) {
        keepOnly(made.permissions, *limit);
    }
    std::ranges::sort(made.permissions);
    made.permissions.erase(std::unique(made.permissions.begin(), made.permissions.end()), made.permissions.end());
    if (!offered->node.empty()) {
        const std::string envelope = R"({"caller":)" + jsonString(made.caller) + R"(,"permissions":)" +
                                     listed(made.permissions) + R"(,"input":)" + args + "}";
        const script::NodeAnswer answer =
            m_replies.node ? m_replies.node(provider->id, offered->node,
                                            {script::NodeValue{.type = script::NodeType::Json, .text = envelope}})
                           : script::NodeAnswer{{}, "GS-API-060", "no extension offers this node now"};
        if (!answer.code.empty()) {
            finish(id, std::string(answer.code == "GS-API-060" ? kMissing : kFailed),
                   service + " failed: " + shortened(answer.problem));
            return;
        }
        complete(id, answer.value.text);
        return;
    }
    made.deadline = std::chrono::steady_clock::now() + m_timeout;
    deliver(id);
    if (!m_sweep.isActive()) {
        m_sweep.start();
    }
}

void ExtensionServices::beginNative(std::uint64_t native, const std::string& caller, const std::string& service,
                                    const std::string& json)
{
    Pending pending;
    pending.native = native;
    pending.caller = caller;
    begin(std::move(pending), service, json, std::nullopt);
}

void ExtensionServices::deliver(quint64 id)
{
    const auto found = m_calls.find(id);
    if (found == m_calls.end() || found->second.provider != 0) {
        return;
    }
    Pending& pending = found->second;
    std::uint64_t logic = 0;
    {
        const std::scoped_lock lock(m_shared->mutex);
        if (const auto provider = m_shared->provided.find(std::pair(pending.extension, pending.name));
            provider != m_shared->provided.end()) {
            logic = provider->second;
        }
    }
    if (logic == 0) {
        return;
    }
    pending.provider = logic;
    const std::string json = R"({"type":"call","name":)" + jsonString(pending.name) + R"(,"caller":)" +
                             jsonString(pending.caller) + R"(,"permissions":)" + listed(pending.permissions) +
                             R"(,"args":)" + pending.args + "}";
    if (m_replies.tell) {
        m_replies.tell(logic, QString::fromUtf8(kEventKind.data(), static_cast<qsizetype>(kEventKind.size())),
                       QString::number(id), QString::fromStdString(json));
    }
}

void ExtensionServices::provided(const std::string& extension, const std::string& name)
{
    std::vector<quint64> waiting;
    for (const auto& [id, pending] : m_calls) {
        if (pending.provider == 0 && pending.extension == extension && pending.name == name) {
            waiting.push_back(id);
        }
    }
    for (const quint64 id : waiting) {
        deliver(id);
    }
}

void ExtensionServices::settle(std::uint64_t logic, quint64 id, bool failed, bool output, const std::string& text)
{
    const auto found = m_calls.find(id);
    if (found == m_calls.end() || found->second.provider != logic) {
        return;
    }
    const std::string target = found->second.target;
    if (failed && output) {
        finish(id, std::string(kMismatch), "the result of " + target + " is not JSON: " + shortened(text));
    } else if (failed) {
        finish(id, std::string(kFailed), target + " failed: " + shortened(text));
    } else {
        complete(id, text);
    }
}

void ExtensionServices::complete(quint64 id, const std::string& text)
{
    const auto found = m_calls.find(id);
    if (found == m_calls.end()) {
        return;
    }
    const std::string target = found->second.target;
    if (text.size() > exthost::kMostServiceBytes) {
        finish(id, std::string(kMismatch), "the result of " + target + " is larger than 252 KiB");
        return;
    }
    if (const std::string problem = ext::valueProblem(found->second.output, text); !problem.empty()) {
        finish(id, std::string(kMismatch), "the result of " + target + " does not match its output: " + problem);
        return;
    }
    finish(id, {}, text);
}

void ExtensionServices::cancelProvider(quint64 id)
{
    const auto found = m_calls.find(id);
    if (found == m_calls.end() || found->second.provider == 0 || !m_replies.tell) {
        return;
    }
    m_replies.tell(found->second.provider,
                   QString::fromUtf8(kEventKind.data(), static_cast<qsizetype>(kEventKind.size())),
                   QString::number(id), QStringLiteral(R"({"type":"cancel"})"));
}

void ExtensionServices::finish(quint64 id, const std::string& code, const std::string& text)
{
    auto node = m_calls.extract(id);
    if (node.empty()) {
        return;
    }
    const Pending& ended = node.mapped();
    if (ended.native != 0) {
        Done answer;
        {
            const std::scoped_lock lock(m_shared->mutex);
            if (const auto found = m_shared->natives.find(ended.native); found != m_shared->natives.end()) {
                answer = std::move(found->second);
                m_shared->natives.erase(found);
            }
        }
        if (answer) {
            answer({statusOf(code), code.empty() ? text : code + " " + text});
        }
    } else if (code.empty()) {
        if (m_replies.answer) {
            m_replies.answer(ended.logic, ended.request, QString::fromStdString(text));
        }
    } else if (m_replies.refuse) {
        m_replies.refuse(ended.logic, ended.request, QString::fromStdString(code + " " + text));
    }
    if (m_calls.empty()) {
        m_sweep.stop();
    }
}

void ExtensionServices::sweep()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<quint64> late;
    for (const auto& [id, pending] : m_calls) {
        if (pending.deadline != std::chrono::steady_clock::time_point{} && now >= pending.deadline) {
            late.push_back(id);
        }
    }
    const std::string limit = std::to_string(m_timeout.count()) + " ms";
    for (const quint64 id : late) {
        const Pending& pending = m_calls.at(id);
        const std::string target = pending.target;
        if (pending.provider == 0) {
            finish(id, std::string(kMissing),
                   target + " was not provided within " + limit + ": the app script of " + pending.extension +
                       " is not running or does not call ext.provide(\"" + pending.name + "\")");
        } else {
            cancelProvider(id);
            finish(id, std::string(kFailed), target + " did not answer within " + limit);
        }
    }
}

void ExtensionServices::endLogic(std::uint64_t logic)
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        std::erase_if(m_shared->provided, [logic](const auto& entry) { return entry.second == logic; });
    }
    std::vector<quint64> dropped;
    std::vector<quint64> stranded;
    for (const auto& [id, pending] : m_calls) {
        if (pending.native == 0 && pending.logic == logic) {
            dropped.push_back(id);
        } else if (pending.provider == logic) {
            stranded.push_back(id);
        }
    }
    for (const quint64 id : dropped) {
        cancelProvider(id);
        m_calls.erase(id);
    }
    for (const quint64 id : stranded) {
        const std::string target = m_calls.at(id).target;
        finish(id, std::string(kFailed), "the logic that answers " + target + " stopped before it answered");
    }
    if (m_calls.empty()) {
        m_sweep.stop();
    }
}

bool ExtensionServices::depends(const QString& listener, const QString& emitter) const
{
    const std::string from = listener.toStdString();
    const std::string to = emitter.toStdString();
    const std::scoped_lock lock(m_shared->mutex);
    const ServiceExtension* own = m_shared->find(from);
    return own != nullptr && m_shared->find(to) != nullptr &&
           std::ranges::find(own->dependencies, to) != own->dependencies.end();
}

void ExtensionServices::callFromNative(const std::string& caller, std::string service, std::string json, Done done)
{
    std::shared_ptr<Shared> shared;
    {
        const std::scoped_lock lock(s_instanceMutex);
        if (s_instance != nullptr) {
            shared = s_instance->m_shared;
        }
    }
    if (shared != nullptr) {
        const std::scoped_lock lock(shared->mutex);
        if (shared->owner != nullptr) {
            const std::uint64_t native = ++shared->lastNative;
            shared->natives.emplace(native, std::move(done));
            ExtensionServices* target = shared->owner;
            QMetaObject::invokeMethod(
                target,
                [target, native, caller, service = std::move(service), json = std::move(json)] {
                    target->beginNative(native, caller, service, json);
                },
                Qt::QueuedConnection);
            return;
        }
    }
    if (done) {
        done({exthost::ServiceStatus::Unsupported, std::string(kMissing) + " GameScrcpy offers no extension services now"});
    }
}

exthost::ServiceResult ExtensionServices::callFromNativeAndWait(const std::string& caller, std::string_view service,
                                                                std::string_view json, std::stop_token stop)
{
    const QCoreApplication* application = QCoreApplication::instance();
    if (application == nullptr || QThread::currentThread() == application->thread()) {
        return {exthost::ServiceStatus::Unsupported,
                "call_service works only on a thread of the DLL's own, not on one of GameScrcpy's"};
    }
    const auto stopped = [service] {
        return exthost::ServiceResult{exthost::ServiceStatus::Failed,
                                      std::string(kFailed) + " " + std::string(service) +
                                          " was not answered: the extension stopped"};
    };
    if (stop.stop_requested()) {
        return stopped();
    }
    struct Wait {
        std::mutex mutex;
        std::condition_variable_any answered;
        std::optional<exthost::ServiceResult> result;
    };
    const auto wait = std::make_shared<Wait>();
    callFromNative(caller, std::string(service), std::string(json), [wait](exthost::ServiceResult result) {
        {
            const std::scoped_lock lock(wait->mutex);
            wait->result = std::move(result);
        }
        wait->answered.notify_all();
    });
    std::unique_lock lock(wait->mutex);
    if (!wait->answered.wait_for(lock, stop, kTimeout + kNativeMargin, [&wait] { return wait->result.has_value(); })) {
        if (stop.stop_requested()) {
            return stopped();
        }
        return {exthost::ServiceStatus::Failed, std::string(kFailed) + " " + std::string(service) + " did not answer in time"};
    }
    return std::move(*wait->result);
}

}
