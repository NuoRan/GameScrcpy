#include "SyncGroups.h"

#include <algorithm>

namespace gs::app {

bool SyncGroups::setFollowers(const QString& master, const QStringList& followers)
{
    if (master.isEmpty() || !masterOf(master).isEmpty()) {
        return false;
    }
    QStringList accepted;
    for (const QString& serial : followers) {
        if (!serial.isEmpty() && !accepted.contains(serial) && canFollow(serial, master)) {
            accepted.append(serial);
        }
    }
    const auto found = std::ranges::find(m_groups, master, &SyncGroup::master);
    if (accepted.isEmpty()) {
        if (found == m_groups.end()) {
            return false;
        }
        m_groups.erase(found);
        return true;
    }
    if (found != m_groups.end()) {
        if (found->followers == accepted) {
            return false;
        }
        found->followers = accepted;
        return true;
    }
    m_groups.push_back({.master = master, .followers = accepted, .tone = freeTone()});
    return true;
}

bool SyncGroups::keepOnly(const QStringList& present)
{
    bool changed = false;
    for (auto group = m_groups.begin(); group != m_groups.end();) {
        if (!present.contains(group->master)) {
            group = m_groups.erase(group);
            changed = true;
            continue;
        }
        const qsizetype before = group->followers.size();
        group->followers.removeIf([&present](const QString& serial) { return !present.contains(serial); });
        changed = changed || group->followers.size() != before;
        if (group->followers.isEmpty()) {
            group = m_groups.erase(group);
            continue;
        }
        ++group;
    }
    return changed;
}

const SyncGroup* SyncGroups::groupOf(const QString& serial) const
{
    const auto found = std::ranges::find_if(m_groups, [&serial](const SyncGroup& group) {
        return group.master == serial || group.followers.contains(serial);
    });
    return found != m_groups.end() ? &*found : nullptr;
}

QString SyncGroups::masterOf(const QString& follower) const
{
    const auto found = std::ranges::find_if(
        m_groups, [&follower](const SyncGroup& group) { return group.followers.contains(follower); });
    return found != m_groups.end() ? found->master : QString();
}

QStringList SyncGroups::followersOf(const QString& master) const
{
    const auto found = std::ranges::find(m_groups, master, &SyncGroup::master);
    return found != m_groups.end() ? found->followers : QStringList();
}

bool SyncGroups::leads(const QString& serial) const
{
    return std::ranges::any_of(m_groups, [&serial](const SyncGroup& group) { return group.master == serial; });
}

bool SyncGroups::canFollow(const QString& serial, const QString& master) const
{
    if (serial == master || leads(serial)) {
        return false;
    }
    const QString current = masterOf(serial);
    return current.isEmpty() || current == master;
}

int SyncGroups::freeTone() const
{
    int tone = 0;
    while (std::ranges::any_of(m_groups, [tone](const SyncGroup& group) { return group.tone == tone; })) {
        ++tone;
    }
    return tone;
}

}
