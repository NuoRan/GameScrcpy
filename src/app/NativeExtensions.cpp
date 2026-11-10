#include "NativeExtensions.h"

#include "ExtensionTouchSink.h"

#include <gs/base/Log.h>
#include <gs/exthost/Hosted.h>
#include <gs/exthost/InProcess.h>
#include <gs/platform/TrayNotice.h>

#include <QMetaObject>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <map>
#include <optional>
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

}

struct NativeExtensions::Shared {
    std::mutex mutex;
    NativeExtensions* owner = nullptr;
    std::map<std::pair<std::string, std::uint64_t>, std::weak_ptr<ModuleTouchPort>> ports;

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

class EntryServices final : public exthost::HostServices
{
public:
    EntryServices(std::string id, QString name, std::weak_ptr<NativeExtensions::Shared> shared)
        : m_id(std::move(id))
        , m_name(std::move(name))
        , m_shared(std::move(shared))
    {
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

private:
    std::string m_id;
    QString m_name;
    std::weak_ptr<NativeExtensions::Shared> m_shared;
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
    bool pending = false;
};

namespace {

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
    m_modules = makeModules();
    s_instance = this;
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
    const std::string where = "$[\"" + wanted.id + "\"].contributes.native";
    const std::filesystem::path dll = wanted.directory / pathOf(wanted.native.dll);
    const std::filesystem::path store =
        m_options.storeRoot.empty() ? std::filesystem::path() : m_options.storeRoot / pathOf(wanted.id + ".json");
    std::string problem;
    if (wanted.official) {
        auto services = std::make_shared<EntryServices>(wanted.id, wanted.name, m_shared);
        std::unique_ptr<exthost::InProcessModule> module = exthost::InProcessModule::load(
            {.dll = dll, .store = store, .granted = wanted.granted, .services = services.get()}, problem);
        if (module != nullptr) {
            entry->module = std::shared_ptr<exthost::Module>(module.release(),
                                                             [services](exthost::Module* done) { delete done; });
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
        options.clock = m_options.clock;
        options.log = [id](int level, const std::string& text) { writeLog(id, level, text); };
        options.notify = [shared, name](const std::string& title, const std::string& text) {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->notify(name, title, text);
            }
        };
        options.touchLost = [shared, id](std::uint64_t session) {
            if (const std::shared_ptr<Shared> alive = shared.lock()) {
                alive->lose(id, session);
            }
        };
        options.restarted = [id] { log::info(kCategory, "{}: its process stopped and started again", id); };
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
        entry->module = std::move(hosted);
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
    emit changed();
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
            kept->wanted = std::move(item);
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
    if (nodes() != nodesBefore || touchMethods() != methodsBefore || attachmentList() != attachmentsBefore ||
        behaviorList() != behaviorsBefore || moduleKeys() != modulesBefore) {
        emit changed();
    }
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
    }
    return list;
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

}
