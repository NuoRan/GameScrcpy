#include "ShortcutModel.h"

#include <algorithm>
#include <utility>

namespace gs::app {

using namespace Qt::StringLiterals;

ShortcutModel::ShortcutModel(QObject* parent)
    : QAbstractListModel(parent)
{
    if (Commands* commands = Commands::instance()) {
        connect(commands, &Commands::shortcutsChanged, this, &ShortcutModel::refresh);
    }
    m_rows = build();
}

int ShortcutModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant ShortcutModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const ShortcutInfo& row = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case CommandIdRole:
        return row.id;
    case TitleRole:
        return row.title;
    case KeysRole:
        return Commands::keyCaps(row.keys);
    case DefaultTextRole:
        return Commands::keyText(row.defaults);
    case DefaultPortableRole:
        return Commands::portableKeys(row.defaults);
    case CustomRole:
        return row.custom;
    case ConflictedRole:
        return row.conflicted;
    default:
        return {};
    }
}

QHash<int, QByteArray> ShortcutModel::roleNames() const
{
    return {{CommandIdRole, "commandId"},       {TitleRole, "title"},   {KeysRole, "keys"},
            {DefaultTextRole, "defaultText"},   {CustomRole, "custom"}, {ConflictedRole, "conflicted"},
            {DefaultPortableRole, "defaultPortable"}};
}

void ShortcutModel::setDevice(bool device)
{
    if (device == m_device) {
        return;
    }
    m_device = device;
    emit deviceChanged();
    refresh();
}

void ShortcutModel::setFiltered(bool filtered)
{
    if (filtered == m_filtered) {
        return;
    }
    m_filtered = filtered;
    emit filterChanged();
    refresh();
}

void ShortcutModel::setHits(const QVariantMap& hits)
{
    if (hits == m_hits) {
        return;
    }
    m_hits = hits;
    emit filterChanged();
    refresh();
}

std::vector<ShortcutInfo> ShortcutModel::build() const
{
    const Commands* commands = Commands::instance();
    if (commands == nullptr) {
        return {};
    }
    const bool everything = !m_filtered || m_hits.contains(u"shortcuts"_s);
    std::vector<ShortcutInfo> rows = commands->shortcutList();
    std::erase_if(rows, [this, everything](const ShortcutInfo& row) {
        return row.device != m_device || (!everything && !m_hits.contains(u"shortcuts."_s + row.id));
    });
    return rows;
}

void ShortcutModel::refresh()
{
    std::vector<ShortcutInfo> rows = build();
    if (std::ranges::equal(rows, m_rows, {}, &ShortcutInfo::id, &ShortcutInfo::id)) {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i] != m_rows[i]) {
                m_rows[i] = std::move(rows[i]);
                const QModelIndex changed = index(static_cast<int>(i));
                emit dataChanged(changed, changed);
            }
        }
        return;
    }
    const int before = count();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (count() != before) {
        emit countChanged();
    }
}

}
