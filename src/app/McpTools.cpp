#include "McpTools.h"

#include "BlueprintModel.h"
#include "ComponentLibrary.h"
#include "McpBlueprint.h"
#include "McpDrafts.h"
#include "McpGuides.h"
#include "McpStarters.h"
#include "McpTrace.h"
#include "ProfileLibrary.h"
#include "QtConversions.h"
#include "ScriptWorkbenchFiles.h"
#include "TaskLibrary.h"
#include "VisionSearch.h"

#include <gs/input/Profile.h>
#include <gs/platform/AtomicFile.h>
#include <gs/record/RecordedVision.h>
#include <gs/record/Recording.h>
#include <gs/script/ApiFiles.h>
#include <gs/script/Replay.h>
#include <gs/script/Report.h>
#include <gs/script/Resources.h>
#include <gs/script/Session.h>
#include <gs/vision/Matcher.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

namespace fs = std::filesystem;

constexpr qsizetype kMostReferenceChars = 60'000;
constexpr std::size_t kMostReferenceSections = 6;
constexpr qsizetype kMostLogBytes = qsizetype{4} << 20;
constexpr int kDefaultLogLines = 100;
constexpr int kMostLogLines = 1000;
constexpr double kDefaultUntilMs = 60'000.0;
constexpr double kLongestUntilMs = 600'000.0;
constexpr qsizetype kMostReadBytes = qsizetype{8} << 20;
constexpr std::uintmax_t kMostCopiedBytes = std::uintmax_t{64} << 20;
constexpr int kMostCopiedFiles = 500;
constexpr qsizetype kMostConsoleChars = 2000;
constexpr std::size_t kMostProfileBytes = std::size_t{1} << 20;
constexpr double kFindThreshold = 0.8;
constexpr int kFindMost = 16;
constexpr qsizetype kMostListedRecordings = 50;

QJsonObject textItem(const QString& text)
{
    return {{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}};
}

McpOutcome success(const QString& text)
{
    return McpOutcome{.content = QJsonArray{textItem(text)}};
}

McpOutcome failure(const QString& code, const QString& sentence)
{
    return McpOutcome{.content = QJsonArray{textItem(code + u' ' + sentence)}, .failed = true, .code = code};
}

McpOutcome failureOf(const std::string& problem)
{
    const QString text = toQString(problem);
    const qsizetype space = text.indexOf(u' ');
    return space > 0 ? failure(text.left(space), text.mid(space + 1)) : failure(QStringLiteral("GS-MCP-012"), text);
}

QString pathText(const fs::path& path)
{
    return QDir::fromNativeSeparators(QString::fromStdWString(path.wstring()));
}

fs::path toPath(const QString& text)
{
    return {text.toStdWString()};
}

fs::path utf8Path(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

QString jsonText(const QJsonDocument& document)
{
    return QString::fromUtf8(document.toJson(QJsonDocument::Indented));
}

QString jsonText(const QJsonObject& object)
{
    return jsonText(QJsonDocument(object));
}

class Arguments
{
public:
    Arguments(const QJsonObject& given, std::initializer_list<QStringView> known)
        : m_given(given)
    {
        for (auto at = given.begin(); at != given.end(); ++at) {
            if (std::ranges::find(known, QStringView(at.key())) == known.end()) {
                complain(QStringLiteral("there is no argument named %1").arg(at.key()));
            }
        }
    }

    QString text(QStringView name, bool required = false)
    {
        const QJsonValue value = m_given.value(name);
        if (value.isUndefined() || value.isNull()) {
            if (required) {
                complain(QStringLiteral("the argument %1 is missing").arg(name));
            }
            return {};
        }
        if (!value.isString()) {
            complain(QStringLiteral("%1 must be text").arg(name));
            return {};
        }
        return value.toString();
    }

    std::optional<double> number(QStringView name, double least, double most)
    {
        const QJsonValue value = m_given.value(name);
        if (value.isUndefined() || value.isNull()) {
            return std::nullopt;
        }
        if (!value.isDouble() || !(value.toDouble() >= least && value.toDouble() <= most)) {
            complain(QStringLiteral("%1 must be a number from %2 to %3").arg(name).arg(least).arg(most));
            return std::nullopt;
        }
        return value.toDouble();
    }

    std::optional<int> whole(QStringView name, int least, int most)
    {
        const std::optional<double> value = number(name, least, most);
        if (!value) {
            return std::nullopt;
        }
        if (std::floor(*value) != *value) {
            complain(QStringLiteral("%1 must be a whole number from %2 to %3").arg(name).arg(least).arg(most));
            return std::nullopt;
        }
        return static_cast<int>(*value);
    }

    std::optional<bool> flag(QStringView name)
    {
        const QJsonValue value = m_given.value(name);
        if (value.isUndefined() || value.isNull()) {
            return std::nullopt;
        }
        if (!value.isBool()) {
            complain(QStringLiteral("%1 must be true or false").arg(name));
            return std::nullopt;
        }
        return value.toBool();
    }

    QJsonObject object(QStringView name, bool required = false)
    {
        const QJsonValue value = m_given.value(name);
        if (value.isUndefined() || value.isNull()) {
            if (required) {
                complain(QStringLiteral("the argument %1 is missing").arg(name));
            }
            return {};
        }
        if (!value.isObject()) {
            complain(QStringLiteral("%1 must be an object").arg(name));
            return {};
        }
        return value.toObject();
    }

    QJsonArray array(QStringView name, bool required = false)
    {
        const QJsonValue value = m_given.value(name);
        if (value.isUndefined() || value.isNull()) {
            if (required) {
                complain(QStringLiteral("the argument %1 is missing").arg(name));
            }
            return {};
        }
        if (!value.isArray()) {
            complain(QStringLiteral("%1 must be a list").arg(name));
            return {};
        }
        return value.toArray();
    }

    QString target(QStringView name)
    {
        const QString given = text(name, true);
        const QString target = mcpTargetOf(given);
        if (ok() && target.isEmpty()) {
            complain(QStringLiteral("%1 must be profile:<id>, component:<ref> or task:<id>").arg(name));
        }
        return target;
    }

    [[nodiscard]] bool has(QStringView name) const { return m_given.contains(name); }
    [[nodiscard]] bool ok() const { return m_problem.isEmpty(); }
    [[nodiscard]] McpOutcome problem() const { return failure(QStringLiteral("GS-MCP-004"), m_problem); }

    void complain(const QString& text)
    {
        if (m_problem.isEmpty()) {
            m_problem = text;
        }
    }

private:
    const QJsonObject& m_given;
    QString m_problem;
};

QJsonObject property(const char* type, const QString& description)
{
    return {{QStringLiteral("type"), QString::fromLatin1(type)}, {QStringLiteral("description"), description}};
}

QJsonObject toolOf(const QString& name, const QString& description, const QJsonObject& properties,
                   const QStringList& required = {})
{
    QJsonObject schema{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("additionalProperties"), false}};
    if (!required.isEmpty()) {
        schema.insert(QStringLiteral("required"), QJsonArray::fromStringList(required));
    }
    return {{QStringLiteral("name"), name}, {QStringLiteral("description"), description}, {QStringLiteral("inputSchema"), schema}};
}

QJsonObject filesProperty(const QString& description)
{
    QJsonObject files = property("object", description);
    files.insert(QStringLiteral("additionalProperties"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}});
    return files;
}

QJsonObject choices(const char* type, const QString& description, const QStringList& allowed)
{
    QJsonObject made = property(type, description);
    made.insert(QStringLiteral("enum"), QJsonArray::fromStringList(allowed));
    return made;
}

QJsonArray toolList()
{
    const QJsonObject serial = property("string", QStringLiteral("The phone's serial from list_phones; may be left out "
                                                                 "when one phone is mirroring."));
    const QJsonObject level = choices("string", QStringLiteral("The least important level to show (default info)."),
                                      {QStringLiteral("debug"), QStringLiteral("info"), QStringLiteral("warning"),
                                       QStringLiteral("error")});
    const QJsonObject source = choices("string",
                                       QStringLiteral("Only the program's own lines, only the lines of logic, "
                                                      "tasks and scripts, or all (default)."),
                                       {QStringLiteral("all"), QStringLiteral("program"), QStringLiteral("logic")});
    const QJsonObject match = choices("string", QStringLiteral("Compare in grey (default) or in color."),
                                      {QStringLiteral("grey"), QStringLiteral("color")});
    QJsonObject region = property("object", QStringLiteral("Where to look, as ratios of the picture (default the "
                                                           "whole picture)."));
    region.insert(QStringLiteral("properties"),
                  QJsonObject{{QStringLiteral("x"), property("number", QStringLiteral("Left edge, 0 to 1."))},
                              {QStringLiteral("y"), property("number", QStringLiteral("Top edge, 0 to 1."))},
                              {QStringLiteral("width"), property("number", QStringLiteral("Width, more than 0."))},
                              {QStringLiteral("height"), property("number", QStringLiteral("Height, more than 0."))}});
    region.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("x"), QStringLiteral("y"),
                                                         QStringLiteral("width"), QStringLiteral("height")});
    const QJsonObject target = property("string", QStringLiteral("What it is about: profile:<id>, component:<ref> or "
                                                                 "task:<id>; a bare id means a profile."));
    const QJsonObject draft = property("boolean", QStringLiteral("Use this client's draft instead of the version in "
                                                                 "use (default false)."));
    const QJsonObject profileId = property("string", QStringLiteral("A profile id from list_profiles."));
    const QJsonObject componentRef =
        property("string", QStringLiteral("A component ref from list_components, such as mine/burst."));
    const QJsonObject taskId = property("string", QStringLiteral("A task id from list_tasks."));
    const QJsonObject file =
        property("string", QStringLiteral("The code file inside the folder (default logic.ts, task.ts or component.ts)."));
    QJsonObject operations = property("array", QStringLiteral("The changes, in order; see the description."));
    operations.insert(QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}});
    QJsonObject controls = property("array", QStringLiteral("The controls to suggest, written as in profile.json."));
    controls.insert(QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}});
    QJsonObject material = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("name"),
              property("string", QStringLiteral("What it is, named as the logic will use it, such as \"start button\" "
                                                "or \"hp red\"; 1 to 64 characters."))},
             {QStringLiteral("kind"),
              choices("string", QStringLiteral("A picture to find, a color, an area to search in, or a place to tap."),
                      {QStringLiteral("template"), QStringLiteral("color"), QStringLiteral("region"),
                       QStringLiteral("point")})},
             {QStringLiteral("note"),
              property("string", QStringLiteral("One sentence for the user about it, such as where it shows up."))}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("name"), QStringLiteral("kind")}},
        {QStringLiteral("additionalProperties"), false}};
    QJsonObject materials = property("array", QStringLiteral("What to ask the user for, 1 to %1 items; name and kind "
                                                             "are required.")
                                                        .arg(McpTools::kMostMaterials));
    materials.insert(QStringLiteral("items"), material);
    return QJsonArray{
        toolOf(QStringLiteral("api_reference"),
               QStringLiteral("Looks up GameScrcpy's script API and the profile format and returns Markdown. Give a "
                              "name such as vision.find, store or Component for its whole entry, words such as "
                              "\"swipe duration\" to search, \"profile format\" for profile.json and the profile "
                              "folder, or nothing for the index."),
               {{QStringLiteral("query"), property("string", QStringLiteral("A name or words to look for; leave out "
                                                                            "for the index."))}}),
        toolOf(QStringLiteral("run_replay"),
               QStringLiteral("Runs a profile's logic, a task or the given code against a recorded session with a "
                              "virtual clock, as gs run --replay does, and returns the lines it would send (taps, "
                              "keys, control presses, picture searches, logs and problems). Never touches a phone. "
                              "With draft: true it runs this client's drafts of the profile and the task; with "
                              "component it runs this client's draft of that component wherever the profile uses it. "
                              "With trace: true it also returns every step (the code positions passed with their "
                              "local values, the picture searches with their results and scores, the taps and keys "
                              "sent) and up to 12 small pictures of the recording."),
               {{QStringLiteral("session"),
                 property("string", QStringLiteral("The full path of a session.json file, or of a recording folder "
                                                   "that holds one."))},
                {QStringLiteral("profile"),
                 property("string", QStringLiteral("A profile id from list_profiles, or the full path of a profile or "
                                                   "script folder."))},
                {QStringLiteral("code"),
                 property("string", QStringLiteral("TypeScript to run instead of the folder's logic. It runs from a "
                                                   "temporary folder with a copy of the profile's profile.json and "
                                                   "res folder."))},
                {QStringLiteral("task"), property("string", QStringLiteral("A task id from list_tasks to run instead "
                                                                           "of the profile's logic; the profile gives "
                                                                           "its controls."))},
                {QStringLiteral("component"),
                 property("string", QStringLiteral("A component ref whose draft runs in place of the component in "
                                                   "use, in the given profile."))},
                {QStringLiteral("draft"), property("boolean", QStringLiteral("Run this client's drafts of the profile "
                                                                             "and the task (default false)."))},
                {QStringLiteral("trace"), property("boolean", QStringLiteral("Also return every step and pictures of "
                                                                             "the recording (default false)."))},
                {QStringLiteral("untilMs"),
                 property("number", QStringLiteral("How far to replay, in milliseconds of the session (default "
                                                   "60000, at most 600000)."))},
                {QStringLiteral("seed"), property("integer", QStringLiteral("The seed of Math.random (default 1)."))}},
               {QStringLiteral("session")}),
        toolOf(QStringLiteral("list_phones"),
               QStringLiteral("Lists the phones that are mirroring now: serial, name, screen size in pixels, the app "
                              "in front and the profile in use."),
               {}),
        toolOf(QStringLiteral("get_frame"),
               QStringLiteral("Returns the current picture of a phone as PNG with its size: the phone's own lossless "
                              "screenshot when it can take one, otherwise the latest frame of the video."),
               {{QStringLiteral("serial"), serial},
                {QStringLiteral("maxSide"),
                 property("integer", QStringLiteral("Scale the picture down so its longer side is at most this many "
                                                    "pixels (16 to 8192; default the full size)."))}}),
        toolOf(QStringLiteral("find_image"),
               QStringLiteral("Looks for a picture in a phone's current picture as vision.find and vision.findAll do "
                              "in scripts, and returns the same JSON: {x, y, width, height, score} with the center "
                              "and the size as ratios of the picture, null when it is not there, or a list with all. "
                              "The picture is a template name or a picture file of the profile in use on the phone, "
                              "or PNG data you give."),
               {{QStringLiteral("serial"), serial},
                {QStringLiteral("image"),
                 property("string", QStringLiteral("A template name from the profile's res/resources.json, or a "
                                                   "picture inside the profile folder such as res/chest.png."))},
                {QStringLiteral("png"), property("string", QStringLiteral("A PNG picture, base64-encoded, instead of "
                                                                          "image."))},
                {QStringLiteral("region"), region},
                {QStringLiteral("threshold"),
                 property("number", QStringLiteral("The least score that counts as found, 0 to 1 (default 0.8 or the "
                                                   "template's own)."))},
                {QStringLiteral("all"), property("boolean", QStringLiteral("Return every place it is, as findAll "
                                                                           "does (default false)."))},
                {QStringLiteral("max"), property("integer", QStringLiteral("With all, at most this many places, 1 to "
                                                                           "64 (default 16)."))},
                {QStringLiteral("match"), match}}),
        toolOf(QStringLiteral("read_logs"),
               QStringLiteral("Returns the latest lines of GameScrcpy's log, the lines of logic included, oldest "
                              "first."),
               {{QStringLiteral("lines"),
                 property("integer", QStringLiteral("How many lines at most, 1 to 1000 (default 100)."))},
                {QStringLiteral("level"), level},
                {QStringLiteral("contains"), property("string", QStringLiteral("Only lines that contain this text, "
                                                                               "in any case."))},
                {QStringLiteral("source"), source}}),
        toolOf(QStringLiteral("list_profiles"),
               QStringLiteral("Lists the key mapping profiles: id, name, app, whether it is a folder and has logic, "
                              "whether it is read-only (from an extension), whether this client has a draft of it and "
                              "its problem when it cannot be loaded."),
               {}),
        toolOf(QStringLiteral("read_profile"),
               QStringLiteral("Returns a profile as JSON: profile.json, its logic and its other text files by path, "
                              "the names of its other files and the names of its resources (templates, regions, "
                              "points, paths and colors)."),
               {{QStringLiteral("profile"), profileId}, {QStringLiteral("draft"), draft}},
               {QStringLiteral("profile")}),
        toolOf(QStringLiteral("write_profile"),
               QStringLiteral("Writes text files of a profile into this client's draft of it, without asking the "
                              "user; use apply_draft to make the draft count. The first write copies the profile "
                              "into the draft. profile.json and res/resources.json are checked first, and each file "
                              "is replaced as a whole. Try the draft with check and run_replay, both with draft: "
                              "true. A profile that is a single file becomes a folder when a draft that adds other "
                              "files is applied. Profiles from extensions are read-only."),
               {{QStringLiteral("profile"), profileId},
                {QStringLiteral("files"),
                 filesProperty(QStringLiteral("The files to write: the path inside the profile folder, such as "
                                              "logic.ts or profile.json, and its whole new text."))}},
               {QStringLiteral("profile"), QStringLiteral("files")}),
        toolOf(QStringLiteral("run_script"),
               QStringLiteral("Runs a script on a phone once the user confirms in GameScrcpy; the user may let this "
                              "client run scripts on the phone without asking until GameScrcpy closes. Give code, a "
                              "TypeScript module whose top-level code runs and then its default export, an async "
                              "function or a Task class, or give function, the name of a function that the logic of "
                              "the profile in use on the phone exports. Returns the console output when the script "
                              "ends or at the time limit; its fingers are lifted when it ends."),
               {{QStringLiteral("serial"), serial},
                {QStringLiteral("code"), property("string", QStringLiteral("The script to run."))},
                {QStringLiteral("function"),
                 property("string", QStringLiteral("An exported function of the logic of the profile in use, such as "
                                                   "start."))},
                {QStringLiteral("timeoutMs"),
                 property("number", QStringLiteral("Stop the script after this many milliseconds, 100 to 60000 "
                                                   "(default 60000)."))}}),
        toolOf(QStringLiteral("list_components"),
               QStringLiteral("Lists the custom components: ref (mine/<name> for the user's own, examples/<name> for "
                              "the read-only examples), name, description, the built-in component it extends, its "
                              "keys and settings, and whether this client has a draft of it."),
               {}),
        toolOf(QStringLiteral("read_component"),
               QStringLiteral("Returns a custom component as JSON: component.ts and its other text files by path, "
                              "and the names of its other files."),
               {{QStringLiteral("component"), componentRef}, {QStringLiteral("draft"), draft}},
               {QStringLiteral("component")}),
        toolOf(QStringLiteral("write_component"),
               QStringLiteral("Writes text files of a custom component into this client's draft of it, without asking "
                              "the user, as write_profile does for profiles. Give a ref from list_components, or "
                              "mine/<name> (lower-case letters, digits and hyphens) for a new component, whose draft "
                              "must hold component.ts. Example components are read-only. Profiles that use the "
                              "component use the change once apply_draft has made it count."),
               {{QStringLiteral("component"), componentRef},
                {QStringLiteral("files"),
                 filesProperty(QStringLiteral("The files to write: the path inside the component folder, such as "
                                              "component.ts, and its whole new text."))}},
               {QStringLiteral("component"), QStringLiteral("files")}),
        toolOf(QStringLiteral("list_tasks"),
               QStringLiteral("Lists the tasks: id, name, the phones it runs on, its schedule and its parameters, and "
                              "whether this client has a draft of it."),
               {}),
        toolOf(QStringLiteral("read_task"),
               QStringLiteral("Returns a task as JSON: task.ts and its other text files by path, and the names of its "
                              "other files."),
               {{QStringLiteral("task"), taskId}, {QStringLiteral("draft"), draft}},
               {QStringLiteral("task")}),
        toolOf(QStringLiteral("write_task"),
               QStringLiteral("Writes text files of a task into this client's draft of it, without asking the user, "
                              "as write_profile does for profiles. Give an id from list_tasks, or a new id (lower-case "
                              "letters, digits and hyphens), whose draft must hold task.ts. task.json holds the "
                              "task's settings in GameScrcpy and cannot be written."),
               {{QStringLiteral("task"), taskId},
                {QStringLiteral("files"),
                 filesProperty(QStringLiteral("The files to write: the path inside the task folder, such as "
                                              "task.ts, and its whole new text."))}},
               {QStringLiteral("task"), QStringLiteral("files")}),
        toolOf(QStringLiteral("read_draft"),
               QStringLiteral("Returns this client's draft of a profile, component or task as JSON: its text files by "
                              "path, the names of its other files and the files that differ from the version in use."),
               {{QStringLiteral("target"), target}},
               {QStringLiteral("target")}),
        toolOf(QStringLiteral("draft_diff"),
               QStringLiteral("Lists this client's drafts and their changes: every file added, changed or removed "
                              "against the version in use, with the lines added and removed, and conflict: true where "
                              "the version in use changed after the draft was made. Give target for one draft with "
                              "its unified diff, and path as well for one file."),
               {{QStringLiteral("target"), target},
                {QStringLiteral("path"), property("string", QStringLiteral("One file of the draft, such as logic.ts."))}}),
        toolOf(QStringLiteral("apply_draft"),
               QStringLiteral("Makes this client's draft of a profile, component or task the version in use once the "
                              "user confirms in GameScrcpy; the confirmation lists every file with its added and "
                              "removed lines, files that changed in GameScrcpy after the draft was made first. It "
                              "counts as refused when the user does not answer within two minutes; a draft that is "
                              "not applied is kept."),
               {{QStringLiteral("target"), target}},
               {QStringLiteral("target")}),
        toolOf(QStringLiteral("discard_draft"),
               QStringLiteral("Throws away this client's draft of a profile, component or task. Drafts are also "
                              "thrown away when the user removes this client, and after 7 days without a change."),
               {{QStringLiteral("target"), target}},
               {QStringLiteral("target")}),
        toolOf(QStringLiteral("open_draft_preview"),
               QStringLiteral("Opens this client's draft of a profile in GameScrcpy's key mapping editor for the "
                              "user, with the keys and the logic blocks that the draft adds, changes and removes "
                              "marked in green, orange and red, and buttons to apply, discard or close it. Nothing "
                              "changes until the user applies it there. Says whether the editor was opened; the "
                              "user needs a phone that is mirroring."),
               {{QStringLiteral("target"), target}},
               {QStringLiteral("target")}),
        toolOf(QStringLiteral("read_blueprint"),
               QStringLiteral("Returns the blueprint drawn from a code file of a profile, component or task: its "
                              "pages, and for each page the blocks (key, kind, title, input pins with their values, "
                              "output pins, position, the code of code blocks), the wires between pins and the comment "
                              "boxes. Give catalog (words, or empty text for everything) to also list the blocks "
                              "edit_blueprint can add, with their ids."),
               {{QStringLiteral("target"), target},
                {QStringLiteral("file"), file},
                {QStringLiteral("page"), property("string", QStringLiteral("One page only, such as \"\" for the main "
                                                                           "page or fn:<name> for a function."))},
                {QStringLiteral("draft"), draft},
                {QStringLiteral("catalog"), property("string", QStringLiteral("Words to look for in the catalog of "
                                                                              "blocks."))}},
               {QStringLiteral("target")}),
        toolOf(QStringLiteral("edit_blueprint"),
               QStringLiteral("Changes a blueprint in this client's draft the way the blueprint editor does, and "
                              "writes the code and its layout file there. The operations run in order and stop at the "
                              "first one refused, writing nothing: {op: \"add\", id, x, y, page?, from?, fromPin?} "
                              "adds a block from the catalog (after from when given), {op: \"remove\", nodes}, "
                              "{op: \"connect\", from, fromPin, to, toPin}, {op: \"disconnect\", node, pin} or "
                              "{op: \"disconnect\", wire}, {op: \"setValue\", node, pin, value}, {op: \"setCode\", "
                              "node, code} for a code block, {op: \"setDetail\", node, name, value} for an event's "
                              "details such as its key, {op: \"move\", nodes, dx, dy}, {op: \"comment\", nodes, "
                              "title?, color?} draws a comment box around blocks, {op: \"editComment\", comment, "
                              "title?, color?, x?, y?, width?, height?} and {op: \"arrange\", page?} lays a page out "
                              "again. A key such as \"$0\" stands for the block that operation 0 made. A block that is "
                              "not connected stays out of the code until it is. Returns the keys made and the page "
                              "after the changes."),
               {{QStringLiteral("target"), target}, {QStringLiteral("file"), file}, {QStringLiteral("operations"), operations}},
               {QStringLiteral("target"), QStringLiteral("operations")}),
        toolOf(QStringLiteral("check"),
               QStringLiteral("Checks the code of a profile, component or task as gs check does: the type checker, "
                              "the check of the TypeScript GameScrcpy can erase, the profile and its resources. "
                              "Returns every problem with its file, line, column, code and message."),
               {{QStringLiteral("target"), target}, {QStringLiteral("draft"), draft}},
               {QStringLiteral("target")}),
        toolOf(QStringLiteral("start_recording"),
               QStringLiteral("Starts recording a phone the way the record button of its mirror window does, once the "
                              "user confirms in GameScrcpy; it counts as refused when the user does not answer within "
                              "two minutes. Stop it with stop_recording."),
               {{QStringLiteral("serial"), serial}}),
        toolOf(QStringLiteral("stop_recording"),
               QStringLiteral("Stops recording a phone and returns the recording's id and folder; give the folder to "
                              "run_replay as session."),
               {{QStringLiteral("serial"), serial}}),
        toolOf(QStringLiteral("list_recordings"),
               QStringLiteral("Lists the recordings in GameScrcpy's recordings folder, newest first: id, folder, when "
                              "it was made and whether it has video."),
               {}),
        toolOf(QStringLiteral("propose_keymap"),
               QStringLiteral("Shows controls to the user as dashed hints in the keymap editor of a phone, each with "
                              "Accept and Reject; the editor opens on the phone's profile, or on the given profile, "
                              "when it is not open. The user can drag a hint before accepting it. Accepted controls "
                              "join the profile in the editor, where the user can undo them and saves as usual. "
                              "Returns at once with an id; keymap_draft_status tells what the user chose. Write each "
                              "control as in profile.json (type, inputs and its settings) and place it with anchor and "
                              "offset, or with x and y as ratios of the picture."),
               {{QStringLiteral("serial"), serial}, {QStringLiteral("profile"), profileId},
                {QStringLiteral("controls"), controls}},
               {QStringLiteral("controls")}),
        toolOf(QStringLiteral("keymap_draft_status"),
               QStringLiteral("Tells what the user chose for the controls of a propose_keymap call: accepted (with the "
                              "control's id), rejected or undecided, and whether the editor is still open."),
               {{QStringLiteral("id"), property("string", QStringLiteral("The id propose_keymap returned."))}},
               {QStringLiteral("id")}),
        toolOf(QStringLiteral("request_materials"),
               QStringLiteral("Asks the user for the pictures, colors, regions and points the logic needs, instead of "
                              "guessing them: GameScrcpy brings its window forward and opens the vision workbench on "
                              "the phone's picture with this list beside it. The user frames a template, picks a color, "
                              "boxes a region or taps a point for each item, and may skip an item or answer it with a "
                              "resource the profile already has. Give to AI saves them into this client's draft of the "
                              "profile (res/resources.json and its pictures), never into the profile in use, and the "
                              "call returns each item's state and value. Name each item as the logic will use it, such "
                              "as \"start button\". When the request is vague, ask which game, which screen and what "
                              "should happen before calling. The call waits for the user, up to 10 minutes."),
               {{QStringLiteral("serial"), serial}, {QStringLiteral("profile"), profileId}, {QStringLiteral("items"), materials}},
               {QStringLiteral("items")}),
        toolOf(QStringLiteral("create_extension"),
               QStringLiteral("Makes a working starter extension in this client's area of GameScrcpy's data folder: a "
                              "colour theme, a video filter (an HLSL shader with a setting) or a script component. "
                              "Returns its folder; change it with write_extension, check it with validate_extension "
                              "and pack it with pack_extension. The user installs packages on GameScrcpy's Extensions "
                              "page."),
               {{QStringLiteral("kind"), choices("string", QStringLiteral("What to make."), mcpStarterKinds())},
                {QStringLiteral("name"), property("string", QStringLiteral("The extension's name, up to 100 "
                                                                           "characters."))}},
               {QStringLiteral("kind"), QStringLiteral("name")}),
        toolOf(QStringLiteral("write_extension"),
               QStringLiteral("Writes text files (code, JSON, Markdown, text and HLSL) of an extension that "
                              "create_extension made. Each file is replaced as a whole."),
               {{QStringLiteral("extension"), property("string", QStringLiteral("The folder create_extension "
                                                                                "returned."))},
                {QStringLiteral("files"),
                 filesProperty(QStringLiteral("The files to write: the path inside the extension folder, such as "
                                              "manifest.json, and its whole new text."))}},
               {QStringLiteral("extension"), QStringLiteral("files")}),
        toolOf(QStringLiteral("validate_extension"),
               QStringLiteral("Checks an extension folder or package as gs validate does: the manifest, the files it "
                              "names, the shaders and the profiles. Returns every problem with its code, place and a "
                              "hint."),
               {{QStringLiteral("folder"), property("string", QStringLiteral("The full path of an extension folder, a "
                                                                             ".gsext package or a .gsbundle."))}},
               {QStringLiteral("folder")}),
        toolOf(QStringLiteral("pack_extension"),
               QStringLiteral("Packs an extension folder into a .gsext package as gs pack does, after the same checks, "
                              "and returns the package's path in this client's area of the data folder. Packing never "
                              "installs it."),
               {{QStringLiteral("folder"), property("string", QStringLiteral("The full path of an extension "
                                                                             "folder."))}},
               {QStringLiteral("folder")}),
    };
}

QString profileFormat()
{
    QString text = QStringLiteral(
        "# Profile format\n\n"
        "A profile is one JSON file or a folder. A folder holds:\n\n"
        "- `profile.json`: the controls and the shared states, below.\n"
        "- `logic.ts` (or `logic.js`): the profile's logic. It runs on every phone that uses the profile and may "
        "import other `.ts` files of the folder.\n"
        "- `res/resources.json` and its pictures: the templates, regions, points, paths and colors made in the vision "
        "workbench; logic uses them by name in `vision.find`, `vision.findAll`, `vision.findColor` and `res`.\n"
        "- `sessions/*.json` and `tests/*.test.json`: recorded sessions and the output they should give, for "
        "`run_replay` and `gs test`.\n"
        "- `tsconfig.json` and `types/`: written by GameScrcpy for code editors.\n\n"
        "## profile.json\n\n"
        "```json\n"
        "{\n"
        "  \"format\": \"gamescrcpy.profile\",\n"
        "  \"version\": 3,\n"
        "  \"meta\": { \"name\": \"Shooter\", \"app\": \"com.example.game\", \"orientation\": \"landscape\" },\n"
        "  \"screen\": { \"scaleMode\": \"shortSide\", \"safeArea\": \"ignore\" },\n"
        "  \"gamepad\": \"passthrough\",\n"
        "  \"controls\": [\n"
        "    { \"id\": \"fire\", \"name\": \"Fire\", \"type\": \"hold\", \"anchor\": \"bottomRight\", "
        "\"offset\": [-0.2, -0.3], \"inputs\": { \"press\": \"Mouse.Left\" } }\n"
        "  ],\n"
        "  \"vars\": [ { \"id\": \"aiming\", \"name\": \"Aiming\", \"initial\": false } ]\n"
        "}\n"
        "```\n\n"
        "- `meta`: `name` (up to 64 characters), `app` (the Android package the profile is for) and `orientation` "
        "(`landscape` or `portrait`).\n"
        "- `screen`: `scaleMode` is `shortSide` (offsets and sizes are fractions of the picture's short side) or "
        "`stretch` (fractions of its width and height); `safeArea` is `ignore` or `respect` (keep clear of display "
        "cutouts).\n"
        "- `gamepad`: `passthrough` gives the gamepad to the phone as a real gamepad: its buttons, triggers and "
        "sticks go straight to the game instead of driving controls, while keyboard and mouse controls work as "
        "usual. Leave it out (or write `map`) for the normal mapping, where gamepad inputs such as `Gamepad.A` "
        "drive controls.\n"
        "- `controls`: `id` (stays the same when the control is renamed; logic reaches it as `controls.<id>`), "
        "`name`, `type`, `anchor` (`topLeft`, `top`, `topRight`, `left`, `center`, `right`, `bottomLeft`, `bottom` "
        "or `bottomRight`), `offset` ([x, y] from the anchor), `enabled`, `when` (a condition on the states for the "
        "control to work), `inputs` (input slot to key, such as `KeyF`, `Space` or `Mouse.Left`) and the settings of "
        "its type.\n"
        "- `vars`: shared states that controls and logic read and change: `id`, `name` and `initial` (true or false, "
        "a number or text).\n\n"
        "## Control types\n\n"
        "| Type | Input slots |\n"
        "|---|---|\n");
    for (const std::string_view type : input::kControlTypeNames) {
        QStringList names;
        for (const std::string_view name : input::inputSlots(type)) {
            names.append(u'`' + toQString(name) + u'`');
        }
        text += QStringLiteral("| `%1` | %2 |\n")
                    .arg(toQString(type), names.isEmpty() ? QStringLiteral("set by the component") : names.join(u", "));
    }
    text += QStringLiteral(
        "\nA `component` control runs a custom component: `component` names it (such as `mine/burst`), `inputs` and "
        "`props` hold its keys and settings.\n\n"
        "write_profile checks profile.json with the reader GameScrcpy uses; a problem is reported as "
        "`profile.json: error GS-CHECK-005 <JSON path>: <detail>`, the same as `gs check`.\n");
    return text;
}

struct Section {
    QString page;
    QString name;
    QString text;
};

QString sectionName(QStringView heading)
{
    QString name = heading.toString();
    name.remove(u'`');
    if (const qsizetype paren = name.indexOf(u'('); paren > 0) {
        name.truncate(paren);
    }
    return name.trimmed();
}

void splitPage(const QString& page, const QString& text, std::vector<Section>& into)
{
    Section current{.page = page};
    bool fenced = false;
    for (const QString& line : text.split(u'\n')) {
        if (line.startsWith(QStringLiteral("```"))) {
            fenced = !fenced;
        }
        if (!fenced && (line.startsWith(QStringLiteral("## ")) || line.startsWith(QStringLiteral("### ")))) {
            if (!current.text.trimmed().isEmpty()) {
                into.push_back(current);
            }
            current = Section{.page = page, .name = sectionName(QStringView(line).sliced(line.indexOf(u' ') + 1))};
        }
        current.text += line + u'\n';
    }
    if (!current.text.trimmed().isEmpty()) {
        into.push_back(current);
    }
}

const std::vector<Section>& referenceSections()
{
    static const std::vector<Section> sections = [] {
        std::vector<Section> made;
        for (const script::ApiFile& file : script::apiDocs({})) {
            QString page = toQString(file.path).section(u'/', -1);
            page.chop(3);
            if (page == u"README") {
                page = QStringLiteral("index");
            }
            splitPage(page, toQString(file.contents), made);
        }
        splitPage(QStringLiteral("profile-format"), profileFormat(), made);
        return made;
    }();
    return sections;
}

QString joined(const std::vector<const Section*>& chosen)
{
    QString text;
    for (const Section* section : chosen) {
        if (!text.isEmpty() && text.size() + section->text.size() > kMostReferenceChars) {
            text += QStringLiteral("\n(The rest is left out; ask for a narrower part.)\n");
            break;
        }
        if (!text.isEmpty()) {
            text += u'\n';
        }
        text += section->text;
    }
    return text;
}

QString readText(const fs::path& file, qsizetype most, QString& problem)
{
    std::string contents;
    const std::error_code error =
        platform::readFile(file, static_cast<std::size_t>(most), contents, platform::WhenBusy::GiveUp);
    if (error == std::errc::file_too_large) {
        problem = QStringLiteral("larger than %1 KiB").arg(most >> 10);
        return {};
    }
    if (error) {
        problem = platform::fileBusy(error) ? QStringLiteral("another program is using the file")
                                            : QStringLiteral("it cannot be read");
        return {};
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(QByteArrayView(contents.data(), static_cast<qsizetype>(contents.size())));
    if (decoder.hasError()) {
        problem = QStringLiteral("not UTF-8 text");
        return {};
    }
    return text;
}

QJsonObject resourceNames(const fs::path& folder)
{
    const script::LoadedResources loaded = script::loadResources(folder / utf8Path(script::kResourcesFile));
    if (!loaded) {
        return {{QStringLiteral("problem"),
                 QStringLiteral("GS-API-020 res/resources.json cannot be used: %1")
                     .arg(loaded.error().empty() ? QStringLiteral("it is not valid") : toQString(loaded.error().front()))}};
    }
    const auto names = [](const auto& list) {
        QJsonArray found;
        for (const auto& each : list) {
            found.append(toQString(each.name));
        }
        return found;
    };
    return {{QStringLiteral("templates"), names(loaded->templates)},
            {QStringLiteral("regions"), names(loaded->regions)},
            {QStringLiteral("points"), names(loaded->points)},
            {QStringLiteral("paths"), names(loaded->paths)},
            {QStringLiteral("colors"), names(loaded->colors)}};
}

QJsonObject folderFiles(const fs::path& folder)
{
    QJsonObject files;
    QJsonArray others;
    QJsonArray skipped;
    qsizetype total = 0;
    for (const ScriptFile& file : scriptFiles(pathText(folder), {})) {
        if (file.folder) {
            continue;
        }
        if (file.kind != ScriptFileKind::Profile && file.kind != ScriptFileKind::Code &&
            file.kind != ScriptFileKind::Text) {
            others.append(file.path);
            continue;
        }
        QString problem;
        const QString text = readText(folder / toPath(file.path), McpTools::kMostFileBytes, problem);
        if (problem.isEmpty() && total + text.size() > kMostReadBytes) {
            problem = QStringLiteral("the text files are larger than 8 MiB in all");
        }
        if (!problem.isEmpty()) {
            skipped.append(QJsonObject{{QStringLiteral("path"), file.path}, {QStringLiteral("reason"), problem}});
            continue;
        }
        total += text.size();
        files.insert(file.path, text);
    }
    QJsonObject result{{QStringLiteral("files"), files}};
    if (!others.isEmpty()) {
        result.insert(QStringLiteral("otherFiles"), others);
    }
    if (!skipped.isEmpty()) {
        result.insert(QStringLiteral("skipped"), skipped);
    }
    return result;
}

QJsonArray filesJson(const std::vector<McpDraftFile>& files)
{
    QJsonArray list;
    for (const McpDraftFile& file : files) {
        QJsonObject item{{QStringLiteral("path"), file.path},
                         {QStringLiteral("state"), file.state},
                         {QStringLiteral("added"), file.added},
                         {QStringLiteral("removed"), file.removed}};
        if (file.conflict) {
            item.insert(QStringLiteral("conflict"), true);
        }
        list.append(item);
    }
    return list;
}

QString cardDiff(const QString& text)
{
    constexpr qsizetype kMostShown = 400;
    if (text.isEmpty()) {
        return {};
    }
    const QStringList lines = text.split(u'\n');
    if (lines.size() <= kMostShown + 1) {
        return text;
    }
    QString shown = lines.mid(0, kMostShown).join(u'\n');
    shown += QStringLiteral("\n… %1 more lines\n").arg(lines.size() - kMostShown - 1);
    return shown;
}

QJsonArray declaredJson(const std::vector<DeclaredSpec>& specs, const QVariantMap& values = {})
{
    QJsonArray list;
    for (const DeclaredSpec& spec : specs) {
        list.append(QJsonObject{{QStringLiteral("name"), spec.name},
                                {QStringLiteral("kind"), spec.kind},
                                {QStringLiteral("label"), spec.label.isEmpty() ? spec.name : spec.label},
                                {QStringLiteral("value"), QJsonValue::fromVariant(values.value(spec.name, spec.value))}});
    }
    return list;
}

QString firstLine(const QByteArray& errors)
{
    const QString text = QString::fromUtf8(errors).trimmed().section(u'\n', 0, 0).trimmed();
    return text.isEmpty() ? QStringLiteral("it gave no answer") : text;
}

QString extensionPath(const QString& given, QString& problem)
{
    const QString trimmed = QDir::fromNativeSeparators(given.trimmed());
    if (trimmed.endsWith(QStringLiteral(".hlsl"), Qt::CaseInsensitive) && trimmed.size() > 5) {
        const QString checked = mcpCheckedPath(trimmed.chopped(5) + QStringLiteral(".txt"), problem);
        return checked.isEmpty() ? QString() : QString(checked.chopped(4) + trimmed.right(5));
    }
    return mcpCheckedPath(trimmed, problem);
}

std::optional<std::string> entryOf(const fs::path& folder)
{
    std::error_code error;
    const bool profile = fs::is_regular_file(folder / "profile.json", error);
    const std::string_view candidates[] = {profile ? "logic.ts" : "src/main.ts", profile ? "logic.js" : "src/main.js",
                                           "task.ts", "component.ts"};
    for (const std::string_view candidate : candidates) {
        if (fs::is_regular_file(folder / utf8Path(candidate), error)) {
            return std::string(candidate);
        }
    }
    return std::nullopt;
}

void copyResources(const fs::path& from, const fs::path& to)
{
    std::error_code error;
    const fs::path source = from / "res";
    if (!fs::is_directory(source, error)) {
        return;
    }
    std::uintmax_t copied = 0;
    int count = 0;
    for (fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator() && count < kMostCopiedFiles; it.increment(error)) {
        std::error_code kind;
        if (!it->is_regular_file(kind)) {
            continue;
        }
        const std::uintmax_t size = it->file_size(kind);
        if (kind || copied + size > kMostCopiedBytes) {
            continue;
        }
        const fs::path target = to / "res" / fs::relative(it->path(), source, kind);
        if (kind) {
            continue;
        }
        fs::create_directories(target.parent_path(), kind);
        if (!kind && fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, kind)) {
            copied += size;
            ++count;
        }
    }
}

std::string_view levelName(log::Level level)
{
    switch (level) {
    case log::Level::Trace:
    case log::Level::Debug:
        return "debug";
    case log::Level::Info:
        return "info";
    case log::Level::Warning:
        return "warn";
    case log::Level::Error:
        return "error";
    }
    return "info";
}

QString consoleLine(const LogicReport& report)
{
    QString line = toQString(levelName(report.level)) + u' ';
    if (!report.file.isEmpty() && report.line > 0) {
        line += QStringLiteral("%1:%2: ").arg(QFileInfo(report.file).fileName()).arg(report.line);
    }
    line += report.text;
    if (line.size() > kMostConsoleChars) {
        line = line.left(kMostConsoleChars - 1) + QChar(0x2026);
    }
    return line;
}

QJsonObject pngItem(const QImage& image)
{
    QByteArray png;
    QBuffer buffer(&png);
    if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return {};
    }
    return {{QStringLiteral("type"), QStringLiteral("image")},
            {QStringLiteral("data"), QString::fromLatin1(png.toBase64())},
            {QStringLiteral("mimeType"), QStringLiteral("image/png")}};
}

struct ReplayPlan {
    fs::path folder;
    std::string entry;
    fs::path session;
    double untilMs = kDefaultUntilMs;
    std::uint64_t seed = 1;
    std::shared_ptr<QTemporaryDir> scratch;
    std::string profileJson;
    std::vector<script::ComponentModule> components;
    bool trace = false;
};

McpOutcome replayed(const ReplayPlan& plan)
{
    const auto session = script::loadSession(plan.session);
    if (!session) {
        return failure(QStringLiteral("GS-MCP-011"),
                       QStringLiteral("session %1: %2").arg(pathText(plan.session), toQString(session.error())));
    }
    std::string profileText = plan.profileJson;
    fs::path profileFile;
    if (!session->profile.empty()) {
        profileFile = plan.session.parent_path() / utf8Path(session->profile);
        profileText.clear();
        if (platform::readFile(profileFile, kMostProfileBytes, profileText, platform::WhenBusy::GiveUp)) {
            return failure(QStringLiteral("GS-CHECK-005"), QStringLiteral("%1: the profile cannot be read").arg(pathText(profileFile)));
        }
    }
    std::vector<script::ControlInfo> controls;
    std::vector<script::StateInfo> states;
    if (!profileText.empty()) {
        const input::LoadResult loaded = input::loadProfile(profileText);
        QStringList problems;
        for (const input::Diagnostic& diagnostic : loaded.diagnostics) {
            if (diagnostic.severity == input::Severity::Error) {
                problems.append(QStringLiteral("profile.json: error GS-CHECK-005 %1")
                                    .arg(toQString(diagnostic.detail.empty() ? diagnostic.path
                                                                             : diagnostic.path + ": " + diagnostic.detail)));
            }
        }
        if (!problems.isEmpty()) {
            return failure(QStringLiteral("GS-CHECK-005"),
                           QStringLiteral("The profile has problems, so nothing was replayed:\n") + problems.join(u'\n'));
        }
        for (const input::Control& control : loaded.profile.controls) {
            controls.push_back({control.id, control.name, std::string(input::kControlTypeNames[control.kind.index()]),
                                control.enabled, input::controlSettings(control)});
        }
        for (const input::Variable& variable : loaded.profile.vars) {
            states.push_back({variable.id, variable.name,
                              std::visit([](const auto& each) { return script::VarValue(each); }, variable.initial)});
        }
    }
    ManualClock clock;
    script::RecordingServices services(clock, session->device, controls, states);
    std::optional<record::RecordedVision> vision;
    fs::path video;
    if (!session->video.empty()) {
        video = plan.session.parent_path() / utf8Path(session->video);
        vision.emplace(plan.folder, video, session->videoStartPtsUs);
        if (vision->opened()) {
            services.setVision(&*vision);
        } else {
            vision.reset();
            video.clear();
        }
    }
    McpTrace trace;
    bool failed = false;
    const std::int64_t epochMs = session->startedAtMs;
    const script::ScriptHost::Sink sink = [&services, &failed, &trace, &plan, epochMs](const script::Report& report) {
        switch (report.kind) {
        case script::ReportKind::Log:
            services.note(std::format("log {} {}", levelName(report.level), report.message));
            break;
        case script::ReportKind::ScriptFailed:
        case script::ReportKind::TaskFailed:
        case script::ReportKind::TaskInterrupted:
        case script::ReportKind::UnhandledRejection:
        case script::ReportKind::MemoryLimit:
            failed = true;
            services.note("error " + script::describe(report));
            break;
        case script::ReportKind::DebugHits:
            if (plan.trace) {
                trace.addHits(report.message, epochMs);
            }
            break;
        default:
            break;
        }
    };
    script::ReplayOptions options;
    options.source = script::ScriptSource{.folder = plan.folder,
                                          .entry = plan.entry,
                                          .modules = script::controlModules(plan.folder),
                                          .components = plan.components};
    if (plan.trace) {
        options.source.transform = [folder = plan.folder](const std::string& module, const std::string& code) {
            return instrumentModule(folder, module, code);
        };
    }
    options.events = session->events;
    options.until = Microseconds{static_cast<std::int64_t>(std::llround(plan.untilMs * 1000.0))};
    options.seed = plan.seed;
    options.wallEpochMs = session->startedAtMs;
    options.frameRate = session->frameRate;
    const script::ReplayResult result = script::replay(options, services, clock, sink);
    QStringList lines;
    for (const script::ReplayLine& line : services.lines()) {
        lines.append(toQString(script::formatLine(line)));
    }
    const double endedMs = static_cast<double>(result.ended.count()) / 1000.0;
    lines.append(toQString(std::format("ended at {} ms{}", endedMs, result.reachedLimit ? " (time limit)" : "")));
    McpOutcome outcome = !result.loaded || failed
                             ? failure(QStringLiteral("GS-MCP-013"),
                                       QStringLiteral("The logic failed while replaying; the lines show where:\n") +
                                           lines.join(u'\n'))
                             : success(lines.join(u'\n'));
    if (!plan.trace) {
        return outcome;
    }
    outcome.content.append(textItem(jsonText(trace.steps(services.lines()))));
    if (!video.empty()) {
        const std::vector<double> times = McpTrace::frameTimes(services.lines(), endedMs);
        for (const McpTraceFrame& frame : mcpTraceFrames(video, session->videoStartPtsUs, times, McpTrace::kFrameSide)) {
            const QJsonObject image = pngItem(frame.image);
            if (image.isEmpty()) {
                continue;
            }
            outcome.content.append(textItem(QStringLiteral("Frame at %1 ms of the recording").arg(frame.atMs)));
            outcome.content.append(image);
        }
    }
    return outcome;
}

QImage imageOf(const media::LumaSnapshot& shot)
{
    if (shot.colored()) {
        if (QImage picture = pictureOf(shot); !picture.isNull()) {
            return picture;
        }
    }
    const auto width = static_cast<std::size_t>(std::max(shot.width, 0));
    if (width == 0 || shot.height <= 0 || shot.luma.size() < width * static_cast<std::size_t>(shot.height)) {
        return {};
    }
    QImage grey(shot.width, shot.height, QImage::Format_Grayscale8);
    for (int y = 0; y < shot.height; ++y) {
        std::memcpy(grey.scanLine(y), shot.luma.data() + static_cast<std::size_t>(y) * width, width);
    }
    return grey;
}

McpOutcome pictureOutcome(QImage image, bool lossless, int maxSide)
{
    if (image.isNull()) {
        return failure(QStringLiteral("GS-MCP-012"), QStringLiteral("The picture could not be read"));
    }
    const QSize original = image.size();
    if (maxSide > 0 && std::max(original.width(), original.height()) > maxSide) {
        image = image.scaled(QSize(maxSide, maxSide), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return failure(QStringLiteral("GS-MCP-012"), QStringLiteral("The picture could not be made into a PNG"));
    }
    QString text = QStringLiteral("%1 x %2 pixels").arg(image.width()).arg(image.height());
    if (image.size() != original) {
        text += QStringLiteral(", scaled down from %1 x %2").arg(original.width()).arg(original.height());
    }
    text += lossless ? QStringLiteral(": the phone's lossless screenshot.")
                     : QStringLiteral(": the latest frame of the video; the phone cannot take a lossless screenshot "
                                      "now.");
    return McpOutcome{.content = QJsonArray{textItem(text),
                                            QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                                                        {QStringLiteral("data"), QString::fromLatin1(png.toBase64())},
                                                        {QStringLiteral("mimeType"), QStringLiteral("image/png")}}}};
}

struct TemplateSpec {
    QImage given;
    fs::path file;
    std::string label;
    double threshold = kFindThreshold;
    bool color = false;
    script::Region region;
    script::PictureSize source;
    int most = kFindMost;
    bool all = false;
};

McpOutcome searched(const media::LumaSnapshot& shot, const TemplateSpec& spec)
{
    std::string problem;
    const QImage picture = spec.given.isNull() ? readPicture(pathText(spec.file), spec.label, problem) : spec.given;
    if (!problem.empty()) {
        return failureOf(problem);
    }
    const QImage scaled = spec.source.width > 0 ? scaledPicture(picture, spec.source, shot.width, shot.height) : picture;
    const std::shared_ptr<const vision::Template> pattern = templateOf(scaled, spec.color, spec.label, problem);
    if (pattern == nullptr) {
        return failureOf(problem);
    }
    const vision::Rect area = pixelArea(spec.region, shot.width, shot.height);
    const std::vector<vision::Match> matches = findTemplate(shot, *pattern, area, spec.threshold, spec.most, spec.all);
    if (!spec.all) {
        return success(matches.empty() ? QStringLiteral("null")
                                       : toQString(foundJson(matches.front().rect, matches.front().score, shot.width,
                                                             shot.height)));
    }
    std::string json = "[";
    for (const vision::Match& match : matches) {
        if (json.size() > 1) {
            json += ',';
        }
        json += foundJson(match.rect, match.score, shot.width, shot.height);
    }
    json += ']';
    return success(toQString(json));
}

bool picturePath(const QString& path)
{
    if (path.isEmpty() || path.size() > 260 || path.startsWith(u'/') || path.startsWith(u'\\') || path.contains(u':')) {
        return false;
    }
    const QStringList segments = QDir::fromNativeSeparators(path).split(u'/');
    if (std::ranges::any_of(segments, [](const QString& segment) {
            return segment.isEmpty() || segment == u"." || segment == u"..";
        })) {
        return false;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == u"png" || suffix == u"jpg" || suffix == u"jpeg" || suffix == u"bmp";
}

bool identifier(const QString& name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_$][A-Za-z0-9_$]*$"));
    return pattern.match(name).hasMatch();
}

bool exportsDefault(const QString& code)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*export\\s+default\\b"),
                                            QRegularExpression::MultilineOption);
    return pattern.match(code).hasMatch();
}

QString kindWord(const QString& target)
{
    return target.section(u':', 0, 0);
}

QString valueText(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isObject()) {
        return jsonText(value.toObject());
    }
    if (value.isArray()) {
        return jsonText(QJsonDocument(value.toArray()));
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 17);
    }
    return QStringLiteral("null");
}

QJsonObject jsonObjectOf(std::string_view json)
{
    return QJsonDocument::fromJson(QByteArray(json.data(), static_cast<qsizetype>(json.size()))).object();
}

void announce(const QPointer<McpTools>& self)
{
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [self] {
            if (self != nullptr) {
                emit self->toolsChanged();
            }
        },
        Qt::QueuedConnection);
}

template <typename Work>
void later(QPointer<McpTools> self, quint64 call, Work work, std::function<void(McpTools&, quint64, McpOutcome)> done)
{
    QThreadPool::globalInstance()->start([self, call, work = std::move(work), done = std::move(done)] {
        McpOutcome outcome = work();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, call, done, outcome = std::move(outcome)] {
                if (self != nullptr) {
                    done(*self, call, outcome);
                }
            },
            Qt::QueuedConnection);
    });
}

}

struct McpTools::Registry {
    std::mutex mutex;
    std::vector<McpExtensionTool> tools;
};

QString mcpExtensionToolName(const QString& extension, const QString& tool)
{
    QString id = extension;
    id.replace(u'.', u'_').replace(u'-', u'_');
    return QStringLiteral("ext_%1_%2").arg(id, tool);
}

McpTools::McpTools(McpToolSetup setup, const Clock& clock, QObject* parent)
    : QObject(parent)
    , m_setup(std::move(setup))
    , m_clock(clock)
    , m_registry(std::make_shared<Registry>())
{
    m_deadline.setSingleShot(true); // source-rule-allow R9: a run stops at its time limit and a screenshot is awaited only briefly
    connect(&m_deadline, &QTimer::timeout, this, &McpTools::expire);
    const QPointer<McpTools> self(this);
    LogicServices& services = LogicServices::instance();
    services.offerCall("mcp.register", [registry = m_registry, self](const LogicCaller& caller, std::string_view json) {
        return registerTool(*registry, self, caller, json);
    });
    services.offerCall("mcp.unregister", [registry = m_registry, self](const LogicCaller& caller, std::string_view json) {
        return unregisterTool(*registry, self, caller, json);
    });
    services.offerCall("mcp.result", [self](const LogicCaller& caller, std::string_view json) {
        return toolResult(self, caller, json);
    });
    connect(&services, &LogicServices::logicEnded, this, &McpTools::logicEnded);
}

McpTools::~McpTools()
{
    LogicServices& services = LogicServices::instance();
    services.withdraw("mcp.register");
    services.withdraw("mcp.unregister");
    services.withdraw("mcp.result");
    m_deadline.stop();
    std::map<quint64, Waiting> waiting = std::move(m_waiting);
    m_waiting.clear();
    for (const auto& entry : waiting) {
        if (entry.second.run != 0 && m_setup.devices.stopScript) {
            m_setup.devices.stopScript(entry.second.run);
        }
        if (QProcess* process = entry.second.process.data()) {
            disconnect(process, nullptr, this, nullptr);
            process->kill();
        }
    }
}

QJsonArray McpTools::tools() const
{
    static const QJsonArray list = toolList();
    QJsonArray all = list;
    const std::scoped_lock lock(m_registry->mutex);
    for (const McpExtensionTool& tool : m_registry->tools) {
        all.append(QJsonObject{
            {QStringLiteral("name"), tool.name},
            {QStringLiteral("description"),
             QStringLiteral("%1 (Offered by the extension %2. It acts on phones, so the user confirms each call or "
                            "lets this client use it without asking until GameScrcpy closes.)")
                 .arg(tool.description, tool.extension)},
            {QStringLiteral("inputSchema"), tool.input}});
    }
    return all;
}

std::vector<McpExtensionTool> McpTools::extensionTools() const
{
    const std::scoped_lock lock(m_registry->mutex);
    return m_registry->tools;
}

void McpTools::call(const McpCall& call, Reply reply)
{
    const QString& tool = call.tool;
    if (tool == u"api_reference") {
        reply(apiReference(call.arguments));
    } else if (tool == u"list_phones") {
        reply(listPhones(call.arguments));
    } else if (tool == u"list_profiles") {
        reply(listProfiles(call));
    } else if (tool == u"read_profile") {
        reply(readProfile(call));
    } else if (tool == u"read_logs") {
        reply(readLogs(call.arguments));
    } else if (tool == u"run_replay") {
        runReplay(call, std::move(reply));
    } else if (tool == u"get_frame") {
        getFrame(call.id, call.arguments, std::move(reply));
    } else if (tool == u"find_image") {
        findImage(call.id, call.arguments, std::move(reply));
    } else if (tool == u"write_profile") {
        reply(writeDraft(call, u"profile"));
    } else if (tool == u"run_script") {
        runScript(call, std::move(reply));
    } else if (tool == u"list_components") {
        reply(listComponents(call));
    } else if (tool == u"read_component") {
        reply(readComponent(call));
    } else if (tool == u"write_component") {
        reply(writeDraft(call, u"component"));
    } else if (tool == u"list_tasks") {
        reply(listTasks(call));
    } else if (tool == u"read_task") {
        reply(readTask(call));
    } else if (tool == u"write_task") {
        reply(writeDraft(call, u"task"));
    } else if (tool == u"read_draft") {
        reply(readDraft(call));
    } else if (tool == u"draft_diff") {
        reply(draftDiff(call));
    } else if (tool == u"apply_draft") {
        applyDraft(call, std::move(reply));
    } else if (tool == u"discard_draft") {
        reply(discardDraft(call));
    } else if (tool == u"open_draft_preview") {
        reply(openDraftPreview(call));
    } else if (tool == u"read_blueprint") {
        reply(readBlueprint(call));
    } else if (tool == u"edit_blueprint") {
        reply(editBlueprint(call));
    } else if (tool == u"check") {
        check(call, std::move(reply));
    } else if (tool == u"start_recording") {
        startRecording(call, std::move(reply));
    } else if (tool == u"stop_recording") {
        reply(stopRecording(call));
    } else if (tool == u"list_recordings") {
        reply(listRecordings(call.arguments));
    } else if (tool == u"propose_keymap") {
        reply(proposeKeymap(call));
    } else if (tool == u"keymap_draft_status") {
        reply(keymapStatus(call));
    } else if (tool == u"request_materials") {
        requestMaterials(call, std::move(reply));
    } else if (tool == u"create_extension") {
        reply(createExtension(call));
    } else if (tool == u"write_extension") {
        reply(writeExtension(call));
    } else if (tool == u"validate_extension") {
        validateExtension(call, std::move(reply));
    } else if (tool == u"pack_extension") {
        packExtension(call, std::move(reply));
    } else if (tool.startsWith(u"ext_")) {
        callExtensionTool(call, std::move(reply));
    } else {
        reply(failure(QStringLiteral("GS-MCP-003"), QStringLiteral("There is no tool named %1").arg(tool)));
    }
}

void McpTools::cancel(quint64 call)
{
    auto node = m_waiting.extract(call);
    if (node.empty()) {
        return;
    }
    if (node.mapped().materials && m_setup.devices.endMaterials) {
        m_setup.devices.endMaterials(node.mapped().client);
    }
    if (node.mapped().run != 0 && m_setup.devices.stopScript) {
        m_setup.devices.stopScript(node.mapped().run);
    }
    if (QProcess* process = node.mapped().process) {
        disconnect(process, nullptr, this, nullptr);
        process->kill();
        process->deleteLater();
    }
    if (node.mapped().logic != 0) {
        static_cast<void>(LogicServices::instance().tell(node.mapped().logic, QStringLiteral("mcp"), QString::number(call),
                                                         QStringLiteral("{\"cancel\":true}")));
    }
    schedule();
}

void McpTools::forget(const QString& client)
{
    std::vector<quint64> owned;
    for (const auto& [call, waiting] : m_waiting) {
        if (waiting.materials && waiting.client == client) {
            owned.push_back(call);
        }
    }
    for (const quint64 call : owned) {
        m_waiting.erase(call);
    }
    if (!owned.empty() && m_setup.devices.endMaterials) {
        m_setup.devices.endMaterials(client);
    }
    std::erase_if(m_quiet, [&client](const auto& entry) { return entry.first.first == client; });
    std::erase_if(m_proposals, [&client](const auto& entry) { return entry.second.client == client; });
}

QVariantList McpTools::allowedNow() const
{
    QVariantList list;
    for (const auto& [key, what] : m_quiet) {
        const bool tool = key.second.startsWith(u"tool:");
        list.append(QVariantMap{{QStringLiteral("client"), key.first},
                                {QStringLiteral("key"), key.second},
                                {QStringLiteral("kind"), tool ? QStringLiteral("tool") : QStringLiteral("run")},
                                {QStringLiteral("what"), what}});
    }
    return list;
}

void McpTools::endAllowed(const QString& client, const QString& key)
{
    if (m_quiet.erase({client, key}) > 0) {
        qInfo("mcp %s: asks again before %s", qUtf8Printable(client), qUtf8Printable(key));
    }
}

void McpTools::expire()
{
    const Microseconds now = m_clock.now();
    std::vector<quint64> due;
    for (const auto& [call, waiting] : m_waiting) {
        if (waiting.deadline && *waiting.deadline <= now) {
            due.push_back(call);
        }
    }
    for (const quint64 call : due) {
        const auto found = m_waiting.find(call);
        if (found == m_waiting.end()) {
            continue;
        }
        found->second.deadline.reset();
        if (found->second.screenshot) {
            found->second.screenshot = false;
            captureFrame(call);
        } else if (found->second.run != 0) {
            found->second.timedOut = true;
            const quint64 run = found->second.run;
            if (m_setup.devices.stopScript) {
                m_setup.devices.stopScript(run);
            }
        } else if (found->second.materials) {
            const QString client = found->second.client;
            auto node = m_waiting.extract(call);
            schedule();
            if (m_setup.devices.endMaterials) {
                m_setup.devices.endMaterials(client);
            }
            if (node.mapped().reply) {
                node.mapped().reply(
                    failure(QStringLiteral("GS-MCP-026"),
                            QStringLiteral("The user did not answer within %1 minutes, so the request for materials "
                                           "was ended; what the user picked before that is in this client's draft")
                                .arg(std::chrono::duration_cast<std::chrono::minutes>(kMaterialsLimit).count())));
            }
        } else if (found->second.logic != 0) {
            static_cast<void>(LogicServices::instance().tell(found->second.logic, QStringLiteral("mcp"),
                                                             QString::number(call), QStringLiteral("{\"cancel\":true}")));
            finish(call, failure(QStringLiteral("GS-MCP-023"),
                                 QStringLiteral("The tool did not answer within %1 seconds, so it was stopped")
                                     .arg(std::chrono::duration_cast<std::chrono::seconds>(kToolLimit).count())));
        } else if (QProcess* process = found->second.process) {
            disconnect(process, nullptr, this, nullptr);
            process->kill();
            process->deleteLater();
            found->second.process = nullptr;
            finish(call, failure(QStringLiteral("GS-MCP-020"),
                                 QStringLiteral("gs.exe did not finish within %1 seconds, so it was stopped")
                                     .arg(std::chrono::duration_cast<std::chrono::seconds>(kProgramLimit).count())));
        }
    }
    schedule();
}

McpOutcome McpTools::apiReference(const QJsonObject& given) const
{
    Arguments args(given, {u"query"});
    const QString query = args.text(u"query").simplified();
    if (!args.ok()) {
        return args.problem();
    }
    const std::vector<Section>& sections = referenceSections();
    const auto page = [&sections](QStringView name) {
        std::vector<const Section*> chosen;
        for (const Section& section : sections) {
            if (section.page == name) {
                chosen.push_back(&section);
            }
        }
        return chosen;
    };
    if (query.isEmpty()) {
        const std::vector<const Section*> index = page(u"index");
        return success((index.empty() ? QString() : index.front()->text) +
                       QStringLiteral("\nAsk for a name above, such as vision or touch.tap, for words to search, or "
                                      "for \"profile format\" to learn profile.json and the profile folder.\n") +
                       QStringLiteral("\n") + mcpRecipeIndex());
    }
    if (query.compare(u"recipes", Qt::CaseInsensitive) == 0) {
        return success(mcpRecipeIndex());
    }
    if (const std::optional<QString> recipe = mcpRecipe(query)) {
        return success(*recipe);
    }
    const QString lower = query.toLower();
    QString pageName = lower;
    pageName.replace(u' ', u'-');
    if (pageName == u"profile.json" || pageName == u"profile-folder" || pageName == u"profiles") {
        pageName = QStringLiteral("profile-format");
    } else if (pageName == u"readme") {
        pageName = QStringLiteral("index");
    }
    if (const std::vector<const Section*> whole = page(pageName); !whole.empty()) {
        return success(joined(whole));
    }
    std::vector<const Section*> named;
    for (const Section& section : sections) {
        if (!section.name.isEmpty() && section.name.compare(query, Qt::CaseInsensitive) == 0) {
            named.push_back(&section);
        }
    }
    if (!named.empty()) {
        return success(joined(named));
    }
    const QStringList words = lower.split(u' ', Qt::SkipEmptyParts);
    std::vector<std::pair<qsizetype, const Section*>> scored;
    for (const Section& section : sections) {
        const QString haystack = section.text.toLower();
        if (!std::ranges::all_of(words, [&haystack](const QString& word) { return haystack.contains(word); })) {
            continue;
        }
        const QString heading = section.name.toLower();
        qsizetype score = 0;
        for (const QString& word : words) {
            score += (heading.contains(word) ? 10 : 0) + std::min<qsizetype>(haystack.count(word), 5);
        }
        scored.emplace_back(score, &section);
    }
    if (scored.empty()) {
        return success(QStringLiteral("Nothing in the API reference matches \"%1\". Leave the query out for the index.")
                           .arg(query));
    }
    std::ranges::stable_sort(scored, std::greater{}, [](const auto& entry) { return entry.first; });
    std::vector<const Section*> chosen;
    for (std::size_t index = 0; index < scored.size() && index < kMostReferenceSections; ++index) {
        chosen.push_back(scored[index].second);
    }
    QString text = joined(chosen);
    if (scored.size() > chosen.size()) {
        text += QStringLiteral("\n%1 more sections match; add words to narrow the search.\n")
                    .arg(static_cast<qulonglong>(scored.size() - chosen.size()));
    }
    return success(text);
}

McpOutcome McpTools::listPhones(const QJsonObject& given) const
{
    Arguments args(given, {});
    if (!args.ok()) {
        return args.problem();
    }
    QJsonArray list;
    for (const McpPhone& phone : m_setup.devices.phones ? m_setup.devices.phones() : std::vector<McpPhone>{}) {
        QJsonObject item{{QStringLiteral("serial"), phone.serial},
                         {QStringLiteral("name"), phone.name},
                         {QStringLiteral("width"), phone.width},
                         {QStringLiteral("height"), phone.height}};
        item.insert(QStringLiteral("app"), phone.app.isEmpty() ? QJsonValue() : QJsonValue(phone.app));
        item.insert(QStringLiteral("profile"),
                    phone.profileId.isEmpty()
                        ? QJsonValue()
                        : QJsonValue(QJsonObject{{QStringLiteral("id"), phone.profileId},
                                                 {QStringLiteral("name"), phone.profileName}}));
        list.append(item);
    }
    return success(jsonText(QJsonDocument(list)));
}

McpOutcome McpTools::listProfiles(const McpCall& call) const
{
    Arguments args(call.arguments, {});
    if (!args.ok()) {
        return args.problem();
    }
    QJsonArray list;
    if (m_setup.profiles != nullptr) {
        for (const ProfileEntry& entry : m_setup.profiles->all()) {
            QJsonObject item{{QStringLiteral("id"), entry.id},
                             {QStringLiteral("name"), entry.name},
                             {QStringLiteral("folder"), !entry.folder.empty()},
                             {QStringLiteral("logic"), entry.logic},
                             {QStringLiteral("readOnly"), entry.readOnly()}};
            if (!entry.app.isEmpty()) {
                item.insert(QStringLiteral("app"), entry.app);
            }
            if (entry.readOnly()) {
                item.insert(QStringLiteral("extension"), entry.extensionName);
            }
            if (!entry.error.isEmpty()) {
                item.insert(QStringLiteral("problem"), entry.error);
            }
            if (drafted(call.client, QStringLiteral("profile:") + entry.id)) {
                item.insert(QStringLiteral("draft"), true);
            }
            list.append(item);
        }
    }
    return success(jsonText(QJsonDocument(list)));
}

McpOutcome McpTools::readProfile(const McpCall& call) const
{
    Arguments args(call.arguments, {u"profile", u"draft"});
    const QString id = args.text(u"profile", true);
    const bool draft = args.flag(u"draft").value_or(false);
    if (!args.ok()) {
        return args.problem();
    }
    if (draft) {
        McpOutcome problem;
        const std::optional<Folder> folder = folderFor(call.client, QStringLiteral("profile:") + id, true, problem);
        if (!folder) {
            return problem;
        }
        QJsonObject result = folderFiles(folder->path);
        result.insert(QStringLiteral("id"), id);
        result.insert(QStringLiteral("name"), folder->name);
        result.insert(QStringLiteral("draft"), true);
        const std::string logic = logicEntryOf(folder->path);
        result.insert(QStringLiteral("logic"), logic.empty() ? QJsonValue() : QJsonValue(toQString(logic)));
        result.insert(QStringLiteral("resources"), resourceNames(folder->path));
        return success(jsonText(result));
    }
    const ProfileEntry* entry = m_setup.profiles != nullptr ? m_setup.profiles->find(id) : nullptr;
    if (entry == nullptr) {
        return failure(QStringLiteral("GS-MCP-006"),
                       QStringLiteral("There is no profile with the id %1; list_profiles lists them").arg(id));
    }
    QJsonObject result;
    if (!entry->folder.empty()) {
        result = folderFiles(entry->folder);
    } else if (entry->readOnly()) {
        result.insert(QStringLiteral("files"),
                      QJsonObject{{QStringLiteral("profile.json"),
                                   entry->profile != nullptr ? toQString(input::saveProfile(*entry->profile)) : QString()}});
    } else {
        QString problem;
        const QString text = readText(entry->path, kMostFileBytes, problem);
        if (problem.isEmpty()) {
            result.insert(QStringLiteral("files"), QJsonObject{{QStringLiteral("profile.json"), text}});
        } else {
            result.insert(QStringLiteral("files"), QJsonObject{});
            result.insert(QStringLiteral("skipped"),
                          QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("profile.json")},
                                                 {QStringLiteral("reason"), problem}}});
        }
    }
    result.insert(QStringLiteral("id"), entry->id);
    result.insert(QStringLiteral("name"), entry->name);
    result.insert(QStringLiteral("readOnly"), entry->readOnly());
    result.insert(QStringLiteral("folder"), !entry->folder.empty());
    const std::string logic = entry->folder.empty() ? std::string() : logicEntryOf(entry->folder);
    result.insert(QStringLiteral("logic"), logic.empty() ? QJsonValue() : QJsonValue(toQString(logic)));
    if (!entry->folder.empty()) {
        result.insert(QStringLiteral("resources"), resourceNames(entry->folder));
    }
    if (!entry->error.isEmpty()) {
        result.insert(QStringLiteral("problem"), entry->error);
    }
    if (drafted(call.client, QStringLiteral("profile:") + entry->id)) {
        result.insert(QStringLiteral("hasDraft"), true);
    }
    return success(jsonText(result));
}

McpOutcome McpTools::readLogs(const QJsonObject& given) const
{
    Arguments args(given, {u"lines", u"level", u"contains", u"source"});
    const int most = args.whole(u"lines", 1, kMostLogLines).value_or(kDefaultLogLines);
    const QString level = args.text(u"level");
    const QString contains = args.text(u"contains");
    const QString source = args.text(u"source");
    static const QStringList kLevels{QStringLiteral("debug"), QStringLiteral("info"), QStringLiteral("warning"),
                                     QStringLiteral("error")};
    if (!level.isEmpty() && !kLevels.contains(level)) {
        args.complain(QStringLiteral("level must be debug, info, warning or error"));
    }
    if (!source.isEmpty() && source != u"all" && source != u"program" && source != u"logic") {
        args.complain(QStringLiteral("source must be all, program or logic"));
    }
    if (!args.ok()) {
        return args.problem();
    }
    if (m_setup.logFile.empty()) {
        return failure(QStringLiteral("GS-MCP-010"), QStringLiteral("GameScrcpy keeps no log in this run"));
    }
    const qsizetype least = level.isEmpty() ? 1 : kLevels.indexOf(level);
    static const QRegularExpression stamped(
        QStringLiteral("^\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d\\.\\d{3} (\\w+)\\s+(.*)$"));
    struct Entry {
        qsizetype rank = 1;
        QString message;
        QString text;
    };
    std::vector<Entry> entries;
    fs::path older = m_setup.logFile;
    older.replace_extension();
    older += ".1";
    older += m_setup.logFile.extension();
    for (const fs::path& file : {older, m_setup.logFile}) {
        QFile log(pathText(file));
        if (!log.open(QIODevice::ReadOnly)) {
            continue;
        }
        const qint64 size = log.size();
        if (size > kMostLogBytes) {
            log.seek(size - kMostLogBytes);
            static_cast<void>(log.readLine());
        }
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString all = decoder(log.readAll());
        for (const QString& line : all.split(u'\n')) {
            QString text = line;
            if (text.endsWith(u'\r')) {
                text.chop(1);
            }
            if (text.isEmpty()) {
                continue;
            }
            const QRegularExpressionMatch match = stamped.match(text);
            if (!match.hasMatch()) {
                if (!entries.empty()) {
                    entries.back().text += u'\n' + text;
                }
                continue;
            }
            const QString word = match.captured(1);
            const qsizetype rank = word == u"debug"     ? 0
                                   : word == u"warning" ? 2
                                   : word == u"critical" || word == u"fatal" ? 3
                                                                              : 1;
            entries.push_back({.rank = rank, .message = match.captured(2), .text = text});
        }
    }
    QStringList shown;
    for (auto at = entries.rbegin(); at != entries.rend() && shown.size() < most; ++at) {
        if (at->rank < least || (!contains.isEmpty() && !at->text.contains(contains, Qt::CaseInsensitive))) {
            continue;
        }
        const bool logic = at->message.startsWith(QStringLiteral("logic ")) ||
                           at->message.startsWith(QStringLiteral("task ")) ||
                           at->message.startsWith(QStringLiteral("script "));
        if ((source == u"logic" && !logic) || (source == u"program" && logic)) {
            continue;
        }
        shown.prepend(at->text);
    }
    return success(shown.isEmpty() ? QStringLiteral("No log line matches.") : shown.join(u'\n'));
}

McpOutcome McpTools::listComponents(const McpCall& call) const
{
    Arguments args(call.arguments, {});
    if (!args.ok()) {
        return args.problem();
    }
    QJsonArray list;
    QStringList listed;
    if (m_setup.components != nullptr) {
        for (const ComponentEntry& entry : m_setup.components->entries()) {
            QJsonObject item{{QStringLiteral("ref"), entry.ref},
                             {QStringLiteral("name"), entry.name},
                             {QStringLiteral("readOnly"), entry.example},
                             {QStringLiteral("props"), declaredJson(entry.props)}};
            if (!entry.description.isEmpty()) {
                item.insert(QStringLiteral("description"), entry.description);
            }
            if (!entry.type.isEmpty()) {
                item.insert(QStringLiteral("extends"), entry.type);
            }
            if (!entry.parent.isEmpty()) {
                item.insert(QStringLiteral("parent"), entry.parent);
            }
            if (drafted(call.client, QStringLiteral("component:") + entry.ref)) {
                item.insert(QStringLiteral("draft"), true);
            }
            listed.append(entry.ref);
            list.append(item);
        }
    }
    if (m_setup.drafts != nullptr) {
        for (const QString& target : m_setup.drafts->targets(call.client)) {
            const QString ref = target.section(u':', 1);
            if (kindWord(target) == u"component" && !listed.contains(ref)) {
                list.append(QJsonObject{{QStringLiteral("ref"), ref},
                                        {QStringLiteral("name"), ref.section(u'/', 1)},
                                        {QStringLiteral("draft"), true},
                                        {QStringLiteral("new"), true}});
            }
        }
    }
    return success(jsonText(QJsonDocument(list)));
}

McpOutcome McpTools::readComponent(const McpCall& call) const
{
    Arguments args(call.arguments, {u"component", u"draft"});
    const QString ref = args.text(u"component", true);
    const bool draft = args.flag(u"draft").value_or(false);
    if (!args.ok()) {
        return args.problem();
    }
    McpOutcome problem;
    const std::optional<Folder> folder = folderFor(call.client, QStringLiteral("component:") + ref, draft, problem);
    if (!folder) {
        return problem;
    }
    const ComponentEntry* entry = m_setup.components != nullptr ? m_setup.components->find(ref) : nullptr;
    QJsonObject result = folderFiles(folder->path);
    result.insert(QStringLiteral("ref"), ref);
    result.insert(QStringLiteral("name"), folder->name);
    result.insert(QStringLiteral("readOnly"), entry != nullptr && entry->example);
    if (entry != nullptr) {
        result.insert(QStringLiteral("props"), declaredJson(entry->props));
    }
    if (draft) {
        result.insert(QStringLiteral("draft"), true);
    } else if (drafted(call.client, QStringLiteral("component:") + ref)) {
        result.insert(QStringLiteral("hasDraft"), true);
    }
    return success(jsonText(result));
}

McpOutcome McpTools::listTasks(const McpCall& call) const
{
    Arguments args(call.arguments, {});
    if (!args.ok()) {
        return args.problem();
    }
    QJsonArray list;
    QStringList listed;
    if (m_setup.tasks != nullptr) {
        for (const TaskEntry& entry : m_setup.tasks->entries()) {
            QJsonObject item{{QStringLiteral("id"), entry.id},
                             {QStringLiteral("name"), entry.name},
                             {QStringLiteral("phones"), QJsonArray::fromStringList(entry.phones)},
                             {QStringLiteral("schedule"), entry.schedule},
                             {QStringLiteral("time"), entry.time},
                             {QStringLiteral("params"), declaredJson(entry.params, entry.values)}};
            if (drafted(call.client, QStringLiteral("task:") + entry.id)) {
                item.insert(QStringLiteral("draft"), true);
            }
            listed.append(entry.id);
            list.append(item);
        }
    }
    if (m_setup.drafts != nullptr) {
        for (const QString& target : m_setup.drafts->targets(call.client)) {
            const QString id = target.section(u':', 1);
            if (kindWord(target) == u"task" && !listed.contains(id)) {
                list.append(QJsonObject{{QStringLiteral("id"), id},
                                        {QStringLiteral("name"), id},
                                        {QStringLiteral("draft"), true},
                                        {QStringLiteral("new"), true}});
            }
        }
    }
    return success(jsonText(QJsonDocument(list)));
}

McpOutcome McpTools::readTask(const McpCall& call) const
{
    Arguments args(call.arguments, {u"task", u"draft"});
    const QString id = args.text(u"task", true);
    const bool draft = args.flag(u"draft").value_or(false);
    if (!args.ok()) {
        return args.problem();
    }
    McpOutcome problem;
    const std::optional<Folder> folder = folderFor(call.client, QStringLiteral("task:") + id, draft, problem);
    if (!folder) {
        return problem;
    }
    QJsonObject result = folderFiles(folder->path);
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("name"), folder->name);
    if (draft) {
        result.insert(QStringLiteral("draft"), true);
    } else if (drafted(call.client, QStringLiteral("task:") + id)) {
        result.insert(QStringLiteral("hasDraft"), true);
    }
    return success(jsonText(result));
}

McpOutcome McpTools::writeDraft(const McpCall& call, QStringView kind)
{
    Arguments args(call.arguments, {kind, u"files"});
    const QString id = args.text(kind, true);
    const QJsonObject given = args.object(u"files", true);
    if (args.ok() && (given.isEmpty() || given.size() > kMostWrittenFiles)) {
        args.complain(QStringLiteral("files must hold from 1 to %1 files").arg(kMostWrittenFiles));
    }
    if (!args.ok()) {
        return args.problem();
    }
    if (m_setup.drafts == nullptr) {
        return failure(QStringLiteral("GS-MCP-010"), QStringLiteral("Drafts cannot be kept in this run"));
    }
    std::vector<McpDraftWrite> files;
    for (auto at = given.begin(); at != given.end(); ++at) {
        if (!at.value().isString()) {
            return failure(QStringLiteral("GS-MCP-004"), QStringLiteral("the text of %1 must be a string").arg(at.key()));
        }
        files.push_back({.path = at.key(), .text = at.value().toString()});
    }
    const QString target = kind.toString() + u':' + id;
    const McpDraftResult result = m_setup.drafts->write(call.client, target, files);
    if (!result.ok()) {
        return failure(result.code, result.problem);
    }
    const QString name = m_setup.drafts->nameOf(target);
    QString text = result.written.isEmpty()
                       ? QStringLiteral("Nothing changed: the draft of %1 already holds this text.").arg(name)
                       : QStringLiteral("Wrote %1 into this client's draft of %2. Nothing changes for the user until "
                                        "apply_draft; try the draft with check and run_replay, both with draft: true.")
                             .arg(result.written.join(QStringLiteral(", ")), name);
    if (!result.warnings.isEmpty()) {
        text += u'\n' + result.warnings.join(u'\n');
    }
    return success(text);
}

McpOutcome McpTools::readDraft(const McpCall& call) const
{
    Arguments args(call.arguments, {u"target"});
    const QString target = args.target(u"target");
    if (!args.ok()) {
        return args.problem();
    }
    McpOutcome problem;
    const std::optional<Folder> folder = folderFor(call.client, target, true, problem);
    if (!folder) {
        return problem;
    }
    QJsonObject result = folderFiles(folder->path);
    result.insert(QStringLiteral("target"), target);
    result.insert(QStringLiteral("name"), folder->name);
    result.insert(QStringLiteral("changes"), filesJson(m_setup.drafts->diff(call.client, target)));
    return success(jsonText(result));
}

McpOutcome McpTools::draftDiff(const McpCall& call) const
{
    Arguments args(call.arguments, {u"target", u"path"});
    const QString given = args.text(u"target");
    const QString path = args.text(u"path");
    const QString target = mcpTargetOf(given);
    if (args.ok() && !given.isEmpty() && target.isEmpty()) {
        args.complain(QStringLiteral("target must be profile:<id>, component:<ref> or task:<id>"));
    }
    if (args.ok() && given.isEmpty() && !path.isEmpty()) {
        args.complain(QStringLiteral("path needs target"));
    }
    if (!args.ok()) {
        return args.problem();
    }
    if (given.isEmpty()) {
        QJsonArray list;
        if (m_setup.drafts != nullptr) {
            for (const QString& each : m_setup.drafts->targets(call.client)) {
                list.append(QJsonObject{{QStringLiteral("target"), each},
                                        {QStringLiteral("name"), m_setup.drafts->nameOf(each)},
                                        {QStringLiteral("files"), filesJson(m_setup.drafts->diff(call.client, each))}});
            }
        }
        return success(jsonText(QJsonObject{{QStringLiteral("drafts"), list}}));
    }
    if (!drafted(call.client, target)) {
        return failure(QStringLiteral("GS-MCP-014"),
                       QStringLiteral("This client has no draft of %1; draft_diff without target lists its drafts")
                           .arg(target));
    }
    if (!path.isEmpty()) {
        const QString text = m_setup.drafts->fileDiff(call.client, target, path);
        return success(text.isEmpty() ? QStringLiteral("%1 is the same in the draft and in the version in use.").arg(path)
                                      : text);
    }
    const std::vector<McpDraftFile> files = m_setup.drafts->diff(call.client, target);
    QString unified;
    for (const McpDraftFile& file : files) {
        const QString one = m_setup.drafts->fileDiff(call.client, target, file.path);
        if (unified.size() + one.size() > kMostReferenceChars) {
            unified += QStringLiteral("(The rest is left out; ask for one file with path.)\n");
            break;
        }
        unified += one;
    }
    return success(jsonText(QJsonObject{{QStringLiteral("target"), target},
                                        {QStringLiteral("name"), m_setup.drafts->nameOf(target)},
                                        {QStringLiteral("files"), filesJson(files)},
                                        {QStringLiteral("diff"), unified}}));
}

void McpTools::applyDraft(const McpCall& call, Reply reply)
{
    Arguments args(call.arguments, {u"target"});
    const QString target = args.target(u"target");
    if (!args.ok()) {
        reply(args.problem());
        return;
    }
    if (!drafted(call.client, target)) {
        reply(failure(QStringLiteral("GS-MCP-014"),
                      QStringLiteral("This client has no draft of %1; draft_diff lists its drafts").arg(target)));
        return;
    }
    std::vector<McpDraftFile> files = m_setup.drafts->diff(call.client, target);
    const QString name = m_setup.drafts->nameOf(target);
    if (files.empty()) {
        m_setup.drafts->discard(call.client, target);
        reply(success(QStringLiteral("The draft of %1 held no change from the version in use, so it was thrown away.")
                          .arg(name)));
        return;
    }
    const McpDraftResult checked = m_setup.drafts->problems(call.client, target);
    if (!checked.ok()) {
        reply(failure(checked.code, checked.problem));
        return;
    }
    std::ranges::stable_sort(files, [](const McpDraftFile& a, const McpDraftFile& b) { return a.conflict && !b.conflict; });
    QVariantList shownFiles;
    int conflicts = 0;
    for (const McpDraftFile& file : files) {
        conflicts += file.conflict ? 1 : 0;
        shownFiles.append(QVariantMap{{QStringLiteral("path"), file.path},
                                      {QStringLiteral("state"), file.state},
                                      {QStringLiteral("added"), file.added},
                                      {QStringLiteral("removed"), file.removed},
                                      {QStringLiteral("created"), file.state == u"added"},
                                      {QStringLiteral("conflict"), file.conflict},
                                      {QStringLiteral("diff"),
                                       cardDiff(m_setup.drafts->fileDiff(call.client, target, file.path))}});
    }
    McpConfirmation shown{.kind = QStringLiteral("apply"),
                          .details = {{QStringLiteral("target"), target},
                                      {QStringLiteral("what"), kindWord(target)},
                                      {QStringLiteral("name"), name},
                                      {QStringLiteral("files"), shownFiles},
                                      {QStringLiteral("conflicts"), conflicts},
                                      {QStringLiteral("previewable"), target.startsWith(QStringLiteral("profile:"))},
                                      {QStringLiteral("folder"), m_setup.drafts->convertsToFolder(call.client, target)}}};
    if (!call.confirm) {
        reply(failure(QStringLiteral("GS-MCP-008"), QStringLiteral("Nobody can confirm the changes, so the draft was not "
                                                                   "applied")));
        return;
    }
    m_waiting[call.id] = Waiting{.reply = std::move(reply)};
    const quint64 callId = call.id;
    const QString client = call.client;
    call.confirm(std::move(shown), [this, callId, client, target, name](McpAnswer answer) {
        if (!m_waiting.contains(callId)) {
            return;
        }
        if (answer == McpAnswer::Refused) {
            finish(callId, failure(QStringLiteral("GS-MCP-008"),
                                   QStringLiteral("The user did not apply the draft of %1; it is kept, so change it or "
                                                  "throw it away with discard_draft")
                                       .arg(name)));
            return;
        }
        if (answer == McpAnswer::Unanswered) {
            finish(callId, failure(QStringLiteral("GS-MCP-009"),
                                   QStringLiteral("The user did not answer within two minutes, so the draft of %1 was not "
                                                  "applied; it is kept")
                                       .arg(name)));
            return;
        }
        const McpDraftResult result = m_setup.drafts->apply(client, target);
        if (!result.ok()) {
            finish(callId, failure(result.code, result.problem));
            return;
        }
        QString text = QStringLiteral("Applied the draft of %1: %2.").arg(name, result.written.join(QStringLiteral(", ")));
        if (!result.conflicts.isEmpty()) {
            text += QStringLiteral("\nGS-MCP-015 These files changed in GameScrcpy after the draft was made and now hold "
                                   "the draft's version: %1")
                        .arg(result.conflicts.join(QStringLiteral(", ")));
        }
        if (result.target != target) {
            text += QStringLiteral("\nIt is now %1.").arg(result.target);
        }
        if (!result.warnings.isEmpty()) {
            text += u'\n' + result.warnings.join(u'\n');
        }
        finish(callId, success(text));
    });
}

McpOutcome McpTools::discardDraft(const McpCall& call)
{
    Arguments args(call.arguments, {u"target"});
    const QString target = args.target(u"target");
    if (!args.ok()) {
        return args.problem();
    }
    if (!drafted(call.client, target)) {
        return failure(QStringLiteral("GS-MCP-014"),
                       QStringLiteral("This client has no draft of %1; draft_diff lists its drafts").arg(target));
    }
    const QString name = m_setup.drafts->nameOf(target);
    m_setup.drafts->discard(call.client, target);
    return success(QStringLiteral("Threw away this client's draft of %1.").arg(name));
}

McpOutcome McpTools::openDraftPreview(const McpCall& call) const
{
    Arguments args(call.arguments, {u"target"});
    const QString target = args.target(u"target");
    if (!args.ok()) {
        return args.problem();
    }
    if (!drafted(call.client, target)) {
        return failure(QStringLiteral("GS-MCP-014"),
                       QStringLiteral("This client has no draft of %1; draft_diff lists its drafts").arg(target));
    }
    if (!m_setup.devices.openDraftPreview) {
        return failure(QStringLiteral("GS-MCP-010"),
                       QStringLiteral("GameScrcpy cannot show a draft preview in this run"));
    }
    QString problem;
    if (!m_setup.devices.openDraftPreview(target, QString(), problem)) {
        return failure(QStringLiteral("GS-MCP-010"), problem);
    }
    return success(QStringLiteral("Opened this client's draft of %1 in GameScrcpy's key mapping editor, with the "
                                  "added, changed and removed keys and blocks marked. Nothing changes until the user "
                                  "applies it there.")
                       .arg(target.section(u':', 1)));
}

McpOutcome McpTools::readBlueprint(const McpCall& call) const
{
    Arguments args(call.arguments, {u"target", u"file", u"page", u"draft", u"catalog"});
    const QString target = args.target(u"target");
    const QString file = args.text(u"file");
    const QString page = args.text(u"page");
    const bool draft = args.flag(u"draft").value_or(false);
    const QString catalog = args.text(u"catalog");
    if (!args.ok()) {
        return args.problem();
    }
    McpOutcome problem;
    const std::optional<Folder> folder = folderFor(call.client, target, draft, problem);
    if (!folder) {
        return problem;
    }
    QString wrong;
    const QString chosen = mcpBlueprintFile(folder->path, file, wrong);
    if (chosen.isEmpty()) {
        return failure(QStringLiteral("GS-MCP-016"), wrong);
    }
    QJsonObject result = mcpReadBlueprint(folder->path, chosen, page,
                                          args.has(u"catalog") ? std::optional<QString>(catalog) : std::nullopt, wrong);
    if (!wrong.isEmpty()) {
        return failure(QStringLiteral("GS-MCP-016"), wrong);
    }
    result.insert(QStringLiteral("target"), target);
    if (draft) {
        result.insert(QStringLiteral("draft"), true);
    }
    return success(jsonText(result));
}

McpOutcome McpTools::editBlueprint(const McpCall& call)
{
    Arguments args(call.arguments, {u"target", u"file", u"operations"});
    const QString target = args.target(u"target");
    const QString file = args.text(u"file");
    const QJsonArray operations = args.array(u"operations", true);
    if (!args.ok()) {
        return args.problem();
    }
    if (m_setup.drafts == nullptr) {
        return failure(QStringLiteral("GS-MCP-010"), QStringLiteral("Drafts cannot be kept in this run"));
    }
    McpDraftResult opened;
    const fs::path folder = m_setup.drafts->open(call.client, target, opened);
    if (folder.empty()) {
        return failure(opened.code, opened.problem);
    }
    QString wrong;
    const QString chosen = mcpBlueprintFile(folder, file, wrong);
    if (chosen.isEmpty()) {
        return failure(QStringLiteral("GS-MCP-016"), wrong);
    }
    const McpBlueprintEdit edit = mcpEditBlueprint(folder, chosen, operations);
    if (!edit.ok()) {
        return failure(edit.code, edit.problem);
    }
    QJsonArray written;
    if (edit.codeChanged) {
        const McpDraftResult stored = m_setup.drafts->store(call.client, target, chosen, edit.source);
        if (!stored.ok()) {
            return failure(stored.code, stored.problem);
        }
        written.append(chosen);
    }
    const McpDraftResult stored = m_setup.drafts->store(call.client, target, edit.layoutFile, edit.layout);
    if (!stored.ok()) {
        return failure(stored.code, stored.problem);
    }
    written.append(edit.layoutFile);
    QJsonObject result = edit.summary;
    result.insert(QStringLiteral("target"), target);
    result.insert(QStringLiteral("written"), written);
    result.insert(QStringLiteral("next"), QStringLiteral("The draft holds the change; try it with check and run_replay "
                                                         "(draft: true), then apply_draft."));
    return success(jsonText(result));
}

void McpTools::check(const McpCall& call, Reply reply)
{
    Arguments args(call.arguments, {u"target", u"draft"});
    const QString target = args.target(u"target");
    const bool draft = args.flag(u"draft").value_or(false);
    if (!args.ok()) {
        reply(args.problem());
        return;
    }
    McpOutcome problem;
    const std::optional<Folder> folder = folderFor(call.client, target, draft, problem);
    if (!folder) {
        reply(problem);
        return;
    }
    m_waiting[call.id] = Waiting{.reply = std::move(reply), .scratch = folder->scratch};
    const QString root = pathText(folder->path);
    runProgram(call.id, {QStringLiteral("check"), QDir::toNativeSeparators(root), QStringLiteral("--json")}, folder->path,
               [root](const QByteArray& output, const QByteArray& errors) {
                   const CheckOutcome outcome = parseCheckOutput(output, root);
                   if (!outcome.ran) {
                       return failure(QStringLiteral("GS-MCP-020"),
                                      QStringLiteral("gs check did not finish: %1")
                                          .arg(outcome.failure.isEmpty() ? firstLine(errors) : outcome.failure));
                   }
                   QJsonArray problems;
                   bool ok = true;
                   for (const ScriptProblem& found : outcome.problems) {
                       ok = ok && found.severity != u"error";
                       problems.append(QJsonObject{{QStringLiteral("file"), found.file},
                                                   {QStringLiteral("line"), found.line},
                                                   {QStringLiteral("column"), found.column},
                                                   {QStringLiteral("severity"), found.severity},
                                                   {QStringLiteral("code"), found.code},
                                                   {QStringLiteral("message"), found.message}});
                   }
                   return success(jsonText(QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("problems"), problems}}));
               });
}

void McpTools::runReplay(const McpCall& call, Reply reply)
{
    Arguments args(call.arguments, {u"session", u"profile", u"code", u"untilMs", u"seed", u"draft", u"component",
                                    u"task", u"trace"});
    const QString sessionText = args.text(u"session", true);
    const QString profile = args.text(u"profile");
    const QString code = args.text(u"code");
    const std::optional<double> until = args.number(u"untilMs", 1.0, kLongestUntilMs);
    const std::optional<int> seed = args.whole(u"seed", 1, 2'000'000'000);
    const bool draft = args.flag(u"draft").value_or(false);
    const QString component = args.text(u"component");
    const QString task = args.text(u"task");
    const bool trace = args.flag(u"trace").value_or(false);
    if (args.ok() && profile.isEmpty() && code.isEmpty() && task.isEmpty()) {
        args.complain(QStringLiteral("give profile, task, code or a mix of them"));
    }
    if (args.ok() && !component.isEmpty() && profile.isEmpty()) {
        args.complain(QStringLiteral("component needs profile, a profile that uses it"));
    }
    if (args.ok() && !code.isEmpty() && !task.isEmpty()) {
        args.complain(QStringLiteral("give code or task, not both"));
    }
    if (args.ok() && code.toUtf8().size() > kMostCodeBytes) {
        args.complain(QStringLiteral("code is longer than 256 KiB"));
    }
    if (args.ok() && !QDir::isAbsolutePath(sessionText)) {
        args.complain(QStringLiteral("session must be a full path"));
    }
    if (!args.ok()) {
        reply(args.problem());
        return;
    }
    std::error_code error;
    fs::path session = toPath(sessionText);
    if (fs::is_directory(session, error)) {
        session /= "session.json";
    }
    if (!fs::is_regular_file(session, error)) {
        reply(failure(QStringLiteral("GS-MCP-011"), QStringLiteral("There is no session file at %1").arg(pathText(session))));
        return;
    }
    fs::path folder;
    fs::path profileFile;
    QString profileText;
    bool usedDraft = false;
    if (!profile.isEmpty()) {
        const QString profileTarget = QStringLiteral("profile:") + profile;
        if (draft && drafted(call.client, profileTarget)) {
            folder = m_setup.drafts->folderOf(call.client, profileTarget);
            usedDraft = true;
        } else if (const ProfileEntry* entry = m_setup.profiles != nullptr ? m_setup.profiles->find(profile) : nullptr) {
            if (!entry->folder.empty()) {
                folder = entry->folder;
            } else if (entry->readOnly()) {
                profileText = entry->profile != nullptr ? toQString(input::saveProfile(*entry->profile)) : QString();
            } else {
                profileFile = entry->path;
            }
        } else if (QDir::isAbsolutePath(profile) && QFileInfo(profile).isDir()) {
            folder = toPath(profile);
        } else {
            reply(failure(QStringLiteral("GS-MCP-006"),
                          QStringLiteral("There is no profile with the id %1; list_profiles lists them, and the full "
                                         "path of a folder works too")
                              .arg(profile)));
            return;
        }
    }
    fs::path logic = folder;
    if (!task.isEmpty()) {
        const QString taskTarget = QStringLiteral("task:") + task;
        if (draft && drafted(call.client, taskTarget)) {
            logic = m_setup.drafts->folderOf(call.client, taskTarget);
            usedDraft = true;
        } else if (const TaskEntry* entry = m_setup.tasks != nullptr ? m_setup.tasks->find(task) : nullptr) {
            logic = toPath(entry->folder);
        } else {
            reply(failure(QStringLiteral("GS-MCP-019"), QStringLiteral("There is no task %1; list_tasks lists them").arg(task)));
            return;
        }
    }
    fs::path componentDraft;
    if (!component.isEmpty()) {
        const QString componentTarget = QStringLiteral("component:") + component;
        if (!drafted(call.client, componentTarget)) {
            reply(failure(QStringLiteral("GS-MCP-014"),
                          QStringLiteral("This client has no draft of the component %1; write one with write_component")
                              .arg(component)));
            return;
        }
        componentDraft = m_setup.drafts->folderOf(call.client, componentTarget);
    }
    if (draft && !usedDraft && component.isEmpty()) {
        reply(failure(QStringLiteral("GS-MCP-014"),
                      QStringLiteral("This client has no draft of the profile or the task given; leave draft out to "
                                     "replay the versions in use")));
        return;
    }
    std::string profileJson = profileText.toStdString();
    if (profileJson.empty() && (!profileFile.empty() || !folder.empty())) {
        const fs::path file = !profileFile.empty() ? profileFile : folder / "profile.json";
        std::string contents;
        if (fs::is_regular_file(file, error) &&
            !platform::readFile(file, kMostProfileBytes, contents, platform::WhenBusy::GiveUp)) {
            profileJson = std::move(contents);
        }
    }
    ReplayPlan plan{.session = session,
                    .untilMs = until.value_or(kDefaultUntilMs),
                    .seed = static_cast<std::uint64_t>(seed.value_or(1)),
                    .profileJson = profileJson,
                    .components = componentModules(profileJson, component, componentDraft),
                    .trace = trace};
    if (code.isEmpty()) {
        const std::optional<std::string> entry = logic.empty() ? std::nullopt : entryOf(logic);
        if (!entry && plan.components.empty()) {
            reply(failure(QStringLiteral("GS-CHECK-006"),
                          logic.empty() ? QStringLiteral("%1 is a profile without logic; give code to try some").arg(profile)
                          : !task.isEmpty() ? QStringLiteral("the task folder has no task.ts")
                          : fs::is_regular_file(logic / "profile.json", error)
                              ? QStringLiteral("the profile folder has no logic.ts or logic.js")
                              : QStringLiteral("the project has no src/main.ts or src/main.js")));
            return;
        }
        plan.folder = logic;
        plan.entry = entry.value_or(std::string("logic.ts"));
    } else {
        auto scratch = std::make_shared<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("gs-mcp-XXXXXX")));
        if (!scratch->isValid()) {
            reply(failure(QStringLiteral("GS-MCP-010"), QStringLiteral("A temporary folder for the code cannot be made")));
            return;
        }
        const fs::path root = toPath(scratch->path());
        if (!folder.empty()) {
            copyResources(folder, root);
        }
        const bool withProfile = !profileJson.empty();
        plan.entry = withProfile ? "logic.ts" : "src/main.ts";
        std::error_code written;
        if (withProfile) {
            written = platform::writeFileAtomically(root / "profile.json", profileJson);
        }
        if (!written) {
            fs::create_directories((root / utf8Path(plan.entry)).parent_path(), written);
        }
        if (!written) {
            written = platform::writeFileAtomically(root / utf8Path(plan.entry), code.toStdString());
        }
        if (written) {
            reply(failure(QStringLiteral("GS-MCP-010"), QStringLiteral("The code cannot be written to a temporary folder")));
            return;
        }
        plan.folder = root;
        plan.scratch = std::move(scratch);
    }
    m_waiting[call.id] = Waiting{.reply = std::move(reply)};
    later(QPointer<McpTools>(this), call.id, [plan] { return replayed(plan); },
          [](McpTools& tools, quint64 done, McpOutcome outcome) { tools.finish(done, std::move(outcome)); });
}

std::vector<script::ComponentModule> McpTools::componentModules(const std::string& profileJson, const QString& drafted,
                                                                const std::filesystem::path& draftFolder) const
{
    std::vector<script::ComponentModule> modules;
    if (profileJson.empty()) {
        return modules;
    }
    const input::LoadResult loaded = input::loadProfile(profileJson);
    if (!loaded.ok()) {
        return modules;
    }
    const ComponentLibrary* library = m_setup.components;
    const std::string entryName = ComponentLibrary::entryName().toStdString();
    const auto listed = [&modules](const std::string& ref) {
        return std::ranges::any_of(modules, [&ref](const script::ComponentModule& each) { return each.ref == ref; });
    };
    for (const input::Control& control : loaded.profile.controls) {
        const std::string& ref = input::componentRef(control);
        if (ref.empty() || listed(ref)) {
            continue;
        }
        const QString name = QString::fromStdString(ref);
        const ComponentEntry* entry = library != nullptr ? library->find(name) : nullptr;
        script::ComponentModule placed{.ref = ref, .entry = entryName};
        if (name == drafted) {
            placed.folder = draftFolder;
        } else if (entry != nullptr) {
            placed.folder = library->folderOf(name);
        } else {
            continue;
        }
        if (entry != nullptr) {
            for (const ComponentChild& child : entry->children) {
                placed.children.push_back(child.name.toStdString());
            }
        }
        modules.push_back(std::move(placed));
    }
    if (!modules.empty() && library != nullptr) {
        for (const ComponentEntry& entry : library->entries()) {
            const std::string ref = entry.ref.toStdString();
            if (!listed(ref)) {
                modules.push_back(script::ComponentModule{.ref = ref,
                                                          .folder = entry.ref == drafted ? draftFolder
                                                                                         : library->folderOf(entry.ref),
                                                          .entry = entryName,
                                                          .placed = false});
            }
        }
    }
    return modules;
}

void McpTools::getFrame(quint64 call, const QJsonObject& given, Reply reply)
{
    Arguments args(given, {u"serial", u"maxSide"});
    const QString serial = args.text(u"serial");
    const std::optional<int> maxSide = args.whole(u"maxSide", 16, 8192);
    if (!args.ok()) {
        reply(args.problem());
        return;
    }
    McpOutcome problem;
    const std::optional<McpPhone> phone = phoneFor(serial, problem);
    if (!phone) {
        reply(problem);
        return;
    }
    Waiting& waiting = m_waiting[call];
    waiting = Waiting{.reply = std::move(reply), .serial = phone->serial, .phone = phone->name,
                      .deadline = m_clock.now() + std::chrono::duration_cast<Microseconds>(kScreenshotWait),
                      .screenshot = true, .maxSide = maxSide.value_or(0)};
    schedule();
    const QPointer<McpTools> self(this);
    const bool asked = m_setup.devices.screenshot &&
                       m_setup.devices.screenshot(phone->serial, [self, call](const QImage& image) {
                           if (self == nullptr) {
                               return;
                           }
                           const auto found = self->m_waiting.find(call);
                           if (found == self->m_waiting.end() || !found->second.screenshot) {
                               return;
                           }
                           found->second.screenshot = false;
                           found->second.deadline.reset();
                           self->schedule();
                           if (image.isNull()) {
                               self->captureFrame(call);
                               return;
                           }
                           const int side = found->second.maxSide;
                           later(self, call, [image, side] { return pictureOutcome(image, true, side); },
                                 [](McpTools& tools, quint64 done, McpOutcome outcome) {
                                     tools.finish(done, std::move(outcome));
                                 });
                       });
    if (asked) {
        return;
    }
    const auto found = m_waiting.find(call);
    if (found != m_waiting.end() && found->second.screenshot) {
        found->second.screenshot = false;
        found->second.deadline.reset();
        schedule();
        captureFrame(call);
    }
}

void McpTools::captureFrame(quint64 call)
{
    const auto found = m_waiting.find(call);
    if (found == m_waiting.end()) {
        return;
    }
    const McpFrameCapture capture = m_setup.devices.frames ? m_setup.devices.frames(found->second.serial) : McpFrameCapture{};
    if (!capture) {
        finish(call, failure(QStringLiteral("GS-MCP-012"), QStringLiteral("There is no picture of %1 yet").arg(found->second.phone)));
        return;
    }
    const int side = found->second.maxSide;
    const QString phone = found->second.phone;
    later(QPointer<McpTools>(this), call,
          [capture, side, phone] {
              const std::shared_ptr<const media::LumaSnapshot> shot = capture();
              if (shot == nullptr) {
                  return failure(QStringLiteral("GS-MCP-012"), QStringLiteral("%1 has shown no picture yet").arg(phone));
              }
              return pictureOutcome(imageOf(*shot), false, side);
          },
          [](McpTools& tools, quint64 done, McpOutcome outcome) { tools.finish(done, std::move(outcome)); });
}

void McpTools::findImage(quint64 call, const QJsonObject& given, Reply reply)
{
    Arguments args(given, {u"serial", u"image", u"png", u"region", u"threshold", u"all", u"max", u"match"});
    const QString serial = args.text(u"serial");
    const QString image = args.text(u"image");
    const QString png = args.text(u"png");
    const QJsonObject region = args.object(u"region");
    const std::optional<double> threshold = args.number(u"threshold", 0.0, 1.0);
    const std::optional<bool> all = args.flag(u"all");
    const std::optional<int> most = args.whole(u"max", 1, 64);
    const QString match = args.text(u"match");
    if (args.ok() && image.isEmpty() == png.isEmpty()) {
        args.complain(QStringLiteral("give either image or png"));
    }
    if (!args.ok()) {
        reply(args.problem());
        return;
    }
    if (!match.isEmpty() && match != u"grey" && match != u"color") {
        reply(failure(QStringLiteral("GS-API-019"), QStringLiteral("match must be \"color\" or \"grey\"")));
        return;
    }
    TemplateSpec spec;
    if (!region.isEmpty() || given.contains(QStringLiteral("region"))) {
        const auto edge = [&region](const char* name, double& out) {
            const QJsonValue value = region.value(QLatin1StringView(name));
            out = value.toDouble(-1.0);
            return value.isDouble() && std::isfinite(out);
        };
        script::Region area;
        const bool read = edge("x", area.x) && edge("y", area.y) && edge("width", area.width) && edge("height", area.height);
        constexpr double kSlack = 1e-9;
        if (!read || region.size() != 4 || area.x < 0.0 || area.y < 0.0 || area.width <= 0.0 || area.height <= 0.0 ||
            area.x + area.width > 1.0 + kSlack || area.y + area.height > 1.0 + kSlack) {
            reply(failure(QStringLiteral("GS-API-002"), QStringLiteral("the region must be an object inside the picture")));
            return;
        }
        spec.region = area;
    }
    McpOutcome problem;
    const std::optional<McpPhone> phone = phoneFor(serial, problem);
    if (!phone) {
        reply(problem);
        return;
    }
    if (!png.isEmpty()) {
        const QByteArray::FromBase64Result decoded =
            QByteArray::fromBase64Encoding(png.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (decoded) {
            static_cast<void>(spec.given.loadFromData(*decoded));
        }
        if (spec.given.isNull()) {
            reply(failure(QStringLiteral("GS-API-016"), QStringLiteral("the given picture cannot be read as a picture")));
            return;
        }
        spec.label = "the given picture";
    } else {
        if (phone->folder.empty()) {
            reply(failure(QStringLiteral("GS-MCP-006"),
                          QStringLiteral("No profile folder is in use on %1, so %2 cannot be found; give png instead")
                              .arg(phone->name, image)));
            return;
        }
        const std::string name = image.toStdString();
        if (script::resourceName(name)) {
            const script::LoadedResources resources =
                script::loadResources(phone->folder / utf8Path(script::kResourcesFile));
            if (!resources) {
                reply(failure(QStringLiteral("GS-API-020"),
                              QStringLiteral("res/resources.json cannot be used: %1")
                                  .arg(resources.error().empty() ? QStringLiteral("it is not valid")
                                                                 : toQString(resources.error().front()))));
                return;
            }
            const script::TemplateResource* named = resources->findTemplate(name);
            if (named == nullptr) {
                reply(failure(QStringLiteral("GS-API-018"), QStringLiteral("there is no template named \"%1\"").arg(image)));
                return;
            }
            spec.file = phone->folder / "res" / utf8Path(named->file);
            spec.threshold = named->threshold;
            spec.color = named->match == script::MatchMode::Color;
            spec.source = named->source;
            if (!named->region.empty()) {
                const script::RegionResource* area = resources->findRegion(named->region);
                if (area == nullptr) {
                    reply(failure(QStringLiteral("GS-API-018"),
                                  QStringLiteral("there is no region named \"%1\"").arg(toQString(named->region))));
                    return;
                }
                if (!given.contains(QStringLiteral("region"))) {
                    spec.region = area->area;
                }
            }
            spec.label = name;
        } else if (picturePath(image)) {
            spec.file = phone->folder / toPath(QDir::fromNativeSeparators(image));
            spec.label = name;
        } else {
            reply(failure(QStringLiteral("GS-API-015"),
                          QStringLiteral("the image must be a template name or a picture inside the profile folder, "
                                         "such as \"res/chest.png\"")));
            return;
        }
    }
    if (threshold) {
        spec.threshold = *threshold;
    }
    if (!match.isEmpty()) {
        spec.color = match == u"color";
    }
    spec.all = all.value_or(false);
    spec.most = most.value_or(kFindMost);
    const McpFrameCapture capture = m_setup.devices.frames ? m_setup.devices.frames(phone->serial) : McpFrameCapture{};
    if (!capture) {
        reply(failure(QStringLiteral("GS-MCP-012"), QStringLiteral("There is no picture of %1 yet").arg(phone->name)));
        return;
    }
    m_waiting[call] = Waiting{.reply = std::move(reply), .serial = phone->serial, .phone = phone->name};
    later(QPointer<McpTools>(this), call,
          [capture, spec, name = phone->name] {
              const std::shared_ptr<const media::LumaSnapshot> shot = capture();
              if (shot == nullptr) {
                  return failure(QStringLiteral("GS-MCP-012"), QStringLiteral("%1 has shown no picture yet").arg(name));
              }
              return searched(*shot, spec);
          },
          [](McpTools& tools, quint64 done, McpOutcome outcome) { tools.finish(done, std::move(outcome)); });
}

void McpTools::runScript(const McpCall& call, Reply reply)
{
    Arguments args(call.arguments, {u"serial", u"code", u"function", u"timeoutMs"});
    const QString serial = args.text(u"serial");
    const QString code = args.text(u"code");
    const QString function = args.text(u"function");
    const std::optional<double> timeout = args.number(u"timeoutMs", 100.0, 60'000.0);
    if (args.ok() && code.isEmpty() == function.isEmpty()) {
        args.complain(QStringLiteral("give either code or function"));
    }
    if (args.ok() && code.toUtf8().size() > kMostCodeBytes) {
        args.complain(QStringLiteral("code is longer than 256 KiB"));
    }
    if (args.ok() && !function.isEmpty() && !identifier(function)) {
        args.complain(QStringLiteral("function must be the name of an exported function, such as start"));
    }
    if (!args.ok()) {
        reply(args.problem());
        return;
    }
    McpOutcome problem;
    const std::optional<McpPhone> phone = phoneFor(serial, problem);
    if (!phone) {
        reply(problem);
        return;
    }
    std::string entry;
    QString shownCode = code;
    if (!function.isEmpty()) {
        entry = phone->folder.empty() ? std::string() : logicEntryOf(phone->folder);
        if (entry.empty()) {
            reply(failure(QStringLiteral("GS-MCP-006"),
                          QStringLiteral("The profile in use on %1 has no logic, so it has no function %2")
                              .arg(phone->name, function)));
            return;
        }
        QString unread;
        shownCode = readText(phone->folder / utf8Path(entry), kMostCodeBytes, unread);
        if (!unread.isEmpty()) {
            shownCode = QStringLiteral("(%1 cannot be shown: %2)").arg(toQString(entry), unread);
        }
    }
    const auto limit = timeout ? std::chrono::milliseconds(std::llround(*timeout))
                               : std::chrono::duration_cast<std::chrono::milliseconds>(kRunLimit);
    if (m_quiet.contains({call.client, phone->serial})) {
        qInfo("mcp %s: run_script on %s without asking; the user allowed it until GameScrcpy closes",
              qUtf8Printable(call.client), qUtf8Printable(phone->name));
        m_waiting[call.id] = Waiting{.reply = std::move(reply), .serial = phone->serial, .phone = phone->name, .limit = limit};
        startRun(call.id, call.client, *phone, code, function, entry);
        return;
    }
    McpConfirmation shown{.kind = QStringLiteral("run"),
                          .details = {{QStringLiteral("phone"), phone->name},
                                      {QStringLiteral("serial"), phone->serial},
                                      {QStringLiteral("profile"), phone->profileName},
                                      {QStringLiteral("code"), shownCode},
                                      {QStringLiteral("functionName"), function},
                                      {QStringLiteral("file"), toQString(entry)},
                                      {QStringLiteral("quiet"), true},
                                      {QStringLiteral("seconds"),
                                       static_cast<int>((limit.count() + 999) / 1000)}}};
    if (!call.confirm) {
        reply(failure(QStringLiteral("GS-MCP-008"), QStringLiteral("Nobody can confirm the run, so the script did not run")));
        return;
    }
    m_waiting[call.id] = Waiting{.reply = std::move(reply), .serial = phone->serial, .phone = phone->name, .limit = limit};
    const quint64 callId = call.id;
    const QString client = call.client;
    call.confirm(std::move(shown), [this, callId, client, phone = *phone, code, function, entry](McpAnswer answer) {
        if (!m_waiting.contains(callId)) {
            return;
        }
        if (answer == McpAnswer::Refused) {
            finish(callId, failure(QStringLiteral("GS-MCP-008"), QStringLiteral("The user refused to run the script")));
            return;
        }
        if (answer == McpAnswer::Unanswered) {
            finish(callId, failure(QStringLiteral("GS-MCP-009"), QStringLiteral("The user did not answer within two "
                                                                                "minutes, so the script did not run")));
            return;
        }
        if (answer == McpAnswer::AllowedForAWhile) {
            m_quiet[{client, phone.serial}] = phone.name;
            qInfo("mcp %s: may run scripts on %s without asking until GameScrcpy closes", qUtf8Printable(client),
                  qUtf8Printable(phone.name));
        }
        startRun(callId, client, phone, code, function, entry);
    });
}

void McpTools::startRun(quint64 call, const QString& client, const McpPhone& phone, const QString& code,
                        const QString& function, const std::string& entry)
{
    auto found = m_waiting.find(call);
    if (found == m_waiting.end()) {
        return;
    }
    ScriptJob job;
    job.name = QStringLiteral("MCP %1").arg(client);
    if (!code.isEmpty()) {
        auto scratch = std::make_shared<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("gs-mcp-XXXXXX")));
        const fs::path root = scratch->isValid() ? toPath(scratch->path()) : fs::path();
        if (!root.empty() && !phone.folder.empty()) {
            copyResources(phone.folder, root);
        }
        const QString module = exportsDefault(code) ? code : code + QStringLiteral("\nexport default async function () {}\n");
        if (root.empty() || platform::writeFileAtomically(root / "main.ts", module.toStdString())) {
            finish(call, failure(QStringLiteral("GS-MCP-010"), QStringLiteral("The script cannot be written to a "
                                                                              "temporary folder")));
            return;
        }
        found->second.scratch = std::move(scratch);
        job.folder = root;
        job.entry = "main.ts";
    } else {
        job.folder = phone.folder;
        job.entry = entry;
        job.handler = function.toStdString();
    }
    job.reported = [this, call](const LogicReport& report) {
        const auto running = m_waiting.find(call);
        if (running == m_waiting.end()) {
            return;
        }
        if (running->second.console.size() >= kMostConsoleLines) {
            ++running->second.dropped;
            return;
        }
        running->second.console.append(consoleLine(report));
    };
    job.ended = [this, call](const QString& result, const QString& message) { ended(call, result, message); };
    quint64 number = 0;
    const QString refused = m_setup.devices.startScript
                                ? m_setup.devices.startScript(phone.serial, std::move(job), number)
                                : QStringLiteral("Scripts cannot run on phones here");
    found = m_waiting.find(call);
    if (found == m_waiting.end()) {
        return;
    }
    if (!refused.isEmpty()) {
        finish(call, failure(QStringLiteral("GS-MCP-005"), refused));
        return;
    }
    found->second.run = number;
    found->second.deadline = m_clock.now() + std::chrono::duration_cast<Microseconds>(found->second.limit);
    schedule();
}

void McpTools::ended(quint64 call, const QString& result, const QString& message)
{
    const auto found = m_waiting.find(call);
    if (found == m_waiting.end()) {
        return;
    }
    Waiting& waiting = found->second;
    QStringList lines = waiting.console;
    if (waiting.dropped > 0) {
        lines.append(QStringLiteral("(%1 more lines were left out)").arg(waiting.dropped));
    }
    const QString output = lines.join(u'\n');
    if (result == u"done") {
        finish(call, success(output.isEmpty() ? QStringLiteral("The script finished without output.")
                                              : output + QStringLiteral("\nThe script finished.")));
        return;
    }
    QString summary;
    if (waiting.timedOut) {
        summary = QStringLiteral("The script was stopped at its time limit of %1 ms; its fingers were lifted")
                      .arg(waiting.limit.count());
    } else if (result == u"failed") {
        summary = QStringLiteral("The script failed: %1").arg(message);
    } else if (result == u"disconnected") {
        summary = QStringLiteral("The script was stopped because %1 disconnected").arg(waiting.phone);
    } else {
        summary = QStringLiteral("The script was stopped");
    }
    finish(call, failure(QStringLiteral("GS-MCP-013"), output.isEmpty() ? summary : summary + u'\n' + output));
}

void McpTools::startRecording(const McpCall& call, Reply reply)
{
    Arguments args(call.arguments, {u"serial"});
    const QString serial = args.text(u"serial");
    if (!args.ok()) {
        reply(args.problem());
        return;
    }
    McpOutcome problem;
    const std::optional<McpPhone> phone = phoneFor(serial, problem);
    if (!phone) {
        reply(problem);
        return;
    }
    if (m_setup.devices.recording && m_setup.devices.recording(phone->serial)) {
        reply(success(QStringLiteral("%1 is being recorded already; stop_recording finishes it.").arg(phone->name)));
        return;
    }
    if (!m_setup.devices.startRecording) {
        reply(failure(QStringLiteral("GS-MCP-005"), QStringLiteral("Phones cannot be recorded here")));
        return;
    }
    if (!call.confirm) {
        reply(failure(QStringLiteral("GS-MCP-008"), QStringLiteral("Nobody can confirm the recording, so it did not start")));
        return;
    }
    const McpConfirmation shown{.kind = QStringLiteral("record"),
                                .details = {{QStringLiteral("phone"), phone->name}, {QStringLiteral("serial"), phone->serial}}};
    m_waiting[call.id] = Waiting{.reply = std::move(reply), .serial = phone->serial, .phone = phone->name};
    const quint64 callId = call.id;
    call.confirm(shown, [this, callId, serial = phone->serial, name = phone->name](McpAnswer answer) {
        if (!m_waiting.contains(callId)) {
            return;
        }
        if (answer == McpAnswer::Refused) {
            finish(callId, failure(QStringLiteral("GS-MCP-008"), QStringLiteral("The user refused to record %1").arg(name)));
            return;
        }
        if (answer == McpAnswer::Unanswered) {
            finish(callId, failure(QStringLiteral("GS-MCP-009"), QStringLiteral("The user did not answer within two "
                                                                                "minutes, so %1 is not recorded")
                                                                     .arg(name)));
            return;
        }
        const QString refused = m_setup.devices.startRecording(serial);
        if (!refused.isEmpty()) {
            finish(callId, failure(QStringLiteral("GS-MCP-005"), refused));
            return;
        }
        finish(callId, success(QStringLiteral("Recording %1. Do what the recording should hold, then call stop_recording; "
                                              "its folder works as the session of run_replay.")
                                   .arg(name)));
    });
}

void McpTools::requestMaterials(const McpCall& call, Reply reply)
{
    Arguments args(call.arguments, {u"serial", u"profile", u"items"});
    const QString serial = args.text(u"serial");
    const QString profile = args.text(u"profile");
    const QJsonArray given = args.array(u"items", true);
    if (args.ok() && (given.isEmpty() || given.size() > kMostMaterials)) {
        args.complain(QStringLiteral("items must hold from 1 to %1 items").arg(kMostMaterials));
    }
    if (!args.ok()) {
        reply(args.problem());
        return;
    }
    std::vector<McpMaterialItem> items;
    for (qsizetype index = 0; index < given.size(); ++index) {
        const QJsonValue value = given.at(index);
        const QString where = QStringLiteral("item %1").arg(index + 1);
        if (!value.isObject()) {
            reply(failure(QStringLiteral("GS-MCP-004"), QStringLiteral("%1 must be an object with name, kind and note").arg(where)));
            return;
        }
        const QJsonObject entry = value.toObject();
        for (auto at = entry.begin(); at != entry.end(); ++at) {
            if (at.key() != u"name" && at.key() != u"kind" && at.key() != u"note") {
                reply(failure(QStringLiteral("GS-MCP-004"),
                              QStringLiteral("%1 has no argument named %2").arg(where, at.key())));
                return;
            }
            if (!at.value().isString()) {
                reply(failure(QStringLiteral("GS-MCP-004"), QStringLiteral("the %1 of %2 must be text").arg(at.key(), where)));
                return;
            }
        }
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
        const QString kind = entry.value(QStringLiteral("kind")).toString().trimmed();
        const QString note = entry.value(QStringLiteral("note")).toString().trimmed();
        if (name.isEmpty() || script::characterCount(name.toStdString()) > script::kMostResourceNameChars) {
            reply(failure(QStringLiteral("GS-MCP-004"),
                          QStringLiteral("%1 needs a name of 1 to %2 characters")
                              .arg(where)
                              .arg(static_cast<qulonglong>(script::kMostResourceNameChars))));
            return;
        }
        if (kind != u"template" && kind != u"color" && kind != u"region" && kind != u"point") {
            reply(failure(QStringLiteral("GS-MCP-004"),
                          QStringLiteral("the kind of %1 must be template, color, region or point").arg(where)));
            return;
        }
        if (std::ranges::any_of(items, [&name](const McpMaterialItem& item) { return item.name.compare(name, Qt::CaseInsensitive) == 0; })) {
            reply(failure(QStringLiteral("GS-MCP-004"), QStringLiteral("two items are named %1").arg(name)));
            return;
        }
        items.push_back({.name = name, .kind = kind, .note = note});
    }
    if (m_setup.drafts == nullptr) {
        reply(failure(QStringLiteral("GS-MCP-010"), QStringLiteral("Drafts cannot be kept in this run")));
        return;
    }
    if (!m_setup.devices.materials) {
        reply(failure(QStringLiteral("GS-MCP-025"), QStringLiteral("The vision workbench cannot be opened in this run")));
        return;
    }
    for (const auto& entry : m_waiting) {
        if (entry.second.materials) {
            reply(failure(QStringLiteral("GS-MCP-024"),
                          QStringLiteral("Another client is already asking the user for materials; call again when the "
                                         "user is done with it")));
            return;
        }
    }
    McpOutcome unknown;
    const std::optional<McpPhone> phone = phoneFor(serial, unknown);
    if (!phone) {
        reply(unknown);
        return;
    }
    QString wanted;
    if (!profile.isEmpty()) {
        const QString target = mcpTargetOf(profile);
        if (!target.startsWith(QStringLiteral("profile:"))) {
            reply(failure(QStringLiteral("GS-MCP-004"), QStringLiteral("profile must be a profile id such as shooter")));
            return;
        }
        wanted = target.section(u':', 1);
    } else {
        wanted = phone->profileId;
    }
    const ProfileEntry* entry = m_setup.profiles != nullptr ? m_setup.profiles->find(wanted) : nullptr;
    if (entry == nullptr) {
        reply(failure(QStringLiteral("GS-MCP-006"),
                      wanted.isEmpty()
                          ? QStringLiteral("%1 uses no profile, so there is nowhere to keep the materials; give profile")
                                .arg(phone->name)
                          : QStringLiteral("There is no profile with the id %1; list_profiles lists them").arg(wanted)));
        return;
    }
    const QString target = QStringLiteral("profile:") + wanted;
    McpDraftResult opened;
    const fs::path folder = m_setup.drafts->open(call.client, target, opened);
    if (folder.empty()) {
        reply(failure(opened.code, opened.problem));
        return;
    }
    const QString name = entry->name;
    McpMaterialsRequest request{.client = call.client,
                                .serial = phone->serial,
                                .profileId = wanted,
                                .profileName = name,
                                .folder = folder,
                                .items = items};
    m_waiting[call.id] = Waiting{.reply = std::move(reply),
                                 .serial = phone->serial,
                                 .phone = phone->name,
                                 .deadline = m_clock.now() + std::chrono::duration_cast<Microseconds>(kMaterialsLimit),
                                 .materials = true,
                                 .client = call.client};
    schedule();
    const quint64 callId = call.id;
    const QPointer<McpTools> self(this);
    const QString refused = m_setup.devices.materials(request, [self, callId, target, name](const std::vector<McpMaterialResult>& results) {
        if (self.isNull() || !self->m_waiting.contains(callId)) {
            return;
        }
        QJsonArray list;
        int done = 0;
        int skipped = 0;
        for (const McpMaterialResult& result : results) {
            done += result.state == u"done" || result.state == u"existing" ? 1 : 0;
            skipped += result.state == u"skipped" ? 1 : 0;
            QJsonObject item{{QStringLiteral("name"), result.name},
                             {QStringLiteral("kind"), result.kind},
                             {QStringLiteral("state"), result.state}};
            if (!result.resource.isEmpty()) {
                item.insert(QStringLiteral("resource"), result.resource);
            }
            if (!result.value.isEmpty()) {
                item.insert(QStringLiteral("value"), QJsonObject::fromVariantMap(result.value));
            }
            list.append(item);
        }
        QString note = QStringLiteral("They are in this client's draft of %1, in res/resources.json and its pictures; "
                                      "nothing changed for the user until the draft is applied. Use the resource names "
                                      "above in the logic, such as vision.find(\"start button\").")
                           .arg(name);
        if (skipped > 0) {
            note += QStringLiteral(" The user skipped %1 of %2 items; ask about those instead of guessing them.")
                        .arg(skipped)
                        .arg(static_cast<int>(results.size()));
        }
        if (done == 0) {
            note += QStringLiteral(" The user answered none of the items, so ask which game, which screen and what "
                                   "should happen before writing code.");
        }
        self->finish(callId, success(jsonText(QJsonObject{{QStringLiteral("target"), target},
                                                    {QStringLiteral("name"), name},
                                                    {QStringLiteral("draft"), true},
                                                    {QStringLiteral("items"), list},
                                                    {QStringLiteral("note"), note}})));
    });
    if (!refused.isEmpty() && m_waiting.contains(call.id)) {
        auto node = m_waiting.extract(call.id);
        schedule();
        if (node.mapped().reply) {
            node.mapped().reply(failure(QStringLiteral("GS-MCP-025"), refused));
        }
    }
}

McpOutcome McpTools::stopRecording(const McpCall& call)
{
    Arguments args(call.arguments, {u"serial"});
    const QString serial = args.text(u"serial");
    if (!args.ok()) {
        return args.problem();
    }
    McpOutcome problem;
    const std::optional<McpPhone> phone = phoneFor(serial, problem);
    if (!phone) {
        return problem;
    }
    const QVariantMap info = m_setup.devices.stopRecording ? m_setup.devices.stopRecording(phone->serial) : QVariantMap();
    if (info.isEmpty()) {
        return failure(QStringLiteral("GS-MCP-017"),
                       QStringLiteral("%1 is not being recorded; start_recording starts a recording").arg(phone->name));
    }
    const QString folder = QDir::fromNativeSeparators(info.value(QStringLiteral("folder")).toString());
    QJsonObject result{{QStringLiteral("id"), info.value(QStringLiteral("name")).toString()},
                       {QStringLiteral("folder"), folder},
                       {QStringLiteral("session"), folder},
                       {QStringLiteral("duration"), info.value(QStringLiteral("duration")).toString()},
                       {QStringLiteral("video"), info.value(QStringLiteral("video")).toBool()}};
    if (const QString broken = info.value(QStringLiteral("problem")).toString(); !broken.isEmpty()) {
        result.insert(QStringLiteral("problem"), broken);
    }
    return success(jsonText(result));
}

McpOutcome McpTools::listRecordings(const QJsonObject& given) const
{
    Arguments args(given, {});
    if (!args.ok()) {
        return args.problem();
    }
    const QString location = m_setup.devices.recordingsFolder ? m_setup.devices.recordingsFolder() : QString();
    QJsonArray list;
    if (!location.isEmpty()) {
        const QFileInfoList folders =
            QDir(location).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Time);
        for (const QFileInfo& info : folders) {
            if (list.size() >= kMostListedRecordings) {
                break;
            }
            const fs::path path = toPath(info.absoluteFilePath());
            if (!record::isRecordingFolder(path)) {
                continue;
            }
            std::error_code error;
            list.append(QJsonObject{{QStringLiteral("id"), info.fileName()},
                                    {QStringLiteral("folder"), QDir::fromNativeSeparators(info.absoluteFilePath())},
                                    {QStringLiteral("made"), info.lastModified().toString(Qt::ISODate)},
                                    {QStringLiteral("video"), fs::is_regular_file(path / utf8Path(record::kVideoFile), error)}});
        }
    }
    return success(jsonText(QJsonObject{{QStringLiteral("folder"), QDir::fromNativeSeparators(location)},
                                        {QStringLiteral("recordings"), list}}));
}

McpOutcome McpTools::proposeKeymap(const McpCall& call)
{
    Arguments args(call.arguments, {u"serial", u"profile", u"controls"});
    const QString serial = args.text(u"serial");
    const QString profile = args.text(u"profile");
    const QJsonArray given = args.array(u"controls", true);
    if (args.ok() && (given.isEmpty() || given.size() > kMostProposedControls)) {
        args.complain(QStringLiteral("controls must hold from 1 to %1 controls").arg(kMostProposedControls));
    }
    if (!args.ok()) {
        return args.problem();
    }
    QJsonArray cleaned;
    std::vector<std::optional<input::Vec2>> positions;
    for (qsizetype index = 0; index < given.size(); ++index) {
        if (!given.at(index).isObject()) {
            return failure(QStringLiteral("GS-MCP-004"), QStringLiteral("controls[%1] must be an object").arg(index));
        }
        QJsonObject control = given.at(index).toObject();
        std::optional<input::Vec2> at;
        if (control.contains(QStringLiteral("x")) || control.contains(QStringLiteral("y"))) {
            const double x = control.value(QStringLiteral("x")).toDouble(-1.0);
            const double y = control.value(QStringLiteral("y")).toDouble(-1.0);
            if (!(x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0)) {
                return failure(QStringLiteral("GS-MCP-004"),
                               QStringLiteral("controls[%1]: x and y must both be ratios of the picture, 0 to 1").arg(index));
            }
            at = input::Vec2{x, y};
            control.remove(QStringLiteral("x"));
            control.remove(QStringLiteral("y"));
            if (!control.contains(QStringLiteral("anchor"))) {
                control.insert(QStringLiteral("anchor"), QStringLiteral("center"));
            }
            if (!control.contains(QStringLiteral("offset"))) {
                control.insert(QStringLiteral("offset"), QJsonArray{0, 0});
            }
        }
        if (!control.contains(QStringLiteral("id"))) {
            const QString type = control.value(QStringLiteral("type")).toString();
            control.insert(QStringLiteral("id"),
                           QStringLiteral("%1%2").arg(identifier(type) ? type : QStringLiteral("control")).arg(index + 1));
        }
        cleaned.append(control);
        positions.push_back(at);
    }
    const QJsonObject wrapper{{QStringLiteral("format"), QStringLiteral("gamescrcpy.profile")},
                              {QStringLiteral("version"), 3},
                              {QStringLiteral("controls"), cleaned}};
    const input::LoadResult loaded =
        input::loadProfile(QJsonDocument(wrapper).toJson(QJsonDocument::Compact).toStdString());
    QStringList errors;
    for (const input::Diagnostic& diagnostic : loaded.diagnostics) {
        if (diagnostic.severity == input::Severity::Error) {
            errors.append(QStringLiteral("error GS-CHECK-005 %1")
                              .arg(toQString(diagnostic.detail.empty() ? diagnostic.path
                                                                       : diagnostic.path + ": " + diagnostic.detail)));
        }
    }
    if (!errors.isEmpty() || loaded.profile.controls.size() != positions.size()) {
        return failure(QStringLiteral("GS-CHECK-005"),
                       QStringLiteral("The controls have problems, so nothing was shown:\n") + errors.join(u'\n'));
    }
    QString chosen = serial;
    if (chosen.isEmpty() && profile.isEmpty()) {
        McpOutcome problem;
        const std::optional<McpPhone> phone = phoneFor(serial, problem);
        if (!phone) {
            return problem;
        }
        chosen = phone->serial;
    }
    QString refused;
    ProfileEditor* editor = m_setup.devices.openEditor ? m_setup.devices.openEditor(chosen, profile, refused) : nullptr;
    if (editor == nullptr) {
        return failure(QStringLiteral("GS-MCP-005"),
                       refused.isEmpty() ? QStringLiteral("The keymap editor cannot be opened here") : refused);
    }
    std::vector<ProfileEditor::Suggested> suggested;
    Proposal proposal{.client = call.client, .phone = chosen, .editor = editor};
    for (std::size_t index = 0; index < positions.size(); ++index) {
        const input::Control& control = loaded.profile.controls[index];
        proposal.names.append(QString::fromStdString(control.name));
        proposal.types.append(toQString(input::kControlTypeNames[control.kind.index()]));
        proposal.states.append(QStringLiteral("undecided"));
        proposal.controls.append(QString());
        suggested.push_back({.control = control, .at = positions[index]});
    }
    proposal.suggestions = editor->suggest(std::move(suggested));
    const QString id = QStringLiteral("k%1").arg(++m_lastProposal);
    connect(editor, &ProfileEditor::suggestionDecided, this,
            [this, id](const QString& suggestion, const QString& state, const QString& control) {
                const auto found = m_proposals.find(id);
                if (found == m_proposals.end()) {
                    return;
                }
                const qsizetype index = found->second.suggestions.indexOf(suggestion);
                if (index >= 0) {
                    found->second.states[index] = state;
                    found->second.controls[index] = control;
                }
            });
    QJsonArray shown;
    for (qsizetype index = 0; index < proposal.suggestions.size(); ++index) {
        shown.append(QJsonObject{{QStringLiteral("index"), static_cast<int>(index)},
                                 {QStringLiteral("name"), proposal.names.value(index)},
                                 {QStringLiteral("type"), proposal.types.value(index)}});
    }
    m_proposals.insert_or_assign(id, proposal);
    qInfo("mcp %s: suggested %lld control(s) in the keymap editor", qUtf8Printable(call.client),
          static_cast<long long>(proposal.suggestions.size()));
    return success(jsonText(QJsonObject{{QStringLiteral("id"), id},
                                        {QStringLiteral("shown"), shown},
                                        {QStringLiteral("note"),
                                         QStringLiteral("Shown to the user as dashed hints in the keymap editor. The user "
                                                        "accepts or rejects each one; ask keymap_draft_status with this id "
                                                        "for the choices.")}}));
}

McpOutcome McpTools::keymapStatus(const McpCall& call) const
{
    Arguments args(call.arguments, {u"id"});
    const QString id = args.text(u"id", true);
    if (!args.ok()) {
        return args.problem();
    }
    const auto found = m_proposals.find(id);
    if (found == m_proposals.end() || found->second.client != call.client) {
        return failure(QStringLiteral("GS-MCP-018"),
                       QStringLiteral("There is no keymap draft %1 for this client; propose_keymap returns the id").arg(id));
    }
    const Proposal& proposal = found->second;
    const ProfileEditor* editor = proposal.editor.data();
    QJsonArray controls;
    for (qsizetype index = 0; index < proposal.suggestions.size(); ++index) {
        QJsonObject item{{QStringLiteral("index"), static_cast<int>(index)},
                         {QStringLiteral("name"), proposal.names.value(index)},
                         {QStringLiteral("type"), proposal.types.value(index)},
                         {QStringLiteral("state"), proposal.states.value(index)}};
        const QString control = proposal.controls.value(index);
        if (!control.isEmpty()) {
            item.insert(QStringLiteral("id"), control);
            if (editor != nullptr) {
                const std::string wanted = control.toStdString();
                item.insert(QStringLiteral("inProfile"),
                            std::ranges::any_of(editor->profile().controls,
                                                [&wanted](const input::Control& each) { return each.id == wanted; }));
            }
        }
        controls.append(item);
    }
    return success(jsonText(QJsonObject{{QStringLiteral("id"), id},
                                        {QStringLiteral("open"), editor != nullptr},
                                        {QStringLiteral("controls"), controls}}));
}

McpOutcome McpTools::createExtension(const McpCall& call)
{
    Arguments args(call.arguments, {u"kind", u"name"});
    const QString kind = args.text(u"kind", true);
    const QString name = args.text(u"name", true);
    if (!args.ok()) {
        return args.problem();
    }
    if (m_setup.drafts == nullptr) {
        return failure(QStringLiteral("GS-MCP-010"), QStringLiteral("Extensions cannot be made in this run"));
    }
    QString wrong;
    const std::optional<McpStarter> starter = mcpStarter(kind, name, wrong);
    if (!starter) {
        return failure(QStringLiteral("GS-MCP-021"), wrong);
    }
    const fs::path folder = m_setup.drafts->clientFolder(call.client) / toPath(QStringLiteral("ext-") + starter->slug);
    std::error_code error;
    if (fs::exists(folder, error)) {
        return failure(QStringLiteral("GS-MCP-021"),
                       QStringLiteral("A starter for %1 exists already at %2; change it with write_extension or choose "
                                      "another name")
                           .arg(name, pathText(folder)));
    }
    QJsonArray files;
    for (const auto& [path, bytes] : starter->files) {
        const fs::path destination = folder / toPath(path);
        fs::create_directories(destination.parent_path(), error);
        if (error || platform::writeFileAtomically(destination, bytes.toStdString())) {
            std::error_code ignored;
            fs::remove_all(folder, ignored);
            return failure(QStringLiteral("GS-MCP-010"), QStringLiteral("The starter could not be written to %1").arg(pathText(folder)));
        }
        files.append(path);
    }
    qInfo("mcp %s: made a %s extension starter in %s", qUtf8Printable(call.client), qUtf8Printable(kind),
          qUtf8Printable(pathText(folder)));
    return success(jsonText(QJsonObject{{QStringLiteral("folder"), pathText(folder)},
                                        {QStringLiteral("id"), starter->id},
                                        {QStringLiteral("kind"), kind},
                                        {QStringLiteral("files"), files},
                                        {QStringLiteral("next"),
                                         QStringLiteral("Change it with write_extension, check it with validate_extension "
                                                        "and pack it with pack_extension; the user installs the package "
                                                        "on GameScrcpy's Extensions page.")}}));
}

std::filesystem::path McpTools::extensionFolder(const QString& client, const QString& given, McpOutcome& problem) const
{
    const auto refuse = [&problem] {
        problem = failure(QStringLiteral("GS-MCP-004"),
                          QStringLiteral("extension must be a folder that create_extension made for this client"));
        return fs::path();
    };
    if (m_setup.drafts == nullptr || given.isEmpty() || !QDir::isAbsolutePath(given)) {
        return refuse();
    }
    std::error_code error;
    const fs::path base = fs::weakly_canonical(m_setup.drafts->clientFolder(client), error);
    const fs::path folder = fs::weakly_canonical(toPath(given), error);
    if (error || !fs::is_directory(folder, error)) {
        return refuse();
    }
    const fs::path inside = folder.lexically_relative(base);
    if (inside.empty() || std::distance(inside.begin(), inside.end()) != 1 ||
        !QString::fromStdWString(inside.wstring()).startsWith(QStringLiteral("ext-"))) {
        return refuse();
    }
    return folder;
}

McpOutcome McpTools::writeExtension(const McpCall& call)
{
    Arguments args(call.arguments, {u"extension", u"files"});
    const QString given = args.text(u"extension", true);
    const QJsonObject files = args.object(u"files", true);
    if (args.ok() && (files.isEmpty() || files.size() > kMostWrittenFiles)) {
        args.complain(QStringLiteral("files must hold from 1 to %1 files").arg(kMostWrittenFiles));
    }
    if (!args.ok()) {
        return args.problem();
    }
    McpOutcome problem;
    const fs::path folder = extensionFolder(call.client, given, problem);
    if (folder.empty()) {
        return problem;
    }
    std::vector<std::pair<QString, QString>> planned;
    qsizetype total = 0;
    for (auto at = files.begin(); at != files.end(); ++at) {
        QString wrong;
        const QString path = extensionPath(at.key(), wrong);
        if (path.isEmpty()) {
            return failure(QStringLiteral("GS-MCP-004"), wrong);
        }
        if (!at.value().isString()) {
            return failure(QStringLiteral("GS-MCP-004"), QStringLiteral("the text of %1 must be a string").arg(path));
        }
        const qsizetype bytes = at.value().toString().toUtf8().size();
        total += bytes;
        if (bytes > kMostFileBytes || total > kMostWrittenBytes) {
            return failure(QStringLiteral("GS-MCP-004"),
                           QStringLiteral("%1 makes the files too large: at most 2 MiB a file and 8 MiB in all").arg(path));
        }
        planned.emplace_back(path, at.value().toString());
    }
    QStringList written;
    for (const auto& [path, text] : planned) {
        const fs::path destination = folder / toPath(path);
        std::string existing;
        static_cast<void>(platform::readFile(destination, static_cast<std::size_t>(kMostFileBytes), existing,
                                             platform::WhenBusy::GiveUp));
        std::error_code error;
        fs::create_directories(destination.parent_path(), error);
        if (error ||
            platform::writeFileAtomically(destination, mcpEncodedLike(text, QByteArray::fromStdString(existing)).toStdString())) {
            return failure(QStringLiteral("GS-MCP-010"), QStringLiteral("%1 could not be written").arg(path));
        }
        written.append(path);
    }
    return success(QStringLiteral("Wrote %1 into %2. Check it with validate_extension.")
                       .arg(written.join(QStringLiteral(", ")), pathText(folder)));
}

void McpTools::validateExtension(const McpCall& call, Reply reply)
{
    Arguments args(call.arguments, {u"folder"});
    const QString given = args.text(u"folder", true);
    if (args.ok() && !QDir::isAbsolutePath(given)) {
        args.complain(QStringLiteral("folder must be a full path"));
    }
    if (!args.ok()) {
        reply(args.problem());
        return;
    }
    const QFileInfo info(given);
    if (!info.exists()) {
        reply(failure(QStringLiteral("GS-MCP-004"), QStringLiteral("There is nothing at %1").arg(QDir::fromNativeSeparators(given))));
        return;
    }
    m_waiting[call.id] = Waiting{.reply = std::move(reply)};
    runProgram(call.id, {QStringLiteral("validate"), QDir::toNativeSeparators(info.absoluteFilePath()), QStringLiteral("--json")},
               toPath(info.absolutePath()), [](const QByteArray& output, const QByteArray& errors) {
                   const QJsonDocument parsed = QJsonDocument::fromJson(output);
                   if (!parsed.isObject()) {
                       return failure(QStringLiteral("GS-MCP-020"),
                                      QStringLiteral("gs validate did not finish: %1").arg(firstLine(errors)));
                   }
                   return success(jsonText(parsed));
               });
}

void McpTools::packExtension(const McpCall& call, Reply reply)
{
    Arguments args(call.arguments, {u"folder"});
    const QString given = args.text(u"folder", true);
    if (args.ok() && !QDir::isAbsolutePath(given)) {
        args.complain(QStringLiteral("folder must be a full path"));
    }
    if (!args.ok()) {
        reply(args.problem());
        return;
    }
    const QFileInfo info(given);
    if (!info.isDir()) {
        reply(failure(QStringLiteral("GS-MCP-004"),
                      QStringLiteral("%1 is not a folder").arg(QDir::fromNativeSeparators(given))));
        return;
    }
    if (m_setup.drafts == nullptr) {
        reply(failure(QStringLiteral("GS-MCP-010"), QStringLiteral("Packages cannot be made in this run")));
        return;
    }
    const fs::path packages = m_setup.drafts->clientFolder(call.client) / "packages";
    std::error_code error;
    fs::create_directories(packages, error);
    if (error) {
        reply(failure(QStringLiteral("GS-MCP-010"), QStringLiteral("The folder for packages cannot be made")));
        return;
    }
    QString name = info.fileName();
    std::string manifest;
    if (!platform::readFile(toPath(info.absoluteFilePath()) / "manifest.json", std::size_t{256} << 10, manifest,
                            platform::WhenBusy::GiveUp)) {
        const QJsonObject read = QJsonDocument::fromJson(QByteArray::fromStdString(manifest)).object();
        const QString id = read.value(QStringLiteral("id")).toString();
        const QString version = read.value(QStringLiteral("version")).toString();
        static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9.+-]*$"));
        if (safe.match(id).hasMatch() && safe.match(version).hasMatch()) {
            name = id + u'-' + version;
        }
    }
    const fs::path output = packages / toPath(name + QStringLiteral(".gsext"));
    fs::remove(output, error);
    m_waiting[call.id] = Waiting{.reply = std::move(reply)};
    const QString outputText = pathText(output);
    runProgram(call.id,
               {QStringLiteral("pack"), QDir::toNativeSeparators(info.absoluteFilePath()), QStringLiteral("-o"),
                QDir::toNativeSeparators(outputText), QStringLiteral("--json")},
               toPath(info.absoluteFilePath()), [outputText](const QByteArray& out, const QByteArray& errors) {
                   const QJsonDocument parsed = QJsonDocument::fromJson(out);
                   if (!parsed.isObject()) {
                       return failure(QStringLiteral("GS-MCP-020"),
                                      QStringLiteral("gs pack did not finish: %1").arg(firstLine(errors)));
                   }
                   QJsonObject result = parsed.object();
                   if (!result.value(QStringLiteral("ok")).toBool() || !QFileInfo::exists(outputText)) {
                       return failure(QStringLiteral("GS-MCP-021"),
                                      QStringLiteral("The extension has problems, so nothing was packed:\n") + jsonText(result));
                   }
                   result.insert(QStringLiteral("output"), outputText);
                   result.insert(QStringLiteral("next"), QStringLiteral("The user installs it on GameScrcpy's Extensions "
                                                                        "page."));
                   return success(jsonText(result));
               });
}

void McpTools::runProgram(quint64 call, const QStringList& arguments, const std::filesystem::path& folder, Parse parse)
{
    const auto found = m_waiting.find(call);
    if (found == m_waiting.end()) {
        return;
    }
    const QString tool = program();
    if (!QFileInfo::exists(tool)) {
        finish(call, failure(QStringLiteral("GS-MCP-020"), QStringLiteral("gs.exe is missing next to GameScrcpy, so this "
                                                                          "cannot run")));
        return;
    }
    auto* process = new QProcess(this);
    process->setProgram(tool);
    process->setArguments(arguments);
    process->setWorkingDirectory(pathText(folder));
    found->second.process = process;
    found->second.deadline = m_clock.now() + std::chrono::duration_cast<Microseconds>(kProgramLimit);
    schedule();
    connect(process, &QProcess::finished, this, [this, call, process, parse](int, QProcess::ExitStatus) {
        const QByteArray output = process->readAllStandardOutput();
        const QByteArray errors = process->readAllStandardError();
        process->deleteLater();
        const auto running = m_waiting.find(call);
        if (running == m_waiting.end() || running->second.process.data() != process) {
            return;
        }
        running->second.process = nullptr;
        running->second.deadline.reset();
        finish(call, parse(output, errors));
    });
    connect(process, &QProcess::errorOccurred, this, [this, call, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        process->deleteLater();
        const auto running = m_waiting.find(call);
        if (running == m_waiting.end() || running->second.process.data() != process) {
            return;
        }
        running->second.process = nullptr;
        running->second.deadline.reset();
        finish(call, failure(QStringLiteral("GS-MCP-020"), QStringLiteral("gs.exe did not start")));
    });
    process->start();
}

void McpTools::finish(quint64 call, McpOutcome outcome)
{
    auto node = m_waiting.extract(call);
    schedule();
    if (!node.empty() && node.mapped().reply) {
        node.mapped().reply(std::move(outcome));
    }
}

void McpTools::schedule()
{
    std::optional<Microseconds> next;
    for (const auto& entry : m_waiting) {
        if (const std::optional<Microseconds>& due = entry.second.deadline) {
            next = next ? std::min(*next, *due) : *due;
        }
    }
    if (!next) {
        m_deadline.stop();
        return;
    }
    const Microseconds left = std::max(*next - m_clock.now(), Microseconds{0});
    m_deadline.start(std::chrono::ceil<std::chrono::milliseconds>(left));
}

std::optional<McpPhone> McpTools::phoneFor(const QString& serial, McpOutcome& problem) const
{
    const std::vector<McpPhone> phones = m_setup.devices.phones ? m_setup.devices.phones() : std::vector<McpPhone>{};
    if (phones.empty()) {
        problem = failure(QStringLiteral("GS-MCP-005"),
                          QStringLiteral("No phone is mirroring; ask the user to mirror one in GameScrcpy"));
        return std::nullopt;
    }
    if (serial.isEmpty()) {
        if (phones.size() == 1) {
            return phones.front();
        }
        problem = failure(QStringLiteral("GS-MCP-005"),
                          QStringLiteral("Several phones are mirroring; give serial (list_phones lists them)"));
        return std::nullopt;
    }
    const auto found = std::ranges::find(phones, serial, &McpPhone::serial);
    if (found == phones.end()) {
        problem = failure(QStringLiteral("GS-MCP-005"),
                          QStringLiteral("No mirroring phone has the serial %1; list_phones lists them").arg(serial));
        return std::nullopt;
    }
    return *found;
}

std::optional<McpTools::Folder> McpTools::folderFor(const QString& client, const QString& target, bool draft,
                                                    McpOutcome& problem) const
{
    if (target.isEmpty()) {
        problem = failure(QStringLiteral("GS-MCP-004"),
                          QStringLiteral("the target must be profile:<id>, component:<ref> or task:<id>"));
        return std::nullopt;
    }
    if (draft) {
        if (!drafted(client, target)) {
            problem = failure(QStringLiteral("GS-MCP-014"),
                              QStringLiteral("This client has no draft of %1; write one first, or leave draft out to "
                                             "use the version in use")
                                  .arg(target));
            return std::nullopt;
        }
        return Folder{.path = m_setup.drafts->folderOf(client, target), .name = m_setup.drafts->nameOf(target), .draft = true};
    }
    const QString kind = kindWord(target);
    const QString id = target.section(u':', 1);
    if (kind == u"profile") {
        const ProfileEntry* entry = m_setup.profiles != nullptr ? m_setup.profiles->find(id) : nullptr;
        if (entry == nullptr) {
            problem = failure(QStringLiteral("GS-MCP-006"),
                              QStringLiteral("There is no profile with the id %1; list_profiles lists them").arg(id));
            return std::nullopt;
        }
        if (!entry->folder.empty()) {
            return Folder{.path = entry->folder, .name = entry->name};
        }
        auto scratch = std::make_shared<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("gs-mcp-XXXXXX")));
        std::string text;
        if (entry->readOnly()) {
            text = entry->profile != nullptr ? input::saveProfile(*entry->profile) : std::string();
        } else if (platform::readFile(entry->path, kMostProfileBytes, text, platform::WhenBusy::GiveUp)) {
            text.clear();
        }
        const fs::path root = scratch->isValid() ? toPath(scratch->path()) : fs::path();
        if (root.empty() || text.empty() || platform::writeFileAtomically(root / "profile.json", text)) {
            problem = failure(QStringLiteral("GS-MCP-010"), QStringLiteral("%1 cannot be copied to a temporary folder").arg(entry->name));
            return std::nullopt;
        }
        return Folder{.path = root, .name = entry->name, .scratch = std::move(scratch)};
    }
    if (kind == u"component") {
        if (const ComponentEntry* entry = m_setup.components != nullptr ? m_setup.components->find(id) : nullptr) {
            return Folder{.path = m_setup.components->folderOf(id), .name = entry->name};
        }
        problem = failure(QStringLiteral("GS-MCP-019"),
                          QStringLiteral("There is no component %1; list_components lists them").arg(id));
        return std::nullopt;
    }
    if (kind == u"task") {
        if (const TaskEntry* entry = m_setup.tasks != nullptr ? m_setup.tasks->find(id) : nullptr) {
            return Folder{.path = toPath(entry->folder), .name = entry->name};
        }
        problem = failure(QStringLiteral("GS-MCP-019"), QStringLiteral("There is no task %1; list_tasks lists them").arg(id));
        return std::nullopt;
    }
    problem = failure(QStringLiteral("GS-MCP-004"),
                      QStringLiteral("the target must be profile:<id>, component:<ref> or task:<id>"));
    return std::nullopt;
}

bool McpTools::drafted(const QString& client, const QString& target) const
{
    return m_setup.drafts != nullptr && !target.isEmpty() && m_setup.drafts->has(client, target);
}

QString McpTools::program() const
{
    return m_setup.program.empty() ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("gs.exe"))
                                   : pathText(m_setup.program);
}

void McpTools::callExtensionTool(const McpCall& call, Reply reply)
{
    std::optional<McpExtensionTool> tool;
    {
        const std::scoped_lock lock(m_registry->mutex);
        const auto found = std::ranges::find(m_registry->tools, call.tool, &McpExtensionTool::name);
        if (found != m_registry->tools.end()) {
            tool = *found;
        }
    }
    if (!tool) {
        reply(failure(QStringLiteral("GS-MCP-022"),
                      QStringLiteral("The tool %1 is no longer offered; tools/list lists the tools").arg(call.tool)));
        return;
    }
    const QString quietKey = QStringLiteral("tool:") + tool->name;
    m_waiting[call.id] = Waiting{.reply = std::move(reply)};
    if (m_quiet.contains({call.client, quietKey})) {
        qInfo("mcp %s: %s without asking; the user allowed it until GameScrcpy closes", qUtf8Printable(call.client),
              qUtf8Printable(tool->name));
        startExtensionCall(call.id, *tool, call.arguments);
        return;
    }
    if (!call.confirm) {
        finish(call.id, failure(QStringLiteral("GS-MCP-008"), QStringLiteral("Nobody can confirm the call, so the tool did not run")));
        return;
    }
    McpConfirmation shown{
        .kind = QStringLiteral("tool"),
        .details = {{QStringLiteral("tool"), tool->name},
                    {QStringLiteral("title"), tool->tool},
                    {QStringLiteral("extension"), tool->extension},
                    {QStringLiteral("description"), tool->description},
                    {QStringLiteral("arguments"), QString::fromUtf8(QJsonDocument(call.arguments).toJson(QJsonDocument::Indented))},
                    {QStringLiteral("quiet"), true},
                    {QStringLiteral("seconds"),
                     static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(kToolLimit).count())}}};
    const quint64 callId = call.id;
    const QString client = call.client;
    const McpExtensionTool chosen = *tool;
    const QJsonObject arguments = call.arguments;
    call.confirm(std::move(shown), [this, callId, client, chosen, arguments, quietKey](McpAnswer answer) {
        if (!m_waiting.contains(callId)) {
            return;
        }
        if (answer == McpAnswer::Refused) {
            finish(callId, failure(QStringLiteral("GS-MCP-008"), QStringLiteral("The user refused to run the tool")));
            return;
        }
        if (answer == McpAnswer::Unanswered) {
            finish(callId, failure(QStringLiteral("GS-MCP-009"),
                                   QStringLiteral("The user did not answer within two minutes, so the tool did not run")));
            return;
        }
        if (answer == McpAnswer::AllowedForAWhile) {
            m_quiet[{client, quietKey}] = chosen.tool;
            qInfo("mcp %s: may use %s without asking until GameScrcpy closes", qUtf8Printable(client),
                  qUtf8Printable(chosen.name));
        }
        startExtensionCall(callId, chosen, arguments);
    });
}

void McpTools::startExtensionCall(quint64 call, const McpExtensionTool& tool, const QJsonObject& arguments)
{
    const auto found = m_waiting.find(call);
    if (found == m_waiting.end()) {
        return;
    }
    bool offered = false;
    {
        const std::scoped_lock lock(m_registry->mutex);
        offered = std::ranges::any_of(m_registry->tools, [&tool](const McpExtensionTool& each) {
            return each.name == tool.name && each.logic == tool.logic;
        });
    }
    const QJsonObject asked{{QStringLiteral("tool"), tool.tool}, {QStringLiteral("arguments"), arguments}};
    if (!offered ||
        !LogicServices::instance().tell(tool.logic, QStringLiteral("mcp"), QString::number(call),
                                        QString::fromUtf8(QJsonDocument(asked).toJson(QJsonDocument::Compact)))) {
        finish(call, failure(QStringLiteral("GS-MCP-022"),
                             QStringLiteral("The extension's logic that offers %1 stopped; tools/list lists the tools")
                                 .arg(tool.name)));
        return;
    }
    found->second.logic = tool.logic;
    found->second.deadline = m_clock.now() + std::chrono::duration_cast<Microseconds>(kToolLimit);
    schedule();
}

void McpTools::toolAnswered(std::uint64_t logic, quint64 call, bool ok, const QString& text)
{
    const auto found = m_waiting.find(call);
    if (found == m_waiting.end() || found->second.logic != logic) {
        return;
    }
    if (ok) {
        finish(call, success(text));
        return;
    }
    finish(call, failure(QStringLiteral("GS-MCP-024"),
                         text.isEmpty() ? QStringLiteral("The tool failed") : QStringLiteral("The tool failed: %1").arg(text)));
}

void McpTools::logicEnded(quint64 logic)
{
    std::size_t removed = 0;
    {
        const std::scoped_lock lock(m_registry->mutex);
        removed = std::erase_if(m_registry->tools, [logic](const McpExtensionTool& tool) { return tool.logic == logic; });
    }
    std::vector<quint64> stranded;
    for (const auto& [call, waiting] : m_waiting) {
        if (waiting.logic == logic) {
            stranded.push_back(call);
        }
    }
    for (const quint64 call : stranded) {
        finish(call, failure(QStringLiteral("GS-MCP-022"),
                             QStringLiteral("The extension's logic that offers the tool stopped before it answered")));
    }
    if (removed > 0) {
        emit toolsChanged();
    }
}

script::ServiceAnswer McpTools::registerTool(Registry& registry, const QPointer<McpTools>& self,
                                             const LogicCaller& caller, std::string_view json)
{
    const auto refused = [](const char* why) { return script::ServiceAnswer{"null", "GS-API-074", why}; };
    if (caller.extension.isEmpty() || !caller.device.isEmpty() || caller.task || caller.script.contains(u'/')) {
        return refused("mcp.tool works only in a script that the extension's manifest.json lists with \"run\": \"app\"");
    }
    const QJsonObject given = jsonObjectOf(json);
    const QString tool = given.value(QStringLiteral("name")).toString();
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9_]{0,31}$"));
    if (!pattern.match(tool).hasMatch()) {
        return refused("the tool name must be a lowercase letter, then up to 31 lowercase letters, digits and underscores");
    }
    const QString description = given.value(QStringLiteral("description")).toString().trimmed();
    if (description.isEmpty() || description.size() > kMostToolDescriptionChars) {
        return refused("the tool needs a description of at most 1000 characters");
    }
    const QJsonValue input = given.value(QStringLiteral("input"));
    if (!input.isUndefined() && !input.isNull() && !input.isObject()) {
        return refused("the input schema of the tool must be an object");
    }
    QJsonObject schema = input.toObject();
    if (!schema.contains(QStringLiteral("type"))) {
        schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    }
    if (schema.value(QStringLiteral("type")).toString() != u"object") {
        return refused("the input schema of the tool must be of type object");
    }
    if (QJsonDocument(schema).toJson(QJsonDocument::Compact).size() > kMostToolSchemaBytes) {
        return refused("the input schema of the tool must be at most 16 KiB as JSON");
    }
    const QString name = mcpExtensionToolName(caller.extension, tool);
    {
        const std::scoped_lock lock(registry.mutex);
        const auto same = std::ranges::find(registry.tools, name, &McpExtensionTool::name);
        if (same != registry.tools.end() && same->logic != caller.logic) {
            return refused("another script or extension already offers a tool with this name");
        }
        if (same != registry.tools.end()) {
            same->description = description;
            same->input = schema;
        } else {
            const auto offered = std::ranges::count(registry.tools, caller.extension, &McpExtensionTool::extension);
            if (offered >= kMostToolsPerExtension) {
                return refused("the extension already offers 32 tools");
            }
            registry.tools.push_back({.name = name,
                                      .tool = tool,
                                      .extension = caller.extension,
                                      .logic = caller.logic,
                                      .description = description,
                                      .input = schema});
        }
    }
    qInfo("mcp: the extension %s offers the tool %s", qUtf8Printable(caller.extension), qUtf8Printable(name));
    announce(self);
    return {"true", {}, {}};
}

script::ServiceAnswer McpTools::unregisterTool(Registry& registry, const QPointer<McpTools>& self,
                                               const LogicCaller& caller, std::string_view json)
{
    const QString name = mcpExtensionToolName(caller.extension, jsonObjectOf(json).value(QStringLiteral("name")).toString());
    std::size_t removed = 0;
    {
        const std::scoped_lock lock(registry.mutex);
        removed = std::erase_if(registry.tools, [&](const McpExtensionTool& tool) {
            return tool.name == name && tool.logic == caller.logic;
        });
    }
    if (removed > 0) {
        announce(self);
    }
    return {"true", {}, {}};
}

script::ServiceAnswer McpTools::toolResult(const QPointer<McpTools>& self, const LogicCaller& caller,
                                           std::string_view json)
{
    const QJsonObject given = jsonObjectOf(json);
    bool known = false;
    const quint64 call = given.value(QStringLiteral("call")).toString().toULongLong(&known);
    if (!known) {
        return {"null", "GS-API-074", "the result of the tool names no call"};
    }
    const bool ok = given.value(QStringLiteral("ok")).toBool();
    const QString text = ok ? valueText(given.value(QStringLiteral("value")))
                            : given.value(QStringLiteral("error")).toString();
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [self, logic = caller.logic, call, ok, text] {
            if (self != nullptr) {
                self->toolAnswered(logic, call, ok, text);
            }
        },
        Qt::QueuedConnection);
    return {"true", {}, {}};
}

}
