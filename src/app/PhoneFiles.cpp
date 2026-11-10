#include "PhoneFiles.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QMetaObject>
#include <QStandardPaths>

#include <algorithm>
#include <chrono>
#include <string>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr qsizetype kMostNameBytes = 255;
constexpr auto kStaleDragFolders = std::chrono::hours(24);

PhoneEntry entryOf(const adb::RemoteEntry& remote)
{
    return PhoneEntry{.name = QString::fromStdString(remote.name),
                      .directory = remote.directory,
                      .size = remote.size,
                      .modified = remote.modifiedSeconds};
}

}

QString filesRoot()
{
    return QStringLiteral("/sdcard");
}

std::optional<QString> phoneFolder(const QString& path)
{
    const QString root = filesRoot();
    QString folder = path.trimmed();
    while (folder.size() > 1 && folder.endsWith(u'/')) {
        folder.chop(1);
    }
    if (folder != root && !folder.startsWith(root + u'/')) {
        return std::nullopt;
    }
    const QStringList parts = folder.mid(root.size()).split(u'/', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part == u"." || part == u"..") {
            return std::nullopt;
        }
    }
    return parts.isEmpty() ? root : root + u'/' + parts.join(u'/');
}

QVariantList folderCrumbs(const QString& folder)
{
    const QString root = filesRoot();
    QVariantList crumbs;
    crumbs.append(QVariantMap{{QStringLiteral("name"), QString()}, {QStringLiteral("path"), root}});
    if (!folder.startsWith(root + u'/')) {
        return crumbs;
    }
    QString path = root;
    const QStringList parts = folder.mid(root.size() + 1).split(u'/', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        path += u'/';
        path += part;
        crumbs.append(QVariantMap{{QStringLiteral("name"), part}, {QStringLiteral("path"), path}});
    }
    return crumbs;
}

QString fileSizeText(quint64 bytes)
{
    constexpr double kUnit = 1024.0;
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double kilobytes = static_cast<double>(bytes) / kUnit;
    if (kilobytes < kUnit) {
        return QStringLiteral("%1 KB").arg(kilobytes, 0, 'f', 1);
    }
    const double megabytes = kilobytes / kUnit;
    if (megabytes < kUnit) {
        return QStringLiteral("%1 MB").arg(megabytes, 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(megabytes / kUnit, 0, 'f', 1);
}

NameProblem checkName(const QString& name, const QStringList& taken, const QString& current)
{
    if (name.trimmed().isEmpty()) {
        return NameProblem::Empty;
    }
    if (name.contains(u'/') || name.contains(QChar(u'\0'))) {
        return NameProblem::Slash;
    }
    if (name == u"." || name == u"..") {
        return NameProblem::Dots;
    }
    if (name.toUtf8().size() > kMostNameBytes) {
        return NameProblem::TooLong;
    }
    if (name.compare(current, Qt::CaseInsensitive) != 0 && taken.contains(name, Qt::CaseInsensitive)) {
        return NameProblem::Taken;
    }
    return NameProblem::None;
}

PhoneFileModel::PhoneFileModel(QObject* parent) : QAbstractListModel(parent) {}

int PhoneFileModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant PhoneFileModel::data(const QModelIndex& index, int role) const
{
    const PhoneEntry* found = index.isValid() ? entry(index.row()) : nullptr;
    if (found == nullptr) {
        return {};
    }
    switch (role) {
    case NameRole:
    case Qt::DisplayRole:
        return found->name;
    case DirectoryRole:
        return found->directory;
    case SizeRole:
        return static_cast<double>(found->size);
    case SizeTextRole:
        return found->directory ? QString() : fileSizeText(found->size);
    case ModifiedRole:
        return found->modified > 0 ? QVariant(QDateTime::fromSecsSinceEpoch(found->modified)) : QVariant();
    case PathRole:
        return remoteChild(m_folder, found->name);
    case SelectedRole:
        return m_selected[static_cast<std::size_t>(index.row())] != 0;
    default:
        return {};
    }
}

QHash<int, QByteArray> PhoneFileModel::roleNames() const
{
    return {{NameRole, "name"},         {DirectoryRole, "directory"}, {SizeRole, "size"},
            {SizeTextRole, "sizeText"}, {ModifiedRole, "modified"},   {PathRole, "path"},
            {SelectedRole, "selected"}};
}

void PhoneFileModel::reset(const QString& folder, std::vector<PhoneEntry> entries)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::stable_sort(entries, [&collator](const PhoneEntry& a, const PhoneEntry& b) {
        if (a.directory != b.directory) {
            return a.directory;
        }
        const int order = collator.compare(a.name, b.name);
        return order != 0 ? order < 0 : a.name < b.name;
    });
    beginResetModel();
    m_folder = folder;
    m_entries = std::move(entries);
    m_selected.assign(m_entries.size(), 0);
    m_anchor = -1;
    endResetModel();
    emit selectionChanged();
}

void PhoneFileModel::select(int row, Qt::KeyboardModifiers modifiers)
{
    const auto count = static_cast<int>(m_entries.size());
    const bool add = modifiers.testFlag(Qt::ControlModifier);
    const bool range = modifiers.testFlag(Qt::ShiftModifier);
    if (row < 0 || row >= count) {
        if (!add && !range) {
            clearSelection();
        }
        return;
    }
    std::vector<char> next = add ? m_selected : std::vector<char>(m_entries.size(), 0);
    if (range && m_anchor >= 0 && m_anchor < count) {
        for (int i = std::min(m_anchor, row); i <= std::max(m_anchor, row); ++i) {
            next[static_cast<std::size_t>(i)] = 1;
        }
    } else if (add) {
        next[static_cast<std::size_t>(row)] = next[static_cast<std::size_t>(row)] != 0 ? 0 : 1;
        m_anchor = row;
    } else {
        next[static_cast<std::size_t>(row)] = 1;
        m_anchor = row;
    }
    changeSelection(std::move(next));
}

void PhoneFileModel::selectAll()
{
    changeSelection(std::vector<char>(m_entries.size(), 1));
}

void PhoneFileModel::clearSelection()
{
    changeSelection(std::vector<char>(m_entries.size(), 0));
}

void PhoneFileModel::selectNames(const QStringList& names)
{
    std::vector<char> next(m_entries.size(), 0);
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        next[i] = names.contains(m_entries[i].name) ? 1 : 0;
    }
    changeSelection(std::move(next));
}

void PhoneFileModel::changeSelection(std::vector<char> next)
{
    bool changed = false;
    for (std::size_t i = 0; i < next.size() && i < m_selected.size(); ++i) {
        if ((next[i] != 0) != (m_selected[i] != 0)) {
            m_selected[i] = next[i];
            const QModelIndex at = index(static_cast<int>(i));
            emit dataChanged(at, at, {SelectedRole});
            changed = true;
        }
    }
    if (changed) {
        emit selectionChanged();
    }
}

const PhoneEntry* PhoneFileModel::entry(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_entries.size())) {
        return nullptr;
    }
    return &m_entries[static_cast<std::size_t>(row)];
}

QStringList PhoneFileModel::names() const
{
    QStringList all;
    all.reserve(static_cast<qsizetype>(m_entries.size()));
    for (const PhoneEntry& each : m_entries) {
        all.append(each.name);
    }
    return all;
}

QStringList PhoneFileModel::selectedNames() const
{
    QStringList chosen;
    for (const PhoneEntry& each : selected()) {
        chosen.append(each.name);
    }
    return chosen;
}

std::vector<PhoneEntry> PhoneFileModel::selected() const
{
    std::vector<PhoneEntry> chosen;
    for (std::size_t i = 0; i < m_entries.size() && i < m_selected.size(); ++i) {
        if (m_selected[i] != 0) {
            chosen.push_back(m_entries[i]);
        }
    }
    return chosen;
}

quint64 PhoneFileModel::selectedBytes() const
{
    quint64 bytes = 0;
    for (const PhoneEntry& each : selected()) {
        bytes += each.directory ? 0 : each.size;
    }
    return bytes;
}

bool PhoneFileModel::selectionHasFolders() const
{
    return std::ranges::any_of(selected(), &PhoneEntry::directory);
}

TransferListModel::TransferListModel(QObject* parent) : QAbstractListModel(parent) {}

int TransferListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_jobs.size());
}

QVariant TransferListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_jobs.size())) {
        return {};
    }
    const TransferJob& job = m_jobs[static_cast<std::size_t>(index.row())];
    switch (role) {
    case JobIdRole:
        return static_cast<double>(job.id);
    case DirectionRole:
        return job.direction == TransferDirection::Push ? QStringLiteral("push") : QStringLiteral("pull");
    case NameRole:
    case Qt::DisplayRole:
        return job.name;
    case DoneRole:
        return job.done;
    case TotalRole:
        return job.total;
    case StateRole:
        return transferStateName(job.state);
    case MessageRole:
        return job.message;
    case ProgressRole:
        if (job.total > 0) {
            return std::clamp(job.done / job.total, 0.0, 1.0);
        }
        return job.state == TransferState::Done ? 1.0 : 0.0;
    case ActiveRole:
        return !job.finished();
    default:
        return {};
    }
}

QHash<int, QByteArray> TransferListModel::roleNames() const
{
    return {{JobIdRole, "jobId"},     {DirectionRole, "direction"}, {NameRole, "name"},
            {DoneRole, "done"},       {TotalRole, "total"},         {StateRole, "status"},
            {MessageRole, "message"}, {ProgressRole, "progress"},   {ActiveRole, "active"}};
}

void TransferListModel::reset(std::vector<TransferJob> jobs)
{
    beginResetModel();
    m_jobs = std::move(jobs);
    endResetModel();
}

void TransferListModel::upsert(const TransferJob& job)
{
    const auto found = std::ranges::find(m_jobs, job.id, &TransferJob::id);
    if (found != m_jobs.end()) {
        *found = job;
        const QModelIndex at = index(static_cast<int>(found - m_jobs.begin()));
        emit dataChanged(at, at);
        return;
    }
    beginInsertRows({}, 0, 0);
    m_jobs.insert(m_jobs.begin(), job);
    endInsertRows();
}

void TransferListModel::removeFinished()
{
    for (auto row = static_cast<int>(m_jobs.size()) - 1; row >= 0; --row) {
        if (m_jobs[static_cast<std::size_t>(row)].finished()) {
            beginRemoveRows({}, row, row);
            m_jobs.erase(m_jobs.begin() + row);
            endRemoveRows();
        }
    }
}

int TransferListModel::active() const
{
    return static_cast<int>(std::ranges::count_if(m_jobs, [](const TransferJob& job) { return !job.finished(); }));
}

PhoneFiles::PhoneFiles(adb::AdbClient adb, FileTransfers& transfers, Source source, std::filesystem::path dragFolder,
                       QObject* parent)
    : QObject(parent),
      m_adb(adb),
      m_transfers(transfers),
      m_source(std::move(source)),
      m_dragRoot(std::move(dragFolder)),
      m_worker([this](const std::stop_token& stop) { work(stop); })
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    m_dragRun = m_dragRoot / QStringLiteral("%1-%2")
                                 .arg(QString::number(QCoreApplication::applicationPid()),
                                      QString::number(QDateTime::currentMSecsSinceEpoch()))
                                 .toStdWString();
    cleanDragFolders();
    connect(&m_model, &PhoneFileModel::selectionChanged, this, &PhoneFiles::selectionChanged);
    connect(&m_transfers, &FileTransfers::updated, this, &PhoneFiles::transferUpdated);
}

PhoneFiles::~PhoneFiles()
{
    m_worker.request_stop();
    if (m_worker.joinable()) {
        m_worker.join();
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

PhoneFiles* PhoneFiles::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

bool PhoneFiles::canGoUp() const
{
    return !m_serial.isEmpty() && m_folder != filesRoot();
}

QString PhoneFiles::problem() const
{
    if (m_serial.isEmpty()) {
        return {};
    }
    if (!m_availability.online) {
        return tr("Connect this phone with USB debugging or wireless debugging to see its files");
    }
    if (!m_listProblem.isEmpty()) {
        return tr("This folder could not be read: %1").arg(m_listProblem);
    }
    return {};
}

QString PhoneFiles::selectedName() const
{
    const std::vector<PhoneEntry> chosen = m_model.selected();
    return chosen.size() == 1 ? chosen.front().name : QString();
}

double PhoneFiles::dragDone() const
{
    double done = 0;
    for (const auto& each : m_dragDone) {
        done += each.second;
    }
    return done;
}

QUrl PhoneFiles::downloadsFolder() const
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
}

void PhoneFiles::open(const QString& serial)
{
    if (serial != m_serial) {
        cancelDrag();
        m_serial = serial;
        m_folder = filesRoot();
        m_listProblem.clear();
        ++m_generation;
        m_loading = false;
        m_relist = false;
        m_model.reset(m_folder, {});
        m_transferModel.reset(serial.isEmpty() ? std::vector<TransferJob>() : m_transfers.jobs(serial));
        emit openedChanged();
        emit folderChanged();
        emit transfersChanged();
    }
    m_availability = serial.isEmpty() || !m_source.availability ? FilesAvailability{} : m_source.availability(serial);
    list(true);
    emit stateChanged();
}

void PhoneFiles::close()
{
    open({});
}

void PhoneFiles::openFolder(const QString& path)
{
    const std::optional<QString> folder = phoneFolder(path);
    if (!folder || m_serial.isEmpty()) {
        return;
    }
    m_folder = *folder;
    m_listProblem.clear();
    m_model.reset(m_folder, {});
    emit folderChanged();
    list(true);
    emit stateChanged();
}

void PhoneFiles::enter(int row)
{
    const PhoneEntry* found = m_model.entry(row);
    if (found != nullptr && found->directory) {
        openFolder(remoteChild(m_folder, found->name));
    }
}

void PhoneFiles::up()
{
    if (canGoUp()) {
        openFolder(remoteParent(m_folder));
    }
}

void PhoneFiles::refresh()
{
    list(true);
}

void PhoneFiles::select(int row, int modifiers)
{
    m_model.select(row, Qt::KeyboardModifiers::fromInt(modifiers));
}

void PhoneFiles::selectAll()
{
    m_model.selectAll();
}

void PhoneFiles::clearSelection()
{
    m_model.clearSelection();
}

bool PhoneFiles::isSelected(int row) const
{
    return m_model.data(m_model.index(row), PhoneFileModel::SelectedRole).toBool();
}

QString PhoneFiles::nameProblem(const QString& name, const QString& current) const
{
    switch (checkName(name.trimmed(), m_model.names(), current)) {
    case NameProblem::None:
        return {};
    case NameProblem::Empty:
        return tr("Type a name");
    case NameProblem::Slash:
        return tr("A name cannot contain /");
    case NameProblem::Dots:
        return tr("This name is reserved");
    case NameProblem::TooLong:
        return tr("This name is too long");
    case NameProblem::Taken:
        return tr("Something with this name is already here");
    }
    return {};
}

bool PhoneFiles::makeFolder(const QString& name)
{
    const QString chosen = name.trimmed();
    if (!online() || !nameProblem(chosen).isEmpty()) {
        return false;
    }
    const std::string path = remoteChild(m_folder, chosen).toStdString();
    operate(
        [path](const adb::AdbClient& client, const std::string& phone) {
            return client.makeFolders(phone, std::vector<std::string>{path});
        },
        tr("Made the folder %1").arg(chosen),
        [chosen](const QString& detail) { return tr("Could not make the folder %1: %2").arg(chosen, detail); });
    return true;
}

bool PhoneFiles::rename(const QString& from, const QString& to)
{
    const QString chosen = to.trimmed();
    if (!online() || from.isEmpty()) {
        return false;
    }
    if (chosen == from) {
        return true;
    }
    if (!nameProblem(chosen, from).isEmpty()) {
        return false;
    }
    const std::string source = remoteChild(m_folder, from).toStdString();
    const std::string target = remoteChild(m_folder, chosen).toStdString();
    operate(
        [source, target](const adb::AdbClient& client, const std::string& phone) {
            return client.renamePath(phone, source, target);
        },
        tr("Renamed %1 to %2").arg(from, chosen),
        [from](const QString& detail) { return tr("Could not rename %1: %2").arg(from, detail); });
    return true;
}

bool PhoneFiles::removeSelected()
{
    const QStringList names = m_model.selectedNames();
    if (!online() || names.isEmpty()) {
        return false;
    }
    std::vector<std::string> paths;
    paths.reserve(static_cast<std::size_t>(names.size()));
    for (const QString& name : names) {
        paths.push_back(remoteChild(m_folder, name).toStdString());
    }
    const auto count = static_cast<int>(names.size());
    operate([paths](const adb::AdbClient& client, const std::string& phone) { return client.removePaths(phone, paths); },
            tr("Deleted %n item(s)", "", count),
            [](const QString& detail) { return tr("Could not delete: %1").arg(detail); });
    return true;
}

int PhoneFiles::upload(const QList<QUrl>& urls)
{
    if (!online()) {
        emit notice(tr("This phone is not connected"), QStringLiteral("bad"));
        return 0;
    }
    int queued = 0;
    for (const QUrl& url : urls) {
        if (!url.isLocalFile() || !QFileInfo::exists(url.toLocalFile())) {
            continue;
        }
        m_transfers.push(m_serial, url.toLocalFile(), m_folder, filesTag());
        ++queued;
    }
    if (queued == 0) {
        emit notice(tr("Only files and folders on this computer can be uploaded"), QStringLiteral("warn"));
    }
    return queued;
}

int PhoneFiles::downloadSelected(const QUrl& folder)
{
    const QString local = folder.isLocalFile() ? folder.toLocalFile() : QString();
    const std::vector<PhoneEntry> chosen = m_model.selected();
    if (!online() || local.isEmpty() || chosen.empty()) {
        return 0;
    }
    for (const PhoneEntry& each : chosen) {
        m_transfers.pull(m_serial, remoteChild(m_folder, each.name), local, filesTag());
    }
    const auto count = static_cast<int>(chosen.size());
    emit notice(tr("Downloading %n item(s) to %1", "", count).arg(QDir::toNativeSeparators(local)),
                QStringLiteral("info"));
    return count;
}

QString PhoneFiles::prepareDrag()
{
    const std::vector<PhoneEntry> chosen = m_model.selected();
    if (!online()) {
        return tr("This phone is not connected");
    }
    if (chosen.empty()) {
        return tr("Select the files to drag out first");
    }
    if (std::ranges::any_of(chosen, &PhoneEntry::directory)) {
        return tr("Folders cannot be dragged out. Use Download to… instead");
    }
    const quint64 bytes = m_model.selectedBytes();
    if (bytes > static_cast<quint64>(kDragLimit)) {
        return tr("Files over 200 MB in total cannot be dragged out. Use Download to… instead");
    }
    cancelDrag();
    const std::filesystem::path folder = m_dragRun / std::to_wstring(++m_dragNumber);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error) {
        return tr("A temporary folder for dragging could not be made");
    }
    const QString local = QString::fromStdWString(folder.wstring());
    m_dragTotal = static_cast<double>(bytes);
    m_dragPhase = kDragPreparing;
    for (const PhoneEntry& each : chosen) {
        const qint64 id = m_transfers.pull(m_serial, remoteChild(m_folder, each.name), local, dragTag());
        m_dragOrder.push_back(id);
        m_dragDone[id] = 0;
    }
    emit dragChanged();
    for (const qint64 id : std::vector<qint64>(m_dragOrder)) {
        if (const std::optional<TransferJob> job = m_transfers.job(id); job && job->finished()) {
            dragUpdated(*job);
        }
    }
    return {};
}

void PhoneFiles::cancelDrag()
{
    if (m_dragPhase == kDragIdle && m_dragOrder.empty()) {
        return;
    }
    const std::vector<qint64> jobs = m_dragOrder;
    const std::map<qint64, QString> done = m_dragFiles;
    resetDrag();
    emit dragChanged();
    for (const qint64 id : jobs) {
        if (!done.contains(id)) {
            m_transfers.cancel(id);
        }
    }
}

void PhoneFiles::finishDrag()
{
    resetDrag();
    emit dragChanged();
}

void PhoneFiles::cancelTransfer(double id)
{
    m_transfers.cancel(static_cast<qint64>(id));
}

void PhoneFiles::clearTransfers()
{
    m_transfers.clearFinished(m_serial);
    m_transferModel.removeFinished();
    emit transfersChanged();
}

QString PhoneFiles::sizeText(double bytes) const
{
    return fileSizeText(bytes > 0 ? static_cast<quint64>(bytes) : 0);
}

void PhoneFiles::refreshAvailability()
{
    if (m_serial.isEmpty() || !m_source.availability) {
        return;
    }
    const FilesAvailability now = m_source.availability(m_serial);
    if (now.name == m_availability.name && now.online == m_availability.online) {
        return;
    }
    const bool cameOnline = now.online && !m_availability.online;
    m_availability = now;
    if (cameOnline) {
        list(true);
    } else if (!now.online) {
        m_loading = false;
    }
    emit stateChanged();
}

void PhoneFiles::post(std::function<void()> task)
{
    {
        const std::lock_guard lock(m_mutex);
        m_tasks.push_back(std::move(task));
    }
    m_wake.notify_one();
}

void PhoneFiles::work(const std::stop_token& stop)
{
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return !m_tasks.empty(); })) {
                return;
            }
            task = std::move(m_tasks.front());
            m_tasks.pop_front();
        }
        task();
    }
}

void PhoneFiles::list(bool force)
{
    if (!online()) {
        m_loading = false;
        return;
    }
    if (m_loading && !force) {
        m_relist = true;
        return;
    }
    m_loading = true;
    m_relist = false;
    const quint64 generation = ++m_generation;
    const std::string serial = m_serial.toStdString();
    const std::string folder = m_folder.toStdString();
    post([this, generation, serial, folder] {
        adb::AdbResult<std::vector<adb::RemoteEntry>> result = m_adb.list(serial, folder);
        QMetaObject::invokeMethod(
            this, [this, generation, result = std::move(result)]() mutable { listed(generation, std::move(result)); },
            Qt::QueuedConnection);
    });
    emit stateChanged();
}

void PhoneFiles::listed(quint64 generation, adb::AdbResult<std::vector<adb::RemoteEntry>> result)
{
    if (generation != m_generation) {
        return;
    }
    m_loading = false;
    if (result) {
        std::vector<PhoneEntry> entries;
        entries.reserve(result->size());
        for (const adb::RemoteEntry& remote : *result) {
            entries.push_back(entryOf(remote));
        }
        const QStringList chosen = m_model.folder() == m_folder ? m_model.selectedNames() : QStringList();
        m_model.reset(m_folder, std::move(entries));
        if (!chosen.isEmpty()) {
            m_model.selectNames(chosen);
        }
        m_listProblem.clear();
    } else {
        m_listProblem = QString::fromStdString(result.error().message);
        m_model.reset(m_folder, {});
    }
    emit stateChanged();
    if (m_relist) {
        m_relist = false;
        list(false);
    }
}

void PhoneFiles::operate(std::function<adb::AdbResult<void>(const adb::AdbClient&, const std::string&)> operation,
                         const QString& success, std::function<QString(const QString&)> failure)
{
    ++m_busy;
    emit stateChanged();
    const QString serial = m_serial;
    post([this, serial, operation = std::move(operation), success, failure = std::move(failure)] {
        const adb::AdbResult<void> result = operation(m_adb, serial.toStdString());
        const bool ok = result.has_value();
        const QString text = ok ? success : failure(QString::fromStdString(result.error().message));
        QMetaObject::invokeMethod(this, [this, serial, ok, text] { operated(serial, ok, text); }, Qt::QueuedConnection);
    });
}

void PhoneFiles::operated(const QString& serial, bool ok, const QString& text)
{
    m_busy = std::max(0, m_busy - 1);
    if (serial == m_serial && !m_serial.isEmpty()) {
        emit notice(text, ok ? QStringLiteral("ok") : QStringLiteral("bad"));
        list(false);
    }
    emit stateChanged();
}

void PhoneFiles::transferUpdated(const QVariantMap& update)
{
    const std::optional<TransferJob> job = m_transfers.job(update.value(QStringLiteral("id")).toLongLong());
    if (!job) {
        return;
    }
    dragUpdated(*job);
    if (m_serial.isEmpty() || job->serial != m_serial) {
        return;
    }
    m_transferModel.upsert(*job);
    emit transfersChanged();
    if (job->state == TransferState::Done && job->direction == TransferDirection::Push &&
        remoteParent(job->remote) == m_folder) {
        list(false);
    }
    if (job->state == TransferState::Failed && job->tag == filesTag()) {
        emit notice(job->direction == TransferDirection::Push
                        ? tr("Could not upload %1: %2").arg(job->name, job->message)
                        : tr("Could not download %1: %2").arg(job->name, job->message),
                    QStringLiteral("bad"));
    }
}

void PhoneFiles::dragUpdated(const TransferJob& job)
{
    const auto found = m_dragDone.find(job.id);
    if (found == m_dragDone.end() || m_dragPhase != kDragPreparing) {
        return;
    }
    found->second = job.done;
    if (job.state == TransferState::Failed || job.state == TransferState::Cancelled) {
        const bool failed = job.state == TransferState::Failed;
        const QString name = job.name;
        const QString message = job.message;
        cancelDrag();
        if (failed) {
            emit notice(tr("Could not prepare %1 for dragging: %2").arg(name, message), QStringLiteral("bad"));
        }
        return;
    }
    if (job.state == TransferState::Done) {
        m_dragFiles[job.id] = job.local;
    }
    if (m_dragFiles.size() == m_dragOrder.size()) {
        m_dragUrls.clear();
        for (const qint64 id : m_dragOrder) {
            m_dragUrls.append(QUrl::fromLocalFile(m_dragFiles[id]).toString());
        }
        m_dragPhase = kDragReady;
    }
    emit dragChanged();
}

void PhoneFiles::resetDrag()
{
    m_dragPhase = kDragIdle;
    m_dragOrder.clear();
    m_dragDone.clear();
    m_dragFiles.clear();
    m_dragTotal = 0;
    m_dragUrls.clear();
}

void PhoneFiles::cleanDragFolders() const
{
    std::error_code error;
    if (m_dragRoot.empty() || !std::filesystem::is_directory(m_dragRoot, error)) {
        return;
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(m_dragRoot, error)) {
        std::error_code stale;
        const std::filesystem::file_time_type written = entry.last_write_time(stale);
        if (!stale && now - written > kStaleDragFolders) {
            std::filesystem::remove_all(entry.path(), stale);
        }
    }
}

}
