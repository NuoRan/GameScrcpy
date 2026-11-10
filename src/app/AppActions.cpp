#include "AppActions.h"

#include <gs/platform/Thread.h>

#include <QFileInfo>
#include <QMetaObject>
#include <QtLogging>

#include <algorithm>
#include <format>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr qsizetype kMostPackageName = 255;
constexpr std::size_t kMostOutput = 1U << 20U;
constexpr std::string_view kSuccess = "Success";
constexpr std::string_view kFailure = "Failure [";

std::string_view trimmed(std::string_view text)
{
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && blank(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && blank(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

template <typename Visit>
void eachLine(std::string_view text, Visit visit)
{
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        if (!visit(trimmed(text.substr(start, end - start)))) {
            return;
        }
        start = end + 1;
    }
}

QString textOf(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString detailOf(const adb::ShellResult& result)
{
    QString detail;
    for (const std::string_view text : {std::string_view(result.out), std::string_view(result.err)}) {
        eachLine(text, [&detail](std::string_view line) {
            if (line.empty()) {
                return true;
            }
            detail = textOf(line);
            return false;
        });
        if (!detail.isEmpty()) {
            break;
        }
    }
    return detail;
}

adb::AdbResult<adb::ShellResult> runOnPhone(const adb::AdbClient& adb, const std::string& serial,
                                            const std::string& command, const std::stop_token& stop)
{
    if (stop.stop_requested()) {
        return makeUnexpected(adb::AdbError{adb::AdbErrorKind::Io, "stopped"});
    }
    adb::AdbResult<adb::ShellProcess> process = adb.spawn(serial, command);
    if (!process) {
        return makeUnexpected(std::move(process.error()));
    }
    const std::stop_callback cancel(stop, [&process] { process->interrupt(); });
    adb::ShellResult result;
    while (const std::optional<adb::ShellPacket> packet = process->read()) {
        const std::string_view data(reinterpret_cast<const char*>(packet->data.data()), packet->data.size());
        switch (static_cast<adb::ShellPacketId>(packet->id)) {
        case adb::ShellPacketId::Stdout:
            result.out.append(data);
            break;
        case adb::ShellPacketId::Stderr:
            result.err.append(data);
            break;
        case adb::ShellPacketId::Exit:
            result.exitCode = packet->data.empty() ? 0 : packet->data.front();
            return result;
        default:
            break;
        }
        if (result.out.size() + result.err.size() > kMostOutput) {
            return makeUnexpected(adb::AdbError{adb::AdbErrorKind::Protocol, "the command wrote too much"});
        }
    }
    return makeUnexpected(adb::AdbError{adb::AdbErrorKind::Io, stop.stop_requested()
                                                                   ? "stopped"
                                                                   : "the command ended without an exit status"});
}

}

bool validPackageName(const QString& package)
{
    return !package.isEmpty() && package.size() <= kMostPackageName && std::ranges::all_of(package, [](QChar c) {
               return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') ||
                      c == u'.' || c == u'_';
           });
}

std::optional<PackageAction> packageActionNamed(const QString& name)
{
    const auto found = std::ranges::find_if(kPackageActions,
                                            [&name](PackageAction action) { return packageActionName(action) == name; });
    return found == kPackageActions.end() ? std::nullopt : std::optional<PackageAction>(*found);
}

QString packageActionName(PackageAction action)
{
    switch (action) {
    case PackageAction::ForceStop:
        return QStringLiteral("forceStop");
    case PackageAction::Disable:
        return QStringLiteral("disable");
    case PackageAction::Enable:
        return QStringLiteral("enable");
    case PackageAction::ClearData:
        return QStringLiteral("clearData");
    case PackageAction::Uninstall:
        return QStringLiteral("uninstall");
    }
    return {};
}

bool actionAllowed(PackageAction action, bool system, bool enabled)
{
    switch (action) {
    case PackageAction::Disable:
        return enabled;
    case PackageAction::Enable:
        return !enabled;
    case PackageAction::ForceStop:
        return !system && enabled;
    case PackageAction::ClearData:
    case PackageAction::Uninstall:
        return !system;
    }
    return false;
}

std::optional<std::string> packageCommand(PackageAction action, const QString& package)
{
    if (!validPackageName(package)) {
        return std::nullopt;
    }
    const std::string name = package.toStdString();
    switch (action) {
    case PackageAction::ForceStop:
        return "am force-stop " + name;
    case PackageAction::Disable:
        return "pm disable-user --user 0 " + name;
    case PackageAction::Enable:
        return "pm enable " + name;
    case PackageAction::ClearData:
        return "pm clear " + name;
    case PackageAction::Uninstall:
        return "pm uninstall " + name;
    }
    return std::nullopt;
}

std::string remoteApkPath(quint64 job)
{
    return std::format("/data/local/tmp/gs-install-{}.apk", job);
}

std::string installCommand(std::string_view remote)
{
    return std::format("pm install -r -t {}", remote);
}

std::string removeCommand(std::string_view remote)
{
    return std::format("rm -f {}", remote);
}

PmResult parsePmResult(std::string_view out, std::string_view err)
{
    bool success = false;
    std::optional<QString> failure;
    for (const std::string_view text : {out, err}) {
        eachLine(text, [&success, &failure](std::string_view line) {
            if (line == kSuccess) {
                success = true;
            } else if (line.starts_with(kFailure) && !failure) {
                std::string_view inside = line.substr(kFailure.size());
                if (const std::size_t close = inside.rfind(']'); close != std::string_view::npos) {
                    inside = inside.substr(0, close);
                }
                failure = textOf(trimmed(inside.substr(0, inside.find(':'))));
            }
            return true;
        });
    }
    if (failure) {
        return {.ok = false, .code = *failure};
    }
    return {.ok = success, .code = {}};
}

PmResult judgeAction(PackageAction action, const adb::ShellResult& result)
{
    const PmResult parsed = parsePmResult(result.out, result.err);
    if (!parsed.ok && !parsed.code.isEmpty()) {
        return parsed;
    }
    switch (action) {
    case PackageAction::ClearData:
    case PackageAction::Uninstall:
        return parsed;
    case PackageAction::ForceStop:
    case PackageAction::Disable:
    case PackageAction::Enable:
        break;
    }
    return {.ok = result.exitCode == 0, .code = {}};
}

QString failureReason(const QString& code)
{
    if (code.isEmpty()) {
        return AppActions::tr("The phone did not confirm it");
    }
    if (code == u"INSTALL_FAILED_INSUFFICIENT_STORAGE") {
        return AppActions::tr("Not enough storage on the phone");
    }
    if (code == u"INSTALL_FAILED_UPDATE_INCOMPATIBLE" || code == u"INSTALL_FAILED_SHARED_USER_INCOMPATIBLE" ||
        code == u"INSTALL_PARSE_FAILED_INCONSISTENT_CERTIFICATES") {
        return AppActions::tr("The signature does not match the installed app");
    }
    if (code == u"INSTALL_FAILED_VERSION_DOWNGRADE") {
        return AppActions::tr("A newer version is already installed");
    }
    if (code == u"INSTALL_FAILED_INVALID_APK" || code.startsWith(u"INSTALL_PARSE_FAILED_")) {
        return AppActions::tr("Not a valid APK");
    }
    if (code == u"INSTALL_FAILED_ALREADY_EXISTS") {
        return AppActions::tr("The app is already installed");
    }
    if (code == u"INSTALL_FAILED_USER_RESTRICTED") {
        return AppActions::tr("The phone does not allow installs from a computer");
    }
    if (code == u"DELETE_FAILED_USER_RESTRICTED") {
        return AppActions::tr("The phone does not allow removing this app");
    }
    return code;
}

QVariantMap installUpdate(quint64 id, const QString& serial, const QString& name, const QString& state, double done,
                          double total, const QString& message)
{
    return {{QStringLiteral("id"), static_cast<qint64>(id)},
            {QStringLiteral("serial"), serial},
            {QStringLiteral("name"), name},
            {QStringLiteral("state"), state},
            {QStringLiteral("done"), done},
            {QStringLiteral("total"), total},
            {QStringLiteral("message"), message}};
}

ActionOutcome runPackageAction(const PhoneAccess& phone, const QString& serial, PackageAction action,
                               const QString& package, const std::stop_token& stop)
{
    const std::optional<std::string> command = packageCommand(action, package);
    if (!command) {
        return {.ok = false, .reason = AppActions::tr("Not a valid package name"), .detail = package};
    }
    if (!phone.run) {
        return {.ok = false, .reason = AppActions::tr("The phone did not answer"), .detail = {}};
    }
    const adb::AdbResult<adb::ShellResult> result = phone.run(serial.toStdString(), *command, stop);
    if (!result) {
        return {.ok = false,
                .reason = AppActions::tr("The phone did not answer"),
                .detail = QString::fromStdString(result.error().message)};
    }
    const PmResult judged = judgeAction(action, *result);
    return {.ok = judged.ok, .reason = judged.ok ? QString() : failureReason(judged.code), .detail = detailOf(*result)};
}

void runInstall(const PhoneAccess& phone, const InstallJob& job, const std::function<void(const QVariantMap&)>& report,
                const std::stop_token& stop)
{
    const QString name = QString::fromStdWString(job.apk.filename().wstring());
    const auto update = [&report, &job, &name](const QString& state, double done, double total,
                                               const QString& message) {
        report(installUpdate(job.id, job.serial, name, state, done, total, message));
    };
    std::error_code error;
    const bool regular = std::filesystem::is_regular_file(job.apk, error);
    const std::uintmax_t size = regular ? std::filesystem::file_size(job.apk, error) : 0;
    if (!regular || error) {
        update(QStringLiteral("failed"), 0, 0, AppActions::tr("The APK file cannot be read"));
        return;
    }
    const auto total = static_cast<double>(size);
    if (!phone.push || !phone.run) {
        update(QStringLiteral("failed"), 0, total, AppActions::tr("The phone did not answer"));
        return;
    }
    update(QStringLiteral("copying"), 0, total, {});
    const std::string serial = job.serial.toStdString();
    const std::string remote = remoteApkPath(job.id);
    std::uint64_t shown = 0;
    const adb::AdbResult<void> pushed = phone.push(
        serial, job.apk, remote,
        [&update, &shown](std::uint64_t done, std::uint64_t all) {
            const std::uint64_t step = std::max<std::uint64_t>(all / 100, 1);
            if (done == all || done >= shown + step) {
                shown = done;
                update(QStringLiteral("copying"), static_cast<double>(done), static_cast<double>(all), {});
            }
        },
        stop);
    if (!pushed) {
        static_cast<void>(phone.run(serial, removeCommand(remote), stop));
        update(QStringLiteral("failed"), static_cast<double>(shown), total,
               AppActions::tr("The APK could not be copied to the phone"));
        return;
    }
    update(QStringLiteral("installing"), total, total, {});
    const adb::AdbResult<adb::ShellResult> installed = phone.run(serial, installCommand(remote), stop);
    static_cast<void>(phone.run(serial, removeCommand(remote), stop));
    if (!installed) {
        update(QStringLiteral("failed"), total, total, AppActions::tr("The phone did not finish installing"));
        return;
    }
    const PmResult result = parsePmResult(installed->out, installed->err);
    if (result.ok) {
        update(QStringLiteral("done"), total, total, {});
    } else {
        update(QStringLiteral("failed"), total, total, failureReason(result.code));
    }
}

PhoneAccess phoneAccess(adb::AdbClient adb)
{
    return {.run = [adb](const std::string& serial, const std::string& command, const std::stop_token& stop) {
                return runOnPhone(adb, serial, command, stop);
            },
            .push = [adb](const std::string& serial, const std::filesystem::path& local, const std::string& remote,
                          PushProgress progress, const std::stop_token& stop) {
                return adb.pushFile(serial, local, remote, std::move(progress), stop);
            }};
}

AppActions::AppActions(PhoneAccess phone, QObject* parent) : QObject(parent), m_phone(std::move(phone)) {}

AppActions::~AppActions()
{
    m_waiting.clear();
    for (auto& worker : m_workers) {
        worker.second.request_stop();
    }
    m_workers.clear();
}

quint64 AppActions::act(const QString& serial, PackageAction action, const QString& package)
{
    if (serial.isEmpty() || !validPackageName(package)) {
        return 0;
    }
    return enqueue({.serial = serial, .install = false, .action = action, .package = package});
}

quint64 AppActions::install(const QString& serial, const QString& apkPath)
{
    if (serial.isEmpty()) {
        return 0;
    }
    return enqueue({.serial = serial, .install = true, .apk = std::filesystem::path(apkPath.toStdWString())});
}

quint64 AppActions::refuse(const QString& serial, const QString& apkPath, const QString& message)
{
    const quint64 id = ++m_lastJob;
    const QVariantMap job =
        installUpdate(id, serial, QFileInfo(apkPath).fileName(), QStringLiteral("failed"), 0, 0, message);
    QMetaObject::invokeMethod(this, [this, job] { installReported(job); }, Qt::QueuedConnection);
    return id;
}

quint64 AppActions::enqueue(Job job)
{
    job.id = ++m_lastJob;
    const quint64 id = job.id;
    if (m_workers.contains(job.serial)) {
        m_waiting[job.serial].push_back(std::move(job));
    } else {
        start(std::move(job));
    }
    return id;
}

void AppActions::start(Job job)
{
    const QString key = job.serial;
    m_workers.insert_or_assign(
        key, std::jthread([this, phone = m_phone, job = std::move(job)](const std::stop_token& stop) {
            platform::setCurrentThreadName("gs-apps");
            if (job.install) {
                runInstall(
                    phone, {.id = job.id, .serial = job.serial, .apk = job.apk},
                    [this](const QVariantMap& update) {
                        QMetaObject::invokeMethod(this, [this, update] { installReported(update); },
                                                  Qt::QueuedConnection);
                    },
                    stop);
            } else {
                ActionOutcome outcome = runPackageAction(phone, job.serial, job.action, job.package, stop);
                QMetaObject::invokeMethod(
                    this, [this, job, outcome = std::move(outcome)] { acted(job, outcome); }, Qt::QueuedConnection);
            }
            QMetaObject::invokeMethod(this, [this, serial = job.serial] { ended(serial); }, Qt::QueuedConnection);
        }));
}

void AppActions::acted(const Job& job, const ActionOutcome& outcome)
{
    if (outcome.ok) {
        qInfo("%s %s on %s", qUtf8Printable(packageActionName(job.action)), qUtf8Printable(job.package),
              qUtf8Printable(job.serial));
    } else {
        qWarning("%s %s on %s failed: %s (%s)", qUtf8Printable(packageActionName(job.action)),
                 qUtf8Printable(job.package), qUtf8Printable(job.serial), qUtf8Printable(outcome.reason),
                 qUtf8Printable(outcome.detail));
    }
    emit actionFinished(job.id, job.serial, job.package, outcome.ok, outcome.reason);
}

void AppActions::installReported(const QVariantMap& job)
{
    const QString state = job.value(QStringLiteral("state")).toString();
    const QString name = job.value(QStringLiteral("name")).toString();
    const QString serial = job.value(QStringLiteral("serial")).toString();
    if (state == u"done") {
        qInfo("%s is installed on %s", qUtf8Printable(name), qUtf8Printable(serial));
    } else if (state == u"failed") {
        qWarning("%s was not installed on %s: %s", qUtf8Printable(name), qUtf8Printable(serial),
                 qUtf8Printable(job.value(QStringLiteral("message")).toString()));
    }
    emit installUpdated(job);
}

void AppActions::ended(const QString& serial)
{
    m_workers.erase(serial);
    const auto waiting = m_waiting.find(serial);
    if (waiting == m_waiting.end()) {
        return;
    }
    if (waiting->second.empty()) {
        m_waiting.erase(waiting);
        return;
    }
    Job next = std::move(waiting->second.front());
    waiting->second.pop_front();
    if (waiting->second.empty()) {
        m_waiting.erase(waiting);
    }
    start(std::move(next));
}

}
