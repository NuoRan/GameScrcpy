#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace gs::app {

struct SyncGroup {
    QString master;
    QStringList followers;
    int tone = 0;

    friend bool operator==(const SyncGroup&, const SyncGroup&) = default;
};

class SyncGroups
{
public:
    bool setFollowers(const QString& master, const QStringList& followers);
    bool keepOnly(const QStringList& present);

    [[nodiscard]] const std::vector<SyncGroup>& groups() const noexcept { return m_groups; }
    [[nodiscard]] const SyncGroup* groupOf(const QString& serial) const;
    [[nodiscard]] QString masterOf(const QString& follower) const;
    [[nodiscard]] QStringList followersOf(const QString& master) const;
    [[nodiscard]] bool leads(const QString& serial) const;
    [[nodiscard]] bool canFollow(const QString& serial, const QString& master) const;

private:
    [[nodiscard]] int freeTone() const;

    std::vector<SyncGroup> m_groups;
};

}
