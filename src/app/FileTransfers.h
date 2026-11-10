#pragma once

#include <gs/adb/AdbClient.h>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace gs::app {

enum class TransferDirection : std::uint8_t { Push, Pull };
enum class TransferState : std::uint8_t { Queued, Running, Done, Failed, Cancelled };

struct TransferJob {
    qint64 id = 0;
    QString serial;
    TransferDirection direction = TransferDirection::Push;
    QString name;
    double done = 0;
    double total = 0;
    TransferState state = TransferState::Queued;
    QString message;
    QString local;
    QString remote;
    QString tag;

    [[nodiscard]] bool finished() const noexcept
    {
        return state == TransferState::Done || state == TransferState::Failed || state == TransferState::Cancelled;
    }
};

[[nodiscard]] QString transferStateName(TransferState state);
[[nodiscard]] QVariantMap transferVariant(const TransferJob& job);
[[nodiscard]] QString remoteChild(const QString& folder, const QString& name);
[[nodiscard]] QString remoteParent(const QString& path);
[[nodiscard]] QString remoteName(const QString& path);
[[nodiscard]] QString numberedName(const QString& name, int number, bool directory);
[[nodiscard]] QString windowsName(const QString& name);

class FileTransfers final : public QObject
{
    Q_OBJECT

public:
    static constexpr std::size_t kWorkers = 3;
    static constexpr std::size_t kKeptFinished = 200;
    static constexpr int kMostNameTries = 999;
    static constexpr std::chrono::milliseconds kReportEvery{100};

    explicit FileTransfers(adb::AdbClient adb, QObject* parent = nullptr);
    ~FileTransfers() override;
    FileTransfers(const FileTransfers&) = delete;
    FileTransfers& operator=(const FileTransfers&) = delete;

    qint64 push(const QString& serial, const QString& localPath, const QString& remoteFolder, const QString& tag = {});
    qint64 pull(const QString& serial, const QString& remotePath, const QString& localFolder, const QString& tag = {});
    bool cancel(qint64 id);
    void clearFinished(const QString& serial);
    [[nodiscard]] std::optional<TransferJob> job(qint64 id) const;
    [[nodiscard]] std::vector<TransferJob> jobs(const QString& serial) const;

signals:
    void updated(const QVariantMap& job);

private:
    struct Pending {
        qint64 id = 0;
        QString serial;
        TransferDirection direction = TransferDirection::Push;
        QString local;
        QString remote;
        QString name;
        std::stop_source stop;
    };

    struct Outcome {
        TransferState state = TransferState::Done;
        QString message;
    };

    using Progress = std::function<void(std::uint64_t done, std::uint64_t total)>;

    qint64 enqueue(TransferJob job, Pending pending);
    void work(const std::stop_token& stop);
    void run(const Pending& pending);
    [[nodiscard]] Outcome pushItem(const Pending& pending, TransferJob& job, const std::stop_token& stop,
                                   const Progress& progress);
    [[nodiscard]] Outcome pullItem(const Pending& pending, TransferJob& job, const std::stop_token& stop,
                                   const Progress& progress);
    [[nodiscard]] adb::AdbResult<std::string> freeRemotePath(const std::string& serial, const QString& folder,
                                                             const QString& name, bool directory) const;
    void report(const TransferJob& job);
    void apply(const TransferJob& job);
    void trim();
    [[nodiscard]] std::optional<std::size_t> readyIndex() const;

    const adb::AdbClient m_adb;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<Pending> m_queue;
    std::set<QString> m_busy;
    std::map<qint64, std::stop_source> m_running;
    std::vector<TransferJob> m_jobs;
    qint64 m_lastId = 0;
    std::vector<std::jthread> m_workers;
};

struct DropPlan {
    QStringList apks;
    QStringList files;
    int skipped = 0;
};

[[nodiscard]] DropPlan planDrop(const QList<QUrl>& urls);

struct DropNotice {
    QString source;
    QString title;
    double progress = 0;
    bool clear = false;
    QString toast;
    QString icon;
};

class DropFeedback
{
public:
    static constexpr const char* kPushSource = "gs-files-push";
    static constexpr const char* kInstallSource = "gs-files-install-";

    [[nodiscard]] DropNotice startPush(const QList<qint64>& ids, const QStringList& names);
    [[nodiscard]] std::optional<DropNotice> pushUpdated(const QVariantMap& job);
    [[nodiscard]] DropNotice startInstall(const QString& name);
    [[nodiscard]] std::optional<DropNotice> installUpdated(const QVariantMap& job);
    [[nodiscard]] bool idle() const noexcept { return m_pushes.empty() && m_installs.empty(); }

private:
    struct Push {
        qint64 id = 0;
        QString name;
        double done = 0;
        double total = 0;
        QString state;
        QString message;
    };

    struct Install {
        QString name;
        qint64 id = 0;
    };

    [[nodiscard]] DropNotice pushProgress() const;

    std::vector<Push> m_pushes;
    std::vector<Install> m_installs;
};

}
