#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace gs::app {

struct BatchPhone {
    QString serial;
    QString name;
    bool online = false;
    bool mirroring = false;
    bool apps = false;

    friend bool operator==(const BatchPhone&, const BatchPhone&) = default;
};

class DeviceBatch final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The device manager owns the batch actions on several phones")
    Q_PROPERTY(QStringList selected READ selected NOTIFY selectionChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY selectionChanged FINAL)
    Q_PROPERTY(bool selecting READ selecting NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QString appSource READ appSource NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(bool canStartTask READ canStartTask NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(bool canLaunchApp READ canLaunchApp NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(bool canPushFiles READ canPushFiles NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged FINAL)
    Q_PROPERTY(int running READ running NOTIFY jobsChanged FINAL)

public:
    static constexpr qsizetype kMostPackageLength = 255;

    struct Actions {
        std::function<QString(const QString& task, const QString& serial)> startTask;
        std::function<void(const QString& serial, const QString& package, const QString& label)> launchApp;
        std::function<void(const QStringList& serials, const QString& apkPath)> installApk;
        std::function<void(const QStringList& serials, const QStringList& localPaths)> pushFiles;
    };

    explicit DeviceBatch(Actions actions, QObject* parent = nullptr);

    void setPhones(std::vector<BatchPhone> phones);
    [[nodiscard]] const std::vector<BatchPhone>& phones() const noexcept { return m_phones; }

    [[nodiscard]] QStringList selected() const { return m_selected; }
    [[nodiscard]] int count() const noexcept { return static_cast<int>(m_selected.size()); }
    [[nodiscard]] bool selecting() const noexcept { return !m_selected.isEmpty(); }
    [[nodiscard]] QString appSource() const;
    [[nodiscard]] bool canStartTask() const;
    [[nodiscard]] bool canLaunchApp() const;
    [[nodiscard]] bool canInstall() const;
    [[nodiscard]] bool canPushFiles() const;
    [[nodiscard]] QVariantList jobs() const;
    [[nodiscard]] int running() const;

    Q_INVOKABLE void setSelected(const QString& serial, bool on);
    Q_INVOKABLE void toggle(const QString& serial);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void startTask(const QString& id, const QString& name);
    Q_INVOKABLE void launchApp(const QString& package, const QString& label);
    Q_INVOKABLE void installApk(const QUrl& file);
    Q_INVOKABLE void pushFiles(const QList<QUrl>& files);
    Q_INVOKABLE void clearFinished();

    void installUpdated(const QVariantMap& job);
    void transferUpdated(const QVariantMap& job);
    void appRefused(const QString& serial);
    void appShown(const QString& serial, const QString& package);

signals:
    void selectionChanged();
    void availabilityChanged();
    void jobsChanged();

private:
    enum class Kind : std::uint8_t { Task, App, Apk, File };

    struct Job {
        quint64 key = 0;
        Kind kind = Kind::Task;
        QString serial;
        QString phone;
        QString name;
        QString package;
        QString state;
        QString message;
        double done = 0.0;
        double total = 0.0;
        qint64 id = 0;
        bool assigned = false;
        bool pending = false;
    };

    struct Availability {
        QString appSource;
        bool task = false;
        bool app = false;
        bool install = false;
        bool push = false;

        friend bool operator==(const Availability&, const Availability&) = default;
    };

    [[nodiscard]] Availability availability() const;
    void announce(const Availability& before);
    [[nodiscard]] const BatchPhone* phoneOf(const QString& serial) const;
    [[nodiscard]] QString phoneName(const QString& serial) const;
    [[nodiscard]] bool anySelected(const std::function<bool(const BatchPhone&)>& able) const;
    void add(Job job);
    void follow(Kind kind, const QVariantMap& update);
    [[nodiscard]] static bool finished(const Job& job);
    [[nodiscard]] static QString statusOf(const Job& job);
    [[nodiscard]] static double progressOf(const Job& job);
    [[nodiscard]] static QString kindName(Kind kind);

    Actions m_actions;
    std::vector<BatchPhone> m_phones;
    QStringList m_selected;
    std::vector<Job> m_jobs;
    quint64 m_lastKey = 0;
};

}
