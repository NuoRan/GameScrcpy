#include "TaskLibrary.h"

#include "ExtensionExport.h"
#include "TranslateNoop.h"

#include <gs/platform/AtomicFile.h>
#include <gs/script/ApiFiles.h>

#include <QCoreApplication>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

TaskLibrary* s_instance = nullptr;
constexpr int kSettleMs = 300;
constexpr std::size_t kMostTaskBytes = std::size_t{1} << 20;
constexpr int kMostTaskNames = 100;
constexpr int kDaysInWeek = 7;

bool knownMode(const QString& mode)
{
    return mode == u"manual" || mode == u"daily" || mode == u"weekly" || mode == u"once";
}

std::filesystem::path toPath(const QString& text)
{
    return {text.toStdWString()};
}

QString pathText(const std::filesystem::path& path)
{
    return QDir::fromNativeSeparators(QString::fromStdWString(path.wstring()));
}

QString readText(const std::filesystem::path& path)
{
    std::string contents;
    if (platform::readFile(path, kMostTaskBytes, contents)) {
        return {};
    }
    return QString::fromUtf8(contents);
}

QString idFor(const QString& name)
{
    QString id;
    for (const QChar c : name.trimmed()) {
        if (c.isLetterOrNumber()) {
            id += c.toLower();
        } else if (!id.isEmpty() && !id.endsWith(u'-')) {
            id += u'-';
        }
    }
    while (id.endsWith(u'-')) {
        id.chop(1);
    }
    return id.isEmpty() ? QStringLiteral("task") : id.left(kMostTaskNames);
}

QString starterTask(const QString& rounds)
{
    QString label = rounds;
    label.replace(u'\\', QStringLiteral("\\\\"));
    label.replace(u'"', QStringLiteral("\\\""));
    return QStringLiteral("export default class MyTask extends Task {\n"
                          "    static params = {\n"
                          "        rounds: param.number(3, { label: \"%1\", min: 1 }),\n"
                          "    };\n"
                          "\n"
                          "    async run({ signal }: TaskContext) {\n"
                          "        for (let round = 1; round <= this.params.rounds && !signal.aborted; round++) {\n"
                          "            this.progress(round + \" / \" + this.params.rounds);\n"
                          "            log.info(\"round\", round);\n"
                          "            await sleep(1000);\n"
                          "        }\n"
                          "    }\n"
                          "}\n")
        .arg(label);
}

QVariant chosenValue(const TaskParamSpec& spec, const QVariantMap& values)
{
    const QVariant given = values.value(spec.name);
    if (!given.isValid()) {
        return spec.value;
    }
    if (spec.kind == u"number") {
        bool ok = false;
        const double number = given.toDouble(&ok);
        if (!ok || !std::isfinite(number)) {
            return spec.value;
        }
        if (spec.min && number < *spec.min) {
            return *spec.min;
        }
        if (spec.max && number > *spec.max) {
            return *spec.max;
        }
        return number;
    }
    if (spec.kind == u"toggle") {
        return given.typeId() == QMetaType::Bool ? given : spec.value;
    }
    return given.toString();
}

struct Example {
    const char* id;
    const char* name;
    const char* description;
    const char* icon;
};

constexpr std::array<Example, 4> kExamples{{
    {"find-and-tap", GS_TRANSLATE_NOOP("TaskLibrary", "Find a picture and tap it"),
     GS_TRANSLATE_NOOP("TaskLibrary", "Looks for a picture on the phone and taps it as soon as it shows up, giving up "
                                      "after the time you set. To make it your own, put a picture cut from your "
                                      "phone's mirrored picture in the task's res folder (More, then Open folder) and "
                                      "type its name under Picture."),
     "search"},
    {"wait-then-tap", GS_TRANSLATE_NOOP("TaskLibrary", "Wait for a picture, then tap"),
     GS_TRANSLATE_NOOP("TaskLibrary", "Waits until a picture appears, then taps it or a place you choose, and writes a "
                                      "clear line to the log when it does not appear in time. To make it your own, put "
                                      "in your own picture as for Find a picture and tap it, and set how long to wait "
                                      "and where to tap."),
     "timer"},
    {"tap-in-order", GS_TRANSLATE_NOOP("TaskLibrary", "Tap places in order"),
     GS_TRANSLATE_NOOP("TaskLibrary", "Taps a few places one after another with a pause between taps, and can go round "
                                      "several times. To make it your own, list your places as x,y percentages of the "
                                      "picture, separated by semicolons, and set the pause and how many times to go "
                                      "round."),
     "tap-echo"},
    {"daily-check-in", GS_TRANSLATE_NOOP("TaskLibrary", "Daily check-in template"),
     GS_TRANSLATE_NOOP("TaskLibrary", "Opens an app, waits for its check-in button and taps it, then waits for the "
                                      "picture that shows it worked and writes the result to the log. To make it your "
                                      "own, enter your game's package name and put pictures from your game in place of "
                                      "the two in its res folder, which are stand-ins from the demo recording; then "
                                      "choose Every day under How it starts: the time is already 09:00."),
     "history"},
}};

constexpr std::array<const char*, 13> kExampleLabels{
    GS_TRANSLATE_NOOP("TaskLibrary", "Picture"),
    GS_TRANSLATE_NOOP("TaskLibrary", "Search for (ms)"),
    GS_TRANSLATE_NOOP("TaskLibrary", "Wait up to (ms)"),
    GS_TRANSLATE_NOOP("TaskLibrary", "Tap where the picture is"),
    GS_TRANSLATE_NOOP("TaskLibrary", "Otherwise tap at x (%)"),
    GS_TRANSLATE_NOOP("TaskLibrary", "Otherwise tap at y (%)"),
    GS_TRANSLATE_NOOP("TaskLibrary", "Places as x,y in percent, split by ;"),
    GS_TRANSLATE_NOOP("TaskLibrary", "Wait between taps (ms)"),
    GS_TRANSLATE_NOOP("TaskLibrary", "Times to go round"),
    GS_TRANSLATE_NOOP("TaskLibrary", "App to open (package name)"),
    GS_TRANSLATE_NOOP("TaskLibrary", "Picture of the check-in button"),
    GS_TRANSLATE_NOOP("TaskLibrary", "Picture shown once checked in"),
    GS_TRANSLATE_NOOP("TaskLibrary", "Wait for each picture up to (ms)")};

QString translated(const char* text)
{
    return QCoreApplication::translate("TaskLibrary", text);
}

QString shownLabel(const QString& label)
{
    const std::string text = label.toStdString();
    const bool known = std::ranges::any_of(kExampleLabels, [&text](const char* each) { return text == each; });
    return known ? translated(text.c_str()) : label;
}

QString freeNameIn(const std::vector<TaskEntry>& entries, const QString& wanted)
{
    const auto taken = [&entries](const QString& candidate) {
        return std::ranges::any_of(entries, [&candidate](const TaskEntry& entry) { return entry.name == candidate; });
    };
    QString name = wanted;
    for (int copy = 2; taken(name); ++copy) {
        name = wanted + u' ' + QString::number(copy);
    }
    return name;
}

std::optional<TaskExample> exampleIn(const std::filesystem::path& folder)
{
    const QString code = readText(folder / "task.ts");
    if (code.isEmpty()) {
        return std::nullopt;
    }
    const QString name = pathText(folder.filename());
    const QJsonObject object = QJsonDocument::fromJson(readText(folder / "task.json").toUtf8()).object();
    const QJsonObject schedule = object.value(QStringLiteral("schedule")).toObject();
    TaskExample example{.id = QStringLiteral("examples/") + name,
                        .name = object.value(QStringLiteral("name")).toString(name),
                        .folder = pathText(folder),
                        .params = taskParamsIn(code)};
    if (const QString mode = schedule.value(QStringLiteral("mode")).toString(); knownMode(mode)) {
        example.schedule = mode;
    }
    if (const QString time = schedule.value(QStringLiteral("time")).toString();
        QTime::fromString(time, QStringLiteral("HH:mm")).isValid()) {
        example.time = time;
    }
    for (const QJsonValue day : schedule.value(QStringLiteral("days")).toArray()) {
        const int number = day.toInt();
        if (number >= 1 && number <= kDaysInWeek && !example.days.contains(number)) {
            example.days.append(number);
        }
    }
    std::sort(example.days.begin(), example.days.end());
    for (TaskParamSpec& spec : example.params) {
        spec.label = shownLabel(spec.label);
    }
    const auto known =
        std::ranges::find_if(kExamples, [&name](const Example& each) { return name == QLatin1StringView(each.id); });
    if (known != kExamples.end()) {
        example.name = translated(known->name);
        example.description = translated(known->description);
        example.icon = QString::fromLatin1(known->icon);
    } else {
        example.icon = QStringLiteral("bot");
    }
    return example;
}

std::size_t exampleRank(const QString& id)
{
    const QString folder = id.section(u'/', 1);
    const auto known =
        std::ranges::find_if(kExamples, [&folder](const Example& each) { return folder == QLatin1StringView(each.id); });
    return static_cast<std::size_t>(std::distance(kExamples.begin(), known));
}

}

std::vector<TaskParamSpec> taskParamsIn(const QString& code)
{
    return declaredIn(code, QStringLiteral("params"), QStringLiteral("param"),
                      {QStringLiteral("number"), QStringLiteral("text"), QStringLiteral("toggle")});
}

bool hasSchedule(const TaskEntry& entry) noexcept
{
    return entry.schedule == u"daily" || entry.schedule == u"weekly" || entry.schedule == u"once";
}

QDateTime lastDueTime(const TaskEntry& entry, const QDateTime& now)
{
    const QTime time = QTime::fromString(entry.time, QStringLiteral("HH:mm"));
    if (!hasSchedule(entry) || !time.isValid() || !now.isValid()) {
        return {};
    }
    for (int back = 0; back <= kDaysInWeek; ++back) {
        const QDate day = now.date().addDays(-back);
        if (entry.schedule == u"weekly" && !entry.days.contains(day.dayOfWeek())) {
            continue;
        }
        const QDateTime at(day, time, now.timeZone());
        if (at.isValid() && at <= now) {
            return at;
        }
    }
    return {};
}

ScheduleCheck checkSchedule(const TaskEntry& entry, const QDateTime& previous, const QDateTime& now)
{
    if (entry.schedule == u"once" && !entry.lastScheduled.isEmpty()) {
        return {};
    }
    const QDateTime at = lastDueTime(entry, now);
    if (!at.isValid() || entry.lastScheduled == at.date().toString(Qt::ISODate)) {
        return {};
    }
    if (at.msecsTo(now) <= kScheduleWindowMs) {
        return {.verdict = ScheduleVerdict::Due, .at = at};
    }
    if (previous.isValid() && at > previous) {
        return {.verdict = ScheduleVerdict::Missed, .at = at};
    }
    return {};
}

bool closesToTray(bool settingOn, const std::vector<TaskEntry>& entries)
{
    return settingOn && std::ranges::any_of(entries, [](const TaskEntry& entry) { return hasSchedule(entry); });
}

ScreenPlan screenPlanFor(bool scheduledRun, bool connectedForRun) noexcept
{
    return {.wake = scheduledRun, .sleepAfter = scheduledRun && connectedForRun};
}

bool sleepsWhenDone(bool woke, int otherRunsOnPhone) noexcept
{
    return woke && otherRunsOnPhone == 0;
}

TaskLibrary::TaskLibrary(std::filesystem::path directory, QObject* parent)
    : QObject(parent), m_directory(std::move(directory))
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    std::error_code ignored;
    std::filesystem::create_directories(m_directory, ignored);
    m_settle.setInterval(kSettleMs);
    m_settle.setSingleShot(true); // source-rule-allow R9: a debounce; editors write a file in several steps
    connect(&m_settle, &QTimer::timeout, this, &TaskLibrary::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_settle.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_settle.start(); });
    reload();
}

TaskLibrary::~TaskLibrary()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

TaskLibrary* TaskLibrary::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

TaskLibrary* TaskLibrary::instance() noexcept
{
    return s_instance;
}

const TaskEntry* TaskLibrary::find(const QString& id) const
{
    const auto found = std::ranges::find(m_entries, id, &TaskEntry::id);
    return found != m_entries.end() ? &*found : nullptr;
}

TaskEntry* TaskLibrary::entryOf(const QString& id)
{
    const auto found = std::ranges::find(m_entries, id, &TaskEntry::id);
    return found != m_entries.end() && !found->readOnly ? &*found : nullptr;
}

QString TaskLibrary::paramsJson(const QString& id) const
{
    const TaskEntry* entry = find(id);
    if (entry == nullptr) {
        return QStringLiteral("{}");
    }
    QJsonObject object;
    for (const TaskParamSpec& spec : entry->params) {
        object.insert(spec.name, QJsonValue::fromVariant(chosenValue(spec, entry->values)));
    }
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString TaskLibrary::add(const QString& name)
{
    const QString title = name.trimmed().isEmpty() ? tr("New task") : name.trimmed();
    const QString base = idFor(title);
    QString id = base;
    std::error_code error;
    for (int copy = 2; std::filesystem::exists(m_directory / toPath(id), error); ++copy) {
        id = base + u'-' + QString::number(copy);
    }
    const std::filesystem::path folder = m_directory / toPath(id);
    if (!std::filesystem::create_directories(folder, error) || error) {
        return {};
    }
    TaskEntry entry{.id = id, .name = title, .folder = pathText(folder)};
    if (platform::writeFileAtomically(folder / "task.ts", starterTask(tr("Rounds")).toStdString()) ||
        platform::writeFileAtomically(folder / "tsconfig.json", script::profileTsconfig()) || !write(entry)) {
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
        return {};
    }
    emit taskCreated(id);
    reload();
    return id;
}

QString TaskLibrary::addWithCode(const QString& name, const QString& code, const QString& declarations)
{
    const QString title = freeNameIn(m_entries, name.trimmed().isEmpty() ? tr("New task") : name.trimmed());
    const QString base = idFor(title);
    QString id = base;
    std::error_code error;
    for (int copy = 2; std::filesystem::exists(m_directory / toPath(id), error); ++copy) {
        id = base + u'-' + QString::number(copy);
    }
    const std::filesystem::path folder = m_directory / toPath(id);
    if (!std::filesystem::create_directories(folder, error) || error) {
        return {};
    }
    TaskEntry entry{.id = id, .name = title, .folder = pathText(folder)};
    if (platform::writeFileAtomically(folder / "task.ts", code.toStdString()) ||
        platform::writeFileAtomically(folder / "tsconfig.json", script::profileTsconfig()) ||
        (!declarations.isEmpty() &&
         platform::writeFileAtomically(folder / "profile.d.ts", declarations.toStdString())) ||
        !write(entry)) {
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
        return {};
    }
    emit taskCreated(id);
    reload();
    return id;
}

bool TaskLibrary::remove(const QString& id)
{
    const TaskEntry* entry = find(id);
    if (entry == nullptr || entry->readOnly) {
        return false;
    }
    const QString folder = entry->folder;
    if (!QFile::moveToTrash(folder)) {
        std::error_code error;
        std::filesystem::remove_all(toPath(folder), error);
        if (error) {
            return false;
        }
    }
    emit taskRemoved(id);
    reload();
    return true;
}

bool TaskLibrary::rename(const QString& id, const QString& name)
{
    TaskEntry* entry = entryOf(id);
    const QString title = name.trimmed();
    if (entry == nullptr || title.isEmpty() || title == entry->name) {
        return false;
    }
    entry->name = title;
    return write(*entry);
}

bool TaskLibrary::setPhones(const QString& id, const QStringList& serials)
{
    TaskEntry* entry = entryOf(id);
    if (entry == nullptr) {
        return false;
    }
    entry->phones = serials;
    entry->phones.removeDuplicates();
    return write(*entry);
}

bool TaskLibrary::setSchedule(const QString& id, const QString& mode, const QString& time)
{
    TaskEntry* entry = entryOf(id);
    if (entry == nullptr || !knownMode(mode) || !QTime::fromString(time, QStringLiteral("HH:mm")).isValid()) {
        return false;
    }
    if (entry->schedule != mode || entry->time != time) {
        entry->lastScheduled.clear();
    }
    if (mode == u"weekly" && entry->days.isEmpty()) {
        entry->days = {QDate::currentDate().dayOfWeek()};
    }
    entry->schedule = mode;
    entry->time = time;
    return write(*entry);
}

bool TaskLibrary::setDays(const QString& id, const QList<int>& days)
{
    TaskEntry* entry = entryOf(id);
    if (entry == nullptr || days.isEmpty() ||
        std::any_of(days.cbegin(), days.cend(), [](int day) { return day < 1 || day > kDaysInWeek; })) {
        return false;
    }
    QList<int> chosen = days;
    std::sort(chosen.begin(), chosen.end());
    chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());
    if (chosen == entry->days) {
        return true;
    }
    entry->days = chosen;
    return write(*entry);
}

bool TaskLibrary::setValue(const QString& id, const QString& name, const QVariant& value)
{
    TaskEntry* entry = entryOf(id);
    if (entry == nullptr) {
        return false;
    }
    const auto spec = std::ranges::find(entry->params, name, &TaskParamSpec::name);
    if (spec == entry->params.end()) {
        return false;
    }
    QVariantMap values = entry->values;
    values.insert(name, value);
    entry->values.insert(name, chosenValue(*spec, values));
    return write(*entry);
}

bool TaskLibrary::setOption(const QString& id, const QString& key, const QVariant& value)
{
    TaskEntry* entry = entryOf(id);
    if (entry == nullptr) {
        return false;
    }
    if (key == u"onError" && (value.toString() == u"stop" || value.toString() == u"retry")) {
        entry->onError = value.toString();
    } else if (key == u"wake") {
        entry->wake = value.toBool();
    } else if (key == u"sleepAfter") {
        entry->sleepAfter = value.toBool();
    } else {
        return false;
    }
    return write(*entry);
}

QVariantList TaskLibrary::history() const
{
    return QJsonDocument::fromJson(readText(m_directory.parent_path() / "task-history.json").toUtf8())
        .array()
        .toVariantList();
}

void TaskLibrary::saveHistory(const QVariantList& history) const
{
    const std::string bytes = QJsonDocument(QJsonArray::fromVariantList(history)).toJson(QJsonDocument::Compact).toStdString();
    static_cast<void>(platform::writeFileAtomically(m_directory.parent_path() / "task-history.json", bytes));
}

void TaskLibrary::setExtensionTasks(std::vector<ExtensionTask> tasks)
{
    if (tasks == m_extensionTasks) {
        return;
    }
    m_extensionTasks = std::move(tasks);
    reload();
}

QString TaskLibrary::copyToMine(const QString& id)
{
    const TaskEntry* source = find(id);
    if (source == nullptr || !source->readOnly) {
        return {};
    }
    const QString base = idFor(source->name);
    QString target = base;
    std::error_code error;
    for (int copy = 2; std::filesystem::exists(m_directory / toPath(target), error); ++copy) {
        target = base + u'-' + QString::number(copy);
    }
    const std::filesystem::path folder = m_directory / toPath(target);
    std::filesystem::copy(toPath(source->folder), folder, std::filesystem::copy_options::recursive, error);
    TaskEntry entry{.id = target, .name = source->name, .folder = pathText(folder)};
    entry.onError = source->onError;
    entry.wake = source->wake;
    entry.sleepAfter = source->sleepAfter;
    entry.values = source->values;
    if (error || !write(entry)) {
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
        return {};
    }
    emit taskCreated(target);
    reload();
    return target;
}

bool TaskLibrary::exportPackage(const QString& id, const QString& fileOrUrl) const
{
    const TaskEntry* entry = find(id);
    const std::optional<std::filesystem::path> target = exportTarget(fileOrUrl);
    if (entry == nullptr || entry->readOnly || !target) {
        return false;
    }
    const ExportedTask task{.id = entry->id,
                            .name = entry->name,
                            .folder = toPath(entry->folder),
                            .values = entry->values,
                            .onError = entry->onError,
                            .wake = entry->wake,
                            .sleepAfter = entry->sleepAfter};
    return exportTaskPackage(task, *target);
}

void TaskLibrary::reveal(const QString& id) const
{
    if (const TaskEntry* entry = find(id)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(entry->folder));
    }
}

bool TaskLibrary::markScheduled(const QString& id, const QString& day)
{
    TaskEntry* entry = entryOf(id);
    if (entry == nullptr) {
        return false;
    }
    entry->lastScheduled = day;
    if (entry->schedule == u"once") {
        entry->schedule = QStringLiteral("manual");
    }
    return write(*entry);
}

bool TaskLibrary::write(const TaskEntry& entry)
{
    QJsonObject schedule{{QStringLiteral("mode"), entry.schedule}, {QStringLiteral("time"), entry.time}};
    if (!entry.days.isEmpty()) {
        QJsonArray days;
        for (const int day : entry.days) {
            days.append(day);
        }
        schedule.insert(QStringLiteral("days"), days);
    }
    QJsonObject object{
        {QStringLiteral("format"), QStringLiteral("gamescrcpy.task")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("name"), entry.name},
        {QStringLiteral("phones"), QJsonArray::fromStringList(entry.phones)},
        {QStringLiteral("schedule"), schedule},
        {QStringLiteral("onError"), entry.onError},
        {QStringLiteral("wake"), entry.wake},
        {QStringLiteral("sleepAfter"), entry.sleepAfter},
        {QStringLiteral("values"), QJsonObject::fromVariantMap(entry.values)},
    };
    if (!entry.lastScheduled.isEmpty()) {
        object.insert(QStringLiteral("lastScheduled"), entry.lastScheduled);
    }
    if (!entry.example.isEmpty()) {
        object.insert(QStringLiteral("example"), entry.example);
    }
    const std::string bytes = QJsonDocument(object).toJson(QJsonDocument::Indented).toStdString();
    if (platform::writeFileAtomically(toPath(entry.folder) / "task.json", bytes)) {
        return false;
    }
    refreshList();
    return true;
}

void TaskLibrary::reload()
{
    std::vector<TaskEntry> entries;
    std::error_code error;
    for (std::filesystem::directory_iterator it(m_directory, error);
         !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        std::error_code kind;
        if (!it->is_directory(kind)) {
            continue;
        }
        const std::filesystem::path folder = it->path();
        const QString code = readText(folder / "task.ts");
        if (code.isEmpty() && !std::filesystem::exists(folder / "task.json", kind)) {
            continue;
        }
        TaskEntry entry{.id = QString::fromStdWString(folder.filename().wstring()), .folder = pathText(folder)};
        const QJsonObject object = QJsonDocument::fromJson(readText(folder / "task.json").toUtf8()).object();
        entry.name = object.value(QStringLiteral("name")).toString(entry.id);
        for (const QJsonValue phone : object.value(QStringLiteral("phones")).toArray()) {
            entry.phones.append(phone.toString());
        }
        const QJsonObject schedule = object.value(QStringLiteral("schedule")).toObject();
        entry.schedule = schedule.value(QStringLiteral("mode")).toString(entry.schedule);
        entry.time = schedule.value(QStringLiteral("time")).toString(entry.time);
        for (const QJsonValue day : schedule.value(QStringLiteral("days")).toArray()) {
            const int number = day.toInt();
            if (number >= 1 && number <= kDaysInWeek && !entry.days.contains(number)) {
                entry.days.append(number);
            }
        }
        std::sort(entry.days.begin(), entry.days.end());
        entry.onError = object.value(QStringLiteral("onError")).toString(entry.onError);
        entry.wake = object.value(QStringLiteral("wake")).toBool();
        entry.sleepAfter = object.value(QStringLiteral("sleepAfter")).toBool();
        entry.values = object.value(QStringLiteral("values")).toObject().toVariantMap();
        entry.lastScheduled = object.value(QStringLiteral("lastScheduled")).toString();
        entry.example = object.value(QStringLiteral("example")).toString();
        entry.params = taskParamsIn(code);
        entries.push_back(std::move(entry));
    }
    std::ranges::sort(entries, [](const TaskEntry& left, const TaskEntry& right) {
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });
    std::vector<TaskEntry> contributed;
    for (const ExtensionTask& task : m_extensionTasks) {
        const QString code = readText(task.folder / "task.ts");
        if (code.isEmpty()) {
            continue;
        }
        const QJsonObject object = QJsonDocument::fromJson(readText(task.folder / "task.json").toUtf8()).object();
        TaskEntry entry{.id = task.id,
                        .name = object.value(QStringLiteral("name")).toString(task.name),
                        .folder = pathText(task.folder)};
        entry.onError = object.value(QStringLiteral("onError")).toString(entry.onError);
        entry.wake = object.value(QStringLiteral("wake")).toBool();
        entry.sleepAfter = object.value(QStringLiteral("sleepAfter")).toBool();
        entry.values = object.value(QStringLiteral("values")).toObject().toVariantMap();
        entry.params = taskParamsIn(code);
        entry.readOnly = true;
        entry.extensionId = task.extensionId;
        entry.extensionName = task.extensionName;
        contributed.push_back(std::move(entry));
    }
    std::ranges::sort(contributed, [](const TaskEntry& left, const TaskEntry& right) {
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });
    std::ranges::move(contributed, std::back_inserter(entries));
    m_entries = std::move(entries);
    refreshList();
    watch();
}

void TaskLibrary::refreshList()
{
    QVariantList list;
    for (const TaskEntry& entry : m_entries) {
        QVariantList params;
        for (const TaskParamSpec& spec : entry.params) {
            QVariantMap param{{QStringLiteral("name"), spec.name},
                              {QStringLiteral("kind"), spec.kind},
                              {QStringLiteral("label"), spec.label.isEmpty() ? spec.name : shownLabel(spec.label)},
                              {QStringLiteral("value"), chosenValue(spec, entry.values)},
                              {QStringLiteral("fallback"), spec.value}};
            if (spec.min) {
                param.insert(QStringLiteral("min"), *spec.min);
            }
            if (spec.max) {
                param.insert(QStringLiteral("max"), *spec.max);
            }
            params.append(param);
        }
        QVariantList days;
        for (const int day : entry.days) {
            days.append(day);
        }
        const TaskExample* origin = entry.example.isEmpty() ? nullptr : findExample(entry.example);
        list.append(QVariantMap{{QStringLiteral("id"), entry.id},
                                {QStringLiteral("name"), entry.name},
                                {QStringLiteral("folder"), entry.folder},
                                {QStringLiteral("phones"), entry.phones},
                                {QStringLiteral("schedule"), entry.schedule},
                                {QStringLiteral("time"), entry.time},
                                {QStringLiteral("days"), days},
                                {QStringLiteral("onError"), entry.onError},
                                {QStringLiteral("wake"), entry.wake},
                                {QStringLiteral("sleepAfter"), entry.sleepAfter},
                                {QStringLiteral("params"), params},
                                {QStringLiteral("readOnly"), entry.readOnly},
                                {QStringLiteral("extension"), entry.extensionName},
                                {QStringLiteral("exampleNote"), origin != nullptr ? origin->description : QString()}});
    }
    if (list != m_list) {
        m_list = std::move(list);
        emit tasksChanged();
    }
}

void TaskLibrary::watch()
{
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty()) {
        m_watcher.removePaths(watched);
    }
    QStringList paths{pathText(m_directory)};
    for (const TaskEntry& entry : m_entries) {
        if (!entry.readOnly) {
            paths << entry.folder << entry.folder + QStringLiteral("/task.ts");
        }
    }
    paths.removeIf([](const QString& path) { return !QFile::exists(path); });
    if (!paths.isEmpty()) {
        m_watcher.addPaths(paths);
    }
}

const TaskExample* TaskLibrary::findExample(const QString& id) const
{
    const auto found = std::ranges::find(m_examples, id, &TaskExample::id);
    return found != m_examples.end() ? &*found : nullptr;
}

void TaskLibrary::setExamplesFolder(std::filesystem::path folder)
{
    m_examplesDirectory = std::move(folder);
    std::vector<std::filesystem::path> folders;
    std::error_code error;
    for (std::filesystem::directory_iterator it(m_examplesDirectory, error), end; !error && it != end;
         it.increment(error)) {
        std::error_code kind;
        if (it->is_directory(kind)) {
            folders.push_back(it->path());
        }
    }
    std::ranges::sort(folders);
    std::vector<TaskExample> shelf;
    for (const std::filesystem::path& each : folders) {
        if (std::optional<TaskExample> example = exampleIn(each)) {
            shelf.push_back(std::move(*example));
        }
    }
    std::ranges::stable_sort(shelf, {}, [](const TaskExample& example) { return exampleRank(example.id); });
    QVariantList list;
    for (const TaskExample& example : shelf) {
        QVariantList params;
        for (const TaskParamSpec& spec : example.params) {
            QVariantMap param{{QStringLiteral("name"), spec.name},
                              {QStringLiteral("kind"), spec.kind},
                              {QStringLiteral("label"), spec.label.isEmpty() ? spec.name : spec.label},
                              {QStringLiteral("value"), spec.value}};
            if (spec.min) {
                param.insert(QStringLiteral("min"), *spec.min);
            }
            if (spec.max) {
                param.insert(QStringLiteral("max"), *spec.max);
            }
            params.append(param);
        }
        list.append(QVariantMap{{QStringLiteral("id"), example.id},
                                {QStringLiteral("name"), example.name},
                                {QStringLiteral("description"), example.description},
                                {QStringLiteral("icon"), example.icon},
                                {QStringLiteral("schedule"), example.schedule},
                                {QStringLiteral("time"), example.time},
                                {QStringLiteral("params"), params}});
    }
    m_examples = std::move(shelf);
    if (list != m_exampleList) {
        m_exampleList = std::move(list);
        emit examplesChanged();
    }
    refreshList();
}

QString TaskLibrary::copyExample(const QString& id)
{
    const TaskExample* example = findExample(id);
    if (example == nullptr) {
        return {};
    }
    const QString base = idFor(id.section(u'/', 1));
    QString target = base;
    std::error_code error;
    for (int copy = 2; std::filesystem::exists(m_directory / toPath(target), error); ++copy) {
        target = base + u'-' + QString::number(copy);
    }
    const std::filesystem::path folder = m_directory / toPath(target);
    std::filesystem::copy(toPath(example->folder), folder, std::filesystem::copy_options::recursive, error);
    TaskEntry entry{.id = target, .name = freeNameIn(m_entries, example->name), .folder = pathText(folder)};
    entry.time = example->time;
    entry.days = example->days;
    entry.example = example->id;
    if (error || !write(entry)) {
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
        return {};
    }
    emit taskCreated(target);
    reload();
    return target;
}

}
