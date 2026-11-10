#include "DeviceBatch.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <utility>

namespace gs::app {

DeviceBatch::DeviceBatch(Actions actions, QObject* parent)
    : QObject(parent)
    , m_actions(std::move(actions))
{
}

void DeviceBatch::setPhones(std::vector<BatchPhone> phones)
{
    if (phones == m_phones) {
        return;
    }
    const Availability before = availability();
    m_phones = std::move(phones);
    if (m_selected.removeIf([this](const QString& serial) { return phoneOf(serial) == nullptr; }) > 0) {
        emit selectionChanged();
    }
    announce(before);
}

QString DeviceBatch::appSource() const
{
    for (const QString& serial : m_selected) {
        if (const BatchPhone* phone = phoneOf(serial); phone != nullptr && phone->mirroring && phone->apps) {
            return serial;
        }
    }
    return {};
}

bool DeviceBatch::canStartTask() const
{
    return anySelected([](const BatchPhone& phone) { return phone.mirroring; });
}

bool DeviceBatch::canLaunchApp() const
{
    return anySelected([](const BatchPhone& phone) { return phone.mirroring && phone.apps; });
}

bool DeviceBatch::canInstall() const
{
    return anySelected([](const BatchPhone& phone) { return phone.online; });
}

bool DeviceBatch::canPushFiles() const
{
    return canInstall();
}

QVariantList DeviceBatch::jobs() const
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(m_jobs.size()));
    for (const Job& job : m_jobs) {
        list.append(QVariantMap{{QStringLiteral("key"), QString::number(job.key)},
                                {QStringLiteral("kind"), kindName(job.kind)},
                                {QStringLiteral("serial"), job.serial},
                                {QStringLiteral("phone"), job.phone},
                                {QStringLiteral("name"), job.name},
                                {QStringLiteral("state"), job.state},
                                {QStringLiteral("status"), statusOf(job)},
                                {QStringLiteral("progress"), progressOf(job)},
                                {QStringLiteral("finished"), finished(job)},
                                {QStringLiteral("failed"), job.state == u"failed"}});
    }
    return list;
}

int DeviceBatch::running() const
{
    return static_cast<int>(std::ranges::count_if(m_jobs, [](const Job& job) { return !finished(job); }));
}

void DeviceBatch::setSelected(const QString& serial, bool on)
{
    if (on == m_selected.contains(serial) || (on && phoneOf(serial) == nullptr)) {
        return;
    }
    const Availability before = availability();
    if (on) {
        m_selected.append(serial);
    } else {
        m_selected.removeAll(serial);
    }
    emit selectionChanged();
    announce(before);
}

void DeviceBatch::toggle(const QString& serial)
{
    setSelected(serial, !m_selected.contains(serial));
}

void DeviceBatch::clear()
{
    if (m_selected.isEmpty()) {
        return;
    }
    const Availability before = availability();
    m_selected.clear();
    emit selectionChanged();
    announce(before);
}

void DeviceBatch::startTask(const QString& id, const QString& name)
{
    if (id.isEmpty() || !m_actions.startTask || m_selected.isEmpty()) {
        return;
    }
    const QStringList serials = m_selected;
    for (const QString& serial : serials) {
        const QString phone = phoneName(serial);
        const QString problem = m_actions.startTask(id, serial);
        add({.kind = Kind::Task,
             .serial = serial,
             .phone = phone,
             .name = name,
             .state = problem.isEmpty() ? QStringLiteral("done") : QStringLiteral("failed"),
             .message = problem});
    }
    emit jobsChanged();
}

void DeviceBatch::launchApp(const QString& package, const QString& label)
{
    const QString app = package.trimmed();
    if (app.isEmpty() || app.size() > kMostPackageLength || app.contains(u' ') || !m_actions.launchApp ||
        m_selected.isEmpty()) {
        return;
    }
    const QString name = label.trimmed().isEmpty() ? app : label.trimmed();
    const QStringList serials = m_selected;
    for (const QString& serial : serials) {
        const BatchPhone* phone = phoneOf(serial);
        Job job{.kind = Kind::App, .serial = serial, .phone = phoneName(serial), .name = name, .package = app};
        if (phone == nullptr || !phone->mirroring) {
            job.state = QStringLiteral("failed");
            job.message = tr("Not mirroring");
        } else if (!phone->apps) {
            job.state = QStringLiteral("failed");
            job.message = tr("This phone cannot start apps");
        } else {
            job.state = QStringLiteral("done");
            job.pending = true;
        }
        const bool launching = job.pending;
        add(std::move(job));
        if (launching) {
            m_actions.launchApp(serial, app, name);
        }
    }
    emit jobsChanged();
}

void DeviceBatch::installApk(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : QString();
    if (path.isEmpty() || !m_actions.installApk || m_selected.isEmpty()) {
        return;
    }
    const QString name = QFileInfo(path).fileName();
    QStringList targets;
    const QStringList serials = m_selected;
    for (const QString& serial : serials) {
        const BatchPhone* phone = phoneOf(serial);
        const bool online = phone != nullptr && phone->online;
        add({.kind = Kind::Apk,
             .serial = serial,
             .phone = phoneName(serial),
             .name = name,
             .state = online ? QStringLiteral("queued") : QStringLiteral("failed"),
             .message = online ? QString() : tr("Not connected over adb")});
        if (online) {
            targets.append(serial);
        }
    }
    emit jobsChanged();
    if (!targets.isEmpty()) {
        m_actions.installApk(targets, path);
    }
}

void DeviceBatch::pushFiles(const QList<QUrl>& files)
{
    QStringList paths;
    for (const QUrl& file : files) {
        if (file.isLocalFile() && !file.toLocalFile().isEmpty()) {
            paths.append(file.toLocalFile());
        }
    }
    if (paths.isEmpty() || !m_actions.pushFiles || m_selected.isEmpty()) {
        return;
    }
    QStringList targets;
    const QStringList serials = m_selected;
    for (const QString& serial : serials) {
        const BatchPhone* phone = phoneOf(serial);
        if (phone == nullptr || !phone->online) {
            add({.kind = Kind::File,
                 .serial = serial,
                 .phone = phoneName(serial),
                 .name = tr("%n file(s)", "", static_cast<int>(paths.size())),
                 .state = QStringLiteral("failed"),
                 .message = tr("Not connected over adb")});
            continue;
        }
        targets.append(serial);
        for (const QString& path : paths) {
            add({.kind = Kind::File,
                 .serial = serial,
                 .phone = phoneName(serial),
                 .name = QFileInfo(path).fileName(),
                 .state = QStringLiteral("queued")});
        }
    }
    emit jobsChanged();
    if (!targets.isEmpty()) {
        m_actions.pushFiles(targets, paths);
    }
}

void DeviceBatch::clearFinished()
{
    if (std::erase_if(m_jobs, [](const Job& job) { return finished(job); }) > 0) {
        emit jobsChanged();
    }
}

void DeviceBatch::installUpdated(const QVariantMap& job)
{
    follow(Kind::Apk, job);
}

void DeviceBatch::transferUpdated(const QVariantMap& job)
{
    if (job.value(QStringLiteral("direction")).toString() == u"push") {
        follow(Kind::File, job);
    }
}

void DeviceBatch::appRefused(const QString& serial)
{
    const auto found = std::find_if(m_jobs.rbegin(), m_jobs.rend(), [&serial](const Job& job) {
        return job.kind == Kind::App && job.pending && job.serial == serial;
    });
    if (found == m_jobs.rend()) {
        return;
    }
    found->pending = false;
    found->state = QStringLiteral("failed");
    found->message = tr("This app is not on the phone");
    emit jobsChanged();
}

void DeviceBatch::appShown(const QString& serial, const QString& package)
{
    for (Job& job : m_jobs) {
        if (job.kind == Kind::App && job.pending && job.serial == serial && job.package == package) {
            job.pending = false;
        }
    }
}

DeviceBatch::Availability DeviceBatch::availability() const
{
    return {.appSource = appSource(),
            .task = canStartTask(),
            .app = canLaunchApp(),
            .install = canInstall(),
            .push = canPushFiles()};
}

void DeviceBatch::announce(const Availability& before)
{
    if (availability() != before) {
        emit availabilityChanged();
    }
}

const BatchPhone* DeviceBatch::phoneOf(const QString& serial) const
{
    const auto found = std::ranges::find(m_phones, serial, &BatchPhone::serial);
    return found != m_phones.end() ? &*found : nullptr;
}

QString DeviceBatch::phoneName(const QString& serial) const
{
    const BatchPhone* phone = phoneOf(serial);
    return phone != nullptr && !phone->name.isEmpty() ? phone->name : serial;
}

bool DeviceBatch::anySelected(const std::function<bool(const BatchPhone&)>& able) const
{
    return std::ranges::any_of(m_selected, [this, &able](const QString& serial) {
        const BatchPhone* phone = phoneOf(serial);
        return phone != nullptr && able(*phone);
    });
}

void DeviceBatch::add(Job job)
{
    job.key = ++m_lastKey;
    m_jobs.push_back(std::move(job));
}

void DeviceBatch::follow(Kind kind, const QVariantMap& update)
{
    const qint64 id = update.value(QStringLiteral("id")).toLongLong();
    auto found = std::ranges::find_if(
        m_jobs, [kind, id](const Job& job) { return job.kind == kind && job.assigned && job.id == id; });
    if (found == m_jobs.end()) {
        const QString serial = update.value(QStringLiteral("serial")).toString();
        const QString name = update.value(QStringLiteral("name")).toString();
        found = std::ranges::find_if(m_jobs, [kind, &serial, &name](const Job& job) {
            return job.kind == kind && !job.assigned && job.state == u"queued" && job.serial == serial &&
                   job.name == name;
        });
        if (found == m_jobs.end()) {
            return;
        }
        found->assigned = true;
        found->id = id;
    }
    found->state = update.value(QStringLiteral("state"), found->state).toString();
    found->done = update.value(QStringLiteral("done")).toDouble();
    found->total = update.value(QStringLiteral("total")).toDouble();
    found->message = update.value(QStringLiteral("message")).toString();
    emit jobsChanged();
}

bool DeviceBatch::finished(const Job& job)
{
    return job.state == u"done" || job.state == u"failed" || job.state == u"cancelled";
}

QString DeviceBatch::statusOf(const Job& job)
{
    const int percent = static_cast<int>(std::lround(std::max(0.0, progressOf(job)) * 100.0));
    if (job.state == u"failed") {
        return job.message.isEmpty() ? tr("Failed") : job.message;
    }
    if (job.state == u"cancelled") {
        return tr("Cancelled");
    }
    if (job.state == u"queued") {
        return tr("Waiting");
    }
    if (job.state == u"copying") {
        return tr("Copying %1%").arg(percent);
    }
    if (job.state == u"installing") {
        return tr("Installing…");
    }
    if (job.state == u"running") {
        return tr("Sending %1%").arg(percent);
    }
    if (job.state == u"done") {
        switch (job.kind) {
        case Kind::Task:
        case Kind::App:
            return tr("Started");
        case Kind::Apk:
            return tr("Installed");
        case Kind::File:
            return tr("In Download");
        }
    }
    return job.state;
}

double DeviceBatch::progressOf(const Job& job)
{
    if ((job.state == u"copying" || job.state == u"running") && job.total > 0.0) {
        return std::clamp(job.done / job.total, 0.0, 1.0);
    }
    return -1.0;
}

QString DeviceBatch::kindName(Kind kind)
{
    switch (kind) {
    case Kind::Task:
        return QStringLiteral("task");
    case Kind::App:
        return QStringLiteral("app");
    case Kind::Apk:
        return QStringLiteral("apk");
    case Kind::File:
        return QStringLiteral("file");
    }
    return {};
}

}
