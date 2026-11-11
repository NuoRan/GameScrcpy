#pragma once

#include "LogicServices.h"
#include "NativeExtensions.h"

#include <gs/base/Clock.h>
#include <gs/exthost/Module.h>

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::app {

enum class AppTopic : std::uint8_t {
    DeviceConnected,
    DeviceDisconnected,
    SessionStarted,
    SessionEnded,
    ProfileApplied,
    AppForeground,
    OrientationChanged,
    RecordingStarted,
    RecordingEnded,
    WindowFocused,
    ExtensionEnabled,
    ExtensionDisabled,
    SettingsChanged
};

inline constexpr std::array<std::string_view, 5> kPublicSettings{
    "general.language", "appearance.theme", "appearance.accent", "appearance.reducedMotion",
    "appearance.reducedTransparency"};

[[nodiscard]] QString appTopicName(AppTopic topic);
[[nodiscard]] bool publicSetting(const QString& key);

struct AppListener {
    QString extension;
    QString phone;
    bool everything = false;
};

struct AppEventRoutes {
    std::function<std::vector<LogicCaller>()> callers;
    std::function<void(std::uint64_t logic, const QString& kind, const QString& id, const QString& json)> tell;
    std::function<bool(const QString& extension)> everything;
    std::function<std::vector<NativeEventTarget>()> natives;
};

class AppEvents final : public QObject
{
    Q_OBJECT

public:
    static constexpr std::size_t kMostPerSecond = exthost::kMostEventsPerSecond;
    static constexpr std::size_t kMostWindows = 4096;

    explicit AppEvents(const Clock* clock = nullptr, QObject* parent = nullptr);
    ~AppEvents() override;
    AppEvents(const AppEvents&) = delete;
    AppEvents& operator=(const AppEvents&) = delete;

    [[nodiscard]] static AppEvents* instance() noexcept { return s_instance; }
    [[nodiscard]] static std::optional<QJsonObject> shaped(const AppListener& listener, AppTopic topic,
                                                           const QString& phone, const QJsonObject& value);

    void setRoutes(AppEventRoutes routes);
    void publish(AppTopic topic, const QString& phone, const QJsonObject& value);
    void flush();
    void forget(quint64 logic);

signals:
    void published(const QString& topic, const QString& phone, const QString& json);

private:
    struct Window {
        Microseconds opened{0};
        std::size_t count = 0;
        std::function<void()> pending;
    };

    [[nodiscard]] const Clock& clock() const noexcept;
    void admit(const std::string& key, std::function<void()> deliver);
    void schedule();

    static inline AppEvents* s_instance = nullptr;

    const Clock* m_clock = nullptr;
    SteadyClock m_steady;
    AppEventRoutes m_routes;
    std::map<std::string, Window, std::less<>> m_windows;
    QTimer m_flush;
};

void publishAppEvent(AppTopic topic, const QString& phone, const QJsonObject& value);
void publishPhoneEvent(AppTopic topic, const QString& serial, const QString& name, QJsonObject more = {});
void publishSettingsChanged(const QStringList& keys, const QJsonObject& values, const QString& extension = {},
                            const QString& device = {});
void publishExtensionTurned(bool on, const QString& id, const QString& name, const QString& version);

}
