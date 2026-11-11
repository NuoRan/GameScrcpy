#include "AppEvents.h"

#include "ExtensionHost.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>

namespace gs::app {

static_assert(static_cast<std::size_t>(AppTopic::SettingsChanged) + 1 == exthost::kAppTopics.size());

namespace {

constexpr Microseconds kWindow{1'000'000};

QString compact(const QJsonObject& value)
{
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
}

QJsonValue nullable(const QString& text)
{
    return text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text);
}

}

QString appTopicName(AppTopic topic)
{
    const std::string_view name = exthost::kAppTopics[static_cast<std::size_t>(topic)];
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

bool publicSetting(const QString& key)
{
    return std::ranges::any_of(kPublicSettings, [&key](std::string_view name) {
        return key == QLatin1StringView(name.data(), static_cast<qsizetype>(name.size()));
    });
}

AppEvents::AppEvents(const Clock* clock, QObject* parent)
    : QObject(parent)
    , m_clock(clock)
    , m_routes{.callers = [] { return LogicServices::instance().running(); },
               .tell =
                   [](std::uint64_t logic, const QString& kind, const QString& id, const QString& json) {
                       static_cast<void>(LogicServices::instance().tell(logic, kind, id, json));
                   },
               .everything =
                   [](const QString& extension) {
                       const ExtensionHost* host = ExtensionHost::instance();
                       if (host == nullptr) {
                           return false;
                       }
                       const std::vector<std::string> granted = host->grantedPermissions(extension.toStdString());
                       return std::ranges::find(granted, exthost::kAppEventsPermission) != granted.end();
                   },
               .natives =
                   [] {
                       const NativeExtensions* native = NativeExtensions::instance();
                       return native != nullptr ? native->eventTargets() : std::vector<NativeEventTarget>{};
                   }}
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    m_flush.setSingleShot(true); // source-rule-allow R9: a merged event goes out when its one-second window ends
    connect(&m_flush, &QTimer::timeout, this, &AppEvents::flush);
    connect(&LogicServices::instance(), &LogicServices::logicEnded, this, &AppEvents::forget);
}

AppEvents::~AppEvents()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void AppEvents::setRoutes(AppEventRoutes routes)
{
    m_routes = std::move(routes);
}

const Clock& AppEvents::clock() const noexcept
{
    return m_clock != nullptr ? *m_clock : static_cast<const Clock&>(m_steady);
}

std::optional<QJsonObject> AppEvents::shaped(const AppListener& listener, AppTopic topic, const QString& phone,
                                             const QJsonObject& value)
{
    if (listener.extension.isEmpty()) {
        return value;
    }
    if (topic == AppTopic::SettingsChanged) {
        const QJsonValue owner = value.value(QStringLiteral("extension"));
        if (owner.isString()) {
            return owner.toString() == listener.extension ? std::optional<QJsonObject>(value) : std::nullopt;
        }
        const QJsonObject values = value.value(QStringLiteral("values")).toObject();
        const QJsonArray changed = value.value(QStringLiteral("keys")).toArray();
        QJsonArray keys;
        QJsonObject shown;
        for (const QJsonValue& key : changed) {
            const QString name = key.toString();
            if (!publicSetting(name)) {
                continue;
            }
            keys.append(name);
            if (values.contains(name)) {
                shown.insert(name, values.value(name));
            }
        }
        if (keys.isEmpty()) {
            return std::nullopt;
        }
        QJsonObject made = value;
        made.insert(QStringLiteral("keys"), keys);
        made.insert(QStringLiteral("values"), shown);
        return made;
    }
    if (!exthost::phoneTopic(exthost::kAppTopics[static_cast<std::size_t>(topic)])) {
        return value;
    }
    if (listener.everything || (!listener.phone.isEmpty() && listener.phone == phone)) {
        return value;
    }
    return std::nullopt;
}

void AppEvents::publish(AppTopic topic, const QString& phone, const QJsonObject& value)
{
    const QString name = appTopicName(topic);
    const std::string topicText = name.toStdString();
    const bool aboutPhones = exthost::phoneTopic(topicText);
    const QString whole = compact(value);
    emit published(name, phone, whole);
    if (m_routes.callers && m_routes.tell) {
        for (const LogicCaller& caller : m_routes.callers()) {
            const bool own = caller.extension.isEmpty();
            const bool permitted = !own && aboutPhones && m_routes.everything && m_routes.everything(caller.extension);
            const AppListener listener{.extension = caller.extension,
                                       .phone = caller.task ? QString() : caller.device,
                                       .everything = own || permitted};
            const std::optional<QJsonObject> shape = shaped(listener, topic, phone, value);
            if (!shape) {
                continue;
            }
            admit("l" + std::to_string(caller.logic) + '\n' + topicText,
                  [tell = m_routes.tell, logic = caller.logic, name, json = *shape == value ? whole : compact(*shape)] {
                      tell(logic, QStringLiteral("extEvent"), name, json);
                  });
        }
    }
    if (!m_routes.natives) {
        return;
    }
    for (const NativeEventTarget& native : m_routes.natives()) {
        if (native.module == nullptr || !native.module->hears(topicText)) {
            continue;
        }
        const AppListener listener{
            .extension = QString::fromStdString(native.extension), .phone = {}, .everything = native.everything};
        const std::optional<QJsonObject> shape = shaped(listener, topic, phone, value);
        if (!shape) {
            continue;
        }
        admit("n" + native.extension + '\n' + topicText,
              [module = std::weak_ptr<exthost::Module>(native.module), topicText,
               json = (*shape == value ? whole : compact(*shape)).toStdString()] {
                  if (const std::shared_ptr<exthost::Module> alive = module.lock()) {
                      alive->appEvent(topicText, json);
                  }
              });
    }
}

void AppEvents::admit(const std::string& key, std::function<void()> deliver)
{
    const Microseconds now = clock().now();
    auto found = m_windows.find(key);
    if (found == m_windows.end()) {
        if (m_windows.size() >= kMostWindows) {
            std::erase_if(m_windows, [now](const auto& entry) {
                return !entry.second.pending && now - entry.second.opened >= kWindow;
            });
        }
        if (m_windows.size() >= kMostWindows) {
            return;
        }
        found = m_windows.emplace(key, Window{.opened = now, .count = 0, .pending = {}}).first;
    }
    Window& window = found->second;
    if (now - window.opened >= kWindow) {
        const std::function<void()> late = std::exchange(window.pending, {});
        window.opened = now;
        window.count = 0;
        if (late) {
            window.count = 1;
            late();
        }
    }
    if (window.count >= kMostPerSecond) {
        window.pending = std::move(deliver);
        schedule();
        return;
    }
    ++window.count;
    deliver();
}

void AppEvents::flush()
{
    const Microseconds now = clock().now();
    std::vector<std::function<void()>> due;
    for (auto& entry : m_windows) {
        Window& window = entry.second;
        if (window.pending && now - window.opened >= kWindow) {
            window.opened = now;
            window.count = 1;
            due.push_back(std::exchange(window.pending, {}));
        }
    }
    for (const std::function<void()>& deliver : due) {
        deliver();
    }
    schedule();
}

void AppEvents::schedule()
{
    std::optional<Microseconds> next;
    for (const auto& entry : m_windows) {
        if (entry.second.pending) {
            const Microseconds due = entry.second.opened + kWindow;
            next = next ? std::min(*next, due) : due;
        }
    }
    if (!next) {
        m_flush.stop();
        return;
    }
    const Microseconds left = std::max(*next - clock().now(), Microseconds{0});
    m_flush.start(std::chrono::ceil<std::chrono::milliseconds>(left) + std::chrono::milliseconds{1});
}

void AppEvents::forget(quint64 logic)
{
    const std::string prefix = "l" + std::to_string(logic) + '\n';
    std::erase_if(m_windows, [&prefix](const auto& entry) { return entry.first.starts_with(prefix); });
}

void publishAppEvent(AppTopic topic, const QString& phone, const QJsonObject& value)
{
    if (AppEvents* events = AppEvents::instance()) {
        events->publish(topic, phone, value);
    }
}

void publishPhoneEvent(AppTopic topic, const QString& serial, const QString& name, QJsonObject more)
{
    AppEvents* events = AppEvents::instance();
    if (events == nullptr) {
        return;
    }
    more.insert(QStringLiteral("phone"), serial);
    more.insert(QStringLiteral("name"), name);
    events->publish(topic, serial, more);
}

void publishSettingsChanged(const QStringList& keys, const QJsonObject& values, const QString& extension,
                            const QString& device)
{
    AppEvents* events = AppEvents::instance();
    if (events == nullptr || keys.isEmpty()) {
        return;
    }
    events->publish(AppTopic::SettingsChanged, {},
                    QJsonObject{{QStringLiteral("keys"), QJsonArray::fromStringList(keys)},
                                {QStringLiteral("values"), values},
                                {QStringLiteral("extension"), nullable(extension)},
                                {QStringLiteral("device"), nullable(device)}});
}

void publishExtensionTurned(bool on, const QString& id, const QString& name, const QString& version)
{
    publishAppEvent(on ? AppTopic::ExtensionEnabled : AppTopic::ExtensionDisabled, {},
                    QJsonObject{{QStringLiteral("id"), id},
                                {QStringLiteral("name"), name},
                                {QStringLiteral("version"), version}});
}

}
