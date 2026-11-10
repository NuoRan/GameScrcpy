#pragma once

#include <gs/adb/AdbClient.h>
#include <gs/adb/ShellProtocol.h>

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

namespace gs::app {

enum class PackageAction : std::uint8_t {
    ForceStop,
    Disable,
    Enable,
    ClearData,
    Uninstall,
};

inline constexpr std::array kPackageActions{PackageAction::ForceStop, PackageAction::Disable, PackageAction::Enable,
                                            PackageAction::ClearData, PackageAction::Uninstall};

struct PmResult {
    bool ok = false;
    QString code;
};

struct ActionOutcome {
    bool ok = false;
    QString reason;
    QString detail;
};

using PushProgress = std::function<void(std::uint64_t done, std::uint64_t total)>;

struct PhoneAccess {
    std::function<adb::AdbResult<adb::ShellResult>(const std::string& serial, const std::string& command,
                                                   const std::stop_token& stop)>
        run;
    std::function<adb::AdbResult<void>(const std::string& serial, const std::filesystem::path& local,
                                       const std::string& remote, PushProgress progress, const std::stop_token& stop)>
        push;
};

struct InstallJob {
    quint64 id = 0;
    QString serial;
    std::filesystem::path apk;
};

[[nodiscard]] bool validPackageName(const QString& package);
[[nodiscard]] std::optional<PackageAction> packageActionNamed(const QString& name);
[[nodiscard]] QString packageActionName(PackageAction action);
[[nodiscard]] bool actionAllowed(PackageAction action, bool system, bool enabled);
[[nodiscard]] std::optional<std::string> packageCommand(PackageAction action, const QString& package);
[[nodiscard]] std::string remoteApkPath(quint64 job);
[[nodiscard]] std::string installCommand(std::string_view remote);
[[nodiscard]] std::string removeCommand(std::string_view remote);
[[nodiscard]] PmResult parsePmResult(std::string_view out, std::string_view err);
[[nodiscard]] PmResult judgeAction(PackageAction action, const adb::ShellResult& result);
[[nodiscard]] QString failureReason(const QString& code);
[[nodiscard]] QVariantMap installUpdate(quint64 id, const QString& serial, const QString& name, const QString& state,
                                        double done, double total, const QString& message);
[[nodiscard]] ActionOutcome runPackageAction(const PhoneAccess& phone, const QString& serial, PackageAction action,
                                             const QString& package, const std::stop_token& stop);
void runInstall(const PhoneAccess& phone, const InstallJob& job, const std::function<void(const QVariantMap&)>& report,
                const std::stop_token& stop);
[[nodiscard]] PhoneAccess phoneAccess(adb::AdbClient adb);

class AppActions final : public QObject
{
    Q_OBJECT

public:
    explicit AppActions(PhoneAccess phone, QObject* parent = nullptr);
    ~AppActions() override;

    quint64 act(const QString& serial, PackageAction action, const QString& package);
    quint64 install(const QString& serial, const QString& apkPath);
    quint64 refuse(const QString& serial, const QString& apkPath, const QString& message);
    [[nodiscard]] bool idle() const noexcept { return m_workers.empty() && m_waiting.empty(); }

signals:
    void actionFinished(quint64 id, const QString& serial, const QString& package, bool ok, const QString& reason);
    void installUpdated(const QVariantMap& job);

private:
    struct Job {
        quint64 id = 0;
        QString serial;
        bool install = false;
        PackageAction action = PackageAction::ForceStop;
        QString package;
        std::filesystem::path apk;
    };

    quint64 enqueue(Job job);
    void start(Job job);
    void acted(const Job& job, const ActionOutcome& outcome);
    void installReported(const QVariantMap& job);
    void ended(const QString& serial);

    PhoneAccess m_phone;
    quint64 m_lastJob = 0;
    std::map<QString, std::deque<Job>> m_waiting;
    std::map<QString, std::jthread> m_workers;
};

}
