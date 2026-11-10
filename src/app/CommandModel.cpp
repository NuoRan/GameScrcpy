#include "CommandModel.h"

#include "Commands.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <algorithm>
#include <iterator>
#include <utility>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

QString translated(const char* source)
{
    return source == nullptr ? QString() : QCoreApplication::translate("Commands", source);
}

QStringList bothLanguages(const char* source)
{
    if (source == nullptr || *source == '\0') {
        return {};
    }
    return {QCoreApplication::translate("Commands", source), QString::fromUtf8(source)};
}

}

CommandModel::CommandModel(QObject* parent)
    : QAbstractListModel(parent)
{
    if (Commands* commands = Commands::instance()) {
        connect(commands, &Commands::stateChanged, this, &CommandModel::rebuild);
        connect(commands, &Commands::recentChanged, this, &CommandModel::rebuild);
    }
    m_rows = build();
    m_sectionCount = sectionsIn(m_rows);
}

int CommandModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant CommandModel::data(const QModelIndex& index, int role) const
{
    const Row* row = index.isValid() ? rowAt(index.row()) : nullptr;
    if (row == nullptr) {
        return {};
    }
    switch (role) {
    case CommandIdRole:
        return row->id;
    case KindRole:
        return row->group ? u"group"_s : u"command"_s;
    case TitleRole:
        return row->title;
    case HighlightsRole: {
        QVariantList ranges;
        for (const TextRange& range : row->highlights) {
            ranges.append(QVariant(QVariantList{range.start, range.length}));
        }
        return ranges;
    }
    case SubtitleRole:
        return row->subtitle;
    case IconRole:
        return row->icon;
    case ShortcutRole:
        return row->shortcut;
    case TargetNameRole:
        return row->targetName;
    case CheckedRole:
        return row->checked;
    case SectionRole:
        return row->section;
    default:
        return {};
    }
}

QHash<int, QByteArray> CommandModel::roleNames() const
{
    return {{CommandIdRole, "commandId"}, {KindRole, "kind"},
            {TitleRole, "title"},         {HighlightsRole, "highlights"},
            {SubtitleRole, "subtitle"},   {IconRole, "icon"},
            {ShortcutRole, "shortcut"},   {TargetNameRole, "targetName"},
            {CheckedRole, "checked"},     {SectionRole, "section"}};
}

void CommandModel::setQuery(const QString& query)
{
    if (query == m_query) {
        return;
    }
    m_query = query;
    emit queryChanged();
    rebuild();
}

void CommandModel::setTarget(const QString& target)
{
    if (target == m_target) {
        return;
    }
    m_target = target;
    emit targetChanged();
    rebuild();
}

void CommandModel::setActive(bool active)
{
    if (active == m_active) {
        return;
    }
    m_active = active;
    emit activeChanged();
    if (m_active && m_stale) {
        rebuild();
    }
}

bool CommandModel::trigger(int row)
{
    Commands* commands = Commands::instance();
    const Row* picked = rowAt(row);
    if (commands == nullptr || picked == nullptr || picked->group) {
        return false;
    }
    const QString id = picked->id;
    const QString serial = picked->target;
    if (!commands->trigger(id, serial)) {
        return false;
    }
    commands->remember(id);
    return true;
}

bool CommandModel::triggerChoice(const QString& group, const QString& commandId)
{
    Commands* commands = Commands::instance();
    const auto picked = std::ranges::find_if(m_rows, [&group](const Row& row) { return row.group && row.id == group; });
    if (commands == nullptr || picked == m_rows.end() || !picked->members.contains(commandId)) {
        return false;
    }
    const QString serial = picked->target;
    if (!commands->trigger(commandId, serial)) {
        return false;
    }
    commands->remember(group);
    return true;
}

QVariantList CommandModel::choices(int row) const
{
    const Commands* commands = Commands::instance();
    const Row* picked = rowAt(row);
    if (commands == nullptr || picked == nullptr || !picked->group) {
        return {};
    }
    QVariantList variants;
    for (const QString& id : picked->members) {
        variants.append(QVariantMap{{u"commandId"_s, id},
                                    {u"title"_s, commands->title(id, picked->target)},
                                    {u"checked"_s, commands->isChecked(id, picked->target)},
                                    {u"enabled"_s, commands->isEnabled(id, picked->target)},
                                    {u"icon"_s, commands->icon(id)},
                                    {u"shortcut"_s, commands->shortcutCaps(id)}});
    }
    return variants;
}

std::vector<CommandModel::Row> CommandModel::build() const
{
    const Commands* commands = Commands::instance();
    if (commands == nullptr) {
        return {};
    }
    struct Entry {
        Row row;
        bool enabled = false;
        std::vector<CommandText> texts;
        CommandMatch match;
    };
    const QString device = m_target.isEmpty() ? commands->currentDevice() : m_target;
    const auto nameOf = [commands](const QString& serial) {
        return serial.isEmpty() ? QString() : commands->device(serial).value(u"name"_s).toString();
    };

    std::vector<Entry> entries;
    for (const Command& command : commands->table().all()) {
        const QString serial = commands->resolve(command, device);
        const bool enabled = commands->isEnabled(command.id, serial);
        const bool checked = commands->isChecked(command.id, serial);
        const QString title = commands->title(command.id, serial);
        const char* sourceTitle =
            checked && command.checkedTitle != nullptr ? command.checkedTitle : command.title;
        CommandText text{.title = title,
                         .sourceTitle = QString::fromUtf8(sourceTitle),
                         .keywords = bothLanguages(command.keywords),
                         .id = command.id,
                         .groupTitles = bothLanguages(command.groupTitle)};
        if (command.group.isEmpty()) {
            if (enabled) {
                Entry entry{.row = {.id = command.id,
                                    .target = serial,
                                    .title = title,
                                    .icon = command.icon,
                                    .shortcut = commands->shortcutCaps(command.id),
                                    .targetName = nameOf(serial),
                                    .checked = checked},
                            .enabled = true};
                entry.texts.push_back(std::move(text));
                entries.push_back(std::move(entry));
            }
            continue;
        }
        auto group = std::ranges::find_if(entries, [&command](const Entry& entry) {
            return entry.row.group && entry.row.id == command.group;
        });
        if (group == entries.end()) {
            Entry entry{.row = {.id = command.group,
                                .group = true,
                                .target = serial,
                                .title = translated(command.groupTitle),
                                .targetName = nameOf(serial)}};
            entry.texts.push_back({.title = entry.row.title,
                                   .sourceTitle = QString::fromUtf8(command.groupTitle),
                                   .id = command.group});
            entries.push_back(std::move(entry));
            group = std::prev(entries.end());
        }
        group->row.members.append(command.id);
        if (group->row.icon.isEmpty()) {
            group->row.icon = command.icon;
        }
        if (checked) {
            group->row.subtitle = title;
        }
        if (enabled) {
            group->enabled = true;
            group->texts.push_back(std::move(text));
        }
    }
    std::erase_if(entries, [](const Entry& entry) { return !entry.enabled; });

    const CommandMatcher matcher(m_query);
    if (!matcher.isEmpty()) {
        for (Entry& entry : entries) {
            entry.match = matcher.match(entry.texts);
            entry.row.highlights = entry.match.highlights;
        }
        std::erase_if(entries, [](const Entry& entry) { return !entry.match.matched; });
        std::ranges::sort(entries, [](const Entry& first, const Entry& second) {
            return CommandMatcher::ranksBefore(first.match, first.texts.front(), second.match,
                                               second.texts.front());
        });
    } else {
        const QStringList& recent = commands->recentCommands();
        const auto rank = [&recent](const Entry& entry) {
            const qsizetype at = recent.indexOf(entry.row.id);
            return at < 0 ? recent.size() : at;
        };
        if (std::ranges::any_of(entries, [&recent](const Entry& entry) { return recent.contains(entry.row.id); })) {
            std::ranges::stable_sort(entries, {}, rank);
            for (Entry& entry : entries) {
                entry.row.section = recent.contains(entry.row.id) ? u"recent"_s : u"other"_s;
            }
        }
    }

    std::vector<Row> rows;
    rows.reserve(entries.size());
    for (Entry& entry : entries) {
        rows.push_back(std::move(entry.row));
    }
    return rows;
}

const CommandModel::Row* CommandModel::rowAt(int row) const
{
    return row >= 0 && row < count() ? &m_rows[static_cast<std::size_t>(row)] : nullptr;
}

void CommandModel::rebuild()
{
    if (!m_active) {
        m_stale = true;
        return;
    }
    m_stale = false;
    std::vector<Row> rows = build();
    if (rows == m_rows) {
        return;
    }
    if (std::ranges::equal(rows, m_rows, {}, &Row::id, &Row::id) &&
        std::ranges::equal(rows, m_rows, {}, &Row::section, &Row::section)) {
        m_rows = std::move(rows);
        emit dataChanged(index(0), index(count() - 1));
        return;
    }
    const int before = count();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (count() != before) {
        emit countChanged();
    }
    const int sections = sectionsIn(m_rows);
    if (sections != m_sectionCount) {
        m_sectionCount = sections;
        emit sectionCountChanged();
    }
}

int CommandModel::sectionsIn(const std::vector<Row>& rows)
{
    QStringList sections;
    for (const Row& row : rows) {
        if (!row.section.isEmpty() && !sections.contains(row.section)) {
            sections.append(row.section);
        }
    }
    return static_cast<int>(sections.size());
}

}
