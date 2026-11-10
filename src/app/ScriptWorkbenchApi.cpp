#include "ScriptWorkbenchApi.h"

#include "QtConversions.h"
#include "ScriptHighlighter.h"

#include <gs/input/Profile.h>
#include <gs/script/ApiFiles.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <map>
#include <string>
#include <utility>

namespace gs::app {

namespace {

std::map<QString, QString> examplesOf(std::string_view declarations)
{
    static const QRegularExpression exampleLine(QStringLiteral(R"(^\s*\*\s*@example\s+(.*)$)"));
    static const QRegularExpression container(
        QStringLiteral(R"(^(?:declare\s+namespace|interface)\s+([A-Za-z_$][\w$]*)\s*\{\s*$)"));
    static const QRegularExpression global(QStringLiteral(R"(^declare\s+(?:function|const)\s+([A-Za-z_$][\w$]*)\s*[(<:])"));
    static const QRegularExpression member(
        QStringLiteral(R"(^\s+(?:function\s+|readonly\s+)?([A-Za-z_$][\w$]*)\s*[(:<?])"));
    std::map<QString, QString> examples;
    const QString text = toQString(declarations);
    QString owner;
    QString example;
    for (const QString& line : text.split(u'\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("/**"))) {
            example.clear();
            continue;
        }
        if (const QRegularExpressionMatch found = exampleLine.match(line); found.hasMatch()) {
            example = found.captured(1).trimmed();
            continue;
        }
        if (trimmed.startsWith(u'*')) {
            continue;
        }
        if (const QRegularExpressionMatch found = container.match(line); found.hasMatch()) {
            owner = found.captured(1);
            example.clear();
            continue;
        }
        if (line.startsWith(u'}')) {
            owner.clear();
            continue;
        }
        const QRegularExpressionMatch found = owner.isEmpty() ? global.match(line) : member.match(line);
        if (!found.hasMatch()) {
            continue;
        }
        const QString label = owner.isEmpty() ? found.captured(1) : owner + u'.' + found.captured(1);
        if (!example.isEmpty()) {
            examples.emplace(label, example);
        }
        example.clear();
    }
    return examples;
}

bool callable(const ApiEntry& entry)
{
    return entry.kind == u"function" || entry.kind == u"method";
}

QString shownDocumentation(const QString& documentation)
{
    if (documentation.isEmpty()) {
        return documentation;
    }
    static const QByteArray context(script::kApiTextContext.data(),
                                    static_cast<qsizetype>(script::kApiTextContext.size()));
    return QCoreApplication::translate(context.constData(), documentation.toUtf8().constData());
}

bool takesArguments(const ApiEntry& entry)
{
    const qsizetype open = entry.detail.indexOf(u'(');
    return open >= 0 && open + 1 < entry.detail.size() && entry.detail[open + 1] != u')';
}

QString shortDetail(const ApiEntry& entry)
{
    if (entry.kind == u"namespace") {
        return {};
    }
    for (const QString& head : {entry.label, entry.name}) {
        if (!head.isEmpty() && entry.detail.startsWith(head)) {
            const QString rest = entry.detail.mid(head.size());
            if (rest.startsWith(u'(') || rest.startsWith(u':')) {
                return rest;
            }
        }
    }
    return entry.detail;
}

CompletionItem itemOf(const ApiEntry& entry, qsizetype from)
{
    CompletionItem item{.label = entry.name,
                        .kind = entry.kind,
                        .detail = shortDetail(entry),
                        .documentation = shownDocumentation(entry.documentation),
                        .from = static_cast<int>(from)};
    if (callable(entry)) {
        item.insert = entry.name + QStringLiteral("()");
        item.caret = static_cast<int>(entry.name.size()) + (takesArguments(entry) ? 1 : 2);
    } else {
        item.insert = entry.name;
        item.caret = static_cast<int>(entry.name.size());
    }
    return item;
}

bool plainIdentifier(QStringView name)
{
    return !name.isEmpty() && identifierStart(name.front()) &&
           std::ranges::all_of(name.sliced(1), [](QChar c) { return identifierPart(c); });
}

QString quoted(const QString& text)
{
    QString escaped = text;
    escaped.replace(u'\\', QStringLiteral("\\\\"));
    escaped.replace(u'"', QStringLiteral("\\\""));
    return u'"' + escaped + u'"';
}

CompletionItem nameItem(const ProfileName& name, const QString& kind, qsizetype from, qsizetype dot)
{
    CompletionItem item{.label = name.id, .kind = kind, .detail = name.detail};
    if (plainIdentifier(name.id)) {
        item.insert = name.id;
        item.from = static_cast<int>(from);
    } else {
        item.insert = u'[' + quoted(name.id) + u']';
        item.from = static_cast<int>(dot);
    }
    item.caret = static_cast<int>(item.insert.size());
    return item;
}

qsizetype identifierBefore(QStringView line, qsizetype end)
{
    qsizetype start = end;
    while (start > 0 && identifierPart(line[start - 1])) {
        --start;
    }
    return start;
}

qsizetype dotBefore(QStringView line, qsizetype at)
{
    if (at <= 0 || line[at - 1] != u'.') {
        return -1;
    }
    if (at >= 2 && line[at - 2] == u'.') {
        return -1;
    }
    return at - 1;
}

qsizetype ownerEnd(QStringView line, qsizetype dot)
{
    return dot > 0 && line[dot - 1] == u'?' ? dot - 1 : dot;
}

}

ScriptApi::ScriptApi(std::string_view completions, std::string_view declarations)
{
    const std::map<QString, QString> examples = examplesOf(declarations);
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray(completions.data(), static_cast<qsizetype>(completions.size())));
    for (const QJsonValue& value : document.array()) {
        const QJsonObject object = value.toObject();
        ApiEntry entry;
        entry.label = object.value(QStringLiteral("label")).toString();
        if (entry.label.isEmpty()) {
            continue;
        }
        entry.kind = object.value(QStringLiteral("kind")).toString();
        entry.detail = object.value(QStringLiteral("detail")).toString();
        entry.documentation = object.value(QStringLiteral("documentation")).toString();
        const qsizetype dot = entry.label.lastIndexOf(u'.');
        entry.owner = dot < 0 ? QString() : entry.label.left(dot);
        entry.name = dot < 0 ? entry.label : entry.label.mid(dot + 1);
        if (const auto example = examples.find(entry.label); example != examples.end()) {
            entry.example = example->second;
        }
        m_entries.push_back(std::move(entry));
    }
}

const ScriptApi& ScriptApi::installed()
{
    static const ScriptApi api = [] {
        std::string completions;
        std::string declarations;
        for (const script::ApiFile& file : script::apiFiles()) {
            if (file.path == "completions.json") {
                completions = file.contents;
            } else if (file.path == "types/gamescrcpy.d.ts") {
                declarations = file.contents;
            }
        }
        return ScriptApi(completions, declarations);
    }();
    return api;
}

const ApiEntry* ScriptApi::find(QStringView label) const
{
    const auto found = std::ranges::find_if(m_entries, [label](const ApiEntry& entry) { return entry.label == label; });
    return found == m_entries.end() ? nullptr : &*found;
}

QString ScriptApi::documentationOf(QStringView label) const
{
    const ApiEntry* entry = label.isEmpty() ? nullptr : find(label);
    return entry != nullptr ? shownDocumentation(entry->documentation) : QString();
}

bool ScriptApi::isNamespace(QStringView name) const
{
    return std::ranges::any_of(m_entries,
                               [name](const ApiEntry& entry) { return entry.kind == u"namespace" && entry.label == name; });
}

QVariantList ScriptApi::list() const
{
    QVariantList list;
    for (const ApiEntry& entry : m_entries) {
        list.append(QVariantMap{{QStringLiteral("label"), entry.label},
                                {QStringLiteral("kind"), entry.kind},
                                {QStringLiteral("detail"), entry.kind == u"namespace" ? QString() : entry.detail},
                                {QStringLiteral("documentation"), shownDocumentation(entry.documentation)},
                                {QStringLiteral("example"), entry.example}});
    }
    return list;
}

ProfileNames profileNamesOf(const input::Profile& profile)
{
    ProfileNames names;
    for (const input::Control& control : profile.controls) {
        const QString type = toQString(input::kControlTypeNames[control.kind.index()]);
        const QString name = toQString(control.name);
        names.controls.push_back({.id = toQString(control.id),
                                  .detail = name.isEmpty() ? type : name + QStringLiteral(" · ") + type,
                                  .shape = type.left(1).toUpper() + type.mid(1)});
    }
    for (const input::Variable& state : profile.vars) {
        names.states.push_back({.id = toQString(state.id), .detail = toQString(state.name), .shape = {}});
    }
    return names;
}

QStringList shapesOf(const QString& shape)
{
    QStringList chain{shape};
    if (shape == u"Wasd" || shape == u"PadStick") {
        chain.append(QStringLiteral("Stick"));
    } else if (shape == u"Camera" || shape == u"PadCamera") {
        chain.append(QStringLiteral("Look"));
    }
    chain.append(QStringLiteral("Component"));
    return chain;
}

std::vector<CompletionItem> completionsAt(const ScriptApi& api, const ProfileNames& profile, QStringView line,
                                          int column, int state, bool asked)
{
    std::vector<CompletionItem> items;
    const qsizetype cursor = std::clamp<qsizetype>(column, 0, line.size());
    if (insideLiteral(line, state, static_cast<int>(cursor))) {
        return items;
    }
    const qsizetype start = identifierBefore(line, cursor);
    const QStringView prefix = line.sliced(start, cursor - start);
    if (!prefix.isEmpty() && !identifierStart(prefix.front())) {
        return items;
    }
    const auto offer = [&items, prefix](CompletionItem item, QStringView key) {
        if (key.startsWith(prefix, Qt::CaseInsensitive)) {
            items.push_back(std::move(item));
        }
    };
    if (const qsizetype dot = dotBefore(line, start); dot >= 0) {
        const qsizetype end = ownerEnd(line, dot);
        const qsizetype ownerStart = identifierBefore(line, end);
        const QStringView owner = line.sliced(ownerStart, end - ownerStart);
        if (owner.isEmpty() || !identifierStart(owner.front())) {
            return items;
        }
        if (const qsizetype outer = dotBefore(line, ownerStart); outer >= 0) {
            const qsizetype rootEnd = ownerEnd(line, outer);
            const qsizetype rootStart = identifierBefore(line, rootEnd);
            const QStringView root = line.sliced(rootStart, rootEnd - rootStart);
            QStringList shapes;
            if (dotBefore(line, rootStart) < 0 && root == QStringView(u"controls")) {
                const auto found = std::ranges::find_if(
                    profile.controls, [owner](const ProfileName& name) { return QStringView(name.id) == owner; });
                shapes = shapesOf(found != profile.controls.end() ? found->shape : QStringLiteral("Component"));
            } else if (dotBefore(line, rootStart) < 0 && root == QStringView(u"vars")) {
                shapes.append(QStringLiteral("Variable"));
            } else if (dotBefore(line, rootStart) < 0 && root == QStringView(u"keys")) {
                shapes.append(QStringLiteral("Key"));
            }
            if (shapes.isEmpty()) {
                return items;
            }
            for (const ApiEntry& entry : api.entries()) {
                if (shapes.contains(entry.owner)) {
                    offer(itemOf(entry, start), entry.name);
                }
            }
        } else if (owner == QStringView(u"controls")) {
            for (const ProfileName& name : profile.controls) {
                offer(nameItem(name, QStringLiteral("control"), start, dot), name.id);
            }
        } else if (owner == QStringView(u"vars")) {
            for (const ProfileName& name : profile.states) {
                offer(nameItem(name, QStringLiteral("state"), start, dot), name.id);
            }
        } else if (owner == QStringView(u"globals")) {
            for (const ApiEntry& entry : api.entries()) {
                if (entry.owner == u"Globals") {
                    offer(itemOf(entry, start), entry.name);
                }
            }
        } else if (api.isNamespace(owner)) {
            for (const ApiEntry& entry : api.entries()) {
                if (entry.owner == owner) {
                    offer(itemOf(entry, start), entry.name);
                }
            }
        }
    } else {
        if (prefix.size() < (asked ? 0 : 2)) {
            return items;
        }
        for (const ApiEntry& entry : api.entries()) {
            if (entry.owner.isEmpty() &&
                (entry.kind == u"function" || entry.kind == u"variable" || entry.kind == u"namespace")) {
                offer(itemOf(entry, start), entry.name);
            }
        }
    }
    if (!asked && items.size() == 1 && items.front().label == prefix) {
        items.clear();
    }
    std::ranges::stable_sort(items, [prefix](const CompletionItem& first, const CompletionItem& second) {
        const bool firstExact = first.label.startsWith(prefix);
        const bool secondExact = second.label.startsWith(prefix);
        if (firstExact != secondExact) {
            return firstExact;
        }
        return QString::compare(first.label, second.label, Qt::CaseInsensitive) < 0;
    });
    if (items.size() > kMostCompletions) {
        items.resize(kMostCompletions);
    }
    return items;
}

}
