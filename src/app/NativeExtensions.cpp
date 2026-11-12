#include "NativeExtensions.h"

#include "ExtensionIcons.h"
#include "ExtensionServices.h"
#include "ExtensionTouchSink.h"
#include "HardwareClaims.h"

#include <gs/base/Log.h>
#include <gs/exthost/Hosted.h>
#include <gs/exthost/InProcess.h>
#include <gs/input/InputSource.h>
#include <gs/platform/TrayNotice.h>

#include <nlohmann/json.hpp>

#include <QMetaObject>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace gs::app {

static_assert(static_cast<int>(script::NodeType::None) == static_cast<int>(exthost::ValueType::None));
static_assert(static_cast<int>(script::NodeType::Boolean) == static_cast<int>(exthost::ValueType::Boolean));
static_assert(static_cast<int>(script::NodeType::Number) == static_cast<int>(exthost::ValueType::Number));
static_assert(static_cast<int>(script::NodeType::String) == static_cast<int>(exthost::ValueType::String));
static_assert(static_cast<int>(script::NodeType::Point) == static_cast<int>(exthost::ValueType::Point));
static_assert(static_cast<int>(script::NodeType::Region) == static_cast<int>(exthost::ValueType::Region));
static_assert(static_cast<int>(script::NodeType::Color) == static_cast<int>(exthost::ValueType::Color));
static_assert(static_cast<int>(script::NodeType::Duration) == static_cast<int>(exthost::ValueType::Duration));
static_assert(static_cast<int>(script::NodeType::Json) == static_cast<int>(exthost::ValueType::Json));
static_assert(static_cast<int>(ext::InputSlotKind::Button) == static_cast<int>(exthost::InputSlotKind::Button));
static_assert(static_cast<int>(ext::InputSlotKind::Axis) == static_cast<int>(exthost::InputSlotKind::Axis));
static_assert(static_cast<int>(ext::InputSlotKind::Stick) == static_cast<int>(exthost::InputSlotKind::Stick));

namespace {

constexpr std::string_view kCategory = "extensions";

std::filesystem::path pathOf(std::string_view text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

void writeLog(const std::string& id, int level, std::string_view text)
{
    if (level >= 2) {
        log::error(kCategory, "{}: {}", id, text);
    } else if (level == 1) {
        log::warning(kCategory, "{}: {}", id, text);
    } else {
        log::info(kCategory, "{}: {}", id, text);
    }
}

exthost::ValueType declaredType(std::string_view name)
{
    return name.empty() ? exthost::ValueType::None : exthost::valueTypeNamed(name).value_or(exthost::ValueType::None);
}

exthost::Value valueOf(const script::NodeValue& value)
{
    exthost::Value made;
    made.type = static_cast<exthost::ValueType>(value.type);
    made.flag = value.flag;
    made.number = value.number;
    made.x = value.area.x;
    made.y = value.area.y;
    made.width = value.area.width;
    made.height = value.area.height;
    made.text = value.text;
    return made;
}

script::NodeValue nodeValueOf(const exthost::Value& value)
{
    script::NodeValue made;
    made.type = static_cast<script::NodeType>(value.type);
    made.flag = value.flag;
    made.number = value.number;
    made.area = script::Region{value.x, value.y, value.width, value.height};
    made.text = value.text;
    return made;
}

nlohmann::json partOf(const nlohmann::json& parsed, const char* name)
{
    return parsed.is_object() && parsed.contains(name) && parsed.at(name).is_object() ? parsed.at(name)
                                                                                      : nlohmann::json::object();
}

std::vector<exthost::DeclaredInput> declaredInputs(const ext::NativeContribution& native)
{
    std::vector<exthost::DeclaredInput> declared;
    for (const ext::NativeInputSource& source : native.inputSources) {
        exthost::DeclaredInput made{.source = source.name, .slotList = {}};
        for (const ext::NativeInputSlot& slot : source.slotList) {
            made.slotList.push_back({.name = slot.name, .kind = static_cast<exthost::InputSlotKind>(slot.kind)});
        }
        declared.push_back(std::move(made));
    }
    return declared;
}

constexpr std::array<std::string_view, 4> kDeviceStates{"ready", "busy", "problem", "offline"};
constexpr std::size_t kMostProvidedDevices = 64;
constexpr std::size_t kMostDeviceActions = 8;

std::optional<std::string> textIn(const nlohmann::json& object, const char* key, std::size_t most)
{
    if (!object.contains(key)) {
        return std::string();
    }
    const nlohmann::json& value = object.at(key);
    if (!value.is_string() || value.get_ref<const std::string&>().size() > most) {
        return std::nullopt;
    }
    return value.get<std::string>();
}

std::optional<ProvidedDevice> providedOf(const NativeWanted& wanted, const ext::NativeDeviceProvider& declared,
                                         const nlohmann::json& device, std::string& problem)
{
    if (!device.is_object()) {
        problem = "it is not an object";
        return std::nullopt;
    }
    const std::optional<std::string> id = textIn(device, "id", 64);
    const std::optional<std::string> name = textIn(device, "name", 64);
    const std::optional<std::string> kind = textIn(device, "kind", 32);
    const std::optional<std::string> summary = textIn(device, "summary", 200);
    const std::optional<std::string> state = textIn(device, "state", 16);
    const std::optional<std::string> stateText = textIn(device, "stateText", 200);
    const std::optional<std::string> method = textIn(device, "touchMethod", 64);
    const std::optional<std::string> resource = textIn(device, "resource", exthost::kMostResourceBytes);
    if (!resource || (!resource->empty() && !exthost::validResource(*resource))) {
        problem = "its resource is not the name of hardware such as serial:COM9";
        return std::nullopt;
    }
    if (!id || id->empty() || !name || name->empty()) {
        problem = "it has no id or no name";
        return std::nullopt;
    }
    if (!kind || std::ranges::find(declared.kinds, *kind) == declared.kinds.end()) {
        problem = "its kind is not one the manifest declares";
        return std::nullopt;
    }
    if (!state || (!state->empty() && std::ranges::find(kDeviceStates, *state) == kDeviceStates.end()) || !summary ||
        !stateText || !method) {
        problem = "its state, summary, stateText or touchMethod is not valid text";
        return std::nullopt;
    }
    if (!method->empty() &&
        std::ranges::find(wanted.native.touchMethods, *method, &ext::NativeTouchMethod::name) ==
            wanted.native.touchMethods.end()) {
        problem = "its touchMethod is not one of the extension's touch methods";
        return std::nullopt;
    }
    QVariantList actions;
    if (device.contains("actions")) {
        const nlohmann::json& listed = device.at("actions");
        if (!listed.is_array() || listed.size() > kMostDeviceActions) {
            problem = "its actions are not a list of at most 8";
            return std::nullopt;
        }
        for (const nlohmann::json& action : listed) {
            const std::optional<std::string> actionId = action.is_object() ? textIn(action, "id", 64) : std::nullopt;
            const std::optional<std::string> title = action.is_object() ? textIn(action, "title", 40) : std::nullopt;
            const std::optional<std::string> panel = action.is_object() ? textIn(action, "panel", 64) : std::nullopt;
            const std::optional<std::string> job = action.is_object() ? textIn(action, "job", 64) : std::nullopt;
            if (!actionId || actionId->empty() || !title || title->empty() || !panel || !job ||
                (!job->empty() &&
                 std::ranges::find(wanted.native.jobs, *job, &ext::NativeJob::name) == wanted.native.jobs.end())) {
                problem = "an action has no id or title, or names a job the manifest does not declare";
                return std::nullopt;
            }
            actions.append(QVariantMap{{QStringLiteral("id"), QString::fromStdString(*actionId)},
                                       {QStringLiteral("title"), QString::fromStdString(*title)},
                                       {QStringLiteral("panel"), QString::fromStdString(*panel)},
                                       {QStringLiteral("job"), QString::fromStdString(*job)}});
        }
    }
    const nlohmann::json capture = device.contains("capture") ? device.at("capture") : nlohmann::json(false);
    const std::string icon = textIn(device, "icon", 128).value_or(std::string());
    constexpr std::string_view kSvg = "svg:";
    const QString iconPath =
        icon.starts_with(kSvg) ? extensionIconPath(wanted.directory, std::string_view(icon).substr(kSvg.size())) : QString();
    return ProvidedDevice{.extension = wanted.id,
                          .extensionName = wanted.name,
                          .provider = declared.name,
                          .id = *id,
                          .name = QString::fromStdString(*name),
                          .kind = QString::fromStdString(*kind),
                          .summary = QString::fromStdString(*summary),
                          .state = state->empty() ? QStringLiteral("ready") : QString::fromStdString(*state),
                          .stateText = QString::fromStdString(*stateText),
                          .icon = builtinIconName(icon) ? QString::fromStdString(icon) : QString(),
                          .iconPath = iconPath,
                          .touchMethod = method->empty() ? std::string() : wanted.id + "/" + *method,
                          .resource = *resource,
                          .capture = capture.is_boolean() && capture.get<bool>(),
                          .actions = std::move(actions)};
}

constexpr std::size_t kMostTransportPhones = 64;

std::optional<TransportPhone> phoneOf(const NativeWanted& wanted, const ext::NativeTransport& declared,
                                      const QString& title, const nlohmann::json& phone, std::string& problem)
{
    if (!phone.is_object()) {
        problem = "it is not an object";
        return std::nullopt;
    }
    const std::optional<std::string> id = textIn(phone, "id", 64);
    const std::optional<std::string> name = textIn(phone, "name", 64);
    const std::optional<std::string> model = textIn(phone, "model", 64);
    const std::optional<std::string> detail = textIn(phone, "detail", 200);
    if (!id || id->empty() || !name || name->empty()) {
        problem = "it has no id or no name";
        return std::nullopt;
    }
    if (!exthost::validUtf8(*id) ||
        std::ranges::any_of(*id, [](char c) { return static_cast<unsigned char>(c) < 0x20; })) {
        problem = "its id is not plain text";
        return std::nullopt;
    }
    if (!model || !detail) {
        problem = "its model or detail is not valid text";
        return std::nullopt;
    }
    constexpr std::string_view kSvg = "svg:";
    const std::string_view icon = declared.icon;
    return TransportPhone{
        .extension = wanted.id,
        .extensionName = wanted.name,
        .transport = declared.name,
        .transportTitle = title,
        .id = *id,
        .name = QString::fromStdString(*name),
        .model = QString::fromStdString(*model),
        .detail = QString::fromStdString(*detail),
        .icon = builtinIconName(icon) ? QString::fromStdString(declared.icon) : QString(),
        .iconPath = icon.starts_with(kSvg) ? extensionIconPath(wanted.directory, icon.substr(kSvg.size())) : QString()};
}

}

std::string nativeTouchOptions(std::string_view snapshot, std::string_view phone, std::string_view device,
                               std::string_view touchBox)
{
    const nlohmann::json parsed = nlohmann::json::parse(snapshot, nullptr, false);
    const nlohmann::json devices = partOf(parsed, "devices");
    const std::string id(phone);
    nlohmann::json chosen = device.empty() ? nlohmann::json(nullptr) : nlohmann::json::parse(device, nullptr, false);
    if (chosen.is_discarded()) {
        chosen = nullptr;
    }
    return nlohmann::json{{"settings", partOf(parsed, "global")},
                          {"deviceSettings", partOf(devices, id.c_str())},
                          {"phone", id},
                          {"device", std::move(chosen)},
                          {"touchBox", touchBox.empty() ? nlohmann::json(nullptr) : nlohmann::json(std::string(touchBox))}}
        .dump();
}

struct NativeExtensions::Shared {
    std::mutex mutex;
    NativeExtensions* owner = nullptr;
    std::map<std::pair<std::string, std::uint64_t>, std::weak_ptr<ModuleTouchPort>> ports;
    std::shared_ptr<TransportChanges> changes;

    void post(std::function<void(NativeExtensions&)> work)
    {
        const std::scoped_lock lock(mutex);
        if (owner == nullptr) {
            return;
        }
        NativeExtensions* target = owner;
        QMetaObject::invokeMethod(target, [target, work = std::move(work)] { work(*target); }, Qt::QueuedConnection);
    }

    void notify(const QString& name, std::string_view title, std::string_view text)
    {
        const QString heading =
            name + QStringLiteral(": ") + QString::fromUtf8(title.data(), static_cast<qsizetype>(title.size()));
        const QString body = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
        post([heading, body](NativeExtensions& owner) { owner.showNotice(heading, body); });
    }

    void emitted(const std::string& id, std::string_view topic, std::string_view json)
    {
        post([extension = QString::fromStdString(id),
              name = QString::fromUtf8(topic.data(), static_cast<qsizetype>(topic.size())),
              value = QString::fromUtf8(json.data(), static_cast<qsizetype>(json.size()))](NativeExtensions& owner) {
            emit owner.extensionEvent(extension, name, value);
        });
    }

    void devicesChanged(const std::string& id)
    {
        post([id](NativeExtensions& owner) { owner.refreshProviders(id); });
    }

    void input(const std::string& id, std::uint64_t loadNumber, std::string_view source, std::string_view slot,
               double x, double y)
    {
        post([id, loadNumber, from = std::string(source), key = std::string(slot), x, y](NativeExtensions& owner) {
            owner.deliverInput(id, loadNumber, from, key, x, y);
        });
    }

    void transportChanged(const std::string& id, std::string_view transport)
    {
        if (changes != nullptr) {
            changes->bump(transportKey(id, transport));
        }
        post([id](NativeExtensions& owner) { owner.refreshTransports(id); });
    }

    void sourcesChanged(const std::string& id)
    {
        post([id](NativeExtensions& owner) { owner.refreshSources(id); });
    }

    void lose(const std::string& id, std::uint64_t session)
    {
        std::shared_ptr<ModuleTouchPort> port;
        {
            const std::scoped_lock lock(mutex);
            if (const auto found = ports.find(std::pair<std::string, std::uint64_t>(id, session)); found != ports.end()) {
                port = found->second.lock();
            }
        }
        if (port != nullptr) {
            port->lose();
        }
    }

    void keep(const std::string& id, const std::shared_ptr<ModuleTouchPort>& port)
    {
        const std::scoped_lock lock(mutex);
        std::erase_if(ports, [](const auto& entry) { return entry.second.expired(); });
        ports.insert_or_assign(std::pair<std::string, std::uint64_t>(id, port->session()), port);
    }
};

namespace {

std::atomic<std::uint64_t> g_lastHolder{0};
std::atomic<std::uint64_t> g_lastLoadNumber{0};

struct ModuleSlot {
    std::mutex mutex;
    std::weak_ptr<exthost::Module> module;

    void set(const std::shared_ptr<exthost::Module>& made)
    {
        const std::scoped_lock lock(mutex);
        module = made;
    }

    [[nodiscard]] bool wanted(const std::string& resource, const QString& by)
    {
        std::shared_ptr<exthost::Module> alive;
        {
            const std::scoped_lock lock(mutex);
            alive = module.lock();
        }
        return alive != nullptr && alive->claimWanted(resource, by.toStdString());
    }
};

void releaseClaims(const std::string& holder)
{
    if (HardwareClaims* claims = HardwareClaims::instance()) {
        claims->releaseAll(holder);
    }
}

class EntryServices final : public exthost::HostServices
{
public:
    EntryServices(std::string id, QString name, std::weak_ptr<NativeExtensions::Shared> shared, ClaimHolder claimer,
                  std::uint64_t loadNumber)
        : m_id(std::move(id))
        , m_name(std::move(name))
        , m_shared(std::move(shared))
        , m_claimer(std::move(claimer))
        , m_loadNumber(loadNumber)
    {
    }

    exthost::ClaimResult claim(std::string_view resource, bool wait) override
    {
        HardwareClaims* claims = HardwareClaims::instance();
        return claims != nullptr ? claims->claim(resource, m_claimer, wait) : exthost::ClaimResult::Unsupported;
    }

    void release(std::string_view resource) override
    {
        if (HardwareClaims* claims = HardwareClaims::instance()) {
            claims->release(resource, m_claimer.id);
        }
    }

    exthost::ServiceResult callService(std::string_view service, std::string_view json) override
    {
        return ExtensionServices::callFromNativeAndWait(m_id, service, json);
    }

    void log(int level, std::string_view text) override { writeLog(m_id, level, text); }

    bool notify(std::string_view title, std::string_view text) override
    {
        const std::shared_ptr<NativeExtensions::Shared> shared = m_shared.lock();
        if (shared == nullptr) {
            return false;
        }
        shared->notify(m_name, title, text);
        return true;
    }

    void emitted(std::string_view topic, std::string_view json) override
    {
        if (const std::shared_ptr<NativeExtensions::Shared> shared = m_shared.lock()) {
            shared->emitted(m_id, topic, json);
        }
    }

    void devicesChanged(std::string_view) override
    {
        if (const std::shared_ptr<NativeExtensions::Shared> shared = m_shared.lock()) {
            shared->devicesChanged(m_id);
        }
    }

    void inputEvent(std::string_view source, std::string_view slot, double x, double y, std::uint64_t) override
    {
        if (const std::shared_ptr<NativeExtensions::Shared> shared = m_shared.lock()) {
            shared->input(m_id, m_loadNumber, source, slot, x, y);
        }
    }

    void transportChanged(std::string_view transport) override
    {
        if (const std::shared_ptr<NativeExtensions::Shared> shared = m_shared.lock()) {
            shared->transportChanged(m_id, transport);
        }
    }

    void sourcesChanged(std::string_view) override
    {
        if (const std::shared_ptr<NativeExtensions::Shared> shared = m_shared.lock()) {
            shared->sourcesChanged(m_id);
        }
    }

private:
    std::string m_id;
    QString m_name;
    std::weak_ptr<NativeExtensions::Shared> m_shared;
    ClaimHolder m_claimer;
    std::uint64_t m_loadNumber = 0;
};

}

struct NativeExtensions::Entry {
    NativeWanted wanted;
    std::shared_ptr<exthost::Module> module;
    std::vector<std::optional<std::size_t>> nodeIndex;
    std::vector<std::optional<std::size_t>> methodIndex;
    std::vector<ext::Diagnostic> problems;
    std::vector<std::optional<std::size_t>> attachmentIndex;
    std::vector<std::optional<std::size_t>> behaviorIndex;
    std::vector<std::optional<std::size_t>> jobIndex;
    std::vector<std::optional<std::size_t>> providerIndex;
    bool pending = false;
    std::uint64_t loadNumber = 0;
    std::vector<std::optional<std::size_t>> inputIndex;
    std::vector<std::optional<std::size_t>> transportIndex;
    std::vector<std::optional<std::size_t>> videoIndex;
    std::vector<std::optional<std::size_t>> audioIndex;
    std::vector<std::optional<std::size_t>> sinkIndex;
    std::vector<std::optional<std::size_t>> exporterIndex;
    std::vector<std::optional<std::size_t>> tapIndex;
    std::vector<std::optional<std::size_t>> visionIndex;
};

namespace {

template <typename Declared, typename Offered>
void matchNames(NativeExtensions::Entry& entry, const std::vector<Declared>& declared,
                const std::vector<Offered>& offered, std::vector<std::optional<std::size_t>>& index,
                const std::string& where)
{
    for (std::size_t at = 0; at < declared.size(); ++at) {
        const auto found = std::ranges::find(offered, declared[at].name, &Offered::name);
        const bool fits = found != offered.end();
        index.push_back(fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - offered.begin()))
                             : std::nullopt);
        if (!fits) {
            entry.problems.push_back({ext::Severity::Warning, ext::Code::NativeMismatch,
                                      where + "[" + std::to_string(at) + "]", declared[at].name});
        }
    }
}

void matchEntry(NativeExtensions::Entry& entry)
{
    const NativeWanted& wanted = entry.wanted;
    const std::string where = "$[\"" + wanted.id + "\"].contributes.native";
    const exthost::ModuleInfo& info = entry.module->info();
    for (std::size_t index = 0; index < wanted.native.nodes.size(); ++index) {
        const ext::NativeNode& node = wanted.native.nodes[index];
        const auto found = std::ranges::find(info.nodes, node.name, &exthost::NodeInfo::name);
        bool fits = found != info.nodes.end() && found->returns == declaredType(node.returns) &&
                    found->params.size() == node.params.size();
        for (std::size_t param = 0; fits && param < node.params.size(); ++param) {
            fits = found->params[param] == declaredType(node.params[param].type);
        }
        entry.nodeIndex.push_back(fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.nodes.begin()))
                                       : std::nullopt);
        if (!fits) {
            entry.problems.push_back({ext::Severity::Warning, ext::Code::NativeMismatch,
                                      where + ".nodes[" + std::to_string(index) + "]", node.name});
        }
    }
    for (std::size_t index = 0; index < wanted.native.touchMethods.size(); ++index) {
        const ext::NativeTouchMethod& method = wanted.native.touchMethods[index];
        const auto found = std::ranges::find(info.touchMethods, method.name, &exthost::TouchMethodInfo::name);
        const bool fits = found != info.touchMethods.end();
        entry.methodIndex.push_back(
            fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.touchMethods.begin()))
                 : std::nullopt);
        if (!fits) {
            entry.problems.push_back({ext::Severity::Warning, ext::Code::NativeMismatch,
                                      where + ".touchMethods[" + std::to_string(index) + "]", method.name});
        }
    }
    for (std::size_t index = 0; index < wanted.native.jobs.size(); ++index) {
        const ext::NativeJob& job = wanted.native.jobs[index];
        const auto found = std::ranges::find(info.jobs, job.name, &exthost::JobInfo::name);
        const bool fits = found != info.jobs.end();
        entry.jobIndex.push_back(fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.jobs.begin()))
                                      : std::nullopt);
        if (!fits) {
            entry.problems.push_back({ext::Severity::Warning, ext::Code::NativeMismatch,
                                      where + ".jobs[" + std::to_string(index) + "]", job.name});
        }
    }
    for (std::size_t index = 0; index < wanted.native.deviceProviders.size(); ++index) {
        const ext::NativeDeviceProvider& provider = wanted.native.deviceProviders[index];
        const auto found = std::ranges::find(info.providers, provider.name, &exthost::ProviderInfo::name);
        const bool fits = found != info.providers.end();
        entry.providerIndex.push_back(
            fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.providers.begin())) : std::nullopt);
        if (!fits) {
            entry.problems.push_back({ext::Severity::Warning, ext::Code::NativeMismatch,
                                      where + ".deviceProviders[" + std::to_string(index) + "]", provider.name});
        }
    }
    for (std::size_t index = 0; index < wanted.native.transports.size(); ++index) {
        const ext::NativeTransport& transport = wanted.native.transports[index];
        const auto found = std::ranges::find(info.transports, transport.name, &exthost::TransportInfo::name);
        const bool fits = found != info.transports.end();
        entry.transportIndex.push_back(
            fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.transports.begin())) : std::nullopt);
        if (!fits) {
            entry.problems.push_back({ext::Severity::Warning, ext::Code::NativeMismatch,
                                      where + ".transports[" + std::to_string(index) + "]", transport.name});
        }
    }
    for (std::size_t index = 0; index < wanted.native.frameTaps.size(); ++index) {
        const ext::NativeFrameTap& tap = wanted.native.frameTaps[index];
        const auto found = std::ranges::find(info.frameTaps, tap.name, &exthost::FrameTapInfo::name);
        const bool fits = found != info.frameTaps.end();
        entry.tapIndex.push_back(
            fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.frameTaps.begin())) : std::nullopt);
        if (!fits) {
            entry.problems.push_back({ext::Severity::Warning, ext::Code::NativeMismatch,
                                      where + ".frameTaps[" + std::to_string(index) + "]", tap.name});
        }
    }
    for (std::size_t index = 0; index < wanted.native.attachments.size(); ++index) {
        const ext::NativeAttachment& attachment = wanted.native.attachments[index];
        const auto found = std::ranges::find(info.attachments, attachment.name, &exthost::AttachmentInfo::name);
        const bool fits = found != info.attachments.end();
        entry.attachmentIndex.push_back(
            fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.attachments.begin()))
                 : std::nullopt);
        if (!fits) {
            entry.problems.push_back({ext::Severity::Warning, ext::Code::NativeMismatch,
                                      where + ".attachments[" + std::to_string(index) + "]", attachment.name});
        }
    }
    for (std::size_t index = 0; index < wanted.native.behaviors.size(); ++index) {
        const ext::NativeBehavior& behavior = wanted.native.behaviors[index];
        const auto found = std::ranges::find(info.behaviors, behavior.name, &exthost::BehaviorInfo::name);
        bool fits = found != info.behaviors.end() && found->slotKinds.size() == behavior.slotList.size() &&
                    found->fingers == behavior.fingers.value_or(1) && found->frames == behavior.frames;
        for (std::size_t slot = 0; fits && slot < behavior.slotList.size(); ++slot) {
            fits = static_cast<int>(found->slotKinds[slot]) == static_cast<int>(behavior.slotList[slot].kind);
        }
        entry.behaviorIndex.push_back(
            fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.behaviors.begin())) : std::nullopt);
        if (!fits) {
            entry.problems.push_back({ext::Severity::Warning, ext::Code::NativeMismatch,
                                      where + ".behaviors[" + std::to_string(index) + "]", behavior.name});
        }
    }
    for (std::size_t index = 0; index < wanted.native.inputSources.size(); ++index) {
        const ext::NativeInputSource& source = wanted.native.inputSources[index];
        const auto found = std::ranges::find(info.inputSources, source.name, &exthost::InputSourceInfo::name);
        const bool fits = found != info.inputSources.end();
        entry.inputIndex.push_back(
            fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.inputSources.begin()))
                 : std::nullopt);
        if (!fits) {
            entry.problems.push_back({ext::Severity::Warning, ext::Code::NativeMismatch,
                                      where + ".inputSources[" + std::to_string(index) + "]", source.name});
        }
    }
    matchNames(entry, wanted.native.videoSources, info.videoSources, entry.videoIndex, where + ".videoSources");
    matchNames(entry, wanted.native.audioSources, info.audioSources, entry.audioIndex, where + ".audioSources");
    matchNames(entry, wanted.native.mediaSinks, info.sinks, entry.sinkIndex, where + ".mediaSinks");
    matchNames(entry, wanted.native.recordingExporters, info.exporters, entry.exporterIndex,
               where + ".recordingExporters");
    for (std::size_t index = 0; index < wanted.native.visionEngines.size(); ++index) {
        const ext::NativeVisionEngine& engine = wanted.native.visionEngines[index];
        const auto found = std::ranges::find(info.visionEngines, engine.name, &exthost::VisionEngineInfo::name);
        std::uint32_t declared = 0;
        for (const std::string& kind : engine.kinds) {
            if (const std::optional<exthost::VisionKind> named = exthost::visionKindNamed(kind)) {
                declared |= exthost::visionBit(*named);
            }
        }
        const bool fits = found != info.visionEngines.end() && declared != 0 && (declared & ~found->kinds) == 0;
        entry.visionIndex.push_back(
            fits ? std::optional<std::size_t>(static_cast<std::size_t>(found - info.visionEngines.begin()))
                 : std::nullopt);
        if (!fits) {
            entry.problems.push_back({ext::Severity::Warning, ext::Code::NativeMismatch,
                                      where + ".visionEngines[" + std::to_string(index) + "]", engine.name});
        }
    }
    log::info(kCategory, "{}: the DLL is loaded {}", wanted.id,
              wanted.official ? "inside GameScrcpy" : "in its own process");
}

}

NativeExtensions::NativeExtensions(NativeOptions options, QObject* parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_shared(std::make_shared<Shared>())
{
    m_shared->owner = this;
    m_shared->changes = m_changes;
    m_modules = makeModules();
    s_instance = this;
    connect(this, &NativeExtensions::crashed, this, [this](const QString& id) { emit inputsEnded(id); });
}

NativeExtensions::~NativeExtensions()
{
    NativeExtensions* self = this;
    static_cast<void>(s_instance.compare_exchange_strong(self, nullptr));
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = nullptr;
    }
    std::vector<std::shared_ptr<Entry>> gone;
    {
        const std::scoped_lock lock(m_mutex);
        gone.swap(m_entries);
    }
    gone.clear();
}

std::vector<std::shared_ptr<NativeExtensions::Entry>> NativeExtensions::entries() const
{
    const std::scoped_lock lock(m_mutex);
    return m_entries;
}

std::shared_ptr<NativeExtensions::Entry> NativeExtensions::load(const NativeWanted& wanted) const
{
    auto entry = std::make_shared<Entry>();
    entry->wanted = wanted;
    entry->loadNumber = ++g_lastLoadNumber;
    const std::string where = "$[\"" + wanted.id + "\"].contributes.native";
    const std::string chosen = ext::nativeDllFor(wanted.native, ext::hostArchitecture());
    if (chosen.empty()) {
        entry->problems.push_back({ext::Severity::Error, ext::Code::NativeArchitectureMissing, where + ".dll",
                                   std::string(ext::hostArchitecture())});
        return entry;
    }
    const std::filesystem::path dll = wanted.directory / pathOf(chosen);
    const std::filesystem::path store =
        m_options.storeRoot.empty() ? std::filesystem::path() : m_options.storeRoot / pathOf(wanted.id + ".json");
    std::string problem;
    const std::string holder = "native:" + wanted.id + "#" + std::to_string(++g_lastHolder);
    auto slot = std::make_shared<ModuleSlot>();
    const ClaimHolder claimer{holder, wanted.name, [slot](const std::string& resource, const QString& by) {
                                  return slot->wanted(resource, by);
                              }};
    if (wanted.official) {
        auto services =
            std::make_shared<EntryServices>(wanted.id, wanted.name, m_shared, claimer, entry->loadNumber);
        std::unique_ptr<exthost::InProcessModule> module =
            exthost::InProcessModule::load({.dll = dll,
                                            .store = store,
                                            .granted = wanted.granted,
                                            .services = services.get(),
                                            .settings = wanted.settings,
                                            .inputs = declaredInputs(wanted.native)},
                                           problem);
        if (module != nullptr) {
            entry->module = std::shared_ptr<exthost::Module>(module.release(), [services, holder](exthost::Module* done) {
                delete done;
                releaseClaims(holder);
            });
            slot->set(entry->module);
        }
    } else {
        const std::weak_ptr<Shared> shared = m_shared;
        const std::string id = wanted.id;
        const QString name = wanted.name;
        exthost::HostedOptions options;
        options.program = m_options.program;
        options.dll = dll;
        options.store = store;
        options.granted = wanted.granted;
        options.settings = wanted.settings;
        options.clock = m_options.clock;
        options.log = [id](int level, const std::string& text) { writeLog(id, level, text); };
        options.notify = [shared, name](const std::string& title, const std::string& text) {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->notify(name, title, text);
            }
        };
        options.event = [shared, id](const std::string& topic, const std::string& json) {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->emitted(id, topic, json);
            }
        };
        options.devicesChanged = [shared, id](const std::string&) {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->devicesChanged(id);
            }
        };
        options.inputs = declaredInputs(wanted.native);
        options.input = [shared, id, loadNumber = entry->loadNumber](const std::string& from, const std::string& key,
                                                                     double x, double y, std::uint64_t) {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->input(id, loadNumber, from, key, x, y);
            }
        };
        options.transportChanged = [shared, id](const std::string& transport) {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->transportChanged(id, transport);
            }
        };
        options.sourcesChanged = [shared, id](const std::string&) {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->sourcesChanged(id);
            }
        };
        options.claim = [claimer](const std::string& resource, bool wait,
                                  std::function<void(exthost::ClaimResult)> answer) {
            HardwareClaims* claims = HardwareClaims::instance();
            if (claims == nullptr) {
                answer(exthost::ClaimResult::Unsupported);
            } else if (!wait) {
                answer(claims->claim(resource, claimer, false));
            } else {
                claims->claimLater(resource, claimer, std::move(answer));
            }
        };
        options.release = [holder](const std::string& resource) {
            if (HardwareClaims* claims = HardwareClaims::instance()) {
                claims->release(resource, holder);
            }
        };
        options.callService = [id](const std::string& service, const std::string& json,
                                   std::function<void(exthost::ServiceResult)> answer) {
            ExtensionServices::callFromNative(id, service, json, std::move(answer));
        };
        if (!m_options.crashRoot.empty()) {
            options.crashes = m_options.crashRoot / pathOf(id);
            options.logFile = m_options.logFile;
        }
        options.crashed = [shared, id](std::uint32_t code) {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->post([id, code](NativeExtensions& owner) { emit owner.crashed(QString::fromStdString(id), code); });
            }
        };
        options.touchLost = [shared, id](std::uint64_t session) {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->lose(id, session);
            }
        };
        options.restarted = [shared, id, transports = wanted.native.transports] {
            log::info(kCategory, "{}: its process stopped and started again", id);
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                for (const ext::NativeTransport& transport : transports) {
                    alive->transportChanged(id, transport.name);
                }
            }
        };
        options.stopped = [shared, id, name] {
            log::warning(kCategory, "{}: its process stopped three times within a minute, so it was turned off", id);
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->post([id, name](NativeExtensions& owner) {
                    emit owner.stopped(QString::fromStdString(id), name);
                });
            }
        };
        auto made = std::make_shared<const exthost::Module*>(nullptr);
        options.readyWait = m_options.readyWait;
        options.ready = [shared, made] {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->post([made](NativeExtensions& owner) { owner.matchLate(*made); });
            }
        };
        options.notReady = [shared, made, id](const std::string& why) {
            log::warning(kCategory, "{}: the DLL did not get ready: {}", id, why);
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->post([made, why](NativeExtensions& owner) { owner.failLate(*made, why); });
            }
        };
        std::unique_ptr<exthost::HostedModule> hosted =
            exthost::HostedModule::start(std::move(options), problem);
        const bool waiting = hosted != nullptr && !hosted->ready();
        if (hosted != nullptr) {
            entry->module = std::shared_ptr<exthost::Module>(hosted.release(), [holder](exthost::Module* done) {
                delete done;
                releaseClaims(holder);
            });
            slot->set(entry->module);
        }
        *made = entry->module.get();
        if (waiting) {
            entry->pending = true;
            log::info(kCategory, "{}: the DLL is still getting ready in its own process", wanted.id);
            return entry;
        }
    }
    if (entry->module == nullptr) {
        entry->problems.push_back({ext::Severity::Error, ext::Code::NativeLoadFailed, where + ".dll", problem});
        log::warning(kCategory, "{}: the DLL was not loaded: {}", wanted.id, problem);
        return entry;
    }
    matchEntry(*entry);
    return entry;
}

void NativeExtensions::matchLate(const exthost::Module* module)
{
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = std::ranges::find_if(m_entries, [module](const std::shared_ptr<Entry>& entry) {
            return entry->pending && entry->module.get() == module;
        });
        if (found == m_entries.end()) {
            return;
        }
        auto next = std::make_shared<Entry>(**found);
        next->pending = false;
        matchEntry(*next);
        *found = std::move(next);
    }
    std::shared_ptr<ExtensionModules> modules = makeModules();
    {
        const std::scoped_lock lock(m_mutex);
        m_modules.swap(modules);
    }
    setExtensionInputTitles(inputTitles());
    emit changed();
    refreshProviders();
    refreshTransports();
    refreshSources();
    emit mediaChanged();
}

void NativeExtensions::failLate(const exthost::Module* module, const std::string& why)
{
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = std::ranges::find_if(m_entries, [module](const std::shared_ptr<Entry>& entry) {
            return entry->pending && entry->module.get() == module;
        });
        if (found == m_entries.end()) {
            return;
        }
        auto next = std::make_shared<Entry>(**found);
        next->pending = false;
        next->problems.push_back({ext::Severity::Error, ext::Code::NativeLoadFailed,
                                  "$[\"" + next->wanted.id + "\"].contributes.native.dll", why});
        *found = std::move(next);
    }
    emit changed();
    emit mediaChanged();
}

void NativeExtensions::apply(std::vector<NativeWanted> wanted)
{
    const std::vector<std::shared_ptr<Entry>> previous = entries();
    const std::vector<script::NodeInfo> nodesBefore = nodes();
    const QVariantList methodsBefore = touchMethods();
    const QVariantList attachmentsBefore = attachmentList();
    const QVariantList behaviorsBefore = behaviorList();
    const std::vector<std::pair<std::string, const exthost::Module*>> modulesBefore = moduleKeys();
    std::vector<std::shared_ptr<Entry>> next;
    next.reserve(wanted.size());
    for (NativeWanted& item : wanted) {
        const auto same = std::ranges::find_if(previous, [&item](const std::shared_ptr<Entry>& entry) {
            const NativeWanted& was = entry->wanted;
            return was.id == item.id && was.version == item.version && was.directory == item.directory &&
                   was.official == item.official && was.native == item.native && was.granted == item.granted;
        });
        if (same != previous.end() && ((*same)->module == nullptr || (*same)->module->working())) {
            auto kept = std::make_shared<Entry>(**same);
            const bool resettle = kept->module != nullptr && kept->wanted.settings != item.settings;
            kept->wanted = std::move(item);
            if (resettle) {
                kept->module->setSettings(kept->wanted.settings);
            }
            next.push_back(std::move(kept));
            continue;
        }
        next.push_back(load(item));
    }
    {
        const std::scoped_lock lock(m_mutex);
        m_entries = std::move(next);
    }
    std::shared_ptr<ExtensionModules> modules = makeModules();
    {
        const std::scoped_lock lock(m_mutex);
        m_modules.swap(modules);
    }
    const std::vector<std::shared_ptr<Entry>> current = entries();
    for (const std::shared_ptr<Entry>& was : previous) {
        if (!was->wanted.native.inputSources.empty() &&
            std::ranges::none_of(current, [&was](const std::shared_ptr<Entry>& now) {
                return now->loadNumber == was->loadNumber;
            })) {
            emit inputsEnded(QString::fromStdString(was->wanted.id));
        }
    }
    setExtensionInputTitles(inputTitles());
    if (nodes() != nodesBefore || touchMethods() != methodsBefore || attachmentList() != attachmentsBefore ||
        behaviorList() != behaviorsBefore || moduleKeys() != modulesBefore) {
        emit changed();
    }
    refreshProviders();
    for (const std::shared_ptr<Entry>& entry : previous) {
        const bool kept = std::ranges::any_of(current, [&entry](const std::shared_ptr<Entry>& now) {
            return now->module != nullptr && now->module == entry->module;
        });
        if (kept) {
            continue;
        }
        for (const ext::NativeTransport& transport : entry->wanted.native.transports) {
            m_changes->bump(transportKey(entry->wanted.id, transport.name));
        }
    }
    refreshTransports();
    refreshSources();
    emit mediaChanged();
}

void NativeExtensions::setSettings(const std::string& id, const std::string& json)
{
    std::shared_ptr<exthost::Module> module;
    {
        const std::scoped_lock lock(m_mutex);
        for (const std::shared_ptr<Entry>& entry : m_entries) {
            if (entry->wanted.id == id && entry->wanted.settings != json) {
                entry->wanted.settings = json;
                module = entry->module;
            }
        }
    }
    if (module != nullptr) {
        module->setSettings(json);
    }
}

std::vector<ProvidedDevice> NativeExtensions::providedDevices() const
{
    return m_provided;
}

void NativeExtensions::refreshProviders(std::string_view extension)
{
    std::vector<ProvidedDevice> next;
    if (!extension.empty()) {
        std::ranges::copy_if(m_provided, std::back_inserter(next),
                             [extension](const ProvidedDevice& device) { return device.extension != extension; });
    }
    for (const std::shared_ptr<Entry>& entry : entries()) {
        const NativeWanted& wanted = entry->wanted;
        if ((!extension.empty() && wanted.id != extension) || entry->module == nullptr || entry->pending ||
            !entry->module->working() || std::ranges::find(wanted.granted, ext::kDevicesPermission) == wanted.granted.end()) {
            continue;
        }
        for (std::size_t index = 0; index < wanted.native.deviceProviders.size(); ++index) {
            if (index >= entry->providerIndex.size() || !entry->providerIndex[index]) {
                continue;
            }
            const ext::NativeDeviceProvider& declared = wanted.native.deviceProviders[index];
            const exthost::CallResult listed = entry->module->listDevices(*entry->providerIndex[index]);
            if (!listed.ok()) {
                log::warning(kCategory, "{}: the device provider {} failed: {}", wanted.id, declared.name, listed.problem);
                continue;
            }
            const nlohmann::json devices = nlohmann::json::parse(listed.value.text, nullptr, false);
            std::size_t taken = 0;
            for (const nlohmann::json& device : devices.is_array() ? devices : nlohmann::json::array()) {
                std::string problem;
                std::optional<ProvidedDevice> made = providedOf(wanted, declared, device, problem);
                if (!made) {
                    log::warning(kCategory, "{}: a device from {} was left out: {}", wanted.id, declared.name, problem);
                } else if (taken++ < kMostProvidedDevices) {
                    next.push_back(std::move(*made));
                }
            }
        }
    }
    if (next != m_provided) {
        m_provided = std::move(next);
        emit providedChanged();
    }
}

exthost::CallResult NativeExtensions::actOnDevice(std::string_view extension, std::string_view provider,
                                                  std::string_view device, std::string_view action) const
{
    for (const std::shared_ptr<Entry>& entry : entries()) {
        if (entry->wanted.id != extension || entry->module == nullptr) {
            continue;
        }
        const std::vector<ext::NativeDeviceProvider>& providers = entry->wanted.native.deviceProviders;
        const auto found = std::ranges::find(providers, provider, &ext::NativeDeviceProvider::name);
        const auto index = static_cast<std::size_t>(found - providers.begin());
        if (found == providers.end() || index >= entry->providerIndex.size() || !entry->providerIndex[index]) {
            break;
        }
        return entry->module->actOnDevice(*entry->providerIndex[index], device, action);
    }
    return {exthost::CallStatus::Unavailable, {}, "the extension that adds the device is off"};
}

exthost::CallResult NativeExtensions::recognize(std::string_view extension, std::string_view engine,
                                                exthost::VisionKind kind, const exthost::VisionImage& image,
                                                std::string_view params) const
{
    for (const std::shared_ptr<Entry>& entry : entries()) {
        if (entry->wanted.id != extension) {
            continue;
        }
        if (entry->module == nullptr || entry->pending || !entry->module->working()) {
            break;
        }
        const std::vector<ext::NativeVisionEngine>& engines = entry->wanted.native.visionEngines;
        const auto found = std::ranges::find(engines, engine, &ext::NativeVisionEngine::name);
        const auto index = static_cast<std::size_t>(found - engines.begin());
        if (found == engines.end() || index >= entry->visionIndex.size() || !entry->visionIndex[index]) {
            break;
        }
        const std::string_view named = exthost::visionKindName(kind);
        if (std::ranges::find(found->kinds, named) == found->kinds.end()) {
            return {exthost::CallStatus::BadArgument, {}, "the engine does not recognize " + std::string(named)};
        }
        return entry->module->recognize(*entry->visionIndex[index], kind, image, params);
    }
    return {exthost::CallStatus::Unavailable, {}, "the extension that adds the engine is off or did not load"};
}

std::vector<NativeEventTarget> NativeExtensions::eventTargets() const
{
    std::vector<NativeEventTarget> targets;
    for (const std::shared_ptr<Entry>& entry : entries()) {
        if (entry->module == nullptr || entry->pending || !entry->module->working()) {
            continue;
        }
        const std::vector<std::string>& granted = entry->wanted.granted;
        targets.push_back({.extension = entry->wanted.id,
                           .everything = std::ranges::find(granted, exthost::kAppEventsPermission) != granted.end(),
                           .module = entry->module});
    }
    return targets;
}

std::vector<TransportPhone> NativeExtensions::transportPhones() const
{
    return m_phones;
}

void NativeExtensions::refreshTransports(std::string_view extension)
{
    std::vector<TransportPhone> next;
    if (!extension.empty()) {
        std::ranges::copy_if(m_phones, std::back_inserter(next),
                             [extension](const TransportPhone& phone) { return phone.extension != extension; });
    }
    for (const std::shared_ptr<Entry>& entry : entries()) {
        const NativeWanted& wanted = entry->wanted;
        if ((!extension.empty() && wanted.id != extension) || entry->module == nullptr || entry->pending ||
            !entry->module->working() ||
            std::ranges::find(wanted.granted, ext::kTransportPermission) == wanted.granted.end()) {
            continue;
        }
        for (std::size_t index = 0; index < wanted.native.transports.size(); ++index) {
            if (index >= entry->transportIndex.size() || !entry->transportIndex[index]) {
                continue;
            }
            const ext::NativeTransport& declared = wanted.native.transports[index];
            const QString title = index < wanted.transportTitles.size() && !wanted.transportTitles[index].isEmpty()
                                      ? wanted.transportTitles[index]
                                      : QString::fromStdString(declared.title);
            const exthost::CallResult found = entry->module->discover(*entry->transportIndex[index]);
            if (!found.ok()) {
                log::warning(kCategory, "{}: the transport {} failed: {}", wanted.id, declared.name, found.problem);
                continue;
            }
            const nlohmann::json phones = nlohmann::json::parse(found.value.text, nullptr, false);
            std::set<std::string> seen;
            std::size_t taken = 0;
            for (const nlohmann::json& phone : phones.is_array() ? phones : nlohmann::json::array()) {
                std::string problem;
                std::optional<TransportPhone> made = phoneOf(wanted, declared, title, phone, problem);
                if (made && !seen.insert(made->id).second) {
                    made.reset();
                    problem = "its id comes twice";
                }
                if (!made) {
                    log::warning(kCategory, "{}: a phone from {} was left out: {}", wanted.id, declared.name, problem);
                } else if (taken++ < kMostTransportPhones) {
                    next.push_back(std::move(*made));
                }
            }
        }
    }
    if (next != m_phones) {
        m_phones = std::move(next);
        emit transportPhonesChanged();
    }
}

std::shared_ptr<session::PhoneStreams> NativeExtensions::phoneStreams(
    const TransportPhone& phone, std::shared_ptr<const session::ServerPackage> package) const
{
    PhoneRoute route;
    route.key = transportKey(phone.extension, phone.transport);
    route.phone = phone.id;
    route.through = phone.transportTitle.toStdString();
    for (const std::shared_ptr<Entry>& entry : entries()) {
        if (entry->wanted.id != phone.extension) {
            continue;
        }
        const std::vector<ext::NativeTransport>& transports = entry->wanted.native.transports;
        const auto found = std::ranges::find(transports, phone.transport, &ext::NativeTransport::name);
        const auto index = static_cast<std::size_t>(found - transports.begin());
        const bool allowed = std::ranges::find(entry->wanted.granted, ext::kTransportPermission) !=
                             entry->wanted.granted.end();
        if (entry->module != nullptr && !entry->pending && allowed && found != transports.end() &&
            index < entry->transportIndex.size() && entry->transportIndex[index]) {
            route.module = entry->module;
            route.transport = *entry->transportIndex[index];
        }
        break;
    }
    return makePhoneStreams(std::move(route), m_changes, std::move(package));
}

std::vector<FrameTapSource> NativeExtensions::frameTaps() const
{
    std::vector<FrameTapSource> list;
    for (const std::shared_ptr<Entry>& entry : entries()) {
        const NativeWanted& wanted = entry->wanted;
        if (entry->module == nullptr || entry->pending || !entry->module->working() ||
            std::ranges::find(wanted.granted, ext::kMediaReadPermission) == wanted.granted.end()) {
            continue;
        }
        for (std::size_t index = 0; index < wanted.native.frameTaps.size(); ++index) {
            if (index >= entry->tapIndex.size() || !entry->tapIndex[index]) {
                continue;
            }
            const ext::NativeFrameTap& tap = wanted.native.frameTaps[index];
            list.push_back({.extension = wanted.id,
                            .extensionName = wanted.name,
                            .name = tap.name,
                            .title = index < wanted.tapTitles.size() && !wanted.tapTitles[index].isEmpty()
                                         ? wanted.tapTitles[index]
                                         : QString::fromStdString(tap.title),
                            .fps = static_cast<int>(tap.fps.value_or(ext::kDefaultTapFps)),
                            .size = static_cast<int>(tap.size.value_or(ext::kDefaultTapSize)),
                            .module = entry->module,
                            .identity = entry->module.get(),
                            .index = *entry->tapIndex[index]});
        }
    }
    return list;
}

void NativeExtensions::setGlobals(GlobalChoices globals)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (globals == m_globals) {
            return;
        }
        m_globals = std::move(globals);
    }
    std::shared_ptr<ExtensionModules> modules = makeModules();
    {
        const std::scoped_lock lock(m_mutex);
        m_modules.swap(modules);
    }
    emit globalsChanged();
}

std::vector<script::NodeInfo> NativeExtensions::nodes() const
{
    std::vector<script::NodeInfo> list;
    for (const std::shared_ptr<Entry>& entry : entries()) {
        if (entry->module == nullptr || !entry->module->working()) {
            continue;
        }
        const NativeWanted& wanted = entry->wanted;
        for (std::size_t index = 0; index < wanted.native.nodes.size(); ++index) {
            if (index >= entry->nodeIndex.size() || !entry->nodeIndex[index]) {
                continue;
            }
            const ext::NativeNode& node = wanted.native.nodes[index];
            script::NodeInfo info;
            info.extension = wanted.id;
            info.extensionName = wanted.name.toStdString();
            info.name = node.name;
            for (const ext::NativeParam& param : node.params) {
                info.params.push_back({param.name, script::nodeTypeNamed(param.type).value_or(script::NodeType::Json)});
            }
            info.returns = script::nodeTypeNamed(node.returns).value_or(script::NodeType::None);
            info.description = index < wanted.nodeDescriptions.size() && !wanted.nodeDescriptions[index].isEmpty()
                                   ? wanted.nodeDescriptions[index].toStdString()
                                   : node.description;
            info.permissions = node.permissions;
            list.push_back(std::move(info));
        }
        for (std::size_t index = 0; index < wanted.native.jobs.size(); ++index) {
            if (index >= entry->jobIndex.size() || !entry->jobIndex[index]) {
                continue;
            }
            const ext::NativeJob& job = wanted.native.jobs[index];
            script::NodeInfo info;
            info.extension = wanted.id;
            info.extensionName = wanted.name.toStdString();
            info.name = job.name;
            for (const ext::NativeParam& param : job.params) {
                info.params.push_back({param.name, script::nodeTypeNamed(param.type).value_or(script::NodeType::Json)});
            }
            info.returns = script::nodeTypeNamed(job.returns).value_or(script::NodeType::None);
            info.description = job.description.empty() ? job.title : job.description;
            info.permissions = job.permissions;
            info.job = true;
            list.push_back(std::move(info));
        }
    }
    return list;
}

NativeJobTarget NativeExtensions::jobTarget(std::string_view extension, std::string_view job) const
{
    std::shared_ptr<Entry> entry;
    for (const std::shared_ptr<Entry>& each : entries()) {
        if (each->wanted.id == extension) {
            entry = each;
            break;
        }
    }
    if (entry == nullptr || entry->module == nullptr || !entry->module->working()) {
        return {.code = "GS-API-060",
                .problem = "the extension that adds it is off or did not load; turn it on on the Extensions page"};
    }
    const NativeWanted& wanted = entry->wanted;
    if (entry->pending) {
        return {.code = "GS-API-060", .problem = wanted.name.toStdString() + " is still starting; try again in a moment"};
    }
    const auto found = std::ranges::find(wanted.native.jobs, job, &ext::NativeJob::name);
    const auto index = static_cast<std::size_t>(found - wanted.native.jobs.begin());
    if (found == wanted.native.jobs.end() || index >= entry->jobIndex.size() || !entry->jobIndex[index]) {
        return {.code = "GS-API-060", .problem = wanted.name.toStdString() + " does not offer this job"};
    }
    for (const std::string& permission : found->permissions) {
        if (std::ranges::find(wanted.granted, permission) == wanted.granted.end()) {
            return {.code = "GS-API-063",
                    .problem = "it needs the permission \"" + permission + "\", which " + wanted.name.toStdString() +
                               " has not been allowed; turn the extension off and on again on the Extensions page to "
                               "allow it"};
        }
    }
    return {.module = entry->module,
            .index = *entry->jobIndex[index],
            .declared = *found,
            .extensionName = wanted.name.toStdString()};
}

script::NodeAnswer NativeExtensions::call(std::string_view extension, std::string_view node,
                                          const std::vector<script::NodeValue>& arguments) const
{
    std::shared_ptr<Entry> entry;
    for (const std::shared_ptr<Entry>& each : entries()) {
        if (each->wanted.id == extension) {
            entry = each;
            break;
        }
    }
    if (entry == nullptr || entry->module == nullptr || !entry->module->working()) {
        return {{}, "GS-API-060", "the extension that adds it is off or did not load; turn it on on the Extensions page"};
    }
    if (entry->pending) {
        return {{}, "GS-API-060",
                entry->wanted.name.toStdString() + " is still starting; try again in a moment"};
    }
    const NativeWanted& wanted = entry->wanted;
    const auto found = std::ranges::find(wanted.native.nodes, node, &ext::NativeNode::name);
    const auto index = static_cast<std::size_t>(found - wanted.native.nodes.begin());
    if (found == wanted.native.nodes.end() || index >= entry->nodeIndex.size() || !entry->nodeIndex[index]) {
        return {{}, "GS-API-060", wanted.name.toStdString() + " does not offer this node"};
    }
    for (const std::string& permission : found->permissions) {
        if (std::ranges::find(wanted.granted, permission) == wanted.granted.end()) {
            return {{},
                    "GS-API-063",
                    "it needs the permission \"" + permission + "\", which " + wanted.name.toStdString() +
                        " has not been allowed; turn the extension off and on again on the Extensions page to allow it"};
        }
    }
    std::vector<exthost::Value> values;
    values.reserve(arguments.size());
    for (const script::NodeValue& argument : arguments) {
        values.push_back(valueOf(argument));
    }
    const exthost::CallResult result = entry->module->call(*entry->nodeIndex[index], values);
    switch (result.status) {
    case exthost::CallStatus::Ok:
        return {nodeValueOf(result.value), {}, {}};
    case exthost::CallStatus::BadArgument:
        return {{}, "GS-API-061", result.problem};
    case exthost::CallStatus::TooSlow:
        return {{}, "GS-API-064", result.problem};
    case exthost::CallStatus::Unavailable:
        return {{}, "GS-API-060", result.problem};
    case exthost::CallStatus::Failed:
    case exthost::CallStatus::Crashed:
        break;
    }
    return {{}, "GS-API-062", result.problem};
}

QVariantList NativeExtensions::touchMethods() const
{
    QVariantList list;
    for (const std::shared_ptr<Entry>& entry : entries()) {
        if (entry->module == nullptr || !entry->module->working()) {
            continue;
        }
        const NativeWanted& wanted = entry->wanted;
        for (std::size_t index = 0; index < wanted.native.touchMethods.size(); ++index) {
            if (index >= entry->methodIndex.size() || !entry->methodIndex[index]) {
                continue;
            }
            const ext::NativeTouchMethod& method = wanted.native.touchMethods[index];
            list.append(QVariantMap{
                {QStringLiteral("key"), QString::fromStdString(wanted.id + "/" + method.name)},
                {QStringLiteral("title"), index < wanted.methodTitles.size() && !wanted.methodTitles[index].isEmpty()
                                              ? wanted.methodTitles[index]
                                              : QString::fromStdString(method.title)},
                {QStringLiteral("description"),
                 index < wanted.methodDescriptions.size() && !wanted.methodDescriptions[index].isEmpty()
                     ? wanted.methodDescriptions[index]
                     : QString::fromStdString(method.description)},
                {QStringLiteral("extension"), wanted.name},
                {QStringLiteral("wantsDevice"), method.wantsDevice}});
        }
    }
    return list;
}

std::vector<ext::Diagnostic> NativeExtensions::problems() const
{
    std::vector<ext::Diagnostic> found;
    for (const std::shared_ptr<Entry>& entry : entries()) {
        found.insert(found.end(), entry->problems.begin(), entry->problems.end());
    }
    return found;
}

bool NativeExtensions::loaded(std::string_view id) const
{
    return std::ranges::any_of(entries(), [id](const std::shared_ptr<Entry>& entry) {
        return entry->wanted.id == id && entry->module != nullptr && entry->module->working();
    });
}

std::unique_ptr<ExtensionTouch> NativeExtensions::openTouch(std::string_view key, std::string_view device,
                                                            ControlSink& injection, std::function<void()> fellBack)
{
    const std::size_t slash = key.rfind('/');
    if (slash == std::string_view::npos) {
        return nullptr;
    }
    const std::string_view id = key.substr(0, slash);
    const std::string_view name = key.substr(slash + 1);
    for (const std::shared_ptr<Entry>& entry : entries()) {
        if (entry->wanted.id != id || entry->module == nullptr || !entry->module->working()) {
            continue;
        }
        const std::vector<ext::NativeTouchMethod>& methods = entry->wanted.native.touchMethods;
        const auto found = std::ranges::find(methods, name, &ext::NativeTouchMethod::name);
        const auto index = static_cast<std::size_t>(found - methods.begin());
        if (found == methods.end() || index >= entry->methodIndex.size() || !entry->methodIndex[index]) {
            return nullptr;
        }
        const std::string options = m_touchOptions ? m_touchOptions(key, device) : std::string();
        auto port = std::make_shared<ModuleTouchPort>(entry->module, *entry->methodIndex[index], device, options);
        if (!port->working()) {
            return nullptr;
        }
        m_shared->keep(entry->wanted.id, port);
        return std::make_unique<ExtensionTouchSink>(injection, std::move(port), std::move(fellBack));
    }
    return nullptr;
}

QString NativeExtensions::touchTitle(std::string_view key) const
{
    for (const QVariant& method : touchMethods()) {
        const QVariantMap entry = method.toMap();
        if (entry.value(QStringLiteral("key")).toString().toStdString() == key) {
            return entry.value(QStringLiteral("title")).toString();
        }
    }
    return QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()));
}

void NativeExtensions::showNotice(const QString& title, const QString& text)
{
    if (m_tray == nullptr) {
        m_tray = std::make_unique<platform::TrayNotice>([] {});
    }
    static_cast<void>(m_tray->show(title.toStdWString(), text.toStdWString()));
}

static_assert(static_cast<int>(ext::NativeSlotKind::Key) == static_cast<int>(input::BehaviorSlotKind::Key));
static_assert(static_cast<int>(ext::NativeSlotKind::Axis2) == static_cast<int>(input::BehaviorSlotKind::Axis2));
static_assert(static_cast<int>(ext::NativeSlotKind::Axis1) == static_cast<int>(input::BehaviorSlotKind::Axis1));

namespace {

constexpr Microseconds kFaultWindow{10'000'000};
constexpr std::size_t kFaultLimit = 3;

QString textOf(const std::string& text)
{
    return QString::fromStdString(text);
}

NativeTexts completed(const std::vector<NativeTexts>& given, std::size_t index, const std::string& title,
                      const std::string& description, const std::vector<ext::DeclaredParam>& params,
                      const std::vector<ext::DeclaredSlot>& declaredSlots)
{
    NativeTexts texts = index < given.size() ? given[index] : NativeTexts{};
    if (texts.title.isEmpty()) {
        texts.title = textOf(title);
    }
    if (texts.description.isEmpty()) {
        texts.description = textOf(description);
    }
    texts.paramTitles.resize(params.size());
    texts.paramDescriptions.resize(params.size());
    texts.paramOptions.resize(params.size());
    for (std::size_t at = 0; at < params.size(); ++at) {
        const ext::DeclaredParam& param = params[at];
        if (texts.paramTitles[at].isEmpty()) {
            texts.paramTitles[at] = textOf(param.title.empty() ? param.key : param.title);
        }
        if (texts.paramDescriptions[at].isEmpty()) {
            texts.paramDescriptions[at] = textOf(param.description);
        }
        std::vector<QString>& options = texts.paramOptions[at];
        options.resize(param.options.size());
        for (std::size_t option = 0; option < param.options.size(); ++option) {
            if (options[option].isEmpty()) {
                options[option] = textOf(param.options[option]);
            }
        }
    }
    texts.slotTitles.resize(declaredSlots.size());
    for (std::size_t at = 0; at < declaredSlots.size(); ++at) {
        const ext::DeclaredSlot& slot = declaredSlots[at];
        if (texts.slotTitles[at].isEmpty()) {
            texts.slotTitles[at] = textOf(slot.title.empty() ? slot.name : slot.title);
        }
    }
    return texts;
}

QString slotKindName(ext::NativeSlotKind kind)
{
    const std::string_view name = ext::kNativeSlotKindNames[static_cast<std::size_t>(kind)];
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

}

std::vector<DeclaredModule> NativeExtensions::declaredModules() const
{
    std::vector<DeclaredModule> list;
    for (const std::shared_ptr<Entry>& entry : entries()) {
        if (entry->module == nullptr || !entry->module->working()) {
            continue;
        }
        const NativeWanted& wanted = entry->wanted;
        for (std::size_t index = 0; index < wanted.native.attachments.size(); ++index) {
            if (index >= entry->attachmentIndex.size() || !entry->attachmentIndex[index]) {
                continue;
            }
            const ext::NativeAttachment& declared = wanted.native.attachments[index];
            DeclaredModule made;
            made.module = wanted.id + "/" + declared.name;
            made.extension = wanted.id;
            made.extensionName = wanted.name;
            made.kind = exthost::InstanceKind::Attachment;
            made.appliesTo = declared.appliesTo;
            made.params = declared.params;
            made.texts = completed(wanted.attachmentTexts, index, declared.title, declared.description,
                                   declared.params, {});
            list.push_back(std::move(made));
        }
        for (std::size_t index = 0; index < wanted.native.behaviors.size(); ++index) {
            if (index >= entry->behaviorIndex.size() || !entry->behaviorIndex[index]) {
                continue;
            }
            const ext::NativeBehavior& declared = wanted.native.behaviors[index];
            DeclaredModule made;
            made.module = wanted.id + "/" + declared.name;
            made.extension = wanted.id;
            made.extensionName = wanted.name;
            made.kind = exthost::InstanceKind::Behavior;
            made.slotList = declared.slotList;
            made.fingers = static_cast<int>(declared.fingers.value_or(1));
            made.frames = declared.frames;
            made.params = declared.params;
            made.texts = completed(wanted.behaviorTexts, index, declared.title, declared.description, declared.params,
                                   declared.slotList);
            list.push_back(std::move(made));
        }
    }
    return list;
}

std::optional<DeclaredModule> NativeExtensions::declaredModule(std::string_view module) const
{
    for (DeclaredModule& declared : declaredModules()) {
        if (declared.module == module) {
            return std::move(declared);
        }
    }
    return std::nullopt;
}

QVariantList NativeExtensions::attachmentList() const
{
    QVariantList list;
    for (const DeclaredModule& declared : declaredModules()) {
        if (declared.kind != exthost::InstanceKind::Attachment) {
            continue;
        }
        QStringList fits;
        for (const std::string& type : declared.appliesTo) {
            fits.append(textOf(type));
        }
        list.append(QVariantMap{{QStringLiteral("module"), textOf(declared.module)},
                                {QStringLiteral("title"), declared.texts.title},
                                {QStringLiteral("description"), declared.texts.description},
                                {QStringLiteral("extension"), declared.extensionName},
                                {QStringLiteral("appliesTo"), fits}});
    }
    return list;
}

QVariantList NativeExtensions::behaviorList() const
{
    QVariantList list;
    for (const DeclaredModule& declared : declaredModules()) {
        if (declared.kind != exthost::InstanceKind::Behavior) {
            continue;
        }
        QVariantList inputs;
        for (std::size_t index = 0; index < declared.slotList.size(); ++index) {
            inputs.append(QVariantMap{{QStringLiteral("name"), textOf(declared.slotList[index].name)},
                                      {QStringLiteral("kind"), slotKindName(declared.slotList[index].kind)},
                                      {QStringLiteral("title"), declared.texts.slotTitles[index]}});
        }
        list.append(QVariantMap{{QStringLiteral("module"), textOf(declared.module)},
                                {QStringLiteral("title"), declared.texts.title},
                                {QStringLiteral("description"), declared.texts.description},
                                {QStringLiteral("extension"), declared.extensionName},
                                {QStringLiteral("inputs"), inputs},
                                {QStringLiteral("fingers"), declared.fingers},
                                {QStringLiteral("frames"), declared.frames}});
    }
    return list;
}

std::shared_ptr<ExtensionModules> NativeExtensions::moduleFactory() const
{
    const std::scoped_lock lock(m_mutex);
    return m_modules;
}

input::ModuleCatalog NativeExtensions::moduleCatalog() const
{
    const std::shared_ptr<ExtensionModules> modules = moduleFactory();
    return modules != nullptr ? modules->catalog() : input::ModuleCatalog{};
}

std::shared_ptr<ExtensionModules> NativeExtensions::makeModules() const
{
    GlobalChoices chosen;
    {
        const std::scoped_lock lock(m_mutex);
        chosen = m_globals;
    }
    std::vector<ModuleEntry> modules;
    std::vector<input::GlobalAttachment> globals;
    for (const std::shared_ptr<Entry>& entry : entries()) {
        if (entry->module == nullptr || !entry->module->working()) {
            continue;
        }
        const NativeWanted& wanted = entry->wanted;
        const auto turnedOn = chosen.find(wanted.id);
        for (std::size_t index = 0; index < wanted.native.attachments.size(); ++index) {
            if (index >= entry->attachmentIndex.size() || !entry->attachmentIndex[index]) {
                continue;
            }
            const ext::NativeAttachment& declared = wanted.native.attachments[index];
            modules.push_back({.module = wanted.id + "/" + declared.name,
                               .extension = wanted.id,
                               .kind = exthost::InstanceKind::Attachment,
                               .host = entry->module,
                               .index = *entry->attachmentIndex[index],
                               .params = declared.params,
                               .appliesTo = declared.appliesTo,
                               .shape = {}});
            if (turnedOn == chosen.end()) {
                continue;
            }
            if (const auto params = turnedOn->second.find(declared.name); params != turnedOn->second.end()) {
                globals.push_back({.module = wanted.id + "/" + declared.name,
                                   .params = params->second,
                                   .appliesTo = declared.appliesTo});
            }
        }
        for (std::size_t index = 0; index < wanted.native.behaviors.size(); ++index) {
            if (index >= entry->behaviorIndex.size() || !entry->behaviorIndex[index]) {
                continue;
            }
            const ext::NativeBehavior& declared = wanted.native.behaviors[index];
            input::BehaviorShape shape;
            for (const ext::DeclaredSlot& slot : declared.slotList) {
                shape.slotList.push_back({.name = slot.name, .kind = static_cast<input::BehaviorSlotKind>(slot.kind)});
            }
            shape.fingers = static_cast<std::uint8_t>(std::clamp<std::int64_t>(declared.fingers.value_or(1), 1, 10));
            shape.frames = declared.frames;
            modules.push_back({.module = wanted.id + "/" + declared.name,
                               .extension = wanted.id,
                               .kind = exthost::InstanceKind::Behavior,
                               .host = entry->module,
                               .index = *entry->behaviorIndex[index],
                               .params = declared.params,
                               .appliesTo = {},
                               .shape = std::move(shape)});
        }
    }
    const std::weak_ptr<Shared> shared = m_shared;
    return std::make_shared<ExtensionModules>(
        std::move(modules),
        [shared](const ModuleNotice& notice) {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->post([notice](NativeExtensions& owner) { owner.noteTrouble(notice); });
            }
        },
        std::move(globals));
}

std::vector<std::pair<std::string, const exthost::Module*>> NativeExtensions::moduleKeys() const
{
    std::vector<std::pair<std::string, const exthost::Module*>> keys;
    if (const std::shared_ptr<ExtensionModules> modules = moduleFactory()) {
        for (const ModuleEntry& entry : modules->entries()) {
            keys.emplace_back(entry.module, entry.host.get());
        }
    }
    return keys;
}

void NativeExtensions::noteTrouble(const ModuleNotice& notice)
{
    switch (notice.kind) {
    case input::ModuleTroubleKind::Missing:
        log::warning(kCategory, "{}: {} did not start on {}", notice.extension, notice.module, notice.control);
        break;
    case input::ModuleTroubleKind::Failed:
        log::warning(kCategory, "{}: {} failed on {} and was skipped", notice.extension, notice.module,
                     notice.control);
        break;
    case input::ModuleTroubleKind::Stopped:
        log::warning(kCategory, "{}: {} failed on {}, which stopped until the profile is applied again",
                     notice.extension, notice.module, notice.control);
        break;
    }
    emit moduleTrouble(textOf(notice.module), textOf(notice.control), static_cast<int>(notice.kind));
    if (notice.kind == input::ModuleTroubleKind::Missing) {
        return;
    }
    const Clock& clock = m_options.clock != nullptr ? *m_options.clock : static_cast<const Clock&>(m_steady);
    const Microseconds now = clock.now();
    std::deque<Microseconds>& times = m_faults[notice.extension];
    times.push_back(now);
    while (!times.empty() && now - times.front() >= kFaultWindow) {
        times.pop_front();
    }
    if (times.size() < kFaultLimit) {
        return;
    }
    times.clear();
    QString name = textOf(notice.extension);
    for (const std::shared_ptr<Entry>& entry : entries()) {
        if (entry->wanted.id == notice.extension && !entry->wanted.name.isEmpty()) {
            name = entry->wanted.name;
        }
    }
    log::warning(kCategory, "{}: its attachments or behaviors failed {} times within 10 seconds, so it was turned off",
                 notice.extension, kFaultLimit);
    emit modulesFailed(textOf(notice.extension), name);
}

void NativeExtensions::deliverInput(const std::string& id, std::uint64_t loadNumber, const std::string& source,
                                    const std::string& slot, double x, double y)
{
    std::shared_ptr<Entry> entry;
    for (const std::shared_ptr<Entry>& each : entries()) {
        if (each->wanted.id == id && each->loadNumber == loadNumber) {
            entry = each;
            break;
        }
    }
    if (entry == nullptr || entry->module == nullptr || entry->pending ||
        std::ranges::find(entry->wanted.granted, ext::kInputPermission) == entry->wanted.granted.end()) {
        return;
    }
    const std::vector<ext::NativeInputSource>& sources = entry->wanted.native.inputSources;
    const auto found = std::ranges::find(sources, source, &ext::NativeInputSource::name);
    const auto index = static_cast<std::size_t>(found - sources.begin());
    if (found == sources.end() || index >= entry->inputIndex.size() || !entry->inputIndex[index]) {
        return;
    }
    const auto declared = std::ranges::find(found->slotList, slot, &ext::NativeInputSlot::name);
    if (declared == found->slotList.end()) {
        return;
    }
    const auto unit = [](double value) { return value >= -1.0 && value <= 1.0; };
    const bool button = declared->kind == ext::InputSlotKind::Button;
    const bool stick = declared->kind == ext::InputSlotKind::Stick;
    if (button ? (x != 0.0 && x != 1.0) : (!unit(x) || (stick && !unit(y)))) {
        return;
    }
    emit inputEvent(textOf(id), textOf(input::extensionInputName(id, source, slot)), button, x, stick ? y : 0.0);
}

ExtensionInputTitles NativeExtensions::inputTitles() const
{
    ExtensionInputTitles titles;
    for (const std::shared_ptr<Entry>& entry : entries()) {
        if (entry->module == nullptr) {
            continue;
        }
        const NativeWanted& wanted = entry->wanted;
        for (std::size_t index = 0; index < wanted.native.inputSources.size(); ++index) {
            if (index >= entry->inputIndex.size() || !entry->inputIndex[index]) {
                continue;
            }
            const ext::NativeInputSource& source = wanted.native.inputSources[index];
            const NativeTexts texts = index < wanted.inputTexts.size() ? wanted.inputTexts[index] : NativeTexts{};
            const QString sourceTitle = texts.title.isEmpty() ? textOf(source.title) : texts.title;
            for (std::size_t at = 0; at < source.slotList.size(); ++at) {
                const ext::NativeInputSlot& slot = source.slotList[at];
                const QString slotTitle = at < texts.slotTitles.size() && !texts.slotTitles[at].isEmpty()
                                              ? texts.slotTitles[at]
                                              : textOf(slot.title.empty() ? slot.name : slot.title);
                titles.insert_or_assign(input::extensionInputName(wanted.id, source.name, slot.name),
                                        ExtensionInputTitle{.source = sourceTitle, .slot = slotTitle});
            }
        }
    }
    return titles;
}

namespace {

constexpr std::size_t kMostListedSources = 16;

bool grants(const NativeWanted& wanted, std::string_view permission)
{
    return std::ranges::find(wanted.granted, permission) != wanted.granted.end();
}

QString titleAt(const std::vector<QString>& titles, std::size_t index, const std::string& fallback)
{
    return index < titles.size() && !titles[index].isEmpty() ? titles[index] : QString::fromStdString(fallback);
}

template <typename Declared>
std::optional<std::size_t> matchedIndex(const std::vector<Declared>& declared, std::string_view name,
                                        const std::vector<std::optional<std::size_t>>& matched)
{
    const auto found = std::ranges::find(declared, name, &Declared::name);
    const auto at = static_cast<std::size_t>(found - declared.begin());
    if (found == declared.end() || at >= matched.size()) {
        return std::nullopt;
    }
    return matched[at];
}

bool usable(const NativeExtensions::Entry& entry)
{
    return entry.module != nullptr && !entry.pending && entry.module->working();
}

}

std::vector<MediaSourceDevice> NativeExtensions::mediaSources() const
{
    return m_sources;
}

void NativeExtensions::refreshSources(std::string_view extension)
{
    std::vector<MediaSourceDevice> next;
    if (!extension.empty()) {
        std::ranges::copy_if(m_sources, std::back_inserter(next),
                             [extension](const MediaSourceDevice& source) { return source.extension != extension; });
    }
    for (const std::shared_ptr<Entry>& entry : entries()) {
        const NativeWanted& wanted = entry->wanted;
        if ((!extension.empty() && wanted.id != extension) || !usable(*entry) ||
            !grants(wanted, ext::kMediaCapturePermission)) {
            continue;
        }
        for (const exthost::MediaKind kind : {exthost::MediaKind::Video, exthost::MediaKind::Audio}) {
            const bool video = kind == exthost::MediaKind::Video;
            const std::size_t declared = video ? wanted.native.videoSources.size() : wanted.native.audioSources.size();
            const std::vector<std::optional<std::size_t>>& matched = video ? entry->videoIndex : entry->audioIndex;
            for (std::size_t index = 0; index < declared; ++index) {
                if (index >= matched.size() || !matched[index]) {
                    continue;
                }
                const std::string& name =
                    video ? wanted.native.videoSources[index].name : wanted.native.audioSources[index].name;
                const exthost::CallResult listed = entry->module->listSources(kind, *matched[index]);
                if (!listed.ok()) {
                    log::warning(kCategory, "{}: the source {} failed: {}", wanted.id, name, listed.problem);
                    continue;
                }
                const nlohmann::json found = nlohmann::json::parse(listed.value.text, nullptr, false);
                std::size_t taken = 0;
                for (const nlohmann::json& item : found.is_array() ? found : nlohmann::json::array()) {
                    const std::optional<std::string> id =
                        item.is_object() ? textIn(item, "id", exthost::kMostSourceIdBytes) : std::nullopt;
                    const std::optional<std::string> shown =
                        item.is_object() ? textIn(item, "name", 64) : std::nullopt;
                    if (!id || !exthost::validSourceId(*id) || !shown) {
                        log::warning(kCategory, "{}: an entry of the source {} has no valid id or name", wanted.id,
                                     name);
                        continue;
                    }
                    if (taken++ >= kMostListedSources) {
                        break;
                    }
                    next.push_back({.extension = wanted.id,
                                    .extensionName = wanted.name,
                                    .kind = kind,
                                    .source = name,
                                    .sourceTitle = video ? titleAt(wanted.videoSourceTitles, index,
                                                                   wanted.native.videoSources[index].title)
                                                         : titleAt(wanted.audioSourceTitles, index,
                                                                   wanted.native.audioSources[index].title),
                                    .id = *id,
                                    .name = QString::fromStdString(shown->empty() ? *id : *shown),
                                    .kinds = video ? wanted.native.videoSources[index].kinds
                                                   : std::vector<std::string>{}});
                }
            }
        }
    }
    if (next != m_sources) {
        m_sources = std::move(next);
        emit sourcesChanged();
    }
}

std::unique_ptr<SourceLease> NativeExtensions::openSource(const MediaSourceDevice& source,
                                                          std::string_view params) const
{
    for (const std::shared_ptr<Entry>& entry : entries()) {
        const NativeWanted& wanted = entry->wanted;
        if (wanted.id != source.extension || !usable(*entry) || !grants(wanted, ext::kMediaCapturePermission)) {
            continue;
        }
        const std::optional<std::size_t> index =
            source.kind == exthost::MediaKind::Video
                ? matchedIndex(wanted.native.videoSources, source.source, entry->videoIndex)
                : matchedIndex(wanted.native.audioSources, source.source, entry->audioIndex);
        if (!index) {
            return nullptr;
        }
        std::shared_ptr<exthost::MediaStream> stream =
            entry->module->openSource(source.kind, *index, source.id, params);
        if (stream == nullptr) {
            log::warning(kCategory, "{}: the source {} did not open {}", wanted.id, source.source, source.id);
            return nullptr;
        }
        return std::make_unique<SourceLease>(entry->module, std::move(stream));
    }
    return nullptr;
}

std::vector<MediaSinkOffer> NativeExtensions::mediaSinks() const
{
    std::vector<MediaSinkOffer> list;
    for (const std::shared_ptr<Entry>& entry : entries()) {
        const NativeWanted& wanted = entry->wanted;
        if (!usable(*entry) || !grants(wanted, ext::kMediaOutputPermission)) {
            continue;
        }
        for (std::size_t index = 0; index < wanted.native.mediaSinks.size(); ++index) {
            if (index >= entry->sinkIndex.size() || !entry->sinkIndex[index]) {
                continue;
            }
            const ext::NativeMediaSink& sink = wanted.native.mediaSinks[index];
            const ext::SinkVideo video = sink.video.value_or(ext::SinkVideo{});
            list.push_back({.extension = wanted.id,
                            .extensionName = wanted.name,
                            .name = sink.name,
                            .title = titleAt(wanted.sinkTitles, index, sink.title),
                            .width = static_cast<std::uint32_t>(video.width.value_or(0)),
                            .height = static_cast<std::uint32_t>(video.height.value_or(0)),
                            .fps = static_cast<std::uint32_t>(video.fps.value_or(0)),
                            .audio = sink.audio});
        }
    }
    return list;
}

std::unique_ptr<SinkLease> NativeExtensions::openSink(std::string_view extension, std::string_view sink,
                                                      const exthost::SinkStart& start) const
{
    for (const std::shared_ptr<Entry>& entry : entries()) {
        const NativeWanted& wanted = entry->wanted;
        if (wanted.id != extension || !usable(*entry) || !grants(wanted, ext::kMediaOutputPermission)) {
            continue;
        }
        const std::optional<std::size_t> index = matchedIndex(wanted.native.mediaSinks, sink, entry->sinkIndex);
        if (!index) {
            return nullptr;
        }
        std::shared_ptr<exthost::SinkPort> port = entry->module->openSink(*index, start);
        if (port == nullptr) {
            log::warning(kCategory, "{}: the sink {} did not start", wanted.id, sink);
            return nullptr;
        }
        return std::make_unique<SinkLease>(entry->module, std::move(port));
    }
    return nullptr;
}

std::vector<ExporterOffer> NativeExtensions::exporters() const
{
    std::vector<ExporterOffer> list;
    for (const std::shared_ptr<Entry>& entry : entries()) {
        const NativeWanted& wanted = entry->wanted;
        if (!usable(*entry) || !grants(wanted, ext::kMediaOutputPermission)) {
            continue;
        }
        for (std::size_t index = 0; index < wanted.native.recordingExporters.size(); ++index) {
            if (index >= entry->exporterIndex.size() || !entry->exporterIndex[index]) {
                continue;
            }
            const ext::NativeRecordingExporter& exporter = wanted.native.recordingExporters[index];
            list.push_back({.extension = wanted.id,
                            .extensionName = wanted.name,
                            .name = exporter.name,
                            .title = titleAt(wanted.exporterTitles, index, exporter.title),
                            .fileExtension = exporter.extension});
        }
    }
    return list;
}

bool NativeExtensions::startExport(std::string_view extension, std::string_view exporter, std::string_view input,
                                   std::string_view output, exthost::JobEvents events) const
{
    for (const std::shared_ptr<Entry>& entry : entries()) {
        const NativeWanted& wanted = entry->wanted;
        if (wanted.id != extension || !usable(*entry) || !grants(wanted, ext::kMediaOutputPermission)) {
            continue;
        }
        const std::optional<std::size_t> index =
            matchedIndex(wanted.native.recordingExporters, exporter, entry->exporterIndex);
        return index && entry->module->startExport(*index, input, output, std::move(events)) != 0;
    }
    return false;
}

}
