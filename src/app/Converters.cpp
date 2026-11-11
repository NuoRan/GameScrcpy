#include "Converters.h"

#include "ExtensionExport.h"
#include "ExtensionHost.h"
#include "ProfileLibrary.h"
#include "TaskLibrary.h"

#include <gs/ext/Manifest.h>
#include <gs/platform/AtomicFile.h>
#include <gs/record/Recording.h>
#include <gs/script/LogicHost.h>
#include <gs/script/Services.h>
#include <gs/scripthost/RemoteLogicHost.h>

#include <nlohmann/json.hpp>

#include <QChar>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <mutex>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

using Json = nlohmann::json;

constexpr int kTickMs = 250;
constexpr qsizetype kBytePart = qsizetype{512} * 1024;
constexpr qsizetype kTextPart = qsizetype{256} * 1024;
constexpr qsizetype kMostNameChars = 255;
constexpr qsizetype kMostProblemChars = 300;
constexpr qint64 kMostOwnProfileBytes = 1024 * 1024;
constexpr std::string_view kInputService = "convert.input";
constexpr std::string_view kReadService = "convert.read";
constexpr std::string_view kWriteService = "convert.write";
constexpr std::string_view kDoneService = "convert.done";
constexpr std::array kServices{kInputService, kReadService, kWriteService, kDoneService};

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::filesystem::path pathOf(std::string_view utf8)
{
    return {std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size())};
}

ConvertOutcome failure(QString problem)
{
    return {.ok = false, .name = {}, .bytes = {}, .problem = std::move(problem)};
}

script::ServiceAnswer refused(std::string_view code, std::string problem)
{
    return {"null", std::string(code), std::move(problem)};
}

script::Limits converterLimits(std::chrono::milliseconds limit)
{
    return {.memoryWarningBytes = std::size_t{160} << 20,
            .memoryLimitBytes = std::size_t{192} << 20,
            .stackBytes = std::size_t{1} << 20,
            .sliceBudget = std::chrono::duration_cast<Microseconds>(limit),
            .spikeThreshold = Microseconds{0},
            .sustainedWindow = Microseconds{1'000'000},
            .sustainedBusy = Microseconds{0}};
}

QString problemOf(const script::Report& report)
{
    if (report.kind == script::ReportKind::MemoryLimit) {
        return Converters::tr("it used too much memory");
    }
    if (report.kind == script::ReportKind::TaskInterrupted ||
        (report.kind == script::ReportKind::ScriptFailed && report.limit.count() > 0)) {
        return Converters::tr("it ran too long without waiting");
    }
    QString text = QString::fromStdString(report.message).simplified();
    if (report.kind == script::ReportKind::ScriptFailed && text == toQString(scripthost::kStoppedText)) {
        return Converters::tr("its logic stopped unexpectedly");
    }
    static const QRegularExpression kind(QStringLiteral("^(Error: |[A-Za-z]+Error: (?=GS-API-))"));
    text.remove(kind);
    if (text.size() > kMostProblemChars) {
        text = text.left(kMostProblemChars - 1) + QChar(0x2026);
    }
    return text.isEmpty() ? Converters::tr("it failed without saying why") : text;
}

bool ownProfile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMostOwnProfileBytes) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.read(kMostOwnProfileBytes));
    return document.isObject() &&
           document.object().value(QStringLiteral("format")).toString() == QLatin1StringView("gamescrcpy.profile");
}

std::optional<QByteArray> readInput(const std::filesystem::path& file, QString& problem)
{
    std::string contents;
    if (const std::error_code error =
            platform::readFile(file, static_cast<std::size_t>(Converters::kMostBytes), contents)) {
        problem = error == std::errc::file_too_large ? Converters::tr("it is larger than 32 MiB")
                                                      : Converters::tr("it cannot be read");
        return std::nullopt;
    }
    return QByteArray::fromStdString(contents);
}

}

bool ConverterEntry::imports() const
{
    return ext::appFormat(to.toStdString());
}

class Converters::Bridge final : public script::ScriptServices
{
public:
    Bridge(ConvertInput input, QString from, QString to)
        : m_input(std::move(input))
        , m_from(std::move(from))
        , m_to(std::move(to))
    {
    }

    script::DeviceInfo device() override { return {}; }
    void tap(std::uint64_t, script::Point, double) override {}
    void swipe(std::uint64_t, script::Point, script::Point, double) override {}
    void key(std::string_view, bool) override {}
    std::vector<script::ControlInfo> controls() override { return {}; }
    bool pressControl(std::string_view, bool) override { return false; }
    bool enableControl(std::string_view, bool) override { return false; }
    std::vector<script::StateInfo> states() override { return {}; }
    std::optional<script::VarValue> stateValue(std::string_view) override { return std::nullopt; }
    bool setState(std::string_view, const script::VarValue&) override { return false; }
    std::optional<std::string> loadStore(std::string_view) override { return std::nullopt; }
    void saveStore(std::string_view, std::string_view) override {}

    bool offersService(std::string_view name) override { return std::ranges::find(kServices, name) != kServices.end(); }

    script::ServiceAnswer callService(std::string_view name, std::string_view json) override
    {
        const Json asked = Json::parse(json.begin(), json.end(), nullptr, false);
        if (asked.is_discarded() || !asked.is_object()) {
            return refused("GS-API-166", "the converter sent something that is not a JSON object");
        }
        const std::scoped_lock lock(m_mutex);
        if (name == kInputService) {
            return {Json{{"name", m_input.name.toStdString()},
                         {"from", m_from.toStdString()},
                         {"to", m_to.toStdString()},
                         {"size", static_cast<std::int64_t>(m_input.bytes.size())}}
                        .dump(-1, ' ', false, Json::error_handler_t::replace),
                    {},
                    {}};
        }
        if (name == kReadService) {
            return read(asked);
        }
        if (name == kWriteService) {
            return write(asked);
        }
        if (name == kDoneService) {
            const auto named = asked.find("name");
            const QString given =
                named != asked.end() && named->is_string() ? QString::fromStdString(named->get<std::string>()) : QString();
            m_name = QFileInfo(given).fileName().left(kMostNameChars);
            m_done = true;
            return {"true", {}, {}};
        }
        return refused("GS-API-070", "no part of GameScrcpy offers this service");
    }

    [[nodiscard]] ConvertOutcome outcome() const
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_done) {
            return failure(Converters::tr("it ended without a result"));
        }
        return {.ok = true, .name = m_name, .bytes = m_output, .problem = {}};
    }

private:
    script::ServiceAnswer read(const Json& asked)
    {
        const auto at = asked.find("at");
        if (at == asked.end() || !at->is_number_integer() || at->get<std::int64_t>() < 0) {
            return refused("GS-API-168", "the converter asked for a part of the file that is not there");
        }
        const auto from = static_cast<qsizetype>(at->get<std::int64_t>());
        const auto text = asked.find("text");
        if (text != asked.end() && text->is_boolean() && text->get<bool>()) {
            if (!m_text) {
                m_text = QString::fromUtf8(m_input.bytes);
            }
            const qsizetype length = m_text->size();
            if (from > length) {
                return refused("GS-API-168", "the converter asked for a part of the text that is not there");
            }
            qsizetype count = std::min(kTextPart, length - from);
            if (count > 1 && from + count < length && m_text->at(from + count - 1).isHighSurrogate()) {
                --count;
            }
            return {Json{{"text", m_text->mid(from, count).toStdString()}, {"end", from + count >= length}}.dump(
                        -1, ' ', false, Json::error_handler_t::replace),
                    {},
                    {}};
        }
        if (from >= m_input.bytes.size()) {
            return refused("GS-API-168", "the converter asked for a part of the file that is not there");
        }
        return {Json{{"data", m_input.bytes.mid(from, kBytePart).toBase64().toStdString()}}.dump(), {}, {}};
    }

    script::ServiceAnswer write(const Json& given)
    {
        if (m_done) {
            return refused("GS-API-166", "the result is already complete");
        }
        QByteArray part;
        const auto data = given.find("data");
        const auto text = given.find("text");
        if (data != given.end() && data->is_string()) {
            const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(
                QByteArray::fromStdString(data->get<std::string>()), QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) {
                return refused("GS-API-166", "the bytes of the result are damaged");
            }
            part = *decoded;
        } else if (text != given.end() && text->is_string()) {
            part = QByteArray::fromStdString(text->get<std::string>());
        } else {
            return refused("GS-API-166", "the converter sent neither bytes nor text");
        }
        if (m_output.size() + part.size() > kMostBytes) {
            return refused("GS-API-167", "the result is larger than 32 MiB");
        }
        m_output.append(part);
        return {"true", {}, {}};
    }

    mutable std::mutex m_mutex;
    const ConvertInput m_input;
    const QString m_from;
    const QString m_to;
    std::optional<QString> m_text;
    QByteArray m_output;
    QString m_name;
    bool m_done = false;
};

struct Converters::Running {
    std::uint64_t id = 0;
    ConverterEntry entry;
    QString file;
    Done done;
    std::shared_ptr<Bridge> bridge;
    std::unique_ptr<scripthost::RemoteLogicHost> host;
    script::ScriptId script = 0;
    script::TaskId task = 0;
    std::chrono::steady_clock::time_point started;
};

Converters::Converters(QObject* parent)
    : QObject(parent)
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    m_clock.setInterval(kTickMs);
    connect(&m_clock, &QTimer::timeout, this, &Converters::checkLimit);
}

Converters::~Converters()
{
    m_running.reset();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

Converters* Converters::create(QQmlEngine*, QJSEngine*)
{
    if (s_instance == nullptr) {
        auto* made = new Converters(nullptr);
        QJSEngine::setObjectOwnership(made, QJSEngine::JavaScriptOwnership);
        return made;
    }
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void Converters::setConverters(std::vector<ConverterEntry> converters)
{
    if (converters == m_converters) {
        return;
    }
    m_converters = std::move(converters);
    m_list.clear();
    for (const ConverterEntry& entry : m_converters) {
        m_list.append(QVariantMap{{QStringLiteral("key"), entry.key()},
                                  {QStringLiteral("extensionId"), entry.extensionId},
                                  {QStringLiteral("extensionName"), entry.extensionName},
                                  {QStringLiteral("name"), entry.name},
                                  {QStringLiteral("title"), entry.title},
                                  {QStringLiteral("from"), entry.from},
                                  {QStringLiteral("to"), entry.to},
                                  {QStringLiteral("extensions"), entry.extensions},
                                  {QStringLiteral("imports"), entry.imports()}});
    }
    emit convertersChanged();
}

void Converters::setTimeLimit(std::chrono::milliseconds limit)
{
    m_limit = limit;
}

void Converters::follow(ExtensionHost& extensions)
{
    const auto update = [this, &extensions] {
        std::vector<ConverterEntry> next;
        for (const ExtensionHost::Extension& extension : extensions.extensions()) {
            if (!extension.active || !extension.manifest) {
                continue;
            }
            const ext::Manifest& manifest = *extension.manifest;
            for (const ext::ConverterContribution& converter : manifest.contributes.converters) {
                std::error_code error;
                if (!std::filesystem::is_regular_file(extension.directory / pathOf(converter.script), error)) {
                    continue;
                }
                ConverterEntry made{
                    .extensionId = toQString(manifest.id),
                    .extensionName = extensions.textOf(manifest, "name", manifest.name),
                    .name = toQString(converter.name),
                    .title = extensions.textOf(manifest, "converters." + converter.name + ".title", converter.title),
                    .from = toQString(converter.from),
                    .to = toQString(converter.to),
                    .extensions = {},
                    .folder = extension.directory,
                    .script = converter.script,
                    .permissions = extensions.grantedPermissions(manifest.id)};
                for (const std::string& suffix : converter.extensions) {
                    made.extensions.append(toQString(suffix));
                }
                if (made.extensions.isEmpty() && made.imports()) {
                    made.extensions.append(made.from);
                }
                next.push_back(std::move(made));
            }
        }
        setConverters(std::move(next));
    };
    update();
    connect(&extensions, &ExtensionHost::extensionsChanged, this, update);
}

const ConverterEntry* Converters::find(const QString& key) const
{
    const auto found = std::ranges::find_if(m_converters, [&key](const ConverterEntry& entry) { return entry.key() == key; });
    return found != m_converters.end() ? &*found : nullptr;
}

std::vector<const ConverterEntry*> Converters::importersOf(const QString& format) const
{
    std::vector<const ConverterEntry*> importers;
    for (const ConverterEntry& entry : m_converters) {
        if (entry.to == format) {
            importers.push_back(&entry);
        }
    }
    return importers;
}

QStringList Converters::importFilters(const QString& format, const QString& own) const
{
    const std::vector<const ConverterEntry*> importers = importersOf(format);
    if (importers.empty()) {
        return {own};
    }
    QStringList patterns;
    const qsizetype open = own.lastIndexOf(u'(');
    const qsizetype close = own.lastIndexOf(u')');
    if (open >= 0 && close > open) {
        patterns = own.mid(open + 1, close - open - 1).split(u' ', Qt::SkipEmptyParts);
    }
    QStringList filters{QString(), own};
    for (const ConverterEntry* entry : importers) {
        QStringList globs;
        for (const QString& suffix : entry->extensions) {
            const QString glob = QStringLiteral("*.") + suffix;
            globs.append(glob);
            if (!patterns.contains(glob)) {
                patterns.append(glob);
            }
        }
        filters.append(QStringLiteral("%1 (%2)").arg(entry->title, globs.join(u' ')));
    }
    filters[0] = tr("All files that can be imported (%1)").arg(patterns.join(u' '));
    return filters;
}

QString Converters::filterConverter(const QString& format, int index) const
{
    const std::vector<const ConverterEntry*> importers = importersOf(format);
    const auto at = static_cast<std::size_t>(std::max(index - 2, 0));
    return index >= 2 && at < importers.size() ? importers[at]->key() : QString();
}

bool Converters::accepts(const QString& format, const QUrl& url) const
{
    if (!url.isLocalFile()) {
        return false;
    }
    const QFileInfo info(url.toLocalFile());
    const QString suffix = info.suffix().toLower();
    return !info.isDir() && !suffix.isEmpty() && std::ranges::any_of(importersOf(format), [&suffix](const ConverterEntry* entry) {
        return entry->extensions.contains(suffix);
    });
}

QString Converters::importerFor(const QString& format, const QUrl& url, const QString& chosen) const
{
    if (!url.isLocalFile()) {
        return {};
    }
    const QString path = url.toLocalFile();
    const QFileInfo info(path);
    if (info.isDir()) {
        return {};
    }
    if (!chosen.isEmpty()) {
        const ConverterEntry* entry = find(chosen);
        return entry != nullptr && entry->to == format ? chosen : QString();
    }
    const QString suffix = info.suffix().toLower();
    if (suffix.isEmpty() || (format == u"profile" && suffix == u"gsprofile")) {
        return {};
    }
    const std::vector<const ConverterEntry*> importers = importersOf(format);
    const auto reads = std::ranges::find_if(importers, [&suffix](const ConverterEntry* entry) {
        return entry->extensions.contains(suffix);
    });
    if (reads == importers.end() || (format == u"profile" && suffix == u"json" && ownProfile(path))) {
        return {};
    }
    return (*reads)->key();
}

int Converters::importProfiles(const QList<QUrl>& files, const QString& chosen)
{
    const int ticket = ++m_lastTicket;
    m_imports.insert_or_assign(ticket, Import{.left = static_cast<int>(files.size()), .imported = {}, .ids = {}, .failed = {}});
    if (files.isEmpty()) {
        m_imports.erase(ticket);
        const QVariantMap nothing{{QStringLiteral("imported"), QStringList()},
                                  {QStringLiteral("ids"), QStringList()},
                                  {QStringLiteral("failed"), QVariantList()}};
        QMetaObject::invokeMethod(this, [this, ticket, nothing] { emit imported(ticket, nothing); }, Qt::QueuedConnection);
        return ticket;
    }
    for (const QUrl& url : files) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        const QString file = QFileInfo(path).fileName();
        const QString key = importerFor(QStringLiteral("profile"), url, chosen);
        QString problem;
        std::optional<QByteArray> bytes;
        if (key.isEmpty()) {
            problem = tr("no converter reads this file");
        } else {
            bytes = readInput(path.toStdWString(), problem);
        }
        if (!bytes) {
            QMetaObject::invokeMethod(
                this, [this, ticket, file, problem] { settleImport(ticket, file, failure(problem)); },
                Qt::QueuedConnection);
            continue;
        }
        run(key, ConvertInput{.name = file, .bytes = std::move(*bytes)},
            [this, ticket, file](const ConvertOutcome& outcome) { settleImport(ticket, file, outcome); });
    }
    return ticket;
}

void Converters::settleImport(int ticket, const QString& file, const ConvertOutcome& outcome)
{
    const auto found = m_imports.find(ticket);
    if (found == m_imports.end()) {
        return;
    }
    Import& pending = found->second;
    QString reason = outcome.problem;
    ProfileLibrary* library = ProfileLibrary::instance();
    if (outcome.ok && library == nullptr) {
        reason = tr("profiles cannot be saved now");
    } else if (outcome.ok) {
        QString name;
        const QString id =
            library->importConverted(outcome.name.isEmpty() ? file : outcome.name, outcome.bytes, name, reason);
        if (!id.isEmpty()) {
            pending.imported.append(name);
            pending.ids.append(id);
        }
    }
    if (!reason.isEmpty()) {
        pending.failed.append(QVariantMap{{QStringLiteral("file"), file}, {QStringLiteral("reason"), reason}});
    }
    if (--pending.left > 0) {
        return;
    }
    const QVariantMap result{{QStringLiteral("imported"), pending.imported},
                             {QStringLiteral("ids"), pending.ids},
                             {QStringLiteral("failed"), pending.failed}};
    m_imports.erase(found);
    emit imported(ticket, result);
}

std::optional<ConvertInput> Converters::sourceOf(const ConverterEntry& entry, const QString& source,
                                                 QString& problem) const
{
    if (entry.from == u"profile") {
        const ProfileLibrary* library = ProfileLibrary::instance();
        const ProfileEntry* profile = library != nullptr ? library->find(source) : nullptr;
        if (profile == nullptr || profile->profile == nullptr) {
            problem = tr("the profile cannot be used");
            return std::nullopt;
        }
        return ConvertInput{.name = source.section(u'/', -1) + QStringLiteral(".json"),
                            .bytes = library->contents(source)};
    }
    std::filesystem::path file;
    QString name;
    if (entry.from == u"task") {
        const TaskLibrary* tasks = TaskLibrary::instance();
        const TaskEntry* task = tasks != nullptr ? tasks->find(source) : nullptr;
        if (task == nullptr) {
            problem = tr("the task is not there");
            return std::nullopt;
        }
        file = std::filesystem::path(task->folder.toStdWString()) / TaskLibrary::entryName().toStdWString();
        name = source.section(u'/', -1) + QStringLiteral(".ts");
    } else {
        const QString folder = QDir::cleanPath(QDir::fromNativeSeparators(source));
        if (folder.isEmpty() || !record::isRecordingFolder(folder.toStdWString())) {
            problem = tr("the recording is not there");
            return std::nullopt;
        }
        QString base = QFileInfo(folder).fileName();
        if (base.endsWith(toQString(record::kRecordingExtension), Qt::CaseInsensitive)) {
            base.chop(static_cast<qsizetype>(record::kRecordingExtension.size()));
        }
        file = std::filesystem::path(folder.toStdWString()) / pathOf(record::kSessionFile);
        name = base + QStringLiteral(".json");
    }
    std::optional<QByteArray> bytes = readInput(file, problem);
    if (!bytes) {
        return std::nullopt;
    }
    return ConvertInput{.name = name, .bytes = std::move(*bytes)};
}

int Converters::exportTo(const QString& key, const QString& source, const QString& target)
{
    const int ticket = ++m_lastTicket;
    const ConverterEntry* entry = find(key);
    const std::optional<std::filesystem::path> path = exportTarget(target);
    QString problem;
    std::optional<ConvertInput> input;
    if (entry == nullptr || entry->imports()) {
        problem = tr("Its extension was turned off, so the converter is no longer there.");
    } else if (!path) {
        problem = tr("Choose a file on this computer to export to.");
    } else if (input = sourceOf(*entry, source, problem); !input) {
        problem = tr("Could not export as %1: %2").arg(entry->title, problem);
    }
    if (!input) {
        QMetaObject::invokeMethod(
            this, [this, ticket, problem] { emit exported(ticket, false, problem); }, Qt::QueuedConnection);
        return ticket;
    }
    const QString title = entry->title;
    const QString shown = QFileInfo(QString::fromStdWString(path->wstring())).fileName();
    run(key, std::move(*input), [this, ticket, title, shown, file = *path](const ConvertOutcome& outcome) {
        if (!outcome.ok) {
            emit exported(ticket, false, tr("Could not export as %1: %2").arg(title, outcome.problem));
            return;
        }
        const std::string_view contents(outcome.bytes.constData(), static_cast<std::size_t>(outcome.bytes.size()));
        if (const std::error_code error = platform::writeFileAtomically(file, contents)) {
            qWarning("converter: could not write %s (error %d)", qUtf8Printable(shown), error.value());
            emit exported(ticket, false, tr("Could not save %1.").arg(shown));
            return;
        }
        emit exported(ticket, true, tr("Exported as %1: %2").arg(title, shown));
    });
    return ticket;
}

void Converters::run(const QString& key, ConvertInput input, Done done)
{
    const bool before = busy();
    m_waiting.push_back(Job{.key = key, .input = std::move(input), .done = std::move(done)});
    if (!before) {
        emit busyChanged();
    }
    QMetaObject::invokeMethod(this, [this] { startNext(); }, Qt::QueuedConnection);
}

void Converters::startNext()
{
    if (m_running != nullptr || m_waiting.empty()) {
        return;
    }
    Job job = std::move(m_waiting.front());
    m_waiting.pop_front();
    const ConverterEntry* entry = find(job.key);
    const QString problem = entry == nullptr ? tr("its extension was turned off")
                            : job.input.bytes.size() > kMostBytes ? tr("it is larger than 32 MiB")
                                                                  : QString();
    if (!problem.isEmpty()) {
        if (job.done) {
            job.done(failure(problem));
        }
        if (busy()) {
            QMetaObject::invokeMethod(this, [this] { startNext(); }, Qt::QueuedConnection);
        } else {
            emit busyChanged();
        }
        return;
    }
    auto running = std::make_unique<Running>();
    running->id = ++m_lastRun;
    running->entry = *entry;
    running->file = job.input.name;
    running->done = std::move(job.done);
    running->bridge = std::make_shared<Bridge>(std::move(job.input), entry->from, entry->to);
    running->started = std::chrono::steady_clock::now();
    const std::uint64_t id = running->id;
    running->host = std::make_unique<scripthost::RemoteLogicHost>(
        [this, id](const script::Report& report) {
            QMetaObject::invokeMethod(this, [this, id, report] { handle(id, report); }, Qt::QueuedConnection);
        },
        converterLimits(m_limit), running->bridge,
        scripthost::RemoteOptions{.program = {}, .label = "convert " + running->entry.key().toStdString(),
                                  .restrictedOnly = false});
    qInfo("converter %s: converting %s in the logic sandbox", qUtf8Printable(running->entry.key()),
          qUtf8Printable(running->file));
    m_running = std::move(running);
    m_clock.start();
    m_running->script = m_running->host->load(script::ScriptSource{.folder = m_running->entry.folder,
                                                                   .entry = m_running->entry.script,
                                                                   .permissions = m_running->entry.permissions,
                                                                   .phone = false});
}

void Converters::handle(std::uint64_t id, const script::Report& report)
{
    if (m_running == nullptr || m_running->id != id) {
        return;
    }
    const QByteArray key = m_running->entry.key().toUtf8();
    const std::string text = script::describe(report);
    const bool own = report.task == m_running->task && m_running->task != 0;
    if (report.kind == script::ReportKind::ScriptLoaded) {
        if (m_running->task == 0) {
            m_running->task = m_running->host->start(m_running->script, std::string(script::kConvertHandler));
        }
        return;
    }
    if (report.kind == script::ReportKind::TaskFinished && own) {
        finish(id, m_running->bridge->outcome());
        return;
    }
    if (report.kind == script::ReportKind::TaskCancelled && own) {
        finish(id, failure(tr("it was stopped")));
        return;
    }
    const bool failed = report.kind == script::ReportKind::ScriptFailed ||
                        report.kind == script::ReportKind::MemoryLimit ||
                        ((report.kind == script::ReportKind::TaskFailed ||
                          report.kind == script::ReportKind::TaskInterrupted) &&
                         own);
    if (failed) {
        qWarning("converter %s: %s", key.constData(), text.c_str());
        finish(id, failure(problemOf(report)));
        return;
    }
    if (report.kind == script::ReportKind::Log && report.level != log::Level::Error &&
        report.level != log::Level::Warning) {
        qInfo("converter %s: %s", key.constData(), text.c_str());
    } else if (report.kind != script::ReportKind::DebugHits && report.kind != script::ReportKind::DebugPaused &&
               report.kind != script::ReportKind::DebugResumed) {
        qWarning("converter %s: %s", key.constData(), text.c_str());
    }
}

void Converters::finish(std::uint64_t id, ConvertOutcome outcome)
{
    if (m_running == nullptr || m_running->id != id) {
        return;
    }
    std::unique_ptr<Running> ended = std::move(m_running);
    m_clock.stop();
    const Done done = std::move(ended->done);
    const QString key = ended->entry.key();
    const QString file = ended->file;
    ended.reset();
    if (outcome.ok) {
        qInfo("converter %s: converted %s into %lld bytes", qUtf8Printable(key), qUtf8Printable(file),
              static_cast<long long>(outcome.bytes.size()));
    } else {
        qWarning("converter %s: could not convert %s: %s", qUtf8Printable(key), qUtf8Printable(file),
                 qUtf8Printable(outcome.problem));
    }
    if (done) {
        done(outcome);
    }
    if (busy()) {
        QMetaObject::invokeMethod(this, &Converters::startNext, Qt::QueuedConnection);
    } else {
        emit busyChanged();
    }
}

void Converters::checkLimit()
{
    if (m_running == nullptr) {
        m_clock.stop();
        return;
    }
    if (std::chrono::steady_clock::now() - m_running->started < m_limit) {
        return;
    }
    const auto seconds = static_cast<int>((m_limit.count() + 999) / 1000);
    finish(m_running->id, failure(tr("it took longer than %n second(s)", "", seconds)));
}

}
