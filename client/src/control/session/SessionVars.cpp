#include "SessionVars.h"
#include <mutex>

SessionVars::SessionVars()
{
}

SessionVars::~SessionVars()
{
}

ScriptValue SessionVars::getVar(const std::string& key, const ScriptValue& defaultValue) const
{
    std::lock_guard<std::mutex> locker(m_varsMutex);
    auto it = m_vars.find(key);
    return (it != m_vars.end()) ? it->second : defaultValue;
}

void SessionVars::setVar(const std::string& key, const ScriptValue& value)
{
    std::lock_guard<std::mutex> locker(m_varsMutex);
    m_vars[key] = value;
}

bool SessionVars::hasVar(const std::string& key) const
{
    std::lock_guard<std::mutex> locker(m_varsMutex);
    return m_vars.count(key) > 0;
}

void SessionVars::removeVar(const std::string& key)
{
    std::lock_guard<std::mutex> locker(m_varsMutex);
    m_vars.erase(key);
}

void SessionVars::clearVars()
{
    std::lock_guard<std::mutex> locker(m_varsMutex);
    m_vars.clear();
}

void SessionVars::addTouchSeq(int keyId, uint32_t seqId)
{
    std::lock_guard<std::mutex> locker(m_touchSeqMutex);
    m_touchSeqIds[keyId].push_back(seqId);
}

std::vector<uint32_t> SessionVars::takeTouchSeqs(int keyId)
{
    std::lock_guard<std::mutex> locker(m_touchSeqMutex);
    auto it = m_touchSeqIds.find(keyId);
    if (it != m_touchSeqIds.end()) {
        std::vector<uint32_t> result = std::move(it->second);
        m_touchSeqIds.erase(it);
        return result;
    }
    return {};
}

int SessionVars::touchSeqCount(int keyId) const
{
    std::lock_guard<std::mutex> locker(m_touchSeqMutex);
    return static_cast<int>(m_touchSeqIds.count(keyId) ? m_touchSeqIds.at(keyId).size() : 0);
}

bool SessionVars::hasTouchSeqs(int keyId) const
{
    std::lock_guard<std::mutex> locker(m_touchSeqMutex);
    return m_touchSeqIds.count(keyId) > 0;
}

std::unordered_map<int, std::vector<uint32_t>> SessionVars::takeAllTouchSeqs()
{
    std::lock_guard<std::mutex> locker(m_touchSeqMutex);
    std::unordered_map<int, std::vector<uint32_t>> result;
    result.swap(m_touchSeqIds);
    return result;
}

void SessionVars::clearTouchSeqs()
{
    std::lock_guard<std::mutex> locker(m_touchSeqMutex);
    m_touchSeqIds.clear();
}

void SessionVars::setRadialParamKeyId(const std::string& keyId)
{
    std::lock_guard<std::mutex> locker(m_radialParamMutex);
    m_radialParamKeyId = keyId;
}

std::string SessionVars::radialParamKeyId() const
{
    std::lock_guard<std::mutex> locker(m_radialParamMutex);
    return m_radialParamKeyId;
}
