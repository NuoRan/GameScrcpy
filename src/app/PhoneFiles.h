#pragma once

#include "FileTransfers.h"

#include <gs/adb/AdbClient.h>

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

struct PhoneEntry {
    QString name;
    bool directory = false;
    quint64 size = 0;
    qint64 modified = 0;
};

struct FilesAvailability {
    QString name;
    bool online = false;
};

enum class NameProblem : std::uint8_t { None, Empty, Slash, Dots, TooLong, Taken };

[[nodiscard]] QString filesRoot();
[[nodiscard]] std::optional<QString> phoneFolder(const QString& path);
[[nodiscard]] QVariantList folderCrumbs(const QString& folder);
[[nodiscard]] QString fileSizeText(quint64 bytes);
[[nodiscard]] NameProblem checkName(const QString& name, const QStringList& taken, const QString& current = {});

class PhoneFileModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        DirectoryRole,
        SizeRole,
        SizeTextRole,
        ModifiedRole,
        PathRole,
        SelectedRole,
    };
    Q_ENUM(Role)

    explicit PhoneFileModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void reset(const QString& folder, std::vector<PhoneEntry> entries);
    void select(int row, Qt::KeyboardModifiers modifiers);
    void selectAll();
    void clearSelection();
    void selectNames(const QStringList& names);
    [[nodiscard]] const QString& folder() const noexcept { return m_folder; }
    [[nodiscard]] const std::vector<PhoneEntry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] const PhoneEntry* entry(int row) const;
    [[nodiscard]] QStringList names() const;
    [[nodiscard]] QStringList selectedNames() const;
    [[nodiscard]] std::vector<PhoneEntry> selected() const;
    [[nodiscard]] quint64 selectedBytes() const;
    [[nodiscard]] bool selectionHasFolders() const;

signals:
    void selectionChanged();

private:
    void changeSelection(std::vector<char> next);

    QString m_folder;
    std::vector<PhoneEntry> m_entries;
    std::vector<char> m_selected;
    int m_anchor = -1;
};

class TransferListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        JobIdRole = Qt::UserRole + 1,
        DirectionRole,
        NameRole,
        DoneRole,
        TotalRole,
        StateRole,
        MessageRole,
        ProgressRole,
        ActiveRole,
    };
    Q_ENUM(Role)

    explicit TransferListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void reset(std::vector<TransferJob> jobs);
    void upsert(const TransferJob& job);
    void removeFinished();
    [[nodiscard]] int active() const;
    [[nodiscard]] const std::vector<TransferJob>& jobs() const noexcept { return m_jobs; }

private:
    std::vector<TransferJob> m_jobs;
};

class PhoneFiles final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString serial READ serial NOTIFY openedChanged FINAL)
    Q_PROPERTY(QString phoneName READ phoneName NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool online READ online NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged FINAL)
    Q_PROPERTY(QVariantList crumbs READ crumbs NOTIFY folderChanged FINAL)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY folderChanged FINAL)
    Q_PROPERTY(QAbstractItemModel* entries READ entries CONSTANT FINAL)
    Q_PROPERTY(QAbstractItemModel* transfers READ transfers CONSTANT FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString problem READ problem NOTIFY stateChanged FINAL)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY stateChanged FINAL)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged FINAL)
    Q_PROPERTY(double selectedBytes READ selectedBytes NOTIFY selectionChanged FINAL)
    Q_PROPERTY(bool selectionHasFolders READ selectionHasFolders NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged FINAL)
    Q_PROPERTY(int activeTransfers READ activeTransfers NOTIFY transfersChanged FINAL)
    Q_PROPERTY(int transferCount READ transferCount NOTIFY transfersChanged FINAL)
    Q_PROPERTY(int dragPhase READ dragPhase NOTIFY dragChanged FINAL)
    Q_PROPERTY(double dragDone READ dragDone NOTIFY dragChanged FINAL)
    Q_PROPERTY(double dragTotal READ dragTotal NOTIFY dragChanged FINAL)
    Q_PROPERTY(int dragCount READ dragCount NOTIFY dragChanged FINAL)
    Q_PROPERTY(QStringList dragUrls READ dragUrls NOTIFY dragChanged FINAL)
    Q_PROPERTY(QUrl downloadsFolder READ downloadsFolder CONSTANT FINAL)

public:
    static constexpr qint64 kDragLimit = 200LL * 1024 * 1024;
    static constexpr int kDragIdle = 0;
    static constexpr int kDragPreparing = 1;
    static constexpr int kDragReady = 2;

    struct Source {
        std::function<FilesAvailability(const QString& serial)> availability;
    };

    PhoneFiles(adb::AdbClient adb, FileTransfers& transfers, Source source, std::filesystem::path dragFolder,
               QObject* parent = nullptr);
    ~PhoneFiles() override;
    PhoneFiles(const PhoneFiles&) = delete;
    PhoneFiles& operator=(const PhoneFiles&) = delete;

    static PhoneFiles* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] static QString filesTag() { return QStringLiteral("files"); }
    [[nodiscard]] static QString dragTag() { return QStringLiteral("drag"); }

    [[nodiscard]] const QString& serial() const noexcept { return m_serial; }
    [[nodiscard]] QString phoneName() const { return m_availability.name; }
    [[nodiscard]] bool online() const noexcept { return !m_serial.isEmpty() && m_availability.online; }
    [[nodiscard]] const QString& folder() const noexcept { return m_folder; }
    [[nodiscard]] QVariantList crumbs() const { return folderCrumbs(m_folder); }
    [[nodiscard]] bool canGoUp() const;
    [[nodiscard]] QAbstractItemModel* entries() { return &m_model; }
    [[nodiscard]] QAbstractItemModel* transfers() { return &m_transferModel; }
    [[nodiscard]] const PhoneFileModel& model() const noexcept { return m_model; }
    [[nodiscard]] const TransferListModel& transferModel() const noexcept { return m_transferModel; }
    [[nodiscard]] bool loading() const noexcept { return m_loading; }
    [[nodiscard]] bool busy() const noexcept { return m_busy > 0; }
    [[nodiscard]] QString problem() const;
    [[nodiscard]] int itemCount() const { return m_model.rowCount(); }
    [[nodiscard]] int selectedCount() const { return static_cast<int>(m_model.selected().size()); }
    [[nodiscard]] double selectedBytes() const { return static_cast<double>(m_model.selectedBytes()); }
    [[nodiscard]] bool selectionHasFolders() const { return m_model.selectionHasFolders(); }
    [[nodiscard]] QString selectedName() const;
    [[nodiscard]] int activeTransfers() const { return m_transferModel.active(); }
    [[nodiscard]] int transferCount() const { return m_transferModel.rowCount(); }
    [[nodiscard]] int dragPhase() const noexcept { return m_dragPhase; }
    [[nodiscard]] double dragDone() const;
    [[nodiscard]] double dragTotal() const noexcept { return m_dragTotal; }
    [[nodiscard]] int dragCount() const noexcept { return static_cast<int>(m_dragOrder.size()); }
    [[nodiscard]] QStringList dragUrls() const { return m_dragUrls; }
    [[nodiscard]] QUrl downloadsFolder() const;

    Q_INVOKABLE void open(const QString& serial);
    Q_INVOKABLE void close();
    Q_INVOKABLE void openFolder(const QString& path);
    Q_INVOKABLE void enter(int row);
    Q_INVOKABLE void up();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void select(int row, int modifiers);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(int row) const;
    Q_INVOKABLE QString nameProblem(const QString& name, const QString& current = {}) const;
    Q_INVOKABLE bool makeFolder(const QString& name);
    Q_INVOKABLE bool rename(const QString& from, const QString& to);
    Q_INVOKABLE bool removeSelected();
    Q_INVOKABLE int upload(const QList<QUrl>& urls);
    Q_INVOKABLE int downloadSelected(const QUrl& folder);
    Q_INVOKABLE QString prepareDrag();
    Q_INVOKABLE void cancelDrag();
    Q_INVOKABLE void finishDrag();
    Q_INVOKABLE void cancelTransfer(double id);
    Q_INVOKABLE void clearTransfers();
    Q_INVOKABLE QString sizeText(double bytes) const;

    void refreshAvailability();

signals:
    void openedChanged();
    void folderChanged();
    void stateChanged();
    void selectionChanged();
    void transfersChanged();
    void dragChanged();
    void notice(const QString& text, const QString& tone);

private:
    void post(std::function<void()> task);
    void work(const std::stop_token& stop);
    void list(bool force);
    void listed(quint64 generation, adb::AdbResult<std::vector<adb::RemoteEntry>> result);
    void operate(std::function<adb::AdbResult<void>(const adb::AdbClient&, const std::string&)> operation,
                 const QString& success, std::function<QString(const QString&)> failure);
    void operated(const QString& serial, bool ok, const QString& text);
    void transferUpdated(const QVariantMap& job);
    void dragUpdated(const TransferJob& job);
    void resetDrag();
    void cleanDragFolders() const;

    static inline PhoneFiles* s_instance = nullptr;

    const adb::AdbClient m_adb;
    FileTransfers& m_transfers;
    Source m_source;
    std::filesystem::path m_dragRoot;
    std::filesystem::path m_dragRun;
    PhoneFileModel m_model;
    TransferListModel m_transferModel;
    QString m_serial;
    FilesAvailability m_availability;
    QString m_folder;
    quint64 m_generation = 0;
    bool m_loading = false;
    bool m_relist = false;
    QString m_listProblem;
    int m_busy = 0;
    int m_dragPhase = kDragIdle;
    quint64 m_dragNumber = 0;
    std::vector<qint64> m_dragOrder;
    std::map<qint64, double> m_dragDone;
    std::map<qint64, QString> m_dragFiles;
    double m_dragTotal = 0;
    QStringList m_dragUrls;
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<std::function<void()>> m_tasks;
    std::jthread m_worker;
};

}
