#include "LogicServices.h"

#include "ScriptRuntime.h"

namespace gs::app {

LogicServices& LogicServices::instance()
{
    static LogicServices services;
    return services;
}

void LogicServices::offerCall(std::string name, Call handler)
{
    const std::scoped_lock lock(m_mutex);
    m_calls.insert_or_assign(std::move(name), std::move(handler));
}

void LogicServices::offerRequest(std::string name, Request handler, Drop dropped)
{
    const std::scoped_lock lock(m_mutex);
    m_requests.insert_or_assign(std::move(name), std::pair{std::move(handler), std::move(dropped)});
}

void LogicServices::withdraw(std::string_view name)
{
    const std::scoped_lock lock(m_mutex);
    if (const auto found = m_calls.find(name); found != m_calls.end()) {
        m_calls.erase(found);
    }
    if (const auto found = m_requests.find(name); found != m_requests.end()) {
        m_requests.erase(found);
    }
}

bool LogicServices::offers(std::string_view name) const
{
    const std::scoped_lock lock(m_mutex);
    return m_calls.contains(name) || m_requests.contains(name);
}

script::ServiceAnswer LogicServices::call(const LogicCaller& caller, std::string_view name,
                                          std::string_view json) const
{
    Call handler;
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_calls.find(name);
        if (found == m_calls.end()) {
            return {"null", "GS-API-070", "no part of GameScrcpy answers the service " + std::string(name) + " at once"};
        }
        handler = found->second;
    }
    return handler(caller, json);
}

bool LogicServices::request(const LogicCaller& caller, std::string_view name, quint64 id,
                            const QString& json) const
{
    Request handler;
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_requests.find(name);
        if (found == m_requests.end()) {
            return false;
        }
        handler = found->second.first;
    }
    handler(caller, id, json);
    return true;
}

void LogicServices::drop(const LogicCaller& caller, std::string_view name, quint64 id) const
{
    Drop handler;
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_requests.find(name);
        if (found == m_requests.end() || !found->second.second) {
            return;
        }
        handler = found->second.second;
    }
    handler(caller, id);
}

std::uint64_t LogicServices::join(ScriptRuntime* runtime, LogicCaller caller)
{
    std::uint64_t logic = 0;
    {
        const std::scoped_lock lock(m_mutex);
        logic = ++m_lastLogic;
        caller.logic = logic;
        m_live.insert_or_assign(logic, Live{runtime, std::move(caller)});
    }
    emit logicStarted(logic);
    return logic;
}

void LogicServices::leave(std::uint64_t logic)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (m_live.erase(logic) == 0) {
            return;
        }
    }
    emit logicEnded(logic);
}

std::vector<LogicCaller> LogicServices::running() const
{
    const std::scoped_lock lock(m_mutex);
    std::vector<LogicCaller> callers;
    callers.reserve(m_live.size());
    for (const auto& [logic, live] : m_live) {
        if (!live.runtime.isNull()) {
            callers.push_back(live.caller);
        }
    }
    return callers;
}

ScriptRuntime* LogicServices::runtimeOf(std::uint64_t logic) const
{
    const std::scoped_lock lock(m_mutex);
    const auto found = m_live.find(logic);
    return found != m_live.end() ? found->second.runtime.data() : nullptr;
}

bool LogicServices::tell(std::uint64_t logic, const QString& kind, const QString& id, const QString& json) const
{
    ScriptRuntime* runtime = runtimeOf(logic);
    if (runtime == nullptr) {
        return false;
    }
    runtime->tell(kind, id, json);
    return true;
}

bool LogicServices::answer(std::uint64_t logic, quint64 id, const QString& json) const
{
    ScriptRuntime* runtime = runtimeOf(logic);
    if (runtime == nullptr) {
        return false;
    }
    runtime->answer(id, json);
    return true;
}

bool LogicServices::refuse(std::uint64_t logic, quint64 id, const QString& message) const
{
    ScriptRuntime* runtime = runtimeOf(logic);
    if (runtime == nullptr) {
        return false;
    }
    runtime->refuse(id, message);
    return true;
}

std::uint64_t LogicServices::keepFrame(std::uint64_t logic, const QImage& picture, script::PictureSize source,
                                       quint64 frames) const
{
    ScriptRuntime* runtime = runtimeOf(logic);
    return runtime != nullptr ? runtime->keepFrame(picture, source, frames) : 0;
}

void LogicServices::forgetFrames(std::uint64_t logic, quint64 frames) const
{
    if (ScriptRuntime* runtime = runtimeOf(logic)) {
        runtime->forgetFrames(frames);
    }
}

}
