#include "AiConversation.h"

#include "AiAssistant.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype kMostShownChars = 2000;
constexpr qsizetype kMostSnapshotChars = 60'000;
constexpr auto kSerialTools = std::to_array<QStringView>(
    {QStringView(u"get_frame"), QStringView(u"find_image"), QStringView(u"run_script"),
     QStringView(u"start_recording"), QStringView(u"stop_recording"), QStringView(u"propose_keymap")});
constexpr auto kProfileTools =
    std::to_array<QStringView>({QStringView(u"read_profile"), QStringView(u"write_profile"), QStringView(u"run_replay")});

template <std::size_t N>
bool listed(const std::array<QStringView, N>& names, const QString& name)
{
    for (const QStringView known : names) {
        if (known == name) {
            return true;
        }
    }
    return false;
}

QString shortened(const QString& text, qsizetype most)
{
    if (text.size() <= most) {
        return text;
    }
    return text.left(most) + u"\n..."_s;
}

QJsonArray textContent(const QString& text)
{
    return QJsonArray{QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, text}}};
}

McpOutcome refused(const QString& text)
{
    return McpOutcome{.content = textContent(text), .failed = true, .code = {}};
}

QString pretty(const QJsonObject& arguments)
{
    if (arguments.isEmpty()) {
        return {};
    }
    return shortened(QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Indented)).trimmed(),
                     kMostShownChars);
}

QString summaryOf(const QJsonArray& content)
{
    QStringList parts;
    for (const QJsonValue& value : content) {
        const QJsonObject item = value.toObject();
        const QString type = item.value(u"type"_s).toString();
        if (type == u"text") {
            parts.append(item.value(u"text"_s).toString());
        } else if (type == u"image") {
            parts.append(QCoreApplication::translate("AiConversation", "(a picture)"));
        }
    }
    return shortened(parts.join(u'\n').trimmed(), kMostShownChars);
}

QJsonObject toolNamed(const QJsonArray& tools, const QString& name)
{
    for (const QJsonValue& value : tools) {
        const QJsonObject tool = value.toObject();
        if (tool.value(u"name"_s).toString() == name) {
            return tool;
        }
    }
    return {};
}

bool replaced(QString& field, const QString& value)
{
    if (field == value) {
        return false;
    }
    field = value;
    return true;
}

QString kindName(int kind)
{
    static constexpr std::array<QStringView, 6> kNames{u"user",   u"assistant", u"tool",
                                                       u"notice", u"problem",   u"thinking"};
    return kind >= 0 && kind < static_cast<int>(kNames.size()) ? kNames[static_cast<std::size_t>(kind)].toString()
                                                                 : QString();
}

QString whereText(const AiContext& context)
{
    static constexpr std::array<std::pair<QStringView, QStringView>, 6> kWindows{{
        {u"main", u"the main window"},
        {u"editor", u"the key mapping editor"},
        {u"logic", u"the logic editor"},
        {u"mirror", u"a mirror window that shows the phone's screen"},
        {u"vision", u"the vision workbench, where the pictures, colours, regions and points for logic are made"},
        {u"script", u"the script workbench, where the code of the logic is written"},
    }};
    const auto window = std::ranges::find(kWindows, QStringView(context.window), [](const auto& entry) {
        return entry.first;
    });
    if (window == kWindows.end()) {
        return {};
    }
    QString line = u"The user is in "_s + window->second.toString();
    if (!context.page.isEmpty()) {
        line += u", on the %1 page"_s.arg(context.page);
    }
    line += u'.';
    if (!context.selection.isEmpty()) {
        line += u" Selected there: %1."_s.arg(shortened(context.selection, kMostShownChars));
    }
    return line + u"\n\n"_s;
}

}

QJsonArray aiOfferedTools(const QJsonArray& tools, const AiScope& scope)
{
    QJsonArray offered;
    for (const QJsonValue& value : tools) {
        const QString name = value.toObject().value(u"name"_s).toString();
        if ((name == u"list_profiles" && !scope.profile.isEmpty()) ||
            (name == u"list_phones" && !scope.serial.isEmpty())) {
            continue;
        }
        offered.append(value);
    }
    return offered;
}

AiGuarded aiGuard(const QString& tool, const QJsonObject& arguments, const QJsonArray& offered, const AiScope& scope)
{
    const QJsonObject described = toolNamed(offered, tool);
    if (described.isEmpty()) {
        return {arguments, u"There is no tool named %1 here; use only the tools you are given."_s.arg(tool)};
    }
    const QJsonObject properties =
        described.value(u"inputSchema"_s).toObject().value(u"properties"_s).toObject();
    QJsonObject given = arguments;
    const QString serial = given.value(u"serial"_s).toString();
    if (!serial.isEmpty() && !scope.serial.isEmpty() && serial != scope.serial) {
        return {given, u"In this window the assistant works only with the phone shown here, serial %1."_s.arg(
                           scope.serial)};
    }
    const QString profile = given.value(u"profile"_s).toString();
    const QString profileRefusal =
        u"In this window the assistant works only with the profile open here, id %1."_s.arg(scope.profile);
    if (!profile.isEmpty() && !scope.profile.isEmpty() && profile != scope.profile) {
        return {given, profileRefusal};
    }
    const QString target = given.value(u"target"_s).toString();
    if (target.startsWith(u"profile:") && !scope.profile.isEmpty() && target.mid(8) != scope.profile) {
        return {given, profileRefusal};
    }
    const bool otherProfile = tool == u"propose_keymap" && !profile.isEmpty();
    if (listed(kSerialTools, tool) && properties.contains(u"serial"_s) && serial.isEmpty() && !scope.serial.isEmpty() &&
        !otherProfile) {
        given.insert(u"serial"_s, scope.serial);
    }
    if (listed(kProfileTools, tool) && properties.contains(u"profile"_s) && profile.isEmpty() &&
        !scope.profile.isEmpty()) {
        given.insert(u"profile"_s, scope.profile);
    }
    if (tool == u"read_logs" && properties.contains(u"source"_s)) {
        given.insert(u"source"_s, u"logic"_s);
    }
    if (tool == u"get_frame" && properties.contains(u"maxSide"_s) && !given.contains(u"maxSide"_s)) {
        given.insert(u"maxSide"_s, kAiFrameSide);
    }
    return {given, {}};
}

QString aiToolTitle(const QString& tool, const QJsonObject& arguments)
{
    if (tool == u"api_reference") {
        const QString query = arguments.value(u"query"_s).toString().simplified();
        return query.isEmpty()
                   ? QCoreApplication::translate("AiConversation", "Looked up the script API")
                   : QCoreApplication::translate("AiConversation", "Looked up “%1” in the script API").arg(query);
    }
    if (tool == u"run_replay") {
        const double until = arguments.value(u"untilMs"_s).toDouble();
        if (until >= 1000.0) {
            const int seconds = static_cast<int>(until / 1000.0);
            return QCoreApplication::translate("AiConversation", "Tried the logic on a recording for %n second(s)",
                                               nullptr, seconds);
        }
        return QCoreApplication::translate("AiConversation", "Tried the logic on a recording");
    }
    if (tool == u"get_frame") {
        return QCoreApplication::translate("AiConversation", "Looked at the phone's screen");
    }
    if (tool == u"find_image") {
        return QCoreApplication::translate("AiConversation", "Looked for a picture on the screen");
    }
    if (tool == u"read_logs") {
        return QCoreApplication::translate("AiConversation", "Read the log of the logic");
    }
    if (tool == u"read_profile") {
        return arguments.value(u"draft"_s).toBool()
                   ? QCoreApplication::translate("AiConversation", "Read the draft of the profile")
                   : QCoreApplication::translate("AiConversation", "Read the profile");
    }
    if (tool == u"write_profile") {
        const int files = static_cast<int>(arguments.value(u"files"_s).toObject().size());
        return QCoreApplication::translate("AiConversation", "Wrote %n file(s) into the draft", nullptr, files);
    }
    if (tool == u"run_script") {
        return QCoreApplication::translate("AiConversation", "Ran a script on the phone");
    }
    if (tool == u"read_draft") {
        return QCoreApplication::translate("AiConversation", "Read the draft");
    }
    if (tool == u"draft_diff") {
        return QCoreApplication::translate("AiConversation", "Compared the draft with the profile");
    }
    if (tool == u"apply_draft") {
        return QCoreApplication::translate("AiConversation", "Asked to apply the draft");
    }
    if (tool == u"discard_draft") {
        return QCoreApplication::translate("AiConversation", "Discarded the draft");
    }
    if (tool == u"read_blueprint") {
        return QCoreApplication::translate("AiConversation", "Read the blueprint");
    }
    if (tool == u"edit_blueprint") {
        return QCoreApplication::translate("AiConversation", "Changed the blueprint in the draft");
    }
    if (tool == u"list_components") {
        return QCoreApplication::translate("AiConversation", "Listed the components");
    }
    if (tool == u"read_component") {
        return QCoreApplication::translate("AiConversation", "Read a component");
    }
    if (tool == u"check") {
        return QCoreApplication::translate("AiConversation", "Checked the code");
    }
    if (tool == u"start_recording") {
        return QCoreApplication::translate("AiConversation", "Asked to start recording");
    }
    if (tool == u"stop_recording") {
        return QCoreApplication::translate("AiConversation", "Stopped recording");
    }
    if (tool == u"list_recordings") {
        return QCoreApplication::translate("AiConversation", "Listed the recordings");
    }
    if (tool == u"propose_keymap") {
        return QCoreApplication::translate("AiConversation", "Suggested keys on the picture");
    }
    if (tool == u"keymap_draft_status") {
        return QCoreApplication::translate("AiConversation", "Checked the suggested keys");
    }
    if (tool == u"read_help") {
        const QString topic = arguments.value(u"topic"_s).toString().trimmed();
        const QString query = arguments.value(u"query"_s).toString().simplified();
        if (!topic.isEmpty()) {
            return QCoreApplication::translate("AiConversation", "Read the help: %1").arg(topic);
        }
        return query.isEmpty() ? QCoreApplication::translate("AiConversation", "Listed the help articles")
                               : QCoreApplication::translate("AiConversation", "Searched the help for “%1”").arg(query);
    }
    if (tool == u"read_guide") {
        const QString name = arguments.value(u"name"_s).toString().trimmed();
        return name.isEmpty() ? QCoreApplication::translate("AiConversation", "Listed the guides")
                              : QCoreApplication::translate("AiConversation", "Read the guide %1").arg(name);
    }
    if (tool == u"open_page") {
        return QCoreApplication::translate("AiConversation", "Showed you the %1 page")
            .arg(arguments.value(u"page"_s).toString());
    }
    const QString query = arguments.value(u"query"_s).toString().simplified();
    if (tool == u"read_blocks") {
        return query.isEmpty()
                   ? QCoreApplication::translate("AiConversation", "Listed the blocks")
                   : QCoreApplication::translate("AiConversation", "Looked up “%1” in the blocks").arg(query);
    }
    if (tool == u"read_settings") {
        return query.isEmpty()
                   ? QCoreApplication::translate("AiConversation", "Read the settings")
                   : QCoreApplication::translate("AiConversation", "Looked up “%1” in the settings").arg(query);
    }
    if (tool == u"list_commands") {
        return query.isEmpty()
                   ? QCoreApplication::translate("AiConversation", "Listed the commands and shortcuts")
                   : QCoreApplication::translate("AiConversation", "Looked up “%1” in the commands").arg(query);
    }
    if (tool == u"read_connection_log") {
        return QCoreApplication::translate("AiConversation", "Read the connection log");
    }
    if (tool == u"list_profiles") {
        return QCoreApplication::translate("AiConversation", "Listed the profiles");
    }
    if (tool == u"list_tasks") {
        return QCoreApplication::translate("AiConversation", "Listed the tasks");
    }
    if (tool == u"read_task") {
        return QCoreApplication::translate("AiConversation", "Read a task");
    }
    if (tool == u"list_phones") {
        return QCoreApplication::translate("AiConversation", "Listed the phones");
    }
    if (tool == u"write_component") {
        return QCoreApplication::translate("AiConversation", "Wrote a component into the draft");
    }
    if (tool == u"write_task") {
        return QCoreApplication::translate("AiConversation", "Wrote a task into the draft");
    }
    if (tool == u"open_draft_preview") {
        return QCoreApplication::translate("AiConversation", "Showed the draft on the phone");
    }
    if (tool == u"request_materials") {
        return QCoreApplication::translate("AiConversation", "Asked you for pictures and places");
    }
    if (tool == u"create_extension") {
        return QCoreApplication::translate("AiConversation", "Created an extension");
    }
    if (tool == u"write_extension") {
        return QCoreApplication::translate("AiConversation", "Wrote into the extension");
    }
    if (tool == u"validate_extension") {
        return QCoreApplication::translate("AiConversation", "Checked the extension");
    }
    if (tool == u"pack_extension") {
        return QCoreApplication::translate("AiConversation", "Packed the extension");
    }
    return QCoreApplication::translate("AiConversation", "Used %1").arg(tool);
}

QString aiToolRunningTitle(const QString& tool, const QJsonObject& arguments)
{
    static constexpr std::array<std::pair<QStringView, const char*>, 35> kRunning{{
        {u"run_replay", QT_TRANSLATE_NOOP("AiConversation", "Trying the logic on a recording…")},
        {u"get_frame", QT_TRANSLATE_NOOP("AiConversation", "Looking at the phone's screen…")},
        {u"find_image", QT_TRANSLATE_NOOP("AiConversation", "Looking for a picture on the screen…")},
        {u"read_logs", QT_TRANSLATE_NOOP("AiConversation", "Reading the log of the logic…")},
        {u"write_profile", QT_TRANSLATE_NOOP("AiConversation", "Writing into the draft…")},
        {u"run_script", QT_TRANSLATE_NOOP("AiConversation", "Running a script on the phone…")},
        {u"read_draft", QT_TRANSLATE_NOOP("AiConversation", "Reading the draft…")},
        {u"draft_diff", QT_TRANSLATE_NOOP("AiConversation", "Comparing the draft with the profile…")},
        {u"apply_draft", QT_TRANSLATE_NOOP("AiConversation", "Asking to apply the draft…")},
        {u"discard_draft", QT_TRANSLATE_NOOP("AiConversation", "Discarding the draft…")},
        {u"read_blueprint", QT_TRANSLATE_NOOP("AiConversation", "Reading the blueprint…")},
        {u"edit_blueprint", QT_TRANSLATE_NOOP("AiConversation", "Changing the blueprint in the draft…")},
        {u"list_components", QT_TRANSLATE_NOOP("AiConversation", "Listing the components…")},
        {u"read_component", QT_TRANSLATE_NOOP("AiConversation", "Reading a component…")},
        {u"check", QT_TRANSLATE_NOOP("AiConversation", "Checking the code…")},
        {u"start_recording", QT_TRANSLATE_NOOP("AiConversation", "Asking to start recording…")},
        {u"stop_recording", QT_TRANSLATE_NOOP("AiConversation", "Stopping the recording…")},
        {u"list_recordings", QT_TRANSLATE_NOOP("AiConversation", "Listing the recordings…")},
        {u"propose_keymap", QT_TRANSLATE_NOOP("AiConversation", "Suggesting keys on the picture…")},
        {u"keymap_draft_status", QT_TRANSLATE_NOOP("AiConversation", "Checking the suggested keys…")},
        {u"open_page", QT_TRANSLATE_NOOP("AiConversation", "Opening a page for you…")},
        {u"list_profiles", QT_TRANSLATE_NOOP("AiConversation", "Listing the profiles…")},
        {u"list_tasks", QT_TRANSLATE_NOOP("AiConversation", "Listing the tasks…")},
        {u"read_task", QT_TRANSLATE_NOOP("AiConversation", "Reading a task…")},
        {u"list_phones", QT_TRANSLATE_NOOP("AiConversation", "Listing the phones…")},
        {u"write_component", QT_TRANSLATE_NOOP("AiConversation", "Writing a component into the draft…")},
        {u"write_task", QT_TRANSLATE_NOOP("AiConversation", "Writing a task into the draft…")},
        {u"open_draft_preview", QT_TRANSLATE_NOOP("AiConversation", "Showing the draft on the phone…")},
        {u"request_materials", QT_TRANSLATE_NOOP("AiConversation", "Asking you for pictures and places…")},
        {u"create_extension", QT_TRANSLATE_NOOP("AiConversation", "Creating an extension…")},
        {u"write_extension", QT_TRANSLATE_NOOP("AiConversation", "Writing into the extension…")},
        {u"validate_extension", QT_TRANSLATE_NOOP("AiConversation", "Checking the extension…")},
        {u"pack_extension", QT_TRANSLATE_NOOP("AiConversation", "Packing the extension…")},
        {u"read_guide", QT_TRANSLATE_NOOP("AiConversation", "Reading a guide…")},
        {u"read_connection_log", QT_TRANSLATE_NOOP("AiConversation", "Reading the connection log…")},
    }};
    if (tool == u"api_reference") {
        const QString query = arguments.value(u"query"_s).toString().simplified();
        return query.isEmpty()
                   ? QCoreApplication::translate("AiConversation", "Looking up the script API…")
                   : QCoreApplication::translate("AiConversation", "Looking up “%1” in the script API…").arg(query);
    }
    if (tool == u"read_profile") {
        return arguments.value(u"draft"_s).toBool()
                   ? QCoreApplication::translate("AiConversation", "Reading the draft of the profile…")
                   : QCoreApplication::translate("AiConversation", "Reading the profile…");
    }
    const QString query = arguments.value(u"query"_s).toString().simplified();
    if (tool == u"read_help") {
        return query.isEmpty()
                   ? QCoreApplication::translate("AiConversation", "Reading the help…")
                   : QCoreApplication::translate("AiConversation", "Searching the help for “%1”…").arg(query);
    }
    if (tool == u"read_blocks") {
        return query.isEmpty()
                   ? QCoreApplication::translate("AiConversation", "Looking up the blocks…")
                   : QCoreApplication::translate("AiConversation", "Looking up “%1” in the blocks…").arg(query);
    }
    if (tool == u"read_settings") {
        return query.isEmpty()
                   ? QCoreApplication::translate("AiConversation", "Reading the settings…")
                   : QCoreApplication::translate("AiConversation", "Looking up “%1” in the settings…").arg(query);
    }
    if (tool == u"list_commands") {
        return query.isEmpty()
                   ? QCoreApplication::translate("AiConversation", "Listing the commands and shortcuts…")
                   : QCoreApplication::translate("AiConversation", "Looking up “%1” in the commands…").arg(query);
    }
    const auto found = std::ranges::find(kRunning, QStringView(tool), [](const auto& entry) { return entry.first; });
    return found != kRunning.end() ? QCoreApplication::translate("AiConversation", found->second)
                                   : QCoreApplication::translate("AiConversation", "Using %1…").arg(tool);
}

QString aiChangeSummary(const QString& result)
{
    if (!result.startsWith(u"Wrote "_s) && !result.startsWith(u"Applied the draft"_s)) {
        return {};
    }
    static const QRegularExpression kChanged(uR"(\(\+(\d+) -(\d+)\))"_s);
    static const QRegularExpression kNew(uR"(\(new, (\d+) lines\))"_s);
    qint64 added = 0;
    qint64 removed = 0;
    for (QRegularExpressionMatchIterator each = kChanged.globalMatch(result); each.hasNext();) {
        const QRegularExpressionMatch match = each.next();
        added += match.captured(1).toLongLong();
        removed += match.captured(2).toLongLong();
    }
    for (QRegularExpressionMatchIterator each = kNew.globalMatch(result); each.hasNext();) {
        added += each.next().captured(1).toLongLong();
    }
    if (added == 0 && removed == 0) {
        return {};
    }
    return u"+%1 \u2212%2"_s.arg(added).arg(removed);
}

QString aiSystemPrompt(const AiContext& context, const QString& snapshot, const QString& problem)
{
    QString text = QStringLiteral(
        "You are the assistant built into GameScrcpy. GameScrcpy mirrors Android phones to this computer, maps keys "
        "and the mouse to touches on the phone, and runs TypeScript logic and blueprints for each profile; it also has "
        "tasks, components, extensions, a vision workbench and a help library.\n\n");
    text += whereText(context);
    if (!context.profile.isEmpty()) {
        text += u"The profile open here is \"%1\" (id %2)"_s.arg(context.profileName, context.profile);
        if (!context.serial.isEmpty()) {
            text += u" on the phone \"%1\" (serial %2)"_s.arg(context.phone, context.serial);
        }
        text += u"."_s;
        if (!context.app.isEmpty()) {
            text += u" The app in front on the phone is %1."_s.arg(context.app);
        }
        text += QStringLiteral(" Work only on this profile and this phone: the tools refuse others, and they fill in "
                               "the serial and the profile when you leave them out.\n\n");
    } else if (!context.serial.isEmpty()) {
        text += u"The phone shown here is \"%1\" (serial %2)."_s.arg(context.phone, context.serial);
        if (!context.app.isEmpty()) {
            text += u" The app in front on it is %1."_s.arg(context.app);
        }
        text += QStringLiteral(" The tools fill in this serial when you leave it out. No profile is open here; find "
                               "profiles and tasks with list_profiles and list_tasks.\n\n");
    } else {
        text += QStringLiteral(
            "No profile or phone is open here; find them with list_profiles, list_tasks and list_phones.\n\n");
    }
    text += QStringLiteral(
        "How to work:\n"
        "- Positions and sizes are ratios of the phone picture, from (0, 0) at the top left to (1, 1) at the bottom "
        "right.\n"
        "- Answer from GameScrcpy's own sources before your general knowledge: read_help for how things work and where "
        "they are, read_blocks for the blocks of the logic view, read_settings for what a setting does and how it is "
        "set now, list_commands for commands and shortcuts, and list_phones with read_connection_log when a phone does "
        "not connect or shows no picture or sound. When they do not say, tell the user you are not sure and where to "
        "look; never make up menus, settings, blocks or shortcuts.\n"
        "- Look up the script API and the profile format with api_reference before you write code.\n"
        "- Before you make a key mapping, vision logic, a component, a task or an extension, read the matching guide "
        "with read_guide and follow it.\n"
        "- The tools write every change into a draft. Nothing reaches the real profile until the user presses Apply "
        "in the draft card in the conversation, so do not call apply_draft unless the user asks you to.\n"
        "- Check your work before you suggest running anything on the phone: check finds type and format problems, "
        "and run_replay (with trace) shows what the logic does on a recording. run_script asks the user every time.\n"
        "- Call get_frame when you need to see the screen.\n"
        "- open_page takes the user to a page, a settings section or a help article; it never changes a setting.\n"
        "- Use only the tools you are given.\n"
        "- Answer in the language the user writes in, briefly. Put code in fenced code blocks.\n");
    if (context.profile.isEmpty()) {
        return text;
    }
    if (!snapshot.isEmpty()) {
        text += QStringLiteral("\nThe profile as it is saved now, as read_profile returns it (read_profile with draft: "
                               "true reads your draft):\n");
        if (snapshot.size() > kMostSnapshotChars) {
            text += snapshot.left(kMostSnapshotChars);
            text += u"\n[Cut here; call read_profile for the rest.]"_s;
        } else {
            text += snapshot;
        }
        text += u'\n';
    } else if (!problem.isEmpty()) {
        text += u"\nThe profile could not be read: %1\n"_s.arg(problem);
    }
    return text;
}

AiConversation::AiConversation(QObject* parent)
    : QAbstractListModel(parent)
{
    if (AiAssistant* assistant = AiAssistant::instance()) {
        QPointer<AiAssistant> owner(assistant);
        m_makeProvider = [owner]() -> std::unique_ptr<AiProvider> {
            return owner != nullptr ? owner->provider(true) : nullptr;
        };
        m_tools = &assistant->tools();
    }
}

AiConversation::AiConversation(ProviderFactory provider, AiToolRunner* tools, QObject* parent)
    : QAbstractListModel(parent)
    , m_makeProvider(std::move(provider))
    , m_tools(tools)
{
}

AiConversation::~AiConversation()
{
    ++m_generation;
    m_provider.reset();
}

int AiConversation::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant AiConversation::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const Item& item = m_items[static_cast<std::size_t>(index.row())];
    switch (role) {
    case KindRole:
        return kindName(static_cast<int>(item.kind));
    case TextRole:
        return item.text;
    case BlocksRole:
        return item.kind == Item::Kind::Assistant ? aiBlocks(item.text) : QVariantList();
    case TitleRole:
        return item.title;
    case InputRole:
        return item.input;
    case OutputRole:
        return item.output;
    case RunningRole:
        return item.running;
    case FailedRole:
        return item.failed;
    case SecondsRole:
        return item.seconds;
    case ChangesRole:
        return item.changes;
    case RunRole:
        return runOf(index.row());
    default:
        return {};
    }
}

QHash<int, QByteArray> AiConversation::roleNames() const
{
    return {{KindRole, "kind"_ba},       {TextRole, "text"_ba},       {BlocksRole, "blocks"_ba},
            {TitleRole, "title"_ba},     {InputRole, "input"_ba},     {OutputRole, "output"_ba},
            {RunningRole, "running"_ba}, {FailedRole, "failed"_ba},   {SecondsRole, "seconds"_ba},
            {ChangesRole, "changes"_ba}, {RunRole, "run"_ba}};
}

void AiConversation::setSerial(const QString& serial)
{
    if (replaced(m_context.serial, serial)) {
        emit contextChanged();
    }
}

void AiConversation::setPhone(const QString& phone)
{
    if (replaced(m_context.phone, phone)) {
        emit contextChanged();
    }
}

void AiConversation::setApp(const QString& app)
{
    if (replaced(m_context.app, app)) {
        emit contextChanged();
    }
}

void AiConversation::setProfile(const QString& profile)
{
    if (replaced(m_context.profile, profile)) {
        emit contextChanged();
    }
}

void AiConversation::setProfileName(const QString& profileName)
{
    if (replaced(m_context.profileName, profileName)) {
        emit contextChanged();
    }
}

void AiConversation::setContext(const AiContext& context)
{
    if (m_context == context) {
        return;
    }
    m_context = context;
    emit contextChanged();
}

bool AiConversation::send(const QString& text)
{
    const QString words = text.trimmed();
    if (busy() || words.isEmpty()) {
        return false;
    }
    std::unique_ptr<AiProvider> provider = m_makeProvider ? m_makeProvider() : nullptr;
    if (provider == nullptr) {
        append(Item{.kind = Item::Kind::Problem,
                    .text = QCoreApplication::translate("AiConversation",
                                                        "Set up the AI assistant in Settings first.")});
        return false;
    }
    m_provider = std::move(provider);
    m_history.push_back(AiTurn{.role = AiTurn::Role::User, .text = words});
    append(Item{.kind = Item::Kind::User, .text = words});
    m_rounds = 0;
    m_retried = false;
    ++m_generation;
    setPhase(Phase::Context);
    readContext();
    return true;
}

void AiConversation::stop()
{
    if (!busy()) {
        return;
    }
    ++m_generation;
    if (m_provider != nullptr) {
        m_provider->cancel();
    }
    switch (m_phase) {
    case Phase::Asking: {
        endThinking();
        dropCalls(QCoreApplication::translate("AiConversation", "Stopped before it finished"));
        const int row = std::exchange(m_streamRow, -1);
        if (row >= 0 && !at(row).text.isEmpty()) {
            m_history.push_back(AiTurn{.role = AiTurn::Role::Assistant, .text = at(row).text});
        }
        break;
    }
    case Phase::Tools: {
        for (std::size_t index = m_next; index < m_queue.size(); ++index) {
            const AiToolCall& call = m_queue[index];
            const QString why = index == m_next ? u"The user stopped the assistant while this tool was running; its "
                                                  u"result is unknown."_s
                                                : u"Not run: the user stopped the assistant."_s;
            m_round.results.push_back(
                AiToolResult{.id = call.id, .name = call.name, .content = textContent(why), .failed = true});
        }
        if (m_toolRow >= 0) {
            Item& item = at(m_toolRow);
            item.running = false;
            item.failed = true;
            item.output = QCoreApplication::translate("AiConversation", "Stopped before it finished");
            changed(m_toolRow, {RunningRole, FailedRole, OutputRole});
        }
        for (std::size_t index = m_next + 1; index < m_callRows.size(); ++index) {
            Item& item = at(m_callRows[index]);
            item.running = false;
            item.failed = true;
            item.output = QCoreApplication::translate("AiConversation", "Not run: you stopped the assistant");
            changed(m_callRows[index], {RunningRole, FailedRole, OutputRole});
        }
        m_history.push_back(std::exchange(m_round, AiTurn{}));
        break;
    }
    case Phase::Context:
    case Phase::Idle:
        break;
    }
    setWaiting(false);
    append(Item{.kind = Item::Kind::Notice, .text = QCoreApplication::translate("AiConversation", "Stopped.")});
    finishTurn();
}

void AiConversation::clear()
{
    ++m_generation;
    if (m_provider != nullptr) {
        m_provider->cancel();
    }
    beginResetModel();
    m_items.clear();
    endResetModel();
    m_history.clear();
    m_round = AiTurn{};
    m_rounds = 0;
    m_streamRow = -1;
    m_thinkRow = -1;
    m_callRows.clear();
    m_pictures = true;
    m_retried = false;
    setWaiting(false);
    finishTurn();
    emit countChanged();
}

void AiConversation::note(const QString& text)
{
    if (!text.isEmpty()) {
        append(Item{.kind = Item::Kind::Notice, .text = text});
    }
}

void AiConversation::readContext()
{
    m_snapshot.clear();
    m_snapshotProblem.clear();
    m_offered = m_tools != nullptr
                    ? aiOfferedTools(m_tools->tools(), AiScope{.serial = m_context.serial, .profile = m_context.profile})
                    : QJsonArray();
    if (m_tools == nullptr || m_context.profile.isEmpty()) {
        ask();
        return;
    }
    const quint64 generation = m_generation;
    QPointer<AiConversation> self(this);
    m_tools->run(u"read_profile"_s, QJsonObject{{u"profile"_s, m_context.profile}},
                 [self, generation](McpOutcome outcome) {
                     if (self == nullptr || self->m_generation != generation) {
                         return;
                     }
                     const QString text = aiContentText(outcome.content);
                     if (outcome.failed) {
                         self->m_snapshotProblem = text;
                     } else {
                         self->m_snapshot = text;
                     }
                     self->ask();
                 });
}

void AiConversation::ask()
{
    setPhase(Phase::Asking);
    setWaiting(true);
    m_streamRow = -1;
    m_thought = false;
    const AiRequest request{.system = aiSystemPrompt(m_context, m_snapshot, m_snapshotProblem),
                            .turns = m_history,
                            .tools = m_offered,
                            .pictures = m_pictures};
    m_sentPictures = aiPictureCount(request) > 0;
    const quint64 generation = m_generation;
    QPointer<AiConversation> self(this);
    const auto live = [self, generation] { return self != nullptr && self->m_generation == generation; };
    m_provider->send(request, AiProvider::Handlers{.text =
                                                       [self, live](const QString& delta) {
                                                           if (live()) {
                                                               self->streamed(delta);
                                                           }
                                                       },
                                                   .done =
                                                       [self, live](AiReply reply) {
                                                           if (live()) {
                                                               self->answered(std::move(reply));
                                                           }
                                                       },
                                                   .thinking =
                                                       [self, live](const QString& delta) {
                                                           if (live()) {
                                                               self->thought(delta);
                                                           }
                                                       },
                                                   .calling =
                                                       [self, live](const QString& tool) {
                                                           if (live()) {
                                                               self->calling(tool);
                                                           }
                                                       }});
}

void AiConversation::thought(const QString& delta)
{
    if (delta.isEmpty()) {
        return;
    }
    setWaiting(false);
    m_thought = true;
    if (m_thinkRow < 0) {
        m_thinkRow = append(Item{.kind = Item::Kind::Thinking,
                                 .running = true,
                                 .started = QDateTime::currentMSecsSinceEpoch()});
    }
    at(m_thinkRow).text += delta;
    changed(m_thinkRow, {TextRole});
}

void AiConversation::endThinking()
{
    const int row = std::exchange(m_thinkRow, -1);
    if (row < 0) {
        return;
    }
    Item& item = at(row);
    item.running = false;
    const double elapsed = static_cast<double>(QDateTime::currentMSecsSinceEpoch() - item.started) / 1000.0;
    item.seconds = std::max(1, static_cast<int>(std::lround(elapsed)));
    changed(row, {RunningRole, SecondsRole});
}

void AiConversation::calling(const QString& tool)
{
    endThinking();
    setWaiting(false);
    m_callRows.push_back(append(Item{.kind = Item::Kind::Tool,
                                     .title = aiToolRunningTitle(tool, {}),
                                     .running = true}));
}

void AiConversation::dropCalls(const QString& why)
{
    for (const int row : std::exchange(m_callRows, {})) {
        Item& item = at(row);
        if (!item.running) {
            continue;
        }
        item.running = false;
        item.failed = true;
        item.output = why;
        changed(row, {RunningRole, FailedRole, OutputRole});
    }
}

void AiConversation::streamed(const QString& delta)
{
    if (delta.isEmpty()) {
        return;
    }
    endThinking();
    setWaiting(false);
    if (m_streamRow < 0) {
        m_streamRow = append(Item{.kind = Item::Kind::Assistant});
    }
    at(m_streamRow).text += delta;
    changed(m_streamRow, {TextRole, BlocksRole});
}

void AiConversation::answered(AiReply reply)
{
    endThinking();
    setWaiting(false);
    const int row = std::exchange(m_streamRow, -1);
    if (!reply.problem.isEmpty()) {
        dropCalls(QCoreApplication::translate("AiConversation", "Not run: the answer stopped first"));
        if (m_sentPictures && !m_retried && (reply.status == 400 || reply.status == 422)) {
            m_retried = true;
            m_pictures = false;
            append(Item{.kind = Item::Kind::Notice,
                        .text = QCoreApplication::translate("AiConversation", "The model did not take the picture, so "
                                                                              "the assistant asks again without "
                                                                              "pictures.")});
            ask();
            return;
        }
        const QString partial = row >= 0 ? at(row).text : reply.text;
        if (!partial.isEmpty()) {
            m_history.push_back(AiTurn{.role = AiTurn::Role::Assistant, .text = partial});
        }
        append(Item{.kind = Item::Kind::Problem, .text = reply.problem});
        finishTurn();
        return;
    }
    if (!m_thought && row < 0 && m_callRows.empty() && !reply.thinking.isEmpty()) {
        append(Item{.kind = Item::Kind::Thinking, .text = reply.thinking});
    }
    if (row >= 0) {
        if (!reply.text.isEmpty() && at(row).text != reply.text) {
            at(row).text = reply.text;
            changed(row, {TextRole, BlocksRole});
        }
    } else if (!reply.text.isEmpty()) {
        append(Item{.kind = Item::Kind::Assistant, .text = reply.text});
    }
    for (AiToolCall& call : reply.calls) {
        if (call.id.isEmpty()) {
            call.id = u"call_%1"_s.arg(++m_calls);
        }
    }
    m_history.push_back(AiTurn{.role = AiTurn::Role::Assistant,
                               .text = reply.text,
                               .calls = reply.calls,
                               .thinkingBlocks = reply.thinkingBlocks});
    if (reply.stop == u"length" || reply.stop == u"max_tokens") {
        append(Item{.kind = Item::Kind::Notice,
                    .text = QCoreApplication::translate("AiConversation",
                                                        "The answer reached the service's length limit.")});
    }
    const QString unfinished = QCoreApplication::translate("AiConversation", "Not run: the model did not finish the call");
    while (m_callRows.size() > reply.calls.size()) {
        const int extra = m_callRows.back();
        m_callRows.pop_back();
        Item& item = at(extra);
        item.running = false;
        item.failed = true;
        item.output = unfinished;
        changed(extra, {RunningRole, FailedRole, OutputRole});
    }
    if (reply.calls.empty()) {
        if (reply.text.isEmpty()) {
            append(Item{.kind = Item::Kind::Notice,
                        .text = QCoreApplication::translate("AiConversation", "The model sent an empty answer.")});
        }
        finishTurn();
        return;
    }
    if (m_rounds >= kMostRounds) {
        AiTurn skipped{.role = AiTurn::Role::Tools};
        const QString why = u"Not run: this question reached the limit of %1 rounds of tool calls. Tell the user what "
                            u"is left to do."_s.arg(kMostRounds);
        for (const AiToolCall& call : reply.calls) {
            skipped.results.push_back(
                AiToolResult{.id = call.id, .name = call.name, .content = textContent(why), .failed = true});
        }
        dropCalls(QCoreApplication::translate("AiConversation", "Not run: this question reached the limit of tool calls"));
        m_history.push_back(std::move(skipped));
        append(Item{.kind = Item::Kind::Notice,
                    .text = QCoreApplication::translate("AiConversation",
                                                        "Stopped after %n round(s) of tool calls. Send a message to "
                                                        "let the assistant go on.",
                                                        nullptr, kMostRounds)});
        finishTurn();
        return;
    }
    ++m_rounds;
    m_round = AiTurn{.role = AiTurn::Role::Tools};
    m_queue = std::move(reply.calls);
    m_next = 0;
    setPhase(Phase::Tools);
    runNext();
}

void AiConversation::runNext()
{
    if (m_next >= m_queue.size()) {
        m_history.push_back(std::exchange(m_round, AiTurn{}));
        m_queue.clear();
        m_callRows.clear();
        m_next = 0;
        ask();
        return;
    }
    const AiToolCall call = m_queue[m_next];
    int row = -1;
    if (m_next < m_callRows.size()) {
        row = m_callRows[m_next];
        Item& item = at(row);
        item.title = aiToolRunningTitle(call.name, call.arguments);
        item.input = pretty(call.arguments);
        item.running = true;
        changed(row, {TitleRole, InputRole, RunningRole});
    } else {
        row = append(Item{.kind = Item::Kind::Tool,
                          .title = aiToolRunningTitle(call.name, call.arguments),
                          .input = pretty(call.arguments),
                          .running = true});
    }
    m_toolRow = row;
    if (!call.problem.isEmpty()) {
        ranTool(row, call, refused(call.problem));
        return;
    }
    const AiGuarded guarded = aiGuard(call.name, call.arguments, m_offered,
                                      AiScope{.serial = m_context.serial, .profile = m_context.profile});
    if (!guarded.refusal.isEmpty()) {
        ranTool(row, call, refused(guarded.refusal));
        return;
    }
    if (m_tools == nullptr) {
        ranTool(row, call, refused(u"No tools are available."_s));
        return;
    }
    const quint64 generation = m_generation;
    QPointer<AiConversation> self(this);
    m_tools->run(call.name, guarded.arguments, [self, generation, row, call](McpOutcome outcome) {
        if (self != nullptr && self->m_generation == generation) {
            self->ranTool(row, call, std::move(outcome));
        }
    });
}

void AiConversation::ranTool(int row, const AiToolCall& call, McpOutcome outcome)
{
    Item& item = at(row);
    item.running = false;
    item.failed = outcome.failed;
    item.output = summaryOf(outcome.content);
    const QString said = aiContentText(outcome.content);
    item.title = aiToolTitle(call.name, call.arguments);
    if (call.name == u"read_help" && !outcome.failed && said.startsWith(u"# "_s)) {
        item.title = QCoreApplication::translate("AiConversation", "Read the help: %1")
                         .arg(said.section(u'\n', 0, 0).mid(2).trimmed());
    }
    item.changes = outcome.failed ? QString() : aiChangeSummary(said);
    changed(row, {RunningRole, FailedRole, OutputRole, TitleRole, ChangesRole});
    const bool failed = outcome.failed;
    m_round.results.push_back(
        AiToolResult{.id = call.id, .name = call.name, .content = std::move(outcome.content), .failed = failed});
    m_toolRow = -1;
    ++m_next;
    runNext();
}

void AiConversation::finishTurn()
{
    const bool ended = busy();
    endThinking();
    m_queue.clear();
    m_callRows.clear();
    m_next = 0;
    m_toolRow = -1;
    setPhase(Phase::Idle);
    if (ended) {
        emit turnFinished();
    }
}

int AiConversation::append(Item item)
{
    const int row = count();
    beginInsertRows({}, row, row);
    m_items.push_back(std::move(item));
    endInsertRows();
    emit countChanged();
    if (m_items.back().kind == Item::Kind::Tool) {
        touchRun(row);
    }
    return row;
}

void AiConversation::changed(int row, const QList<int>& roles)
{
    const QModelIndex place = index(row);
    emit dataChanged(place, place, roles);
    if (at(row).kind == Item::Kind::Tool) {
        touchRun(row);
    }
}

std::pair<int, int> AiConversation::runAround(int row) const
{
    const auto tool = [this](int at) {
        return at >= 0 && at < count() && m_items[static_cast<std::size_t>(at)].kind == Item::Kind::Tool;
    };
    int first = row;
    int last = row;
    while (tool(first - 1)) {
        --first;
    }
    while (tool(last + 1)) {
        ++last;
    }
    return {first, last};
}

void AiConversation::touchRun(int row)
{
    const auto [first, last] = runAround(row);
    emit dataChanged(index(first), index(last), {RunRole});
}

QVariantMap AiConversation::runOf(int row) const
{
    if (m_items[static_cast<std::size_t>(row)].kind != Item::Kind::Tool) {
        return {};
    }
    const auto [first, last] = runAround(row);
    bool running = false;
    bool failed = false;
    QString current;
    for (int at = first; at <= last; ++at) {
        const Item& item = m_items[static_cast<std::size_t>(at)];
        failed = failed || item.failed;
        if (item.running && !running) {
            running = true;
            current = item.title;
        }
    }
    if (!running) {
        current = m_items[static_cast<std::size_t>(last)].title;
    }
    return {{u"grouped"_s, last - first + 1 >= kGroupedCalls},
            {u"first"_s, row == first},
            {u"start"_s, first},
            {u"count"_s, last - first + 1},
            {u"running"_s, running},
            {u"failed"_s, failed},
            {u"current"_s, current}};
}

void AiConversation::setPhase(Phase phase)
{
    const bool before = busy();
    m_phase = phase;
    if (busy() != before) {
        emit busyChanged();
    }
}

void AiConversation::setWaiting(bool waiting)
{
    if (m_waiting == waiting) {
        return;
    }
    m_waiting = waiting;
    emit busyChanged();
}

namespace {

constexpr std::array<QStringView, 3> kRoles{u"user", u"assistant", u"tools"};

QJsonArray savedContent(const QJsonArray& content)
{
    QJsonArray kept;
    for (const QJsonValue& value : content) {
        const QJsonObject item = value.toObject();
        const QString text = item.value(u"type"_s).toString() == u"text"
                                 ? shortened(item.value(u"text"_s).toString(), kAiMostSavedResultChars)
                                 : u"(A picture was here; pictures are not kept when a conversation is saved.)"_s;
        kept.append(QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, text}});
    }
    return kept;
}

}

QJsonObject AiConversation::saved() const
{
    QJsonArray items;
    for (const Item& item : m_items) {
        QJsonObject entry{{u"kind"_s, kindName(static_cast<int>(item.kind))}, {u"text"_s, item.text}};
        if (!item.title.isEmpty()) {
            entry.insert(u"title"_s, item.title);
        }
        if (!item.input.isEmpty()) {
            entry.insert(u"input"_s, item.input);
        }
        if (!item.output.isEmpty()) {
            entry.insert(u"output"_s, item.output);
        }
        if (item.failed || (item.running && item.kind != Item::Kind::Thinking)) {
            entry.insert(u"failed"_s, true);
        }
        if (item.seconds > 0) {
            entry.insert(u"seconds"_s, item.seconds);
        }
        if (!item.changes.isEmpty()) {
            entry.insert(u"changes"_s, item.changes);
        }
        items.append(entry);
    }
    QJsonArray turns;
    for (const AiTurn& turn : m_history) {
        QJsonObject entry{{u"role"_s, kRoles[static_cast<std::size_t>(turn.role)].toString()}};
        if (!turn.text.isEmpty()) {
            entry.insert(u"text"_s, turn.text);
        }
        QJsonArray calls;
        for (const AiToolCall& call : turn.calls) {
            calls.append(QJsonObject{{u"id"_s, call.id},
                                     {u"name"_s, call.name},
                                     {u"arguments"_s, call.arguments},
                                     {u"problem"_s, call.problem}});
        }
        if (!calls.isEmpty()) {
            entry.insert(u"calls"_s, calls);
        }
        QJsonArray results;
        for (const AiToolResult& result : turn.results) {
            results.append(QJsonObject{{u"id"_s, result.id},
                                       {u"name"_s, result.name},
                                       {u"content"_s, savedContent(result.content)},
                                       {u"failed"_s, result.failed}});
        }
        if (!results.isEmpty()) {
            entry.insert(u"results"_s, results);
        }
        turns.append(entry);
    }
    return QJsonObject{{u"items"_s, items}, {u"history"_s, turns}};
}

void AiConversation::restore(const QJsonObject& saved)
{
    clear();
    std::vector<Item> items;
    for (const QJsonValue& value : saved.value(u"items"_s).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString kind = entry.value(u"kind"_s).toString();
        int found = -1;
        for (int candidate = 0; !kindName(candidate).isEmpty(); ++candidate) {
            if (kindName(candidate) == kind) {
                found = candidate;
            }
        }
        if (found < 0) {
            continue;
        }
        items.push_back(Item{.kind = static_cast<Item::Kind>(found),
                             .text = entry.value(u"text"_s).toString(),
                             .title = entry.value(u"title"_s).toString(),
                             .input = entry.value(u"input"_s).toString(),
                             .output = entry.value(u"output"_s).toString(),
                             .failed = entry.value(u"failed"_s).toBool(),
                             .seconds = entry.value(u"seconds"_s).toInt(),
                             .changes = entry.value(u"changes"_s).toString()});
    }
    std::vector<AiTurn> turns;
    for (const QJsonValue& value : saved.value(u"history"_s).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString role = entry.value(u"role"_s).toString();
        const auto known = std::ranges::find(kRoles, QStringView(role));
        if (known == kRoles.end()) {
            continue;
        }
        AiTurn turn{.role = static_cast<AiTurn::Role>(known - kRoles.begin()), .text = entry.value(u"text"_s).toString()};
        for (const QJsonValue& call : entry.value(u"calls"_s).toArray()) {
            const QJsonObject described = call.toObject();
            turn.calls.push_back(AiToolCall{.id = described.value(u"id"_s).toString(),
                                            .name = described.value(u"name"_s).toString(),
                                            .arguments = described.value(u"arguments"_s).toObject(),
                                            .problem = described.value(u"problem"_s).toString()});
        }
        for (const QJsonValue& result : entry.value(u"results"_s).toArray()) {
            const QJsonObject described = result.toObject();
            turn.results.push_back(AiToolResult{.id = described.value(u"id"_s).toString(),
                                                .name = described.value(u"name"_s).toString(),
                                                .content = described.value(u"content"_s).toArray(),
                                                .failed = described.value(u"failed"_s).toBool()});
        }
        turns.push_back(std::move(turn));
    }
    if (!turns.empty() && turns.back().role == AiTurn::Role::Assistant && !turns.back().calls.empty()) {
        AiTurn unanswered{.role = AiTurn::Role::Tools};
        for (const AiToolCall& call : turns.back().calls) {
            unanswered.results.push_back(AiToolResult{
                .id = call.id,
                .name = call.name,
                .content = textContent(u"Not run: the conversation was saved before this tool ran."_s),
                .failed = true});
        }
        turns.push_back(std::move(unanswered));
    }
    if (items.empty() && turns.empty()) {
        return;
    }
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    m_history = std::move(turns);
    emit countChanged();
}

}
