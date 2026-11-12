#include "AiChats.h"

#include "AppSettings.h"
#include "BlueprintLabels.h"
#include "BlueprintModel.h"
#include "Commands.h"
#include "HelpLibrary.h"
#include "McpGuides.h"
#include "ScriptWorkbenchApi.h"
#include "SettingsCatalog.h"

#include <gs/base/Log.h>
#include <gs/blueprint/Document.h>
#include <gs/input/Profile.h>
#include <gs/platform/AtomicFile.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJSEngine>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

constexpr std::string_view kCategory = "ai";
constexpr QStringView kFormat = u"gamescrcpy.ai-chat";
constexpr QStringView kIndexFormat = u"gamescrcpy.ai-chats";
constexpr int kVersion = 1;
constexpr std::array<QStringView, 6> kPages{u"devices", u"profiles", u"tasks", u"extensions", u"settings", u"help"};

McpOutcome said(const QString& text, bool failed = false)
{
    return McpOutcome{.content = QJsonArray{QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, text}}},
                      .failed = failed,
                      .code = {}};
}

QJsonObject stringProperty(const QString& description)
{
    return QJsonObject{{u"type"_s, u"string"_s}, {u"description"_s, description}};
}

QJsonObject toolSchema(const QString& name, const QString& description, const QJsonObject& properties,
                       const QJsonArray& required = {})
{
    QJsonObject schema{{u"type"_s, u"object"_s}, {u"properties"_s, properties}};
    if (!required.isEmpty()) {
        schema.insert(u"required"_s, required);
    }
    return QJsonObject{{u"name"_s, name}, {u"description"_s, description}, {u"inputSchema"_s, schema}};
}

QString inLanguage(const std::map<QString, QString>& texts, const QString& language)
{
    if (const auto found = texts.find(language); found != texts.end() && !found->second.isEmpty()) {
        return found->second;
    }
    if (const auto english = texts.find(u"en"_s); english != texts.end()) {
        return english->second;
    }
    return texts.empty() ? QString() : texts.begin()->second;
}

QString titleFrom(const QJsonObject& state)
{
    for (const QJsonValue& value : state.value(u"items"_s).toArray()) {
        const QJsonObject item = value.toObject();
        if (item.value(u"kind"_s).toString() == u"user") {
            const QString words = item.value(u"text"_s).toString().simplified();
            return words.size() > AiChats::kMostTitleChars ? words.left(AiChats::kMostTitleChars) + u"\u2026"_s
                                                           : words;
        }
    }
    return {};
}

bool mentions(const QString& query, std::initializer_list<QStringView> texts)
{
    const QStringList words = query.split(u' ', Qt::SkipEmptyParts);
    return std::ranges::all_of(words, [&texts](const QString& word) {
        return std::ranges::any_of(texts,
                                   [&word](QStringView text) { return text.contains(word, Qt::CaseInsensitive); });
    });
}

QString cut(const QString& text, qsizetype most)
{
    return text.size() > most ? text.left(most) + u"\u2026"_s : text;
}

QString textOf(const nlohmann::ordered_json& item, const char* key)
{
    return QString::fromStdString(item.value(key, std::string()));
}

QString textOf(const McpOutcome& outcome)
{
    QString text;
    for (const QJsonValue& part : outcome.content) {
        const QJsonObject item = part.toObject();
        if (item.value(u"type"_s).toString() == u"text") {
            text += item.value(u"text"_s).toString();
        }
    }
    return text;
}

QString settingValue(const SettingInfo& info, const QVariant& value)
{
    if (info.kind == u"text") {
        return value.toString().isEmpty() ? u"empty"_s : u"filled in (not shown)"_s;
    }
    if (info.kind == u"boolean") {
        return value.toBool() ? u"on"_s : u"off"_s;
    }
    return value.toString();
}

QString commandText(const char* source)
{
    return source == nullptr || *source == '\0' ? QString() : QCoreApplication::translate("Commands", source);
}

QString scopeText(CommandScope scope)
{
    switch (scope) {
    case CommandScope::Hub:
        return u"in the main window"_s;
    case CommandScope::Mirror:
        return u"in a mirror window"_s;
    case CommandScope::Global:
        break;
    }
    return u"in every window"_s;
}

}

QString aiConnectionLog(const QString& log, const QString& home, int most)
{
    static const QRegularExpression stamped(
        u"^\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d\\.\\d{3} \\w+\\s+(.*)$"_s);
    static constexpr std::array<QStringView, 6> kCategories{u"[session]", u"[server]",    u"[audio]",
                                                            u"[ffmpeg]",  u"[companion]", u"GameScrcpy "};
    static constexpr std::array<QStringView, 23> kWords{
        u"connect", u"adb",     u"usb",      u"aoa",     u"wireless", u"wifi",     u"wi-fi", u"pair",
        u"mirror",  u"picture", u"video",    u"audio",   u"decod",    u"encod",    u"codec", u"touch on",
        u"uhid",    u"capture", u"transport", u"tunnel", u"latency",  u"display", u"touch box"};
    std::vector<QString> kept;
    bool keeping = false;
    for (QString line : log.split(u'\n')) {
        if (line.endsWith(u'\r')) {
            line.chop(1);
        }
        const QRegularExpressionMatch match = stamped.match(line);
        if (!match.hasMatch()) {
            if (keeping && !line.isEmpty()) {
                kept.back() += u'\n' + line;
            }
            continue;
        }
        const QString message = match.captured(1);
        const auto starts = [&message](QStringView start) { return message.startsWith(start); };
        const auto says = [&message](QStringView word) { return message.contains(word, Qt::CaseInsensitive); };
        keeping = std::ranges::any_of(kCategories, starts) || std::ranges::any_of(kWords, says);
        if (keeping) {
            kept.push_back(line);
        }
    }
    if (kept.empty()) {
        return u"The recent log has no line about phones connecting or mirroring."_s;
    }
    const auto first = kept.size() > static_cast<std::size_t>(most)
                           ? kept.end() - static_cast<std::ptrdiff_t>(most)
                           : kept.begin();
    QStringList lines(first, kept.end());
    QString text = lines.join(u'\n');
    if (!home.isEmpty()) {
        text.replace(QDir::toNativeSeparators(home), u"~"_s, Qt::CaseInsensitive);
        text.replace(QDir::fromNativeSeparators(home), u"~"_s, Qt::CaseInsensitive);
    }
    return u"Recent lines about phones connecting and mirroring, oldest first:\n"_s + text;
}

AiChatTools::AiChatTools(AiToolRunner* base, const HelpLibrary* help, Navigate navigate)
    : m_base(base)
    , m_help(help)
    , m_navigate(std::move(navigate))
{
}

QJsonArray AiChatTools::tools() const
{
    QJsonArray all = m_base != nullptr ? m_base->tools() : QJsonArray();
    all.append(toolSchema(u"read_help"_s,
                          u"Reads GameScrcpy's help. Without arguments it lists the articles by slug and title; with "
                          u"topic it returns that article; with query it lists the articles that mention the words."_s,
                          QJsonObject{{u"topic"_s, stringProperty(u"Slug of an article from the list."_s)},
                                      {u"query"_s, stringProperty(u"Words to look for in the help."_s)}}));
    all.append(toolSchema(u"read_guide"_s,
                          u"Reads the step-by-step guide for a job before you do it: make_keymap, vision_logic, "
                          u"make_component, make_task or make_extension. Without a name it lists the guides."_s,
                          QJsonObject{{u"name"_s, stringProperty(u"Name of the guide."_s)}}));
    QJsonArray pages;
    for (const QStringView page : kPages) {
        pages.append(page.toString());
    }
    all.append(toolSchema(
        u"open_page"_s,
        u"Shows the user a place in GameScrcpy's main window: a page, a settings section or a help article. It only "
        u"shows the place and never changes a setting."_s,
        QJsonObject{{u"page"_s, QJsonObject{{u"type"_s, u"string"_s}, {u"enum"_s, pages}}},
                    {u"section"_s, stringProperty(u"For settings, one of video, audio, input, shortcuts, general, "
                                                  u"appearance, extensions, ai, about; for help, the article slug from "
                                                  u"read_help."_s)}},
        QJsonArray{u"page"_s}));
    all.append(toolSchema(
        u"read_blocks"_s,
        u"Looks up the blocks of the logic view, the blueprint editor: each block's name as the user sees it, its "
        u"English name, its group, the code it writes and what it does. With query it lists the blocks whose names or "
        u"code mention the words; without it, the groups and the blocks in each. Use it to explain a block or to say "
        u"which block does something."_s,
        QJsonObject{{u"query"_s, stringProperty(u"Words from a block's name or code, in any language."_s)}}));
    all.append(toolSchema(
        u"read_settings"_s,
        u"Reads GameScrcpy's settings: what each one does, the section of the settings page that has it, its value now "
        u"and its default. Text such as folders and addresses is not shown. With query or section it lists the "
        u"matching settings; without them, the setting names by section. It never changes a setting; to show the user "
        u"one, call open_page with page settings and its section."_s,
        QJsonObject{{u"query"_s, stringProperty(u"Words from the setting's name or description."_s)},
                    {u"section"_s, stringProperty(u"One of video, audio, input, general, appearance, ai."_s)}}));
    all.append(toolSchema(
        u"list_commands"_s,
        u"Lists GameScrcpy's commands, the actions of the command palette and the menus, with the window each works in "
        u"and its keyboard shortcut. With query it lists the commands that mention the words and says what each does. "
        u"The user changes shortcuts in the settings, section shortcuts."_s,
        QJsonObject{{u"query"_s, stringProperty(u"Words from the command's name or description."_s)}}));
    all.append(toolSchema(
        u"read_connection_log"_s,
        u"Reads the recent lines of GameScrcpy's log about phones connecting and mirroring: adb, USB, wireless, the "
        u"picture, the sound and touch, oldest first. Use it with list_phones when a phone does not connect, drops, "
        u"or shows no picture or sound."_s,
        QJsonObject{{u"phone"_s, stringProperty(u"Name or serial of a phone, to keep only its lines."_s)}}));
    return all;
}

void AiChatTools::run(const QString& tool, const QJsonObject& arguments, McpToolHost::Reply reply)
{
    if (tool == u"read_help") {
        reply(readHelp(arguments));
        return;
    }
    if (tool == u"read_guide") {
        reply(readGuide(arguments));
        return;
    }
    if (tool == u"open_page") {
        reply(openPage(arguments));
        return;
    }
    if (tool == u"read_blocks") {
        reply(readBlocks(arguments));
        return;
    }
    if (tool == u"read_settings") {
        reply(readSettings(arguments));
        return;
    }
    if (tool == u"list_commands") {
        reply(listCommands(arguments));
        return;
    }
    if (tool == u"read_connection_log") {
        readConnectionLog(arguments, std::move(reply));
        return;
    }
    if (m_base == nullptr) {
        reply(said(u"No tools are available."_s, true));
        return;
    }
    m_base->run(tool, arguments, std::move(reply));
}

McpOutcome AiChatTools::readHelp(const QJsonObject& arguments) const
{
    if (m_help == nullptr) {
        return said(u"The help is not available here."_s, true);
    }
    const QString language = m_help->language();
    const QString slug = arguments.value(u"topic"_s).toString().trimmed();
    if (!slug.isEmpty()) {
        const HelpTopic* topic = m_help->find(slug);
        if (topic == nullptr) {
            return said(u"There is no help article %1; call read_help without arguments for the list."_s.arg(slug),
                        true);
        }
        QString text = inLanguage(topic->texts, language);
        if (text.size() > kMostHelpChars) {
            text = text.left(kMostHelpChars) + u"\n[Cut here.]"_s;
        }
        const QString source =
            topic->extension.isEmpty() ? QString() : u"From the extension %1.\n\n"_s.arg(topic->extensionName);
        return said(u"# %1\n\n%2%3"_s.arg(inLanguage(topic->titles, language), source, text));
    }
    const QString query = arguments.value(u"query"_s).toString().simplified();
    QStringList lines;
    for (const HelpTopic& topic : m_help->topics()) {
        const QString title = inLanguage(topic.titles, language);
        if (!query.isEmpty() && !title.contains(query, Qt::CaseInsensitive) &&
            !inLanguage(topic.texts, language).contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        lines.append(topic.extension.isEmpty() ? u"- %1: %2"_s.arg(topic.slug, title)
                                               : u"- %1: %2 (from the extension %3)"_s.arg(topic.slug, title,
                                                                                            topic.extensionName));
    }
    if (lines.isEmpty()) {
        return said(u"No help article mentions \"%1\"."_s.arg(query));
    }
    const QString heading = query.isEmpty() ? u"Help articles (slug: title):"_s
                                            : u"Help articles that mention \"%1\":"_s.arg(query);
    return said(heading + u'\n' + lines.join(u'\n'));
}

McpOutcome AiChatTools::readGuide(const QJsonObject& arguments)
{
    const QString name = arguments.value(u"name"_s).toString().trimmed();
    if (name.isEmpty()) {
        QStringList lines;
        for (const QJsonValue& value : mcpPromptList()) {
            const QJsonObject guide = value.toObject();
            lines.append(
                u"- %1: %2"_s.arg(guide.value(u"name"_s).toString(), guide.value(u"description"_s).toString()));
        }
        return said(u"Guides:\n"_s + lines.join(u'\n'));
    }
    const std::optional<QJsonObject> prompt = mcpPrompt(name, {});
    if (!prompt) {
        return said(u"There is no guide %1; call read_guide without a name for the list."_s.arg(name), true);
    }
    QStringList parts;
    for (const QJsonValue& message : prompt->value(u"messages"_s).toArray()) {
        parts.append(message.toObject().value(u"content"_s).toObject().value(u"text"_s).toString());
    }
    return said(parts.join(u"\n\n"_s));
}

McpOutcome AiChatTools::openPage(const QJsonObject& arguments) const
{
    const QString page = arguments.value(u"page"_s).toString().trimmed();
    if (std::ranges::find(kPages, QStringView(page)) == kPages.end()) {
        return said(u"There is no page %1; the pages are devices, profiles, tasks, extensions, settings and help."_s.arg(
                        page),
                    true);
    }
    const QString section = arguments.value(u"section"_s).toString().trimmed();
    if (m_navigate) {
        m_navigate(page, section);
    }
    return said(section.isEmpty() ? u"Showed the user the %1 page."_s.arg(page)
                                  : u"Showed the user the %1 page at %2."_s.arg(page, section));
}

McpOutcome AiChatTools::readBlocks(const QJsonObject& arguments)
{
    static const BlueprintLabels labels;
    blueprint::Environment environment = blueprintEnvironment({}, "logic.ts", &labels);
    std::map<std::string, QString, std::less<>> typeTitles;
    for (const std::string_view name : input::kControlTypeNames) {
        if (name == "component" || name == "behavior") {
            continue;
        }
        std::string type(name);
        type[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(type[0])));
        const std::string title = labels.text("type:" + type);
        typeTitles.emplace(std::string(name), QString::fromStdString(title));
        environment.controls.push_back({std::string(name), title, type, {}});
    }
    const blueprint::Document document({}, environment, blueprint::Layout{});
    const nlohmann::ordered_json entries = document.catalog({}, {}, {}, {});
    const auto groupOf = [&typeTitles](const nlohmann::ordered_json& entry) {
        const auto owner = typeTitles.find(entry.value("control", std::string()));
        return owner != typeTitles.end() ? u"blocks of a %1 component"_s.arg(owner->second) : textOf(entry, "group");
    };
    const QString query = arguments.value(u"query"_s).toString().simplified();
    if (query.isEmpty()) {
        std::vector<std::pair<QString, QStringList>> groups;
        for (const nlohmann::ordered_json& entry : entries) {
            const QString group = groupOf(entry);
            auto found = std::ranges::find(groups, group, &std::pair<QString, QStringList>::first);
            if (found == groups.end()) {
                found = groups.emplace(groups.end(), group, QStringList());
            }
            found->second.append(textOf(entry, "title"));
        }
        QStringList lines;
        for (const auto& [group, titles] : groups) {
            lines.append(u"- %1: %2"_s.arg(group, titles.join(u", "_s)));
        }
        return said(u"Blocks by group; call read_blocks with words to see what a block does and the code it writes:\n"_s +
                    lines.join(u'\n'));
    }
    const ScriptApi& api = ScriptApi::installed();
    QStringList lines;
    int found = 0;
    for (const nlohmann::ordered_json& entry : entries) {
        const QString title = textOf(entry, "title");
        const QString english = textOf(entry, "english");
        const QString code = textOf(entry, "subtitle");
        if (!mentions(query, {title, english, code, textOf(entry, "keywords")}) || ++found > kMostBlocks) {
            continue;
        }
        QString line = u"- "_s + title;
        if (!english.isEmpty() && english != title) {
            line += u" (%1)"_s.arg(english);
        }
        line += u" · "_s + groupOf(entry);
        if (!code.isEmpty()) {
            line += u" · `%1`"_s.arg(code);
        }
        const QString description = api.documentationOf(textOf(entry, "api"));
        if (!description.isEmpty()) {
            line += u"\n  "_s + cut(description.simplified(), kMostBlockChars);
        }
        lines.append(line);
    }
    if (lines.isEmpty()) {
        return said(u"No block mentions \"%1\"; call read_blocks without query for all of them."_s.arg(query));
    }
    QString text = u"Blocks that mention \"%1\" (name, English name, group, code):\n"_s.arg(query) + lines.join(u'\n');
    if (found > kMostBlocks) {
        text += u"\n[%1 more; add words to narrow them.]"_s.arg(found - kMostBlocks);
    }
    return said(text);
}

McpOutcome AiChatTools::readSettings(const QJsonObject& arguments)
{
    const QString query = arguments.value(u"query"_s).toString().simplified();
    const QString section = arguments.value(u"section"_s).toString().trimmed();
    const std::vector<SettingInfo> infos = settingInfos();
    if (query.isEmpty() && section.isEmpty()) {
        std::vector<std::pair<QString, QStringList>> sections;
        for (const SettingInfo& info : infos) {
            auto found = std::ranges::find(sections, info.section, &std::pair<QString, QStringList>::first);
            if (found == sections.end()) {
                found = sections.emplace(sections.end(), info.section, QStringList());
            }
            found->second.append(info.title);
        }
        QStringList lines;
        for (const auto& [name, titles] : sections) {
            lines.append(u"- %1: %2"_s.arg(name, titles.join(u", "_s)));
        }
        return said(u"Settings by section; call read_settings with query or section for what they do and their "
                    u"values:\n"_s +
                    lines.join(u'\n'));
    }
    const AppSettings* settings = AppSettings::instance();
    const QVariantMap values = settings != nullptr ? settings->values() : QVariantMap();
    QStringList lines;
    int found = 0;
    for (const SettingInfo& info : infos) {
        if ((!section.isEmpty() && info.section != section) ||
            !mentions(query, {info.title, info.description, info.path}) || ++found > kMostSettings) {
            continue;
        }
        QString line = u"- %1 (`%2`, section %3): now %4, default %5"_s.arg(
            info.title, info.path, info.section, settingValue(info, values.value(info.path, info.defaultValue)),
            settingValue(info, info.defaultValue));
        if (!info.choices.isEmpty()) {
            line += u"; choices %1"_s.arg(info.choices.join(u", "_s));
        }
        if (info.kind == u"integer" && info.maximum > info.minimum) {
            line += u"; from %1 to %2"_s.arg(info.minimum).arg(info.maximum);
        }
        if (info.perDevice) {
            line += u"; each phone can have its own value"_s;
        }
        if (!info.description.isEmpty()) {
            line += u"\n  "_s + info.description;
        }
        lines.append(line);
    }
    if (lines.isEmpty()) {
        return said(u"No setting matches; call read_settings without arguments for all of them."_s);
    }
    QString text = u"Settings (name, path, section, value now, default):\n"_s + lines.join(u'\n');
    if (found > kMostSettings) {
        text += u"\n[%1 more; add words to narrow them.]"_s.arg(found - kMostSettings);
    }
    return said(text);
}

McpOutcome AiChatTools::listCommands(const QJsonObject& arguments)
{
    const Commands* commands = Commands::instance();
    if (commands == nullptr) {
        return said(u"The commands are not available here."_s, true);
    }
    const QString query = arguments.value(u"query"_s).toString().simplified();
    QStringList lines;
    int found = 0;
    for (const Command& command : commands->table().all()) {
        const QString title = commandText(command.title);
        const QString description = commandText(command.description);
        if (!mentions(query, {title, description, commandText(command.keywords), command.id})) {
            continue;
        }
        const QString keys = commands->shortcutCaps(command.id).join(u'+');
        const QString shortcut = keys.isEmpty() ? u"no shortcut"_s : u"shortcut %1"_s.arg(keys);
        if (query.isEmpty()) {
            lines.append(u"- %1, %2"_s.arg(title, shortcut));
            continue;
        }
        if (++found > kMostCommands) {
            continue;
        }
        QString line = u"- %1 (`%2`), %3, %4"_s.arg(title, command.id, scopeText(command.scope), shortcut);
        if (!description.isEmpty()) {
            line += u": "_s + description;
        }
        lines.append(line);
    }
    if (lines.isEmpty()) {
        return said(u"No command mentions \"%1\"."_s.arg(query));
    }
    QString text = query.isEmpty()
                       ? u"Commands and their shortcuts; call list_commands with words for what a command does:\n"_s
                       : u"Commands that mention \"%1\" (name, id, where it works, shortcut):\n"_s.arg(query);
    text += lines.join(u'\n');
    if (found > kMostCommands) {
        text += u"\n[%1 more; add words to narrow them.]"_s.arg(found - kMostCommands);
    }
    return said(text);
}

void AiChatTools::readConnectionLog(const QJsonObject& arguments, McpToolHost::Reply reply)
{
    if (m_base == nullptr) {
        reply(said(u"The log is not available here."_s, true));
        return;
    }
    QJsonObject wanted{{u"source"_s, u"program"_s}, {u"lines"_s, kReadLogLines}};
    const QString phone = arguments.value(u"phone"_s).toString().trimmed();
    if (!phone.isEmpty()) {
        wanted.insert(u"contains"_s, phone);
    }
    m_base->run(u"read_logs"_s, wanted, [answer = std::move(reply)](McpOutcome outcome) {
        if (outcome.failed) {
            answer(std::move(outcome));
            return;
        }
        answer(said(aiConnectionLog(textOf(outcome), QDir::homePath(), kMostConnectionLines)));
    });
}

AiChats::AiChats(AiConversation::ProviderFactory provider, AiToolRunner* tools, const HelpLibrary* help,
                 std::filesystem::path folder, QObject* parent)
    : QObject(parent)
    , m_folder(std::move(folder))
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    m_tools = std::make_unique<AiChatTools>(
        tools, help, [this](const QString& page, const QString& section) { emit pageWanted(page, section); });
    m_conversation = std::make_unique<AiConversation>(std::move(provider), m_tools.get());
    QJSEngine::setObjectOwnership(m_conversation.get(), QJSEngine::CppOwnership);
    connect(m_conversation.get(), &AiConversation::turnFinished, this, &AiChats::save);
    load();
}

AiChats::~AiChats()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

AiChats* AiChats::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QVariantList AiChats::recent() const
{
    QVariantList list;
    for (const Entry& entry : m_entries) {
        list.append(QVariantMap{{u"id"_s, entry.id},
                                {u"title"_s, entry.title},
                                {u"updated"_s, QDateTime::fromMSecsSinceEpoch(entry.updated)}});
    }
    return list;
}

bool AiChats::send(const QString& text, const QVariantMap& context)
{
    const auto field = [&context](QStringView key) { return context.value(key.toString()).toString(); };
    m_conversation->setContext(AiContext{.serial = field(u"serial"),
                                         .phone = field(u"phone"),
                                         .app = field(u"app"),
                                         .profile = field(u"profile"),
                                         .profileName = field(u"profileName"),
                                         .window = field(u"window"),
                                         .page = field(u"page"),
                                         .selection = field(u"selection")});
    return m_conversation->send(text);
}

void AiChats::newConversation()
{
    m_conversation->stop();
    m_conversation->clear();
    setCurrent({});
}

bool AiChats::open(const QString& id)
{
    if (std::ranges::find(m_entries, id, &Entry::id) == m_entries.end()) {
        return false;
    }
    std::string bytes;
    if (platform::readFile(fileOf(id), kMostFileBytes, bytes)) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(bytes));
    if (!document.isObject()) {
        return false;
    }
    m_conversation->stop();
    m_conversation->restore(document.object());
    setCurrent(id);
    return true;
}

void AiChats::remove(const QString& id)
{
    const auto found = std::ranges::find(m_entries, id, &Entry::id);
    if (found == m_entries.end()) {
        return;
    }
    forget(static_cast<std::size_t>(found - m_entries.begin()));
    writeIndex();
    if (id == m_current) {
        m_conversation->clear();
        setCurrent({});
    }
    emit recentChanged();
}

void AiChats::clearAll()
{
    m_conversation->clear();
    while (!m_entries.empty()) {
        forget(m_entries.size() - 1);
    }
    writeIndex();
    setCurrent({});
    emit recentChanged();
}

void AiChats::toggle()
{
    if (m_windowOpen) {
        emit windowWanted();
    } else {
        emit toggleRequested();
    }
}

void AiChats::openWindow()
{
    emit windowWanted();
}

void AiChats::setWindowOpen(bool open)
{
    if (m_windowOpen == open) {
        return;
    }
    m_windowOpen = open;
    emit windowOpenChanged();
}

void AiChats::noteContext(const QVariantMap& context)
{
    if (m_lastContext == context) {
        return;
    }
    m_lastContext = context;
    emit lastContextChanged();
}

void AiChats::load()
{
    std::error_code error;
    if (!std::filesystem::is_directory(m_folder, error)) {
        return;
    }
    std::map<QString, Entry> listed;
    if (std::string index; !platform::readFile(m_folder / kIndexName, kMostFileBytes, index)) {
        const QJsonObject document = QJsonDocument::fromJson(QByteArray::fromStdString(index)).object();
        if (document.value(u"format"_s).toString() == kIndexFormat) {
            for (const QJsonValue& value : document.value(u"chats"_s).toArray()) {
                const QJsonObject chat = value.toObject();
                const QString id = chat.value(u"id"_s).toString();
                listed.insert_or_assign(
                    id, Entry{.id = id,
                              .title = chat.value(u"title"_s).toString(),
                              .updated = static_cast<qint64>(chat.value(u"updated"_s).toDouble())});
            }
        }
    }
    bool stale = false;
    for (const std::filesystem::directory_entry& item : std::filesystem::directory_iterator(m_folder, error)) {
        if (item.path().extension() != L".json") {
            continue;
        }
        const QString name = QString::fromStdWString(item.path().stem().wstring());
        if (const auto known = listed.find(name); known != listed.end() && fileOf(name) == item.path()) {
            m_entries.push_back(known->second);
            listed.erase(known);
            continue;
        }
        stale = true;
        std::string bytes;
        if (platform::readFile(item.path(), kMostFileBytes, bytes)) {
            continue;
        }
        const QJsonObject document = QJsonDocument::fromJson(QByteArray::fromStdString(bytes)).object();
        const QString id = document.value(u"id"_s).toString();
        if (document.value(u"format"_s).toString() != kFormat || id.isEmpty() || fileOf(id) != item.path()) {
            continue;
        }
        m_entries.push_back(Entry{.id = id,
                                  .title = document.value(u"title"_s).toString(),
                                  .updated = static_cast<qint64>(document.value(u"updated"_s).toDouble())});
    }
    std::ranges::sort(m_entries, std::ranges::greater{}, &Entry::updated);
    if (stale || !listed.empty()) {
        writeIndex();
    }
}

void AiChats::save()
{
    if (m_conversation->count() == 0) {
        return;
    }
    const QJsonObject state = m_conversation->saved();
    const QString id = m_current.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : m_current;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString title = titleFrom(state);
    QJsonObject document{{u"format"_s, kFormat.toString()},  {u"version"_s, kVersion},
                         {u"id"_s, id},                      {u"title"_s, title},
                         {u"updated"_s, static_cast<double>(now)}, {u"items"_s, state.value(u"items"_s)},
                         {u"history"_s, state.value(u"history"_s)}};
    if (state.contains(u"choice"_s)) {
        document.insert(u"choice"_s, state.value(u"choice"_s));
    }
    std::error_code made;
    std::filesystem::create_directories(m_folder, made);
    const QByteArray bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
    if (const std::error_code failed =
            platform::writeFileAtomically(fileOf(id), std::string_view(bytes.constData(), bytes.size()))) {
        log::warning(kCategory, "the conversation could not be saved: {}", failed.message());
        return;
    }
    if (const auto found = std::ranges::find(m_entries, id, &Entry::id); found != m_entries.end()) {
        m_entries.erase(found);
    }
    m_entries.insert(m_entries.begin(), Entry{.id = id, .title = title, .updated = now});
    writeIndex();
    setCurrent(id);
    emit recentChanged();
}

void AiChats::writeIndex() const
{
    QJsonArray chats;
    for (const Entry& entry : m_entries) {
        chats.append(QJsonObject{{u"id"_s, entry.id},
                                 {u"title"_s, entry.title},
                                 {u"updated"_s, static_cast<double>(entry.updated)}});
    }
    const QJsonObject document{
        {u"format"_s, kIndexFormat.toString()}, {u"version"_s, kVersion}, {u"chats"_s, chats}};
    const QByteArray bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
    std::error_code made;
    std::filesystem::create_directories(m_folder, made);
    if (const std::error_code failed = platform::writeFileAtomically(
            m_folder / kIndexName, std::string_view(bytes.constData(), bytes.size()))) {
        log::warning(kCategory, "the list of conversations could not be saved: {}", failed.message());
    }
}

void AiChats::setCurrent(const QString& id)
{
    if (m_current == id) {
        return;
    }
    m_current = id;
    emit currentChanged();
}

void AiChats::forget(std::size_t index)
{
    (void)platform::removeFile(fileOf(m_entries[index].id));
    m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
}

std::filesystem::path AiChats::fileOf(const QString& id) const
{
    return m_folder / std::filesystem::path(id.toStdWString() + L".json");
}

}
