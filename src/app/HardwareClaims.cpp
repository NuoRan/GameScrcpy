#include "HardwareClaims.h"

#include <QThread>

#include <algorithm>
#include <utility>

namespace gs::app {

namespace {

std::string keyOf(std::string_view resource)
{
    constexpr std::string_view kHid = "hid:";
    constexpr std::string_view kDevicePrefix = "\\\\?\\";
    std::string key(resource);
    std::ranges::transform(key, key.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (key.starts_with(kHid) && std::string_view(key).substr(kHid.size()).starts_with(kDevicePrefix)) {
        key.erase(kHid.size(), kDevicePrefix.size());
    }
    return key;
}

}

HardwareClaims::HardwareClaims(QObject* parent)
    : QObject(parent)
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
}

HardwareClaims::~HardwareClaims()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
    {
        const std::scoped_lock lock(m_mutex);
        m_closing = true;
    }
    m_freed.notify_all();
    std::vector<Waiter> waiters;
    {
        const std::scoped_lock lock(m_waitersMutex);
        waiters.swap(m_waiters);
    }
    waiters.clear();
}

exthost::ClaimResult HardwareClaims::claim(std::string_view resource, const ClaimHolder& holder, bool wait)
{
    if (!exthost::validResource(resource) || holder.id.empty()) {
        return exthost::ClaimResult::BadArgument;
    }
    const std::string key = keyOf(resource);
    std::function<bool(const std::string&, const QString&)> ask;
    std::string asked;
    std::string current;
    {
        const std::scoped_lock lock(m_mutex);
        if (m_closing) {
            return exthost::ClaimResult::Busy;
        }
        const auto found = m_held.find(key);
        if (found != m_held.end() && found->second.holder == holder.id) {
            found->second.name = holder.name;
            found->second.wanted = holder.wanted;
            return exthost::ClaimResult::Claimed;
        }
        if (found != m_held.end()) {
            if (!wait || !found->second.wanted || QThread::isMainThread()) {
                return exthost::ClaimResult::Busy;
            }
            ask = found->second.wanted;
            asked = found->second.resource;
            current = found->second.holder;
        } else {
            m_held.emplace(key, Held{std::string(resource), holder.id, holder.name, holder.wanted});
        }
    }
    if (ask) {
        if (!ask(asked, holder.name)) {
            return exthost::ClaimResult::Busy;
        }
        std::unique_lock lock(m_mutex);
        m_freed.wait_for(lock, kYieldWait, [this, &key, &current] {
            const auto found = m_held.find(key);
            return m_closing || found == m_held.end() || found->second.holder != current;
        });
        if (m_closing || m_held.contains(key)) {
            return exthost::ClaimResult::Busy;
        }
        m_held.emplace(key, Held{std::string(resource), holder.id, holder.name, holder.wanted});
    }
    emit changed();
    return exthost::ClaimResult::Claimed;
}

void HardwareClaims::claimLater(std::string resource, ClaimHolder holder, Done done)
{
    const exthost::ClaimResult now = claim(resource, holder, false);
    if (now != exthost::ClaimResult::Busy) {
        if (done) {
            done(now);
        }
        return;
    }
    const std::scoped_lock lock(m_waitersMutex);
    std::erase_if(m_waiters, [](const Waiter& waiter) { return waiter.finished->load(); });
    auto finished = std::make_shared<std::atomic<bool>>(false);
    std::jthread thread([this, resource = std::move(resource), holder = std::move(holder), done = std::move(done),
                         finished] {
        const exthost::ClaimResult result = claim(resource, holder, true);
        if (done) {
            done(result);
        }
        finished->store(true);
    });
    m_waiters.push_back(Waiter{std::move(thread), std::move(finished)});
}

void HardwareClaims::release(std::string_view resource, std::string_view holder)
{
    bool released = false;
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_held.find(keyOf(resource));
        if (found != m_held.end() && found->second.holder == holder) {
            m_held.erase(found);
            released = true;
        }
    }
    if (released) {
        m_freed.notify_all();
        emit changed();
    }
}

void HardwareClaims::releaseAll(std::string_view holder)
{
    bool released = false;
    {
        const std::scoped_lock lock(m_mutex);
        released = std::erase_if(m_held, [holder](const auto& entry) { return entry.second.holder == holder; }) > 0;
    }
    if (released) {
        m_freed.notify_all();
        emit changed();
    }
}

QString HardwareClaims::holderName(std::string_view resource) const
{
    const std::scoped_lock lock(m_mutex);
    const auto found = m_held.find(keyOf(resource));
    return found != m_held.end() ? found->second.name : QString();
}

bool HardwareClaims::holds(std::string_view resource, std::string_view holder) const
{
    const std::scoped_lock lock(m_mutex);
    const auto found = m_held.find(keyOf(resource));
    return found != m_held.end() && found->second.holder == holder;
}

}
