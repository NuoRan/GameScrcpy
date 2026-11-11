#pragma once

#include "LogicServices.h"

#include <gs/hardware/Backend.h>
#include <gs/input/DeviceEvents.h>
#include <gs/script/Services.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

class ExtensionHost;

struct HardwareGrant {
    std::string id;
    hardware::Kind kind = hardware::Kind::Serial;
    std::string name;

    friend bool operator==(const HardwareGrant&, const HardwareGrant&) = default;
};

struct HardwareReplies {
    std::function<void(std::uint64_t logic, quint64 request, const QString& json)> answer;
    std::function<void(std::uint64_t logic, quint64 request, const QString& message)> refuse;
    std::function<void(std::uint64_t logic, const QString& kind, const QString& id, const QString& json)> tell;
};

class HardwareAccess final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap picker READ picker NOTIFY pickerChanged FINAL)
    Q_PROPERTY(QVariantMap grants READ grantMap NOTIFY grantsChanged FINAL)
    Q_PROPERTY(QVariantList devices READ deviceList NOTIFY devicesChanged FINAL)

public:
    using Feed = std::function<void(const QString& serial, const input::DeviceEvent& event)>;
    using Front = std::function<QString()>;

    static constexpr std::string_view kOwnLogic = "mine";
    static constexpr std::string_view kEventKind = "hardware";
    static constexpr std::size_t kMostGrants = 64;
    static constexpr std::size_t kMostOpen = 16;
    static constexpr std::size_t kMostWaitingPicks = 8;
    static constexpr std::size_t kMostFilters = 16;

    HardwareAccess(std::unique_ptr<hardware::Backend> backend, std::filesystem::path grantsFile,
                   QObject* parent = nullptr);
    ~HardwareAccess() override;
    HardwareAccess(const HardwareAccess&) = delete;
    HardwareAccess& operator=(const HardwareAccess&) = delete;

    static HardwareAccess* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static HardwareAccess* instance() noexcept { return s_instance; }

    void offerServices();
    void setReplies(HardwareReplies replies);
    void setPermitted(const QStringList& extensions);
    void setExtensionNames(std::map<QString, QString> names);
    void setFeed(Feed feed, Front front);
    void follow(ExtensionHost& extensions);

    [[nodiscard]] script::ServiceAnswer call(const LogicCaller& caller, std::string_view service,
                                             std::string_view json);
    void request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json);
    void drop(const LogicCaller& caller, quint64 id);
    void endLogic(std::uint64_t logic);
    void provideInput(const QString& extension, const QString& name, bool button, double x, double y);
    void endProvidedInput(const QString& extension);

    [[nodiscard]] QVariantMap picker() const;
    [[nodiscard]] QVariantMap grantMap() const;
    [[nodiscard]] QVariantList deviceList() const;
    [[nodiscard]] std::optional<hardware::DeviceInfo> device(const QString& id) const;
    [[nodiscard]] std::vector<HardwareGrant> grantsOf(const QString& owner) const;
    [[nodiscard]] std::size_t openCount() const noexcept { return m_handles.size(); }

    Q_INVOKABLE bool allow(const QStringList& ids);
    Q_INVOKABLE void cancelPicker();
    Q_INVOKABLE bool forget(const QString& owner, const QString& id);
    Q_INVOKABLE QString deviceTitle(const QString& id) const;

signals:
    void pickerChanged();
    void grantsChanged();
    void devicesChanged();
    void attentionWanted();
    void inputSeen(const QString& name);

private:
    struct Shared;
    class Sink;

    struct Pick {
        LogicCaller caller;
        quint64 request = 0;
        hardware::Kind kind = hardware::Kind::Serial;
        std::vector<hardware::Filter> filters;
    };

    struct Opening {
        std::uint64_t logic = 0;
        quint64 request = 0;
        QString owner;
        hardware::DeviceInfo device;
    };

    struct Handle {
        std::uint64_t logic = 0;
        QString owner;
        hardware::DeviceInfo device;
        std::unique_ptr<hardware::Device> link;
        std::set<quint64> waiting;
        std::string claimed;
    };

    struct HeldInput {
        QString serial;
        input::InputSource source;
        bool axis = false;
    };

    [[nodiscard]] static script::ServiceAnswer callWith(const std::shared_ptr<Shared>& shared, const LogicCaller& caller,
                                                        std::string_view service, std::string_view json);

    void refreshDevices();
    void opened(const QString& handle, std::shared_ptr<hardware::OpenResult> result);
    void completed(const QString& handle, quint64 request, const std::string& problem, const QString& json);
    void deviceData(const QString& handle, const QString& json);
    void deviceClosed(const QString& handle, hardware::Closed reason, const std::string& problem);
    void deliverInput(const LogicCaller& caller, const QString& name, input::DeviceAction action, input::Vec2 value);
    void releaseInputs(std::uint64_t logic);
    void closeHandle(const QString& handle, const QString& reason, const QString& message, bool tellLogic);
    void showNextPick();
    void updateScan();
    void loadGrants();
    void saveGrants() const;
    void answer(std::uint64_t logic, quint64 request, const QString& json) const;
    void refuse(std::uint64_t logic, quint64 request, const QString& message) const;
    void tell(std::uint64_t logic, const QString& handle, const QString& json) const;

    void pickDevices(const LogicCaller& caller, quint64 id, const QString& json);
    void openDevice(const LogicCaller& caller, quint64 id, const QString& json);
    void writeDevice(const LogicCaller& caller, quint64 id, const QString& json);
    void controlDevice(const LogicCaller& caller, quint64 id, const QString& json);
    void closeDevice(const LogicCaller& caller, quint64 id, const QString& json);
    void forgetDevice(const LogicCaller& caller, quint64 id, const QString& json);
    void claimHardware(const LogicCaller& caller, quint64 id, const QString& json);
    [[nodiscard]] QString claimerName(const QString& owner) const;

    [[nodiscard]] Handle* handleOf(std::uint64_t logic, const QString& handle);
    [[nodiscard]] hardware::Completion completion(const QString& handle, quint64 request) const;
    [[nodiscard]] std::vector<hardware::DeviceInfo> present() const;
    [[nodiscard]] QString requesterOf(const LogicCaller& caller) const;

    static inline HardwareAccess* s_instance = nullptr;

    std::shared_ptr<Shared> m_shared;
    std::unique_ptr<hardware::Backend> m_backend;
    std::filesystem::path m_grantsFile;
    HardwareReplies m_replies;
    Feed m_feed;
    Front m_front;
    std::map<QString, QString> m_extensionNames;
    std::deque<Pick> m_picks;
    std::map<QString, Opening> m_openings;
    std::map<QString, Handle> m_handles;
    std::map<std::uint64_t, std::map<QString, HeldInput>> m_held;
    std::map<QString, std::map<QString, HeldInput>> m_provided;
    std::set<std::uint64_t> m_endedLogic;
    std::uint64_t m_lastHandle = 0;
    bool m_scanning = false;
    bool m_offered = false;
};

}
