#pragma once

#include <gs/script/Services.h>

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::app {

class ScriptRuntime;

struct LogicCaller {
    std::uint64_t logic = 0;
    QString extension;
    QString script;
    QString device;
    bool task = false;
};

class LogicServices final : public QObject
{
    Q_OBJECT

public:
    using Call = std::function<script::ServiceAnswer(const LogicCaller& caller, std::string_view json)>;
    using Request = std::function<void(const LogicCaller& caller, quint64 request, const QString& json)>;
    using Drop = std::function<void(const LogicCaller& caller, quint64 request)>;

    static LogicServices& instance();

    void offerCall(std::string name, Call handler);
    void offerRequest(std::string name, Request handler, Drop dropped = {});
    void withdraw(std::string_view name);

    [[nodiscard]] bool offers(std::string_view name) const;
    [[nodiscard]] script::ServiceAnswer call(const LogicCaller& caller, std::string_view name,
                                             std::string_view json) const;
    [[nodiscard]] bool request(const LogicCaller& caller, std::string_view name, quint64 id,
                               const QString& json) const;
    void drop(const LogicCaller& caller, std::string_view name, quint64 id) const;

    [[nodiscard]] std::uint64_t join(ScriptRuntime* runtime, LogicCaller caller);
    void leave(std::uint64_t logic);
    [[nodiscard]] std::vector<LogicCaller> running() const;
    [[nodiscard]] bool tell(std::uint64_t logic, const QString& kind, const QString& id, const QString& json) const;
    [[nodiscard]] bool answer(std::uint64_t logic, quint64 id, const QString& json) const;
    [[nodiscard]] bool refuse(std::uint64_t logic, quint64 id, const QString& message) const;

signals:
    void logicStarted(quint64 logic);
    void logicEnded(quint64 logic);

private:
    LogicServices() = default;

    struct Live {
        QPointer<ScriptRuntime> runtime;
        LogicCaller caller;
    };

    [[nodiscard]] ScriptRuntime* runtimeOf(std::uint64_t logic) const;

    mutable std::mutex m_mutex;
    std::map<std::string, Call, std::less<>> m_calls;
    std::map<std::string, std::pair<Request, Drop>, std::less<>> m_requests;
    std::map<std::uint64_t, Live> m_live;
    std::uint64_t m_lastLogic = 0;
};

}
