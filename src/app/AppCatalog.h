#pragma once

#include "AppActions.h"

#include <gs/protocol/Enums.h>
#include <gs/protocol/Messages.h>

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

struct AppItem {
    QString package;
    QString label;
    std::uint64_t versionCode = 0;
    bool system = false;
    bool game = false;
    bool enabled = true;
    QString icon;
};

class AppListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        PackageRole = Qt::UserRole + 1,
        LabelRole,
        IconRole,
        SystemRole,
        GameRole,
        EnabledRole,
        CheckedRole,
    };
    Q_ENUM(Role)

    enum class Filter : std::uint8_t {
        All,
        Games,
        System,
    };

    explicit AppListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void reset(std::vector<AppItem> apps);
    void setIcon(const QString& package, const QString& icon);
    void setFilter(Filter filter);
    void setQuery(const QString& query);
    [[nodiscard]] const std::vector<AppItem>& all() const noexcept { return m_apps; }
    [[nodiscard]] const AppItem* find(const QString& package) const;
    [[nodiscard]] QString packageAt(int row) const;
    void setChecked(const QString& package, bool checked);
    void checkShown();
    void clearChecked();
    [[nodiscard]] bool isChecked(const QString& package) const { return m_checked.contains(package); }
    [[nodiscard]] QStringList checkedPackages() const;
    [[nodiscard]] int checkedCount() const noexcept { return static_cast<int>(m_checked.size()); }

private:
    [[nodiscard]] bool accepts(const AppItem& app) const;
    void refilter();
    void checksChanged();

    std::vector<AppItem> m_apps;
    std::vector<std::size_t> m_shown;
    Filter m_filter = Filter::All;
    QString m_query;
    std::set<QString> m_checked;
};

struct AppAvailability {
    QString name;
    bool connected = false;
    bool apps = false;
    bool bulk = false;
    bool independent = false;
};

class AppCatalog final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString serial READ serial NOTIFY openedChanged FINAL)
    Q_PROPERTY(QString phoneName READ phoneName NOTIFY stateChanged FINAL)
    Q_PROPERTY(QAbstractItemModel* apps READ apps CONSTANT FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool independentDisplays READ independentDisplays NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString problem READ problem NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY stateChanged FINAL)
    Q_PROPERTY(int total READ total NOTIFY countsChanged FINAL)
    Q_PROPERTY(int games READ games NOTIFY countsChanged FINAL)
    Q_PROPERTY(int systemApps READ systemApps NOTIFY countsChanged FINAL)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    Q_PROPERTY(int filter READ filter WRITE setFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString current READ current WRITE setCurrent NOTIFY currentChanged FINAL)
    Q_PROPERTY(QVariantMap currentApp READ currentApp NOTIFY currentChanged FINAL)
    Q_PROPERTY(bool selecting READ selecting WRITE setSelecting NOTIFY selectionChanged FINAL)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QVariantMap eligible READ eligible NOTIFY selectionChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY workChanged FINAL)
    Q_PROPERTY(QString activity READ activity NOTIFY workChanged FINAL)

public:
    static constexpr int kIconSize = 96;
    static constexpr std::size_t kIconsInFlight = 3;
    static constexpr int kBusyRetries = 2;

    struct Source {
        std::function<AppAvailability(const QString& serial)> availability;
        std::function<bool(const QString& serial, std::uint32_t requestId)> list;
        std::function<bool(const QString& serial, std::uint32_t requestId, const QString& package, int sizePx)> icon;
        std::function<void(const QString& serial, const QString& package, const QString& label, bool independent)>
            launch;
    };

    struct Actions {
        std::function<quint64(const QString& serial, PackageAction action, const QString& package)> act;
        std::function<quint64(const QString& serial, const QString& apkPath)> install;
    };

    AppCatalog(std::filesystem::path iconDirectory, Source source, QObject* parent = nullptr);
    ~AppCatalog() override;

    static AppCatalog* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] const QString& serial() const noexcept { return m_serial; }
    [[nodiscard]] QString phoneName() const { return m_availability.name; }
    [[nodiscard]] QAbstractItemModel* apps() { return &m_model; }
    [[nodiscard]] const AppListModel& model() const noexcept { return m_model; }
    [[nodiscard]] bool loading() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool independentDisplays() const { return ready() && m_availability.independent; }
    [[nodiscard]] QString problem() const;
    [[nodiscard]] bool canRetry() const { return m_listFailed; }
    [[nodiscard]] int total() const;
    [[nodiscard]] int games() const;
    [[nodiscard]] int systemApps() const;
    [[nodiscard]] const QString& query() const noexcept { return m_query; }
    void setQuery(const QString& query);
    [[nodiscard]] int filter() const noexcept { return m_filter; }
    void setFilter(int filter);
    [[nodiscard]] std::size_t iconsInFlight() const noexcept { return m_iconRequests.size(); }
    void setActions(Actions actions);
    [[nodiscard]] const QString& current() const noexcept { return m_current; }
    void setCurrent(const QString& package);
    [[nodiscard]] QVariantMap currentApp() const;
    [[nodiscard]] bool selecting() const noexcept { return m_selecting; }
    void setSelecting(bool selecting);
    [[nodiscard]] int checkedCount() const noexcept { return m_model.checkedCount(); }
    [[nodiscard]] QVariantMap eligible() const;
    [[nodiscard]] bool busy() const noexcept { return !m_running.empty(); }
    [[nodiscard]] QString activity() const;

    Q_INVOKABLE void open(const QString& serial);
    Q_INVOKABLE void close();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void launch(const QString& package, bool independent);
    Q_INVOKABLE void copyPackage(const QString& package) const;
    Q_INVOKABLE void toggleChecked(const QString& package);
    Q_INVOKABLE void checkAll();
    Q_INVOKABLE void clearChecked();
    Q_INVOKABLE QVariantMap plan(const QString& action, bool batch) const;
    Q_INVOKABLE int run(const QString& action, const QStringList& packages);
    Q_INVOKABLE int install(const QList<QUrl>& files);

    void listed(const QString& serial, std::uint32_t requestId, const std::vector<protocol::AppEntry>& apps);
    void iconArrived(const QString& serial, std::uint32_t requestId, const QString& package, const QByteArray& png);
    void requestFailed(const QString& serial, std::uint32_t requestId, protocol::ErrorCode code,
                       const QString& message);
    void availabilityChanged(const QString& serial);
    void actionFinished(quint64 id, const QString& serial, const QString& package, bool ok, const QString& reason);
    void installUpdated(const QVariantMap& job);

    [[nodiscard]] static std::optional<std::filesystem::path> iconFile(const std::filesystem::path& directory,
                                                                       const QString& package,
                                                                       std::uint64_t versionCode);

signals:
    void openedChanged();
    void stateChanged();
    void countsChanged();
    void queryChanged();
    void filterChanged();
    void currentChanged();
    void selectionChanged();
    void workChanged();
    void reported(const QString& text, const QString& tone);

private:
    struct Running {
        PackageAction action = PackageAction::ForceStop;
        QString package;
        QString label;
    };

    struct Batch {
        PackageAction action = PackageAction::ForceStop;
        int total = 0;
        int succeeded = 0;
        int failed = 0;
        QString label;
        QString failedLabel;
        QString reason;
    };

    void refresh();
    void relist();
    void pumpIcons();
    void remember(const Running& running);
    [[nodiscard]] static std::pair<QString, QString> summary(const Batch& batch);
    [[nodiscard]] std::uint32_t nextRequest() noexcept;
    [[nodiscard]] std::optional<QString> cachedIcon(const AppItem& app) const;
    [[nodiscard]] std::optional<QString> store(const AppItem& app, const QByteArray& png) const;

    static inline AppCatalog* s_instance = nullptr;

    std::filesystem::path m_iconDirectory;
    Source m_source;
    AppListModel m_model;
    QString m_serial;
    AppAvailability m_availability;
    QString m_query;
    int m_filter = 0;
    std::uint32_t m_lastRequest = 0;
    std::uint32_t m_listRequest = 0;
    bool m_wantList = false;
    bool m_listFailed = false;
    std::deque<QString> m_iconQueue;
    std::map<std::uint32_t, QString> m_iconRequests;
    std::map<QString, int> m_retries;
    Actions m_actions;
    QString m_current;
    bool m_selecting = false;
    std::map<quint64, Running> m_running;
    std::optional<Batch> m_batch;
    std::map<quint64, QString> m_installs;
    QVariantMap m_installing;
    std::map<QString, std::map<QString, AppItem>> m_turnedOff;
};

}
