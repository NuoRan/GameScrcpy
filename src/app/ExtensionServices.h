#pragma once

#include "LogicServices.h"

#include <gs/exthost/Module.h>
#include <gs/script/Services.h>

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace gs::app {

class ExtensionHost;

struct OfferedService {
    std::string name;
    std::string script;
    std::string node;
    std::string input;
    std::string output;

    friend bool operator==(const OfferedService&, const OfferedService&) = default;
};

struct ServiceExtension {
    std::string id;
    std::string name;
    std::vector<std::string> dependencies;
    std::vector<std::string> granted;
    std::vector<OfferedService> services;

    friend bool operator==(const ServiceExtension&, const ServiceExtension&) = default;
};

struct ServiceReplies {
    std::function<void(std::uint64_t logic, quint64 request, const QString& json)> answer;
    std::function<void(std::uint64_t logic, quint64 request, const QString& message)> refuse;
    std::function<void(std::uint64_t logic, const QString& kind, const QString& id, const QString& json)> tell;
    std::function<script::NodeAnswer(std::string_view extension, std::string_view node,
                                     const std::vector<script::NodeValue>& values)>
        node;
};

class ExtensionServices final : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(exthost::ServiceResult)>;

    static constexpr std::string_view kEventKind = "extService";
    static constexpr std::chrono::milliseconds kTimeout{10'000};

    explicit ExtensionServices(QObject* parent = nullptr);
    ~ExtensionServices() override;
    ExtensionServices(const ExtensionServices&) = delete;
    ExtensionServices& operator=(const ExtensionServices&) = delete;

    [[nodiscard]] static ExtensionServices* instance();

    void offerServices();
    void setReplies(ServiceReplies replies);
    void setExtensions(std::vector<ServiceExtension> extensions);
    void setTimeout(std::chrono::milliseconds limit);
    void follow(ExtensionHost& extensions);

    void request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json);
    void drop(const LogicCaller& caller, quint64 id);
    [[nodiscard]] script::ServiceAnswer call(const LogicCaller& caller, std::string_view service,
                                             std::string_view json);
    void endLogic(std::uint64_t logic);
    [[nodiscard]] bool depends(const QString& listener, const QString& emitter) const;

    static void callFromNative(const std::string& caller, std::string service, std::string json, Done done);
    [[nodiscard]] static exthost::ServiceResult callFromNativeAndWait(const std::string& caller,
                                                                     std::string_view service,
                                                                     std::string_view json,
                                                                     std::stop_token stop = {});

private:
    struct Shared;
    struct Pending;

    [[nodiscard]] static script::ServiceAnswer callWith(const std::shared_ptr<Shared>& shared,
                                                        const LogicCaller& caller, std::string_view service,
                                                        std::string_view json);

    void begin(Pending pending, const std::string& service, const std::string& args,
               const std::optional<std::vector<std::string>>& limit);
    void beginNative(std::uint64_t native, const std::string& caller, const std::string& service,
                     const std::string& json);
    void deliver(quint64 id);
    void provided(const std::string& extension, const std::string& name);
    void settle(std::uint64_t logic, quint64 id, bool failed, bool output, const std::string& text);
    void complete(quint64 id, const std::string& text);
    void cancelProvider(quint64 id);
    void finish(quint64 id, const std::string& code, const std::string& text);
    void sweep();

    static inline std::mutex s_instanceMutex;
    static inline ExtensionServices* s_instance = nullptr;

    std::shared_ptr<Shared> m_shared;
    ServiceReplies m_replies;
    std::map<quint64, Pending> m_calls;
    QTimer m_sweep;
    std::chrono::milliseconds m_timeout = kTimeout;
    quint64 m_lastCall = 0;
    bool m_offered = false;
};

}
