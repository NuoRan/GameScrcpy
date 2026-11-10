#pragma once

#include "BlueprintModel.h"
#include "DebugRecord.h"
#include "RecordingConvert.h"
#include "ScriptHighlighter.h"
#include "ScriptWorkbenchApi.h"
#include "ScriptWorkbenchConsole.h"
#include "ScriptWorkbenchFiles.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQuickTextDocument>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

class QQmlEngine;
class QQuickWindow;
class QTextDocument;

namespace gs::app {

class DeviceManager;
class ScriptWorkbench;

class ScriptDocument final : public QObject, public BlueprintText
{
    Q_OBJECT
    Q_INTERFACES(gs::app::BlueprintText)
    QML_ELEMENT
    QML_UNCREATABLE("Files are opened by the script workbench")
    Q_PROPERTY(QString path READ path CONSTANT FINAL)
    Q_PROPERTY(QString name READ name CONSTANT FINAL)
    Q_PROPERTY(bool image READ image CONSTANT FINAL)
    Q_PROPERTY(bool readOnly READ readOnly CONSTANT FINAL)
    Q_PROPERTY(bool draft READ draft CONSTANT FINAL)
    Q_PROPERTY(bool script READ script CONSTANT FINAL)
    Q_PROPERTY(QUrl source READ source CONSTANT FINAL)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged FINAL)
    Q_PROPERTY(bool changedOnDisk READ changedOnDisk NOTIFY changedOnDiskChanged FINAL)
    Q_PROPERTY(QVariantList marks READ marks NOTIFY marksChanged FINAL)
    Q_PROPERTY(bool recorded READ recorded CONSTANT FINAL)
    Q_PROPERTY(QString folder READ folder CONSTANT FINAL)

public:
    ScriptDocument(ScriptWorkbench& bench, QString folder, QString path, ScriptFileKind kind, QObject* parent = nullptr);
    ~ScriptDocument() override;
    ScriptDocument(const ScriptDocument&) = delete;
    ScriptDocument& operator=(const ScriptDocument&) = delete;

    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] QString name() const;
    [[nodiscard]] bool image() const noexcept { return m_kind == ScriptFileKind::Image; }
    [[nodiscard]] bool readOnly() const noexcept;
    void lock() noexcept;
    void markDraft() noexcept;
    void markFresh(QString initial)
    {
        m_fresh = true;
        m_initial = std::move(initial);
    }
    [[nodiscard]] bool draft() const noexcept { return m_draft; }
    [[nodiscard]] bool recorded() const noexcept { return m_recorded; }
    void markRecorded() noexcept;
    [[nodiscard]] QString folder() const { return m_folder; }
    [[nodiscard]] bool script() const noexcept { return m_kind == ScriptFileKind::Code; }
    [[nodiscard]] QUrl source() const;
    [[nodiscard]] bool dirty() const { return m_text != m_saved || m_layoutDirty; }
    [[nodiscard]] bool changedOnDisk() const noexcept { return m_changedOnDisk; }
    [[nodiscard]] QVariantList marks() const;
    [[nodiscard]] QString absolutePath() const;
    [[nodiscard]] QString text() const override { return m_text; }
    bool edit(std::span<const TextChange> changes) override;
    [[nodiscard]] QTextDocument* editor() const override { return m_edit.data(); }
    void setLayoutDirty(bool dirty) override;

    [[nodiscard]] bool load(QString& problem);
    [[nodiscard]] bool save(bool& busy);
    void checkDisk();
    void setProblems(std::vector<ScriptProblem> problems);
    void setColors(const std::array<QColor, kScriptTokenKinds>& colors);

    Q_INVOKABLE void attach(QQuickTextDocument* document);
    Q_INVOKABLE int positionOf(int line, int column) const;
    Q_INVOKABLE QVariantMap completion(int position, bool asked) const;
    Q_INVOKABLE QString lineBreak(int position) const;
    Q_INVOKABLE void reload();
    Q_INVOKABLE void keepMine();
    Q_INVOKABLE QVariantMap referenceAt(int position) const;
    Q_INVOKABLE void findReferences(int position);

signals:
    void dirtyChanged();
    void changedOnDiskChanged();
    void marksChanged();
    void textChanged();
    void saved();

private:
    [[nodiscard]] bool readDisk(QByteArray& bytes) const;
    [[nodiscard]] bool decode(const QByteArray& bytes, QString& text);
    void follow();
    void replaceText(const QString& text);
    void setChangedOnDisk(bool changed);

    ScriptWorkbench& m_bench;
    QString m_folder;
    QString m_path;
    ScriptFileKind m_kind;
    bool m_locked = false;
    bool m_draft = false;
    bool m_recorded = false;
    bool m_layoutDirty = false;
    bool m_fresh = false;
    QString m_initial;
    QString m_text;
    QString m_saved;
    QByteArray m_disk;
    bool m_crlf = false;
    bool m_bom = false;
    bool m_changedOnDisk = false;
    std::vector<ScriptProblem> m_problems;
    QPointer<QTextDocument> m_edit;
    QPointer<ScriptHighlighter> m_highlighter;
};

class ScriptDocumentList final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role { DocumentRole = Qt::UserRole + 1, PathRole };

    using QAbstractListModel::QAbstractListModel;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] const std::vector<ScriptDocument*>& all() const noexcept { return m_documents; }
    [[nodiscard]] int size() const noexcept { return static_cast<int>(m_documents.size()); }
    [[nodiscard]] ScriptDocument* at(int row) const;
    void append(ScriptDocument* document);
    void remove(int row);

private:
    std::vector<ScriptDocument*> m_documents;
};

class ScriptConsoleModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role { TimeRole = Qt::UserRole + 1, TextRole, LevelRole, FileRole, LineRole, ColumnRole, PlaceRole };

    using QAbstractListModel::QAbstractListModel;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void reset(std::vector<LogicReport> lines);
    void append(LogicReport line);

private:
    std::deque<LogicReport> m_lines;
};

class ScriptWorkbench final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The script workbench is opened from a profile")
    Q_PROPERTY(QString name READ name NOTIFY nameChanged FINAL)
    Q_PROPERTY(QString folder READ folder CONSTANT FINAL)
    Q_PROPERTY(bool hasLogic READ hasLogic NOTIFY filesChanged FINAL)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged FINAL)
    Q_PROPERTY(QAbstractItemModel* documents READ documents CONSTANT FINAL)
    Q_PROPERTY(int count READ count NOTIFY documentsChanged FINAL)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged FINAL)
    Q_PROPERTY(int dirtyCount READ dirtyCount NOTIFY dirtyChanged FINAL)
    Q_PROPERTY(QVariantList problems READ problems NOTIFY problemsChanged FINAL)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged FINAL)
    Q_PROPERTY(QString checkNotice READ checkNotice NOTIFY checkingChanged FINAL)
    Q_PROPERTY(QVariantMap references READ references NOTIFY referencesChanged FINAL)
    Q_PROPERTY(QVariantList phones READ phones NOTIFY phonesChanged FINAL)
    Q_PROPERTY(QString phone READ phone WRITE setPhone NOTIFY phoneChanged FINAL)
    Q_PROPERTY(bool pinned READ pinned NOTIFY phoneChanged FINAL)
    Q_PROPERTY(QString task READ task NOTIFY runChanged FINAL)
    Q_PROPERTY(QString component READ component NOTIFY componentChanged FINAL)
    Q_PROPERTY(bool locked READ locked NOTIFY componentChanged FINAL)
    Q_PROPERTY(bool canRun READ canRun NOTIFY runChanged FINAL)
    Q_PROPERTY(QString runNote READ runNote NOTIFY runChanged FINAL)
    Q_PROPERTY(QString logicState READ logicState NOTIFY runChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY runChanged FINAL)
    Q_PROPERTY(QImage thumbnail READ thumbnail NOTIFY thumbnailChanged FINAL)
    Q_PROPERTY(bool hasThumbnail READ hasThumbnail NOTIFY thumbnailChanged FINAL)
    Q_PROPERTY(QAbstractItemModel* console READ console CONSTANT FINAL)
    Q_PROPERTY(QVariantList api READ api NOTIFY apiChanged FINAL)
    Q_PROPERTY(QVariantList liveComponents READ liveComponents NOTIFY liveChanged FINAL)
    Q_PROPERTY(QVariantList liveVars READ liveVars NOTIFY liveChanged FINAL)
    Q_PROPERTY(bool replaying READ replaying NOTIFY replayChanged FINAL)
    Q_PROPERTY(bool debugging READ debugging NOTIFY debugChanged FINAL)
    Q_PROPERTY(gs::app::DebugRecord* debugRecord READ debugRecord CONSTANT FINAL)
    Q_PROPERTY(QUrl debugRecordsFolder READ debugRecordsFolder CONSTANT FINAL)
    Q_PROPERTY(QString replayNote READ replayNote NOTIFY replayChanged FINAL)
    Q_PROPERTY(gs::app::LogicTests* tests READ tests CONSTANT FINAL)
    Q_PROPERTY(bool hasTests READ hasTests NOTIFY filesChanged FINAL)

public:
    ScriptWorkbench(DeviceManager& devices, QString folder, QString name, QObject* parent = nullptr);
    ~ScriptWorkbench() override;
    ScriptWorkbench(const ScriptWorkbench&) = delete;
    ScriptWorkbench& operator=(const ScriptWorkbench&) = delete;

    [[nodiscard]] QQuickWindow* openWindow(QQmlEngine& engine);

    [[nodiscard]] QString name() const { return m_name; }
    [[nodiscard]] QString folder() const { return m_folder; }
    [[nodiscard]] bool hasLogic() const noexcept { return m_hasLogic; }
    [[nodiscard]] QVariantList files() const { return m_fileList; }
    [[nodiscard]] QAbstractItemModel* documents() { return &m_documents; }
    [[nodiscard]] int count() const noexcept { return m_documents.size(); }
    [[nodiscard]] int current() const noexcept { return m_current; }
    void setCurrent(int index);
    [[nodiscard]] QString currentPath() const;
    [[nodiscard]] int dirtyCount() const;
    [[nodiscard]] QVariantList problems() const { return m_problemList; }
    [[nodiscard]] bool checking() const { return m_check.state() != QProcess::NotRunning; }
    [[nodiscard]] QString checkNotice() const { return m_checkNotice; }
    [[nodiscard]] QVariantMap references() const { return m_references; }
    [[nodiscard]] Q_INVOKABLE QStringList unsavedFiles() const;
    [[nodiscard]] Q_INVOKABLE QVariantMap openScriptTexts() const;
    void showReferences(const QString& name);
    [[nodiscard]] QVariantList phones() const { return m_phoneList; }
    [[nodiscard]] QString phone() const { return m_phone; }
    void setPhone(const QString& serial);
    [[nodiscard]] bool pinned() const { return !m_pinned.isEmpty(); }
    void pinPhone(const QString& serial);
    [[nodiscard]] QString task() const { return m_task; }
    void setTask(const QString& id);
    [[nodiscard]] QString component() const { return m_component; }
    [[nodiscard]] bool locked() const noexcept { return m_locked; }
    void setComponent(const QString& ref, bool locked);
    void setReadOnly(bool on);
    [[nodiscard]] bool readOnly() const noexcept { return m_readOnly; }
    [[nodiscard]] bool canRun() const noexcept { return m_canRun; }
    [[nodiscard]] QString runNote() const { return m_runNote; }
    [[nodiscard]] QString logicState() const { return m_logicState; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QImage thumbnail() const { return m_thumbnail; }
    [[nodiscard]] bool hasThumbnail() const { return !m_thumbnail.isNull(); }
    [[nodiscard]] QAbstractItemModel* console() { return &m_console; }
    [[nodiscard]] QVariantList api() const;
    [[nodiscard]] QVariantList liveComponents() const { return m_liveComponents; }
    [[nodiscard]] QVariantList liveVars() const { return m_liveVars; }
    [[nodiscard]] bool replaying() const { return m_replay.state() != QProcess::NotRunning; }
    [[nodiscard]] QString replayNote() const { return m_replayNote; }
    Q_INVOKABLE void setWatching(bool on);
    [[nodiscard]] const ProfileNames& profileNames() const noexcept { return m_names; }
    [[nodiscard]] const std::array<QColor, kScriptTokenKinds>& colors() const noexcept { return m_colors; }

    Q_INVOKABLE int open(const QString& path);
    Q_INVOKABLE int openControl(const QString& id);
    Q_INVOKABLE int openLibrary();
    Q_INVOKABLE void closeDocument(int index);
    Q_INVOKABLE bool save(int index);
    Q_INVOKABLE bool saveAll();
    Q_INVOKABLE void discardAll();
    Q_INVOKABLE void toggleFolder(const QString& path);
    Q_INVOKABLE QString newFile(const QString& name);
    Q_INVOKABLE bool addLogic();
    Q_INVOKABLE int openLogicEntry();
    Q_INVOKABLE QVariantMap snippetFor(const QString& id) const;
    Q_INVOKABLE void highlight(QQuickTextDocument* document) const;
    Q_INVOKABLE void copyText(const QString& text) const;
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE void run();
    Q_INVOKABLE void debug(bool on);
    Q_INVOKABLE void setBreakpoints(const QStringList& places);
    Q_INVOKABLE void resume(quint64 pause, int mode);
    Q_INVOKABLE QString saveDebugRecord();
    Q_INVOKABLE bool openDebugRecord(const QUrl& folder);
    Q_INVOKABLE int openRecorded(const QString& path);
    Q_INVOKABLE void pickOnPicture(const QString& node, const QString& pin, const QString& kind, const QString& prompt);
    [[nodiscard]] DebugRecord* debugRecord() noexcept { return &m_record; }
    [[nodiscard]] QUrl debugRecordsFolder() const;
    [[nodiscard]] bool debugging() const noexcept { return m_debugging; }
    Q_INVOKABLE void runOnRecording(const QUrl& recording);
    Q_INVOKABLE QUrl recordingsFolder() const;
    [[nodiscard]] LogicTests* tests() noexcept { return &m_tests; }
    [[nodiscard]] bool hasTests() const;
    Q_INVOKABLE void runTests();
    Q_INVOKABLE void openFolder() const;
    Q_INVOKABLE void openExternal() const;
    Q_INVOKABLE void clearConsole();
    Q_INVOKABLE void setColors(const QVariantMap& colors);
    void retranslate();
    void reveal(const QString& path, int line, int column);

signals:
    void nameChanged();
    void filesChanged();
    void documentsChanged();
    void currentChanged();
    void dirtyChanged();
    void problemsChanged();
    void checkingChanged();
    void referencesChanged();
    void phonesChanged();
    void apiChanged();
    void phoneChanged();
    void runChanged();
    void componentChanged();
    void thumbnailChanged();
    void liveChanged();
    void replayChanged();
    void notify(const QString& message, const QString& tone);
    void picked(const QString& node, const QString& pin, const QString& kind, const QString& name);
    void replayMachines(const QVariantList& states);
    void revealRequested(const QString& path, int line, int column);
    void machineState(const QVariantMap& state);
    void debugChanged();
    void debugHits(const QVariantList& hits);
    void debugPaused(const QVariantMap& pause);
    void debugResumed(quint64 pause);

private:
    [[nodiscard]] int documentIndex(const QString& path) const;
    int adopt(ScriptDocument* document, const QString& path);
    int openFresh(const QString& path, const QString& initial);
    [[nodiscard]] int recordedIndex(const QString& path) const;
    void closeRecorded();
    [[nodiscard]] std::vector<ScriptProblem> problemsFor(const QString& path) const;
    [[nodiscard]] QString tool() const;
    [[nodiscard]] QString fingerprint() const;
    [[nodiscard]] bool saveDocument(ScriptDocument& document);
    void rescan();
    void watch();
    void settled();
    void readProfile();
    void scheduleCheck();
    void startCheck();
    void checkFinished();
    void setProblems(std::vector<ScriptProblem> problems);
    void startTypes();
    void refreshPhones();
    void refreshRun();
    void refreshThumbnail();
    void refreshConsole();
    void refreshLive();
    void takeDebugFrame();
    void stopDebugRecord();
    void reported(const QString& serial, const LogicReport& report);
    void replayFinished();
    void addConsoleLine(const QString& text, log::Level level);

    DeviceManager& m_devices;
    QString m_folder;
    QString m_name;
    ProfileNames m_names;
    QStringList m_controlIds;
    QByteArray m_profileBytes;
    std::vector<ScriptFile> m_files;
    QVariantList m_fileList;
    QSet<QString> m_collapsed;
    bool m_hasLogic = false;
    ScriptDocumentList m_documents;
    int m_current = -1;
    std::vector<ScriptProblem> m_problems;
    QVariantList m_problemList;
    QString m_checkNotice;
    QVariantMap m_references;
    QString m_checkedPrint;
    bool m_checkAgain = false;
    QProcess m_check;
    QProcess m_types;
    QProcess m_replay;
    QString m_replayNote;
    QString m_replayName;
    LogicTests m_tests;
    QTimer m_checkDelay;
    QTimer m_settle;
    QFileSystemWatcher m_watcher;
    std::vector<LogicPhone> m_phoneStates;
    QVariantList m_phoneList;
    QString m_phone;
    bool m_debugging = false;
    bool m_frameBusy = false;
    std::uint64_t m_lastFrame = 0;
    qint64 m_lastFrameAt = 0;
    QString m_pinned;
    QString m_task;
    QString m_component;
    bool m_locked = false;
    bool m_readOnly = false;
    bool m_canRun = false;
    QString m_runNote;
    QString m_logicState;
    QString m_lastError;
    QImage m_thumbnail;
    ScriptConsoleModel m_console;
    QTimer m_live;
    DebugRecord m_record;
    QTimer m_debugFrames;
    QVariantList m_liveComponents;
    QVariantList m_liveVars;
    std::array<QColor, kScriptTokenKinds> m_colors;
};

}
