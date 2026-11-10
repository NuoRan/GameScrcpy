#include "FileTransfers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QStringView>

#include <algorithm>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr std::uint32_t kTypeMask = 0170000;
constexpr std::uint32_t kDirectoryType = 0040000;

std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdWString());
}

QString fromPath(const std::filesystem::path& path)
{
    return QDir::fromNativeSeparators(QString::fromStdWString(path.wstring()));
}

QString localFile(const QString& text)
{
    if (text.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        return QUrl(text).toLocalFile();
    }
    return text;
}

QString withoutTrailingSlash(QString path)
{
    while (path.size() > 1 && path.endsWith(u'/')) {
        path.chop(1);
    }
    return path;
}

QString failureText(const adb::AdbError& error)
{
    switch (error.kind) {
    case adb::AdbErrorKind::ServerUnavailable:
    case adb::AdbErrorKind::ServerTooOld:
    case adb::AdbErrorKind::LaunchFailed:
        return FileTransfers::tr("adb is not running");
    case adb::AdbErrorKind::Rejected:
        return error.message.empty()
                   ? FileTransfers::tr("The phone refused the transfer")
                   : FileTransfers::tr("The phone refused: %1").arg(QString::fromStdString(error.message));
    case adb::AdbErrorKind::Protocol:
        return FileTransfers::tr("The phone answered something unexpected");
    case adb::AdbErrorKind::Io:
        break;
    }
    if (error.message.find("local file") != std::string::npos) {
        return FileTransfers::tr("The file on this computer could not be read or written");
    }
    return FileTransfers::tr("The connection to the phone was lost");
}

std::optional<std::filesystem::path> freeLocalPath(const std::filesystem::path& folder, const QString& name,
                                                   bool directory)
{
    const QString safe = windowsName(name);
    for (int number = 1; number <= FileTransfers::kMostNameTries; ++number) {
        const std::filesystem::path candidate =
            folder / toPath(number == 1 ? safe : numberedName(safe, number, directory));
        std::error_code error;
        const bool taken = std::filesystem::exists(candidate, error);
        if (!taken && !error) {
            return candidate;
        }
    }
    return std::nullopt;
}

bool finishedState(const QString& state)
{
    return state == u"done" || state == u"failed" || state == u"cancelled";
}

}

QString transferStateName(TransferState state)
{
    switch (state) {
    case TransferState::Queued:
        return QStringLiteral("queued");
    case TransferState::Running:
        return QStringLiteral("running");
    case TransferState::Done:
        return QStringLiteral("done");
    case TransferState::Failed:
        return QStringLiteral("failed");
    case TransferState::Cancelled:
        return QStringLiteral("cancelled");
    }
    return {};
}

QVariantMap transferVariant(const TransferJob& job)
{
    return {{QStringLiteral("id"), job.id},
            {QStringLiteral("serial"), job.serial},
            {QStringLiteral("direction"),
             job.direction == TransferDirection::Push ? QStringLiteral("push") : QStringLiteral("pull")},
            {QStringLiteral("name"), job.name},
            {QStringLiteral("done"), job.done},
            {QStringLiteral("total"), job.total},
            {QStringLiteral("state"), transferStateName(job.state)},
            {QStringLiteral("message"), job.message},
            {QStringLiteral("local"), job.local},
            {QStringLiteral("remote"), job.remote},
            {QStringLiteral("tag"), job.tag}};
}

QString remoteChild(const QString& folder, const QString& name)
{
    return folder.endsWith(u'/') ? folder + name : folder + u'/' + name;
}

QString remoteParent(const QString& path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash <= 0 ? QStringLiteral("/") : path.left(slash);
}

QString remoteName(const QString& path)
{
    return path.mid(path.lastIndexOf(u'/') + 1);
}

QString numberedName(const QString& name, int number, bool directory)
{
    const qsizetype dot = directory ? -1 : name.lastIndexOf(u'.');
    if (dot <= 0) {
        return QStringLiteral("%1 (%2)").arg(name, QString::number(number));
    }
    return QStringLiteral("%1 (%2)%3").arg(name.left(dot), QString::number(number), name.mid(dot));
}

QString windowsName(const QString& name)
{
    static constexpr QStringView kForbidden = u"<>:\"/\\|?*";
    QString safe;
    safe.reserve(name.size());
    for (const QChar c : name) {
        safe.append(c.unicode() < 0x20 || kForbidden.contains(c) ? QChar(u'_') : c);
    }
    while (!safe.isEmpty() && (safe.back() == u'.' || safe.back() == u' ')) {
        safe.chop(1);
    }
    if (safe.isEmpty()) {
        return QStringLiteral("_");
    }
    static const QStringList reserved{
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),  QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9")};
    if (reserved.contains(safe.section(u'.', 0, 0).toUpper())) {
        safe.prepend(u'_');
    }
    return safe;
}

FileTransfers::FileTransfers(adb::AdbClient adb, QObject* parent) : QObject(parent), m_adb(adb)
{
    m_workers.reserve(kWorkers);
    for (std::size_t i = 0; i < kWorkers; ++i) {
        m_workers.emplace_back([this](const std::stop_token& stop) { work(stop); });
    }
}

FileTransfers::~FileTransfers()
{
    {
        const std::lock_guard lock(m_mutex);
        m_queue.clear();
        for (auto& running : m_running) {
            running.second.request_stop();
        }
    }
    for (std::jthread& worker : m_workers) {
        worker.request_stop();
    }
    for (std::jthread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

qint64 FileTransfers::push(const QString& serial, const QString& localPath, const QString& remoteFolder,
                           const QString& tag)
{
    const QString source = QDir::cleanPath(QDir::fromNativeSeparators(localFile(localPath)));
    const QString folder = withoutTrailingSlash(remoteFolder.trimmed());
    TransferJob job;
    job.serial = serial;
    job.direction = TransferDirection::Push;
    job.name = QFileInfo(source).fileName();
    job.local = source;
    job.remote = folder;
    job.tag = tag;
    if (serial.isEmpty() || job.name.isEmpty() || !folder.startsWith(u'/')) {
        job.state = TransferState::Failed;
        job.message = tr("This cannot be sent to the phone");
    }
    Pending pending;
    pending.serial = serial;
    pending.direction = TransferDirection::Push;
    pending.local = source;
    pending.remote = folder;
    pending.name = job.name;
    return enqueue(std::move(job), std::move(pending));
}

qint64 FileTransfers::pull(const QString& serial, const QString& remotePath, const QString& localFolder,
                           const QString& tag)
{
    const QString source = withoutTrailingSlash(remotePath.trimmed());
    const QString folder = QDir::cleanPath(QDir::fromNativeSeparators(localFile(localFolder)));
    TransferJob job;
    job.serial = serial;
    job.direction = TransferDirection::Pull;
    job.name = remoteName(source);
    job.local = folder;
    job.remote = source;
    job.tag = tag;
    if (serial.isEmpty() || job.name.isEmpty() || !source.startsWith(u'/') || localFolder.trimmed().isEmpty()) {
        job.state = TransferState::Failed;
        job.message = tr("This cannot be copied from the phone");
    }
    Pending pending;
    pending.serial = serial;
    pending.direction = TransferDirection::Pull;
    pending.local = folder;
    pending.remote = source;
    pending.name = job.name;
    return enqueue(std::move(job), std::move(pending));
}

qint64 FileTransfers::enqueue(TransferJob job, Pending pending)
{
    job.id = ++m_lastId;
    pending.id = job.id;
    m_jobs.push_back(job);
    if (job.finished()) {
        trim();
    } else {
        {
            const std::lock_guard lock(m_mutex);
            m_queue.push_back(std::move(pending));
        }
        m_wake.notify_all();
    }
    emit updated(transferVariant(job));
    return job.id;
}

bool FileTransfers::cancel(qint64 id)
{
    bool removed = false;
    {
        const std::lock_guard lock(m_mutex);
        if (const auto queued = std::ranges::find(m_queue, id, &Pending::id); queued != m_queue.end()) {
            m_queue.erase(queued);
            removed = true;
        } else if (const auto running = m_running.find(id); running != m_running.end()) {
            running->second.request_stop();
            return true;
        }
    }
    if (!removed) {
        return false;
    }
    const auto found = std::ranges::find(m_jobs, id, &TransferJob::id);
    if (found != m_jobs.end()) {
        found->state = TransferState::Cancelled;
        const TransferJob current = *found;
        trim();
        emit updated(transferVariant(current));
    }
    return true;
}

void FileTransfers::clearFinished(const QString& serial)
{
    std::erase_if(m_jobs, [&serial](const TransferJob& job) { return job.serial == serial && job.finished(); });
}

std::optional<TransferJob> FileTransfers::job(qint64 id) const
{
    const auto found = std::ranges::find(m_jobs, id, &TransferJob::id);
    return found == m_jobs.end() ? std::nullopt : std::optional<TransferJob>(*found);
}

std::vector<TransferJob> FileTransfers::jobs(const QString& serial) const
{
    std::vector<TransferJob> shown;
    for (auto it = m_jobs.rbegin(); it != m_jobs.rend(); ++it) {
        if (it->serial == serial) {
            shown.push_back(*it);
        }
    }
    return shown;
}

std::optional<std::size_t> FileTransfers::readyIndex() const
{
    for (std::size_t i = 0; i < m_queue.size(); ++i) {
        if (!m_busy.contains(m_queue[i].serial)) {
            return i;
        }
    }
    return std::nullopt;
}

void FileTransfers::work(const std::stop_token& stop)
{
    for (;;) {
        Pending pending;
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return readyIndex().has_value(); })) {
                return;
            }
            const auto at = m_queue.begin() + static_cast<std::ptrdiff_t>(*readyIndex());
            pending = std::move(*at);
            m_queue.erase(at);
            m_busy.insert(pending.serial);
            m_running.emplace(pending.id, pending.stop);
        }
        {
            const std::stop_callback forward(stop, [source = pending.stop]() mutable { source.request_stop(); });
            run(pending);
        }
        {
            const std::lock_guard lock(m_mutex);
            m_busy.erase(pending.serial);
            m_running.erase(pending.id);
        }
        m_wake.notify_all();
    }
}

void FileTransfers::run(const Pending& pending)
{
    const std::stop_token stop = pending.stop.get_token();
    TransferJob job;
    job.id = pending.id;
    job.serial = pending.serial;
    job.direction = pending.direction;
    job.name = pending.name;
    job.state = TransferState::Running;
    job.local = pending.local;
    job.remote = pending.remote;
    report(job);
    auto last = std::chrono::steady_clock::now();
    const Progress progress = [this, &job, &last](std::uint64_t done, std::uint64_t total) {
        job.done = static_cast<double>(done);
        job.total = static_cast<double>(total);
        const auto now = std::chrono::steady_clock::now();
        if (now - last >= kReportEvery) {
            last = now;
            report(job);
        }
    };
    const Outcome outcome = pending.direction == TransferDirection::Push ? pushItem(pending, job, stop, progress)
                                                                        : pullItem(pending, job, stop, progress);
    job.state = outcome.state == TransferState::Done || !stop.stop_requested() ? outcome.state
                                                                               : TransferState::Cancelled;
    job.message = job.state == TransferState::Failed ? outcome.message : QString();
    if (job.state == TransferState::Done) {
        job.total = std::max(job.total, job.done);
        job.done = job.total;
    }
    report(job);
}

FileTransfers::Outcome FileTransfers::pushItem(const Pending& pending, TransferJob& job, const std::stop_token& stop,
                                               const Progress& progress)
{
    const std::filesystem::path source = toPath(pending.local);
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(source, error);
    if (error || !std::filesystem::exists(status)) {
        return {TransferState::Failed, tr("The file is no longer on this computer")};
    }
    const bool folder = std::filesystem::is_directory(status);
    const std::string serial = pending.serial.toStdString();
    const adb::AdbResult<std::string> target = freeRemotePath(serial, pending.remote, pending.name, folder);
    if (!target) {
        return {TransferState::Failed, failureText(target.error())};
    }
    job.remote = QString::fromStdString(*target);
    if (!folder) {
        const adb::AdbResult<void> pushed = m_adb.pushFile(serial, source, *target, progress, stop);
        return pushed ? Outcome{} : Outcome{TransferState::Failed, failureText(pushed.error())};
    }
    std::vector<std::string> folders{*target};
    std::vector<std::pair<std::filesystem::path, std::string>> files;
    std::uint64_t total = 0;
    std::filesystem::recursive_directory_iterator walk(
        source, std::filesystem::directory_options::skip_permission_denied, error);
    for (; !error && walk != std::filesystem::recursive_directory_iterator(); walk.increment(error)) {
        if (stop.stop_requested()) {
            return {TransferState::Cancelled, {}};
        }
        const QString relative = QString::fromStdWString(walk->path().lexically_relative(source).generic_wstring());
        const std::string remote = *target + '/' + relative.toStdString();
        std::error_code kind;
        if (walk->is_directory(kind)) {
            folders.push_back(remote);
        } else if (walk->is_regular_file(kind)) {
            const std::uintmax_t bytes = walk->file_size(kind);
            if (!kind) {
                total += bytes;
                files.emplace_back(walk->path(), remote);
            }
        }
    }
    if (error) {
        return {TransferState::Failed, tr("The folder could not be read on this computer")};
    }
    progress(0, total);
    if (const adb::AdbResult<void> made = m_adb.makeFolders(serial, folders); !made) {
        return {TransferState::Failed, failureText(made.error())};
    }
    std::uint64_t offset = 0;
    for (const auto& [file, remote] : files) {
        if (stop.stop_requested()) {
            return {TransferState::Cancelled, {}};
        }
        std::uint64_t fileSize = 0;
        const adb::AdbResult<void> pushed = m_adb.pushFile(
            serial, file, remote,
            [&](std::uint64_t done, std::uint64_t fileTotal) {
                fileSize = fileTotal;
                progress(offset + done, std::max(total, offset + done));
            },
            stop);
        if (!pushed) {
            return {TransferState::Failed,
                    tr("%1: %2").arg(QString::fromStdWString(file.filename().wstring()), failureText(pushed.error()))};
        }
        offset += fileSize;
    }
    return {};
}

FileTransfers::Outcome FileTransfers::pullItem(const Pending& pending, TransferJob& job, const std::stop_token& stop,
                                               const Progress& progress)
{
    const std::string serial = pending.serial.toStdString();
    const std::string source = pending.remote.toStdString();
    const adb::AdbResult<std::optional<adb::RemoteFileStat>> info = m_adb.stat(serial, source);
    if (!info) {
        return {TransferState::Failed, failureText(info.error())};
    }
    if (!info->has_value()) {
        return {TransferState::Failed, tr("The file is no longer on the phone")};
    }
    const bool folder = ((*info)->mode & kTypeMask) == kDirectoryType;
    const std::filesystem::path destination = toPath(pending.local);
    std::error_code error;
    std::filesystem::create_directories(destination, error);
    if (error) {
        return {TransferState::Failed, tr("The folder on this computer cannot be written")};
    }
    const std::optional<std::filesystem::path> target = freeLocalPath(destination, pending.name, folder);
    if (!target) {
        return {TransferState::Failed, tr("No free name is left for %1").arg(pending.name)};
    }
    job.local = fromPath(*target);
    if (!folder) {
        const adb::AdbResult<void> pulled = m_adb.pullFile(serial, source, *target, progress, stop);
        return pulled ? Outcome{} : Outcome{TransferState::Failed, failureText(pulled.error())};
    }
    struct Item {
        std::string remote;
        std::filesystem::path local;
        std::uint64_t size = 0;
    };
    std::vector<Item> files;
    std::vector<std::filesystem::path> folders{*target};
    std::deque<std::pair<std::string, std::filesystem::path>> waiting{{source, *target}};
    std::uint64_t total = 0;
    while (!waiting.empty()) {
        if (stop.stop_requested()) {
            return {TransferState::Cancelled, {}};
        }
        const std::pair<std::string, std::filesystem::path> next = waiting.front();
        waiting.pop_front();
        const adb::AdbResult<std::vector<adb::RemoteEntry>> entries = m_adb.list(serial, next.first);
        if (!entries) {
            return {TransferState::Failed, failureText(entries.error())};
        }
        for (const adb::RemoteEntry& entry : *entries) {
            const std::string childRemote = next.first + '/' + entry.name;
            const std::filesystem::path childLocal =
                next.second / toPath(windowsName(QString::fromStdString(entry.name)));
            if (entry.directory) {
                folders.push_back(childLocal);
                waiting.emplace_back(childRemote, childLocal);
            } else {
                files.push_back(Item{childRemote, childLocal, entry.size});
                total += entry.size;
            }
        }
    }
    for (const std::filesystem::path& each : folders) {
        std::filesystem::create_directories(each, error);
        if (error) {
            return {TransferState::Failed, tr("The folder on this computer cannot be written")};
        }
    }
    progress(0, total);
    std::uint64_t offset = 0;
    for (const Item& item : files) {
        if (stop.stop_requested()) {
            return {TransferState::Cancelled, {}};
        }
        const adb::AdbResult<void> pulled = m_adb.pullFile(
            serial, item.remote, item.local,
            [&](std::uint64_t done, std::uint64_t) { progress(offset + done, std::max(total, offset + done)); }, stop);
        if (!pulled) {
            return {TransferState::Failed, tr("%1: %2").arg(remoteName(QString::fromStdString(item.remote)),
                                                            failureText(pulled.error()))};
        }
        offset += item.size;
    }
    return {};
}

adb::AdbResult<std::string> FileTransfers::freeRemotePath(const std::string& serial, const QString& folder,
                                                          const QString& name, bool directory) const
{
    for (int number = 1; number <= kMostNameTries; ++number) {
        const std::string path =
            remoteChild(folder, number == 1 ? name : numberedName(name, number, directory)).toStdString();
        const adb::AdbResult<std::optional<adb::RemoteFileStat>> found = m_adb.stat(serial, path);
        if (!found) {
            return makeUnexpected(found.error());
        }
        if (!found->has_value()) {
            return path;
        }
    }
    return makeUnexpected(adb::AdbError{adb::AdbErrorKind::Rejected, "no free name"});
}

void FileTransfers::report(const TransferJob& job)
{
    QMetaObject::invokeMethod(this, [this, job] { apply(job); }, Qt::QueuedConnection);
}

void FileTransfers::apply(const TransferJob& job)
{
    const auto found = std::ranges::find(m_jobs, job.id, &TransferJob::id);
    if (found == m_jobs.end() || found->finished()) {
        return;
    }
    found->done = job.done;
    found->total = job.total;
    found->state = job.state;
    found->message = job.message;
    found->local = job.local;
    found->remote = job.remote;
    const TransferJob current = *found;
    if (current.finished()) {
        trim();
    }
    emit updated(transferVariant(current));
}

void FileTransfers::trim()
{
    auto finished = static_cast<std::size_t>(std::ranges::count_if(m_jobs, &TransferJob::finished));
    for (auto it = m_jobs.begin(); it != m_jobs.end() && finished > kKeptFinished;) {
        if (it->finished()) {
            it = m_jobs.erase(it);
            --finished;
        } else {
            ++it;
        }
    }
}

DropPlan planDrop(const QList<QUrl>& urls)
{
    DropPlan plan;
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            ++plan.skipped;
            continue;
        }
        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (!info.exists()) {
            ++plan.skipped;
            continue;
        }
        if (info.isFile() && info.suffix().compare(QStringLiteral("apk"), Qt::CaseInsensitive) == 0) {
            plan.apks.append(path);
        } else {
            plan.files.append(path);
        }
    }
    return plan;
}

DropNotice DropFeedback::startPush(const QList<qint64>& ids, const QStringList& names)
{
    for (qsizetype i = 0; i < ids.size(); ++i) {
        m_pushes.push_back(Push{.id = ids[i],
                                .name = i < names.size() ? names[i] : QString(),
                                .done = 0,
                                .total = 0,
                                .state = QStringLiteral("queued"),
                                .message = {}});
    }
    return pushProgress();
}

DropNotice DropFeedback::pushProgress() const
{
    double done = 0;
    double total = 0;
    int finished = 0;
    for (const Push& push : m_pushes) {
        done += push.done;
        total += push.total;
        finished += finishedState(push.state) ? 1 : 0;
    }
    const auto count = static_cast<int>(m_pushes.size());
    DropNotice notice;
    notice.source = QString::fromLatin1(kPushSource);
    notice.title =
        QCoreApplication::translate("DropFeedback", "Pushing %n file(s) to the Download folder", nullptr, count);
    notice.progress = total > 0 ? done / total : count > 0 ? static_cast<double>(finished) / count : 0;
    return notice;
}

std::optional<DropNotice> DropFeedback::pushUpdated(const QVariantMap& job)
{
    const qint64 id = job.value(QStringLiteral("id")).toLongLong();
    const auto found = std::ranges::find(m_pushes, id, &Push::id);
    if (found == m_pushes.end()) {
        return std::nullopt;
    }
    found->done = job.value(QStringLiteral("done")).toDouble();
    found->total = job.value(QStringLiteral("total")).toDouble();
    found->state = job.value(QStringLiteral("state")).toString();
    found->message = job.value(QStringLiteral("message")).toString();
    if (found->name.isEmpty()) {
        found->name = job.value(QStringLiteral("name")).toString();
    }
    if (!std::ranges::all_of(m_pushes, [](const Push& push) { return finishedState(push.state); })) {
        return pushProgress();
    }
    const auto count = static_cast<int>(m_pushes.size());
    const auto pushed = static_cast<int>(std::ranges::count(m_pushes, QStringLiteral("done"), &Push::state));
    const auto failed = std::ranges::find(m_pushes, QStringLiteral("failed"), &Push::state);
    DropNotice notice;
    notice.source = QString::fromLatin1(kPushSource);
    notice.clear = true;
    notice.progress = 1;
    if (pushed == count) {
        notice.toast =
            QCoreApplication::translate("DropFeedback", "Pushed %n file(s) to the Download folder", nullptr, count);
        notice.icon = QStringLiteral("check");
    } else if (failed == m_pushes.end()) {
        notice.toast = pushed > 0 ? QCoreApplication::translate(
                                        "DropFeedback", "Pushed %1 of %2 files to the Download folder; the rest were cancelled")
                                        .arg(QString::number(pushed), QString::number(count))
                                  : QCoreApplication::translate("DropFeedback", "Sending to the phone was cancelled");
        notice.icon = QStringLiteral("info");
    } else if (pushed > 0) {
        notice.toast = QCoreApplication::translate(
                           "DropFeedback", "Pushed %1 of %2 files to the Download folder. %3 could not be sent: %4")
                           .arg(QString::number(pushed), QString::number(count), failed->name, failed->message);
        notice.icon = QStringLiteral("triangle-alert");
    } else if (count == 1) {
        notice.toast = QCoreApplication::translate("DropFeedback", "%1 could not be sent: %2")
                           .arg(failed->name, failed->message);
        notice.icon = QStringLiteral("triangle-alert");
    } else {
        notice.toast = QCoreApplication::translate("DropFeedback", "None of the %n file(s) could be sent: %1", nullptr,
                                                   count)
                           .arg(failed->message);
        notice.icon = QStringLiteral("triangle-alert");
    }
    m_pushes.clear();
    return notice;
}

DropNotice DropFeedback::startInstall(const QString& name)
{
    m_installs.push_back(Install{.name = name, .id = 0});
    DropNotice notice;
    notice.source = QString::fromLatin1(kInstallSource) + name;
    notice.title = QCoreApplication::translate("DropFeedback", "Installing %1").arg(name);
    return notice;
}

std::optional<DropNotice> DropFeedback::installUpdated(const QVariantMap& job)
{
    const qint64 id = job.value(QStringLiteral("id")).toLongLong();
    const QString name = job.value(QStringLiteral("name")).toString();
    auto found = std::ranges::find(m_installs, id, &Install::id);
    if (found == m_installs.end() || id == 0) {
        found = std::ranges::find_if(m_installs,
                                     [&name](const Install& each) { return each.id == 0 && each.name == name; });
    }
    if (found == m_installs.end()) {
        return std::nullopt;
    }
    found->id = id;
    const QString state = job.value(QStringLiteral("state")).toString();
    DropNotice notice;
    notice.source = QString::fromLatin1(kInstallSource) + found->name;
    if (state == u"copying" || state == u"installing") {
        const double total = job.value(QStringLiteral("total")).toDouble();
        notice.title = QCoreApplication::translate("DropFeedback", "Installing %1").arg(found->name);
        notice.progress =
            state == u"installing" ? 1.0 : total > 0 ? job.value(QStringLiteral("done")).toDouble() / total : 0.0;
        return notice;
    }
    notice.clear = true;
    notice.progress = 1;
    if (state == u"done") {
        notice.toast = QCoreApplication::translate("DropFeedback", "Installed %1").arg(found->name);
        notice.icon = QStringLiteral("check");
    } else if (state == u"failed") {
        notice.toast = QCoreApplication::translate("DropFeedback", "Could not install %1: %2")
                           .arg(found->name, job.value(QStringLiteral("message")).toString());
        notice.icon = QStringLiteral("triangle-alert");
    } else {
        return std::nullopt;
    }
    m_installs.erase(found);
    return notice;
}

}
