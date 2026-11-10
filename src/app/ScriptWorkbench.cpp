#include "ScriptWorkbench.h"

#include "AppSettings.h"
#include "DeviceManager.h"
#include "InputReadGrants.h"
#include "LogicReferences.h"
#include "QtConversions.h"
#include "Recordings.h"

#include <gs/input/Profile.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/platform/AtomicFile.h>

#include <QByteArrayView>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QThreadPool>

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr std::size_t kMostFileBytes = std::size_t{2} << 20;
constexpr std::size_t kMostProfileBytes = std::size_t{1} << 20;
constexpr int kSettleMs = 300;
constexpr int kCheckDelayMs = 400;
constexpr int kLiveMs = 500;
constexpr int kDebugFrameMs = 250;
constexpr qint64 kFrameAgainMs = 10000;
constexpr qint64 kStaleLiveMs = 10 * 60 * 1000;
constexpr std::string_view kBom = "\xEF\xBB\xBF";

QString debugRecordsRoot()
{
    return QDir(dataDirectory()).filePath(QStringLiteral("debug-records"));
}

QString freshLiveFolder()
{
    static int made = 0;
    const QDir root(debugRecordsRoot());
    const QDateTime now = QDateTime::currentDateTime();
    for (const QFileInfo& old : root.entryInfoList({QStringLiteral(".live-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
        if (old.lastModified().msecsTo(now) > kStaleLiveMs) {
            QDir(old.absoluteFilePath()).removeRecursively();
        }
    }
    return root.filePath(QStringLiteral(".live-%1-%2").arg(QCoreApplication::applicationPid()).arg(++made));
}

std::filesystem::path toPath(const QString& text)
{
    return {text.toStdWString()};
}

QString levelName(log::Level level)
{
    switch (level) {
    case log::Level::Error:
        return QStringLiteral("error");
    case log::Level::Warning:
        return QStringLiteral("warning");
    case log::Level::Trace:
    case log::Level::Debug:
    case log::Level::Info:
        break;
    }
    return QStringLiteral("info");
}

QString placeOf(const QString& file, int line, int column)
{
    if (file.isEmpty() || line <= 0) {
        return file;
    }
    return column > 0 ? QStringLiteral("%1:%2:%3").arg(file).arg(line).arg(column)
                      : QStringLiteral("%1:%2").arg(file).arg(line);
}

QString kindName(const ScriptFile& file)
{
    if (file.folder) {
        return QStringLiteral("folder");
    }
    switch (file.kind) {
    case ScriptFileKind::Code:
        return QStringLiteral("code");
    case ScriptFileKind::Text:
        return QStringLiteral("text");
    case ScriptFileKind::Image:
        return QStringLiteral("image");
    case ScriptFileKind::Profile:
        return QStringLiteral("profile");
    case ScriptFileKind::Other:
        break;
    }
    return QStringLiteral("other");
}

bool samePath(const QString& first, const QString& second)
{
    return first.compare(second, Qt::CaseInsensitive) == 0;
}

QColor colorOf(const QVariantMap& colors, const QString& key)
{
    return colors.value(key).value<QColor>();
}

QString busyText()
{
    return ScriptWorkbench::tr("Not saved: another program is using the file");
}

}

ScriptDocument::ScriptDocument(ScriptWorkbench& bench, QString folder, QString path, ScriptFileKind kind,
                               QObject* parent)
    : QObject(parent)
    , m_bench(bench)
    , m_folder(std::move(folder))
    , m_path(std::move(path))
    , m_kind(kind)
{
}

ScriptDocument::~ScriptDocument() = default;

QString ScriptDocument::name() const
{
    return m_path.section(u'/', -1);
}

bool ScriptDocument::readOnly() const noexcept
{
    return m_locked || m_kind == ScriptFileKind::Image || m_kind == ScriptFileKind::Profile ||
           m_kind == ScriptFileKind::Other;
}

void ScriptDocument::lock() noexcept
{
    m_locked = true;
}

void ScriptDocument::markDraft() noexcept
{
    m_draft = true;
    m_locked = true;
}

void ScriptDocument::markRecorded() noexcept
{
    m_recorded = true;
    m_locked = true;
}

QUrl ScriptDocument::source() const
{
    return image() ? QUrl::fromLocalFile(absolutePath()) : QUrl();
}

QString ScriptDocument::absolutePath() const
{
    return QDir(m_folder).filePath(m_path);
}

bool ScriptDocument::readDisk(QByteArray& bytes) const
{
    std::string contents;
    if (platform::readFile(toPath(absolutePath()), kMostFileBytes, contents, platform::WhenBusy::GiveUp)) {
        return false;
    }
    bytes = QByteArray(contents.data(), static_cast<qsizetype>(contents.size()));
    return true;
}

bool ScriptDocument::decode(const QByteArray& bytes, QString& text)
{
    QByteArrayView view(bytes);
    const bool bom = view.startsWith(QByteArrayView(kBom.data(), static_cast<qsizetype>(kBom.size())));
    if (bom) {
        view = view.sliced(static_cast<qsizetype>(kBom.size()));
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString decoded = decoder.decode(view);
    if (decoder.hasError() || decoded.contains(QChar(u'\0'))) {
        return false;
    }
    m_bom = bom;
    m_crlf = decoded.contains(QStringLiteral("\r\n"));
    decoded.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    decoded.replace(u'\r', u'\n');
    text = std::move(decoded);
    return true;
}

bool ScriptDocument::load(QString& problem)
{
    if (image()) {
        return true;
    }
    QByteArray bytes;
    if (!readDisk(bytes)) {
        if (m_fresh && !QFileInfo::exists(absolutePath())) {
            m_disk.clear();
            m_saved = m_initial;
            m_text = m_initial;
            return true;
        }
        problem = tr("%1 cannot be read").arg(name());
        return false;
    }
    m_fresh = false;
    QString text;
    if (!decode(bytes, text)) {
        problem = tr("%1 is not a text file").arg(name());
        return false;
    }
    m_disk = bytes;
    m_saved = text;
    m_text = std::move(text);
    return true;
}

bool ScriptDocument::save(bool& busy)
{
    busy = false;
    if (readOnly()) {
        return false;
    }
    if (m_text == m_saved && !m_changedOnDisk) {
        emit saved();
        return true;
    }
    QString text = m_text;
    if (m_crlf) {
        text.replace(u'\n', QStringLiteral("\r\n"));
    }
    QByteArray bytes = text.toUtf8();
    if (m_bom) {
        bytes.prepend(kBom.data(), static_cast<qsizetype>(kBom.size()));
    }
    if (m_fresh && !QDir().mkpath(QFileInfo(absolutePath()).absolutePath())) {
        return false;
    }
    if (const std::error_code error = platform::writeFileAtomically(
            toPath(absolutePath()), std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
        error) {
        busy = platform::fileBusy(error);
        return false;
    }
    const bool wasDirty = dirty();
    m_disk = bytes;
    m_saved = m_text;
    m_fresh = false;
    setChangedOnDisk(false);
    if (wasDirty) {
        emit dirtyChanged();
    }
    emit saved();
    return true;
}

void ScriptDocument::setLayoutDirty(bool dirty)
{
    if (m_recorded || dirty == m_layoutDirty) {
        return;
    }
    const bool wasDirty = this->dirty();
    m_layoutDirty = dirty;
    if (wasDirty != this->dirty()) {
        emit dirtyChanged();
    }
}

void ScriptDocument::checkDisk()
{
    if (image()) {
        return;
    }
    QByteArray bytes;
    if (!readDisk(bytes) || bytes == m_disk) {
        return;
    }
    if (dirty()) {
        setChangedOnDisk(true);
        return;
    }
    QString text;
    if (!decode(bytes, text)) {
        return;
    }
    m_disk = bytes;
    m_saved = text;
    replaceText(text);
}

void ScriptDocument::reload()
{
    QByteArray bytes;
    QString text;
    if (!readDisk(bytes) || !decode(bytes, text)) {
        return;
    }
    m_disk = bytes;
    m_saved = text;
    m_layoutDirty = false;
    replaceText(text);
    setChangedOnDisk(false);
}

void ScriptDocument::keepMine()
{
    QByteArray bytes;
    QString text;
    if (readDisk(bytes) && decode(bytes, text)) {
        m_disk = bytes;
        m_saved = text;
    }
    setChangedOnDisk(false);
    emit dirtyChanged();
}

void ScriptDocument::replaceText(const QString& text)
{
    if (m_edit != nullptr) {
        QTextCursor cursor(m_edit.data());
        cursor.beginEditBlock();
        cursor.select(QTextCursor::Document);
        cursor.insertText(text);
        cursor.endEditBlock();
    }
    m_text = text;
    emit dirtyChanged();
    emit marksChanged();
    emit textChanged();
}

bool ScriptDocument::edit(std::span<const TextChange> changes)
{
    if (readOnly() || !script()) {
        return false;
    }
    std::vector<TextChange> ordered(changes.begin(), changes.end());
    std::ranges::stable_sort(ordered, {}, &TextChange::start);
    std::ranges::reverse(ordered);
    const qsizetype size = m_text.size();
    qsizetype limit = size;
    for (const TextChange& change : ordered) {
        if (change.start < 0 || change.end < change.start || change.end > limit) {
            return false;
        }
        limit = change.start;
    }
    if (ordered.empty()) {
        return true;
    }
    if (m_edit != nullptr) {
        QTextCursor cursor(m_edit.data());
        cursor.beginEditBlock();
        for (const TextChange& change : ordered) {
            cursor.setPosition(change.start);
            cursor.setPosition(change.end, QTextCursor::KeepAnchor);
            cursor.insertText(change.text);
        }
        cursor.endEditBlock();
        return true;
    }
    const bool wasDirty = dirty();
    QString next = m_text;
    for (const TextChange& change : ordered) {
        next.replace(change.start, change.end - change.start, change.text);
    }
    m_text = std::move(next);
    if (wasDirty != dirty()) {
        emit dirtyChanged();
    }
    emit textChanged();
    return true;
}

void ScriptDocument::setChangedOnDisk(bool changed)
{
    if (changed == m_changedOnDisk) {
        return;
    }
    m_changedOnDisk = changed;
    emit changedOnDiskChanged();
}

void ScriptDocument::setProblems(std::vector<ScriptProblem> problems)
{
    m_problems = std::move(problems);
    emit marksChanged();
}

void ScriptDocument::setColors(const std::array<QColor, kScriptTokenKinds>& colors)
{
    if (m_highlighter != nullptr) {
        m_highlighter->setColors(colors);
    }
}

void ScriptDocument::attach(QQuickTextDocument* document)
{
    QTextDocument* text = document != nullptr ? document->textDocument() : nullptr;
    if (text == nullptr || text == m_edit || image()) {
        return;
    }
    if (m_edit != nullptr) {
        disconnect(m_edit.data(), nullptr, this, nullptr);
    }
    m_edit = text;
    text->setPlainText(m_text);
    text->setModified(false);
    if (script()) {
        m_highlighter = new ScriptHighlighter(text);
        m_highlighter->setColors(m_bench.colors());
    }
    connect(text, &QTextDocument::contentsChanged, this, &ScriptDocument::follow);
}

void ScriptDocument::follow()
{
    if (m_edit == nullptr) {
        return;
    }
    const bool wasDirty = dirty();
    QString text = m_edit->toRawText();
    text.replace(QChar(QChar::ParagraphSeparator), QChar(u'\n'));
    text.replace(QChar(QChar::LineSeparator), QChar(u'\n'));
    m_text = std::move(text);
    if (wasDirty != dirty()) {
        emit dirtyChanged();
    }
    if (!m_problems.empty()) {
        emit marksChanged();
    }
    emit textChanged();
}

QVariantList ScriptDocument::marks() const
{
    QVariantList marks;
    if (m_problems.empty()) {
        return marks;
    }
    std::vector<qsizetype> starts{0};
    for (qsizetype at = m_text.indexOf(u'\n'); at >= 0; at = m_text.indexOf(u'\n', at + 1)) {
        starts.push_back(at + 1);
    }
    std::vector<const ScriptProblem*> shown;
    for (const ScriptProblem& problem : m_problems) {
        if (problem.line > 0 && static_cast<std::size_t>(problem.line) <= starts.size()) {
            shown.push_back(&problem);
        }
    }
    std::ranges::stable_sort(shown, [](const ScriptProblem* first, const ScriptProblem* second) {
        if (first->line != second->line) {
            return first->line < second->line;
        }
        return first->severity == u"error" && second->severity != u"error";
    });
    int lastLine = 0;
    for (const ScriptProblem* problem : shown) {
        const auto row = static_cast<std::size_t>(problem->line);
        const qsizetype lineStart = starts[row - 1];
        const qsizetype lineEnd = row < starts.size() ? starts[row] - 1 : m_text.size();
        qsizetype start = lineStart;
        qsizetype end = lineEnd;
        if (problem->column > 0) {
            start = lineStart + std::min<qsizetype>(problem->column - 1, lineEnd - lineStart);
            end = start;
            while (end < lineEnd && identifierPart(m_text[end])) {
                ++end;
            }
            if (end == start && end < lineEnd) {
                ++end;
            }
        } else {
            while (start < lineEnd && m_text[start].isSpace()) {
                ++start;
            }
        }
        marks.append(QVariantMap{{QStringLiteral("start"), static_cast<int>(start)},
                                 {QStringLiteral("end"), static_cast<int>(end)},
                                 {QStringLiteral("lineEnd"), static_cast<int>(lineEnd)},
                                 {QStringLiteral("line"), problem->line},
                                 {QStringLiteral("severity"), problem->severity},
                                 {QStringLiteral("message"), problem->line != lastLine ? problem->message : QString()}});
        lastLine = problem->line;
    }
    return marks;
}

int ScriptDocument::positionOf(int line, int column) const
{
    qsizetype start = 0;
    for (int at = 1; at < line; ++at) {
        const qsizetype newline = m_text.indexOf(u'\n', start);
        if (newline < 0) {
            break;
        }
        start = newline + 1;
    }
    qsizetype end = m_text.indexOf(u'\n', start);
    if (end < 0) {
        end = m_text.size();
    }
    return static_cast<int>(start + std::clamp<qsizetype>(column - 1, 0, end - start));
}

QVariantMap ScriptDocument::completion(int position, bool asked) const
{
    QVariantList items;
    if (m_edit != nullptr && script()) {
        const QTextBlock block = m_edit->findBlock(position);
        if (block.isValid()) {
            const QTextBlock before = block.previous();
            const int state = before.isValid() ? std::max(before.userState(), kScriptPlain) : kScriptPlain;
            const QString line = block.text();
            const int offset = block.position();
            for (const CompletionItem& item : completionsAt(ScriptApi::installed(), m_bench.profileNames(), line,
                                                             position - offset, state, asked)) {
                items.append(QVariantMap{{QStringLiteral("label"), item.label},
                                         {QStringLiteral("kind"), item.kind},
                                         {QStringLiteral("detail"), item.detail},
                                         {QStringLiteral("documentation"), item.documentation},
                                         {QStringLiteral("insert"), item.insert},
                                         {QStringLiteral("from"), offset + item.from},
                                         {QStringLiteral("caret"), item.caret}});
            }
        }
    }
    return {{QStringLiteral("items"), items}};
}

QString ScriptDocument::lineBreak(int position) const
{
    const qsizetype at = std::clamp<qsizetype>(position, 0, m_text.size());
    const qsizetype lineStart = at == 0 ? 0 : m_text.lastIndexOf(u'\n', at - 1) + 1;
    qsizetype indentEnd = lineStart;
    while (indentEnd < at && (m_text[indentEnd] == u' ' || m_text[indentEnd] == u'\t')) {
        ++indentEnd;
    }
    QString indent = m_text.mid(lineStart, indentEnd - lineStart);
    const QString before = m_text.mid(lineStart, at - lineStart).trimmed();
    if (before.endsWith(u'{') || before.endsWith(u'(') || before.endsWith(u'[')) {
        indent += QStringLiteral("    ");
    }
    return u'\n' + indent;
}

int ScriptDocumentList::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : size();
}

QVariant ScriptDocumentList::data(const QModelIndex& index, int role) const
{
    ScriptDocument* document = index.isValid() ? at(index.row()) : nullptr;
    if (document == nullptr) {
        return {};
    }
    switch (role) {
    case DocumentRole:
        return QVariant::fromValue(document);
    case PathRole:
        return document->path();
    default:
        return {};
    }
}

QHash<int, QByteArray> ScriptDocumentList::roleNames() const
{
    return {{DocumentRole, "document"}, {PathRole, "path"}};
}

ScriptDocument* ScriptDocumentList::at(int row) const
{
    return row >= 0 && row < size() ? m_documents[static_cast<std::size_t>(row)] : nullptr;
}

void ScriptDocumentList::append(ScriptDocument* document)
{
    const int row = size();
    beginInsertRows({}, row, row);
    m_documents.push_back(document);
    endInsertRows();
}

void ScriptDocumentList::remove(int row)
{
    if (row < 0 || row >= size()) {
        return;
    }
    beginRemoveRows({}, row, row);
    m_documents.erase(m_documents.begin() + row);
    endRemoveRows();
}

int ScriptConsoleModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_lines.size());
}

QVariant ScriptConsoleModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const LogicReport& line = m_lines[static_cast<std::size_t>(index.row())];
    switch (role) {
    case TimeRole:
        return line.time.toString(QStringLiteral("HH:mm:ss"));
    case TextRole:
        return line.text;
    case LevelRole:
        return levelName(line.level);
    case FileRole:
        return line.file;
    case LineRole:
        return line.line;
    case ColumnRole:
        return line.column;
    case PlaceRole:
        return placeOf(line.file, line.line, 0);
    default:
        return {};
    }
}

QHash<int, QByteArray> ScriptConsoleModel::roleNames() const
{
    return {{TimeRole, "time"}, {TextRole, "text"},     {LevelRole, "level"}, {FileRole, "file"},
            {LineRole, "line"}, {ColumnRole, "column"}, {PlaceRole, "place"}};
}

void ScriptConsoleModel::reset(std::vector<LogicReport> lines)
{
    beginResetModel();
    m_lines.clear();
    const std::size_t skipped = lines.size() > LogicConsole::kMostLines ? lines.size() - LogicConsole::kMostLines : 0;
    for (std::size_t i = skipped; i < lines.size(); ++i) {
        m_lines.push_back(std::move(lines[i]));
    }
    endResetModel();
}

void ScriptConsoleModel::append(LogicReport line)
{
    if (m_lines.size() >= LogicConsole::kMostLines) {
        beginRemoveRows({}, 0, 0);
        m_lines.pop_front();
        endRemoveRows();
    }
    const int row = static_cast<int>(m_lines.size());
    beginInsertRows({}, row, row);
    m_lines.push_back(std::move(line));
    endInsertRows();
}

ScriptWorkbench::ScriptWorkbench(DeviceManager& devices, QString folder, QString name, QObject* parent)
    : QObject(parent)
    , m_devices(devices)
    , m_folder(QDir::cleanPath(QDir::fromNativeSeparators(folder)))
    , m_name(std::move(name))
    , m_documents(this)
    , m_console(this)
{
    m_settle.setInterval(kSettleMs);
    m_settle.setSingleShot(true); // source-rule-allow R9: a debounce; editors write a file in several steps
    connect(&m_settle, &QTimer::timeout, this, &ScriptWorkbench::settled);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_settle.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_settle.start(); });
    m_checkDelay.setInterval(kCheckDelayMs);
    m_checkDelay.setSingleShot(true); // source-rule-allow R9: a debounce; saves come in bursts and each check runs the type checker
    connect(&m_checkDelay, &QTimer::timeout, this, &ScriptWorkbench::startCheck);
    connect(&m_check, &QProcess::finished, this, &ScriptWorkbench::checkFinished);
    connect(&m_check, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_checkNotice = tr("Problems are not checked: gs.exe did not start");
            emit checkingChanged();
        }
    });
    connect(&m_types, &QProcess::finished, this, [this] { scheduleCheck(); });
    connect(&m_replay, &QProcess::finished, this, &ScriptWorkbench::replayFinished);
    connect(&m_replay, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_replayNote = tr("Could not run on %1: gs.exe did not start").arg(m_replayName);
            emit replayChanged();
        }
    });
    m_live.setInterval(kLiveMs);
    connect(&m_live, &QTimer::timeout, this, &ScriptWorkbench::refreshLive);
    m_debugFrames.setInterval(kDebugFrameMs);
    connect(&m_debugFrames, &QTimer::timeout, this, &ScriptWorkbench::takeDebugFrame);
    connect(&m_record, &DebugRecord::changed, this, [this] {
        if (!m_record.opened()) {
            closeRecorded();
        }
    });
    connect(&m_devices, &DeviceManager::mirrorsChanged, this, &ScriptWorkbench::refreshPhones);
    connect(&m_devices, &DeviceManager::logicChanged, this, [this] { refreshPhones(); });
    connect(&m_devices, &DeviceManager::logicReported, this, &ScriptWorkbench::reported);
    connect(&m_devices, &DeviceManager::machineStateChanged, this,
            [this](const QString& serial, const QString& folder, const QVariantMap& state) {
                if (serial == m_phone && sameFolder(folder, m_folder)) {
                    emit machineState(state);
                }
            });
    connect(&m_devices, &DeviceManager::logicDebugHits, this,
            [this](const QString& serial, const QString& folder, const QVariantList& hits) {
                if (m_debugging && serial == m_phone && sameFolder(folder, m_folder)) {
                    m_record.addHits(hits);
                    emit debugHits(hits);
                }
            });
    connect(&m_devices, &DeviceManager::logicPaused, this,
            [this](const QString& serial, const QString& folder, const QVariantMap& pause) {
                if (m_debugging && serial == m_phone && sameFolder(folder, m_folder)) {
                    m_record.addPause(pause);
                    emit debugPaused(pause);
                }
            });
    connect(&m_devices, &DeviceManager::logicResumed, this,
            [this](const QString& serial, const QString& folder, quint64 pause) {
                if (m_debugging && serial == m_phone && sameFolder(folder, m_folder)) {
                    m_record.addResume(pause);
                    emit debugResumed(pause);
                }
            });
    connect(&m_devices, &DeviceManager::taskRunsChanged, this, &ScriptWorkbench::refreshRun);
    connect(&m_devices, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& first, const QModelIndex& last, const QList<int>& roles) {
                if (m_phone.isEmpty() || (!roles.isEmpty() && !roles.contains(DeviceManager::ThumbnailRole))) {
                    return;
                }
                for (int row = first.row(); row <= last.row(); ++row) {
                    if (m_devices.data(m_devices.index(row), DeviceManager::SerialRole).toString() == m_phone) {
                        refreshThumbnail();
                        return;
                    }
                }
            });
    readProfile();
    rescan();
    watch();
    refreshPhones();
    if (const QString entry = logicEntryIn(m_folder); !entry.isEmpty()) {
        open(entry);
        startCheck();
    }
}

ScriptWorkbench::~ScriptWorkbench()
{
    m_debugFrames.stop();
    if (m_debugging && !m_phone.isEmpty()) {
        static_cast<void>(m_devices.debugLogic(m_phone, false));
    }
    for (QProcess* process : {&m_check, &m_types, &m_replay}) {
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished();
        }
    }
}

QQuickWindow* ScriptWorkbench::openWindow(QQmlEngine& engine)
{
    QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership);
    QQmlComponent component(&engine, QStringLiteral("GameScrcpy.App"), QStringLiteral("LogicWindow"));
    QObject* created = component.createWithInitialProperties({{QStringLiteral("bench"), QVariant::fromValue(this)}});
    auto* window = qobject_cast<QQuickWindow*>(created);
    if (window == nullptr) {
        delete created;
        qWarning("script workbench for %s not created: %s", qUtf8Printable(m_folder),
                 qUtf8Printable(component.errorString()));
        return nullptr;
    }
    QJSEngine::setObjectOwnership(window, QJSEngine::CppOwnership);
    setParent(window);
    return window;
}

void ScriptWorkbench::setCurrent(int index)
{
    const int clamped = std::clamp(index, m_documents.size() > 0 ? 0 : -1, m_documents.size() - 1);
    if (clamped == m_current) {
        return;
    }
    m_current = clamped;
    emit currentChanged();
}

QString ScriptWorkbench::currentPath() const
{
    const ScriptDocument* document = m_documents.at(m_current);
    return document != nullptr ? document->path() : QString();
}

int ScriptWorkbench::dirtyCount() const
{
    return static_cast<int>(
        std::ranges::count_if(m_documents.all(), [](const ScriptDocument* document) { return document->dirty(); }));
}

QVariantList ScriptWorkbench::api() const
{
    return ScriptApi::installed().list();
}

void ScriptWorkbench::retranslate()
{
    emit apiChanged();
}

void ScriptWorkbench::reveal(const QString& path, int line, int column)
{
    settled();
    if (open(path) >= 0) {
        emit revealRequested(path, line, column);
    }
}

int ScriptWorkbench::documentIndex(const QString& path) const
{
    for (int i = 0; i < m_documents.size(); ++i) {
        if (!m_documents.at(i)->recorded() && samePath(m_documents.at(i)->path(), path)) {
            return i;
        }
    }
    return -1;
}

int ScriptWorkbench::recordedIndex(const QString& path) const
{
    for (int i = 0; i < m_documents.size(); ++i) {
        if (m_documents.at(i)->recorded() && samePath(m_documents.at(i)->path(), path)) {
            return i;
        }
    }
    return -1;
}

void ScriptWorkbench::closeRecorded()
{
    for (int index = m_documents.size() - 1; index >= 0; --index) {
        if (m_documents.at(index)->recorded()) {
            closeDocument(index);
        }
    }
}

int ScriptWorkbench::openRecorded(const QString& path)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (m_record.recordedFile(clean).isEmpty()) {
        return open(clean);
    }
    if (const int index = recordedIndex(clean); index >= 0) {
        setCurrent(index);
        return index;
    }
    auto* document = new ScriptDocument(*this, m_record.codeFolder(), clean, fileKindOf(clean), this);
    document->markRecorded();
    QString problem;
    if (!document->load(problem)) {
        delete document;
        emit notify(problem, QStringLiteral("warn"));
        return -1;
    }
    m_documents.append(document);
    emit documentsChanged();
    setCurrent(m_documents.size() - 1);
    return m_current;
}

std::vector<ScriptProblem> ScriptWorkbench::problemsFor(const QString& path) const
{
    std::vector<ScriptProblem> found;
    std::ranges::copy_if(m_problems, std::back_inserter(found),
                         [&path](const ScriptProblem& problem) { return problem.inside && samePath(problem.file, path); });
    return found;
}

QString ScriptWorkbench::tool() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("gs.exe"));
}

QString ScriptWorkbench::fingerprint() const
{
    QString print;
    const QDir root(m_folder);
    for (const ScriptFile& file : scriptFiles(m_folder, {})) {
        if (file.folder || (file.kind != ScriptFileKind::Code && !file.path.endsWith(u".json", Qt::CaseInsensitive))) {
            continue;
        }
        const QFileInfo info(root.filePath(file.path));
        print += file.path + u'|' + QString::number(info.size()) + u'|' +
                 QString::number(info.lastModified().toMSecsSinceEpoch()) + u'\n';
    }
    return print;
}

int ScriptWorkbench::open(const QString& path)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (clean.isEmpty() || clean == u".." || clean.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(clean)) {
        return -1;
    }
    if (const int index = documentIndex(clean); index >= 0) {
        setCurrent(index);
        return index;
    }
    if (!QFileInfo(QDir(m_folder).filePath(clean)).isFile()) {
        return -1;
    }
    return adopt(new ScriptDocument(*this, m_folder, clean, fileKindOf(clean), this), clean);
}

int ScriptWorkbench::openControl(const QString& id)
{
    static const QRegularExpression unsafe(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    if (id.isEmpty() || id == u"." || id == u".." || id.endsWith(u'.') || id.endsWith(u' ') ||
        unsafe.match(id).hasMatch()) {
        return -1;
    }
    QString shape;
    for (const ProfileName& control : m_names.controls) {
        if (control.id == id) {
            shape = control.shape;
            break;
        }
    }
    return openFresh(QStringLiteral("controls/%1.ts").arg(id), controlStarter(id, shape));
}

int ScriptWorkbench::openLibrary()
{
    return openFresh(QStringLiteral("library.ts"), QString());
}

int ScriptWorkbench::openFresh(const QString& path, const QString& initial)
{
    if (documentIndex(path) >= 0 || QFileInfo(QDir(m_folder).filePath(path)).isFile()) {
        return open(path);
    }
    auto* document = new ScriptDocument(*this, m_folder, path, ScriptFileKind::Code, this);
    document->markFresh(initial);
    const int index = adopt(document, path);
    if (index >= 0) {
        connect(document, &ScriptDocument::saved, this, &ScriptWorkbench::rescan, Qt::SingleShotConnection);
    }
    return index;
}

int ScriptWorkbench::adopt(ScriptDocument* document, const QString& clean)
{
    if (m_locked || m_readOnly) {
        document->lock();
    }
    if (m_readOnly) {
        document->markDraft();
    }
    QString problem;
    if (!document->load(problem)) {
        delete document;
        emit notify(problem, QStringLiteral("warn"));
        return -1;
    }
    document->setProblems(problemsFor(clean));
    connect(document, &ScriptDocument::dirtyChanged, this, &ScriptWorkbench::dirtyChanged);
    m_documents.append(document);
    emit documentsChanged();
    watch();
    setCurrent(m_documents.size() - 1);
    return m_current;
}

void ScriptWorkbench::closeDocument(int index)
{
    ScriptDocument* document = m_documents.at(index);
    if (document == nullptr) {
        return;
    }
    m_documents.remove(index);
    document->deleteLater();
    emit documentsChanged();
    const int next = index < m_current || m_current >= m_documents.size() ? m_current - 1 : m_current;
    m_current = std::clamp(next, m_documents.size() > 0 ? 0 : -1, m_documents.size() - 1);
    emit currentChanged();
    emit dirtyChanged();
    watch();
}

bool ScriptWorkbench::saveDocument(ScriptDocument& document)
{
    bool busy = false;
    if (document.save(busy)) {
        return true;
    }
    emit notify(busy ? busyText() : tr("%1 could not be saved").arg(document.name()), QStringLiteral("bad"));
    return false;
}

bool ScriptWorkbench::save(int index)
{
    ScriptDocument* document = m_documents.at(index);
    if (document == nullptr || document->readOnly()) {
        return false;
    }
    const bool wrote = document->dirty() || document->changedOnDisk();
    if (!saveDocument(*document)) {
        return false;
    }
    if (wrote) {
        scheduleCheck();
    }
    return true;
}

bool ScriptWorkbench::saveAll()
{
    bool saved = true;
    bool wrote = false;
    for (ScriptDocument* document : m_documents.all()) {
        if (!document->dirty() || document->readOnly()) {
            continue;
        }
        if (saveDocument(*document)) {
            wrote = true;
        } else {
            saved = false;
        }
    }
    if (wrote) {
        scheduleCheck();
    }
    return saved;
}

void ScriptWorkbench::discardAll()
{
    for (ScriptDocument* document : m_documents.all()) {
        if (document->dirty()) {
            document->reload();
        }
    }
    emit dirtyChanged();
}

void ScriptWorkbench::toggleFolder(const QString& path)
{
    if (!m_collapsed.remove(path)) {
        m_collapsed.insert(path);
    }
    rescan();
}

QString ScriptWorkbench::newFile(const QString& name)
{
    QString problem;
    const QString path = checkedFileName(name, problem);
    if (path.isEmpty()) {
        return problem;
    }
    const QString file = QDir(m_folder).filePath(path);
    if (QFileInfo::exists(file)) {
        return tr("%1 already exists").arg(path);
    }
    if (!QDir().mkpath(QFileInfo(file).absolutePath())) {
        return tr("%1 could not be created").arg(path);
    }
    if (const std::error_code error = platform::writeFileAtomically(toPath(file), {}); error) {
        return platform::fileBusy(error) ? busyText() : tr("%1 could not be created").arg(path);
    }
    if (InputReadGrants* grants = InputReadGrants::instance()) {
        grants->allowFolder(m_folder);
    }
    for (qsizetype slash = path.indexOf(u'/'); slash >= 0; slash = path.indexOf(u'/', slash + 1)) {
        m_collapsed.remove(path.left(slash));
    }
    rescan();
    open(path);
    return {};
}

bool ScriptWorkbench::addLogic()
{
    switch (writeStarterLogic(m_folder, m_controlIds)) {
    case LogicWrite::Busy:
        emit notify(busyText(), QStringLiteral("bad"));
        return false;
    case LogicWrite::Failed:
        emit notify(tr("The logic could not be written"), QStringLiteral("bad"));
        return false;
    case LogicWrite::Exists:
        break;
    case LogicWrite::Written:
        if (InputReadGrants* grants = InputReadGrants::instance()) {
            grants->allowFolder(m_folder);
        }
        break;
    }
    rescan();
    if (const QString entry = logicEntryIn(m_folder); !entry.isEmpty()) {
        open(entry);
    }
    startTypes();
    scheduleCheck();
    for (const LogicPhone& phone : m_phoneStates) {
        if (sameFolder(phone.folder, m_folder) && !phone.started) {
            static_cast<void>(m_devices.restartLogic(phone.serial));
        }
    }
    refreshPhones();
    return true;
}

int ScriptWorkbench::openLogicEntry()
{
    if (!m_hasLogic && !addLogic()) {
        return -1;
    }
    const QString entry = logicEntryIn(m_folder);
    return entry.isEmpty() ? -1 : open(entry);
}

QVariantMap ScriptWorkbench::snippetFor(const QString& id) const
{
    const LogicSnippet snippet = referenceSnippet(id);
    return {{QStringLiteral("text"), snippet.text}, {QStringLiteral("caret"), snippet.caret}};
}

void ScriptWorkbench::highlight(QQuickTextDocument* document) const
{
    QTextDocument* text = document != nullptr ? document->textDocument() : nullptr;
    if (text == nullptr || text->findChild<ScriptHighlighter*>() != nullptr) {
        return;
    }
    auto* highlighter = new ScriptHighlighter(text);
    highlighter->setColors(m_colors);
}

void ScriptWorkbench::copyText(const QString& text) const
{
    if (QClipboard* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setText(text);
    }
}

QString ScriptWorkbench::clipboardText() const
{
    const QClipboard* clipboard = QGuiApplication::clipboard();
    return clipboard != nullptr ? clipboard->text() : QString();
}

void ScriptWorkbench::run()
{
    if (!m_canRun) {
        return;
    }
    bool wroteScript = false;
    for (ScriptDocument* document : m_documents.all()) {
        if (!document->dirty() || document->readOnly()) {
            continue;
        }
        if (!saveDocument(*document)) {
            return;
        }
        wroteScript = wroteScript || document->script();
    }
    if (wroteScript) {
        scheduleCheck();
    }
    if (!m_task.isEmpty()) {
        m_devices.stopTask(m_task, m_phone);
        if (const QString problem = m_devices.startTask(m_task, m_phone); !problem.isEmpty()) {
            emit notify(problem, QStringLiteral("bad"));
        }
        return;
    }
    const auto found = std::ranges::find(m_phoneStates, m_phone, &LogicPhone::serial);
    if (wroteScript && found != m_phoneStates.end() && found->started) {
        return;
    }
    static_cast<void>(m_devices.restartLogic(m_phone));
}

void ScriptWorkbench::debug(bool on)
{
    if (on == m_debugging) {
        return;
    }
    if (on && (!m_canRun || !m_task.isEmpty())) {
        emit notify(m_task.isEmpty() ? m_runNote : tr("Tasks are debugged from their logic once they are running"),
                    QStringLiteral("warn"));
        return;
    }
    if (on) {
        for (ScriptDocument* document : m_documents.all()) {
            if (document->dirty() && !document->readOnly() && !saveDocument(*document)) {
                return;
            }
        }
    }
    if (!m_devices.debugLogic(m_phone, on) && on) {
        emit notify(m_runNote.isEmpty() ? tr("The logic could not be started") : m_runNote, QStringLiteral("warn"));
        return;
    }
    m_debugging = on;
    if (on) {
        m_record.start(freshLiveFolder());
        m_lastFrame = 0;
        m_lastFrameAt = 0;
        m_debugFrames.start();
        takeDebugFrame();
    } else {
        stopDebugRecord();
    }
    emit debugChanged();
}

void ScriptWorkbench::stopDebugRecord()
{
    m_debugFrames.stop();
    m_record.stop();
}

void ScriptWorkbench::takeDebugFrame()
{
    if (!m_record.recording()) {
        return;
    }
    std::shared_ptr<media::FrameSnapshots> snapshots = m_frameBusy ? nullptr : m_devices.snapshotsOf(m_phone);
    if (snapshots == nullptr) {
        m_record.refresh();
        return;
    }
    m_frameBusy = true;
    const QPointer<ScriptWorkbench> self(this);
    const bool again = QDateTime::currentMSecsSinceEpoch() - m_lastFrameAt >= kFrameAgainMs;
    QThreadPool::globalInstance()->start([self, snapshots = std::move(snapshots), last = m_lastFrame, again] {
        const std::shared_ptr<const media::LumaSnapshot> shot = snapshots->capture(true);
        const std::uint64_t frame = shot != nullptr ? shot->frame : last;
        QByteArray jpeg;
        if (shot != nullptr && (frame != last || again)) {
            jpeg = DebugRecord::encodeFrame(snapshotThumbnail(*shot, DebugRecord::kFrameSide));
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, frame, jpeg = std::move(jpeg)] {
                if (self != nullptr) {
                    self->m_frameBusy = false;
                    if (!jpeg.isEmpty()) {
                        self->m_lastFrame = frame;
                        self->m_lastFrameAt = QDateTime::currentMSecsSinceEpoch();
                    }
                    self->m_record.addFrame(jpeg);
                }
            },
            Qt::QueuedConnection);
    });
}

QUrl ScriptWorkbench::debugRecordsFolder() const
{
    const QString root = debugRecordsRoot();
    QDir().mkpath(root);
    return QUrl::fromLocalFile(root);
}

QString ScriptWorkbench::saveDebugRecord()
{
    if (!m_record.recording()) {
        emit notify(m_record.opened() ? tr("This debug record is already saved") : tr("Start debugging to record"),
                    QStringLiteral("info"));
        return {};
    }
    const QString folder = m_record.save(QDir(dataDirectory()).absolutePath(), m_folder, m_name);
    if (folder.isEmpty()) {
        emit notify(tr("The last 5 minutes could not be saved"), QStringLiteral("bad"));
        return {};
    }
    emit notify(tr("Saved the last 5 minutes to %1").arg(QDir::toNativeSeparators(folder)), QStringLiteral("info"));
    return folder;
}

bool ScriptWorkbench::openDebugRecord(const QUrl& folder)
{
    if (m_debugging) {
        emit notify(tr("Stop debugging before opening a debug record"), QStringLiteral("warn"));
        return false;
    }
    const QString path = QDir::cleanPath(folder.isLocalFile() ? folder.toLocalFile() : folder.toString());
    closeRecorded();
    if (!m_record.open(path)) {
        qInfo("not a debug record: %s", qUtf8Printable(path));
        emit notify(tr("This folder is not a debug record"), QStringLiteral("warn"));
        return false;
    }
    qInfo("debug record opened: %s", qUtf8Printable(path));
    if (!m_record.codeFolder().isEmpty()) {
        const bool shown = std::ranges::any_of(m_documents.all(), [](const ScriptDocument* document) {
            return document->recorded();
        });
        if (const QString entry = logicEntryIn(m_record.codeFolder()); !shown && !entry.isEmpty()) {
            openRecorded(entry);
        }
        return true;
    }
    const QString saved = QDir(path).filePath(QStringLiteral("code"));
    const QDir savedDir(saved);
    bool changed = false;
    QDirIterator files(saved, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext() && !changed) {
        const QString file = files.next();
        QFile then(file);
        QFile current(QDir(m_folder).filePath(savedDir.relativeFilePath(file)));
        changed = !then.open(QIODevice::ReadOnly) || !current.open(QIODevice::ReadOnly) ||
                  then.readAll() != current.readAll();
    }
    if (changed) {
        emit notify(tr("The code has changed since this record was made, so some lines may not match"),
                    QStringLiteral("warn"));
    }
    return true;
}

void ScriptWorkbench::pickOnPicture(const QString& node, const QString& pin, const QString& kind, const QString& prompt)
{
    const QPointer<ScriptWorkbench> self(this);
    const QString problem = m_devices.pickOnPicture(m_phone, m_folder, kind, prompt, [self, node, pin, kind](const QString& name) {
        if (self != nullptr) {
            emit self->picked(node, pin, kind, name);
        }
    });
    if (!problem.isEmpty()) {
        emit notify(problem, QStringLiteral("warn"));
    }
}

void ScriptWorkbench::setBreakpoints(const QStringList& places)
{
    if (m_debugging) {
        m_devices.setLogicBreakpoints(m_phone, places);
    }
}

void ScriptWorkbench::resume(quint64 pause, int mode)
{
    if (m_debugging) {
        m_devices.resumeLogic(m_phone, pause, mode);
    }
}

void ScriptWorkbench::runOnRecording(const QUrl& recording)
{
    const QString folder = QDir::cleanPath(recording.isLocalFile() ? recording.toLocalFile() : recording.toString());
    const QString name = QFileInfo(folder).fileName();
    if (replaying()) {
        emit notify(tr("The logic is already running on a recording"), QStringLiteral("warn"));
        return;
    }
    if (!m_hasLogic) {
        emit notify(tr("Add logic to run it"), QStringLiteral("warn"));
        return;
    }
    if (!QFileInfo(QDir(folder).filePath(QStringLiteral("session.json"))).isFile()) {
        emit notify(tr("%1 is not a recording: choose a folder whose name ends in .gsrec").arg(name),
                    QStringLiteral("bad"));
        return;
    }
    bool wroteScript = false;
    for (ScriptDocument* document : m_documents.all()) {
        if (!document->dirty() || document->readOnly()) {
            continue;
        }
        if (!saveDocument(*document)) {
            return;
        }
        wroteScript = wroteScript || document->script();
    }
    if (wroteScript) {
        scheduleCheck();
    }
    const QString program = tool();
    if (!QFileInfo::exists(program)) {
        emit notify(tr("Could not run on the recording: gs.exe is missing next to GameScrcpy"), QStringLiteral("bad"));
        return;
    }
    m_replayName = name;
    m_replayNote = tr("Running on %1…").arg(name);
    addConsoleLine(tr("Running the logic on the recording %1").arg(name), log::Level::Info);
    m_replay.setWorkingDirectory(m_folder);
    m_replay.start(program, {QStringLiteral("run"), QDir::toNativeSeparators(m_folder), QStringLiteral("--replay"),
                             QDir::toNativeSeparators(folder), QStringLiteral("--json")});
    emit replayChanged();
}

QUrl ScriptWorkbench::recordingsFolder() const
{
    const AppSettings* settings = AppSettings::instance();
    return QUrl::fromLocalFile(settings != nullptr ? recordingsLocation(*settings) : defaultRecordingsLocation());
}

bool ScriptWorkbench::hasTests() const
{
    return QFileInfo(QDir(m_folder).filePath(toQString(script::kTestsFolder))).isDir();
}

void ScriptWorkbench::runTests()
{
    if (m_tests.running()) {
        return;
    }
    if (!m_hasLogic) {
        emit notify(tr("Add logic to run it"), QStringLiteral("warn"));
        return;
    }
    bool wroteScript = false;
    for (ScriptDocument* document : m_documents.all()) {
        if (!document->dirty() || document->readOnly()) {
            continue;
        }
        if (!saveDocument(*document)) {
            return;
        }
        wroteScript = wroteScript || document->script();
    }
    if (wroteScript) {
        scheduleCheck();
    }
    m_tests.run(m_folder);
}

void ScriptWorkbench::replayFinished()
{
    const QByteArray output = m_replay.readAllStandardOutput();
    const QByteArray errors = m_replay.readAllStandardError();
    const QJsonDocument document = QJsonDocument::fromJson(output);
    if (!document.isObject()) {
        QString reason = QString::fromUtf8(errors).trimmed().section(u'\n', 0, 0).trimmed();
        if (reason.startsWith(u"gs run: ")) {
            reason.remove(0, 8);
        }
        m_replayNote = reason.isEmpty() ? tr("Could not run on %1").arg(m_replayName)
                                        : tr("Could not run on %1: %2").arg(m_replayName, reason);
        addConsoleLine(m_replayNote, log::Level::Error);
        emit replayChanged();
        return;
    }
    const QJsonObject result = document.object();
    for (const QJsonValue& value : result.value(QStringLiteral("lines")).toArray()) {
        const QJsonObject line = value.toObject();
        const QString text = line.value(QStringLiteral("text")).toString();
        const QString at = QString::number(line.value(QStringLiteral("at")).toDouble());
        addConsoleLine(line.value(QStringLiteral("output")).toBool() ? QStringLiteral("%1 ms · %2").arg(at, text)
                                                                      : QStringLiteral("%1 ms # %2").arg(at, text),
                       text.startsWith(u"error ") ? log::Level::Error : log::Level::Info);
    }
    const double endedAt = result.value(QStringLiteral("ended")).toDouble();
    QVariantList machines;
    for (const QJsonValue& value : result.value(QStringLiteral("machines")).toArray()) {
        const QJsonObject state = value.toObject();
        if (state.value(QStringLiteral("state")).toString().isEmpty() &&
            state.value(QStringLiteral("at")).toDouble() >= endedAt) {
            continue;
        }
        machines.push_back(state.toVariantMap());
    }
    emit replayMachines(machines);
    const bool ok = result.value(QStringLiteral("ok")).toBool();
    const QString ended = QString::number(result.value(QStringLiteral("ended")).toDouble());
    m_replayNote = ok ? tr("Ran on %1 up to %2 ms").arg(m_replayName, ended)
                      : tr("The logic failed on %1; the console shows why").arg(m_replayName);
    addConsoleLine(m_replayNote, ok ? log::Level::Info : log::Level::Error);
    emit replayChanged();
}

void ScriptWorkbench::addConsoleLine(const QString& text, log::Level level)
{
    LogicReport report;
    report.kind = script::ReportKind::Log;
    report.level = level;
    report.text = text;
    report.folder = m_folder;
    report.time = QDateTime::currentDateTime();
    m_console.append(std::move(report));
}

void ScriptWorkbench::openFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder));
}

void ScriptWorkbench::openExternal() const
{
    QString path = currentPath();
    if (path.isEmpty()) {
        path = logicEntryIn(m_folder);
    }
    const QString file = path.isEmpty() ? QString() : QDir(m_folder).filePath(path);
    if (const QString code = QStandardPaths::findExecutable(QStringLiteral("code")); !code.isEmpty()) {
        QStringList arguments{QDir::toNativeSeparators(m_folder)};
        if (!file.isEmpty()) {
            arguments << QStringLiteral("--goto") << QDir::toNativeSeparators(file);
        }
        if (QProcess::startDetached(code, arguments, m_folder)) {
            return;
        }
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(file.isEmpty() ? m_folder : file));
}

void ScriptWorkbench::clearConsole()
{
    m_devices.logicConsole().clear(m_phone, m_folder);
    m_console.reset({});
}

void ScriptWorkbench::setColors(const QVariantMap& colors)
{
    m_colors = {colorOf(colors, QStringLiteral("keyword")), colorOf(colors, QStringLiteral("string")),
                colorOf(colors, QStringLiteral("number")), colorOf(colors, QStringLiteral("comment")),
                colorOf(colors, QStringLiteral("type"))};
    for (ScriptDocument* document : m_documents.all()) {
        document->setColors(m_colors);
    }
}

void ScriptWorkbench::rescan()
{
    m_files = scriptFiles(m_folder, m_collapsed);
    QVariantList list;
    for (const ScriptFile& file : m_files) {
        list.append(QVariantMap{{QStringLiteral("path"), file.path},
                                {QStringLiteral("name"), file.name},
                                {QStringLiteral("depth"), file.depth},
                                {QStringLiteral("folder"), file.folder},
                                {QStringLiteral("open"), file.open},
                                {QStringLiteral("kind"), kindName(file)}});
    }
    const bool hadLogic = m_hasLogic;
    m_hasLogic = !logicEntryIn(m_folder).isEmpty();
    if (list != m_fileList || hadLogic != m_hasLogic) {
        m_fileList = std::move(list);
        emit filesChanged();
    }
}

void ScriptWorkbench::watch()
{
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty()) {
        m_watcher.removePaths(watched);
    }
    const QDir root(m_folder);
    QStringList paths = watchedFolders(m_folder);
    if (const QString profile = root.filePath(QStringLiteral("profile.json")); QFileInfo::exists(profile)) {
        paths.append(profile);
    }
    for (const ScriptDocument* document : m_documents.all()) {
        const QString file = root.filePath(document->path());
        if (QFileInfo::exists(file) && !paths.contains(file, Qt::CaseInsensitive)) {
            paths.append(file);
        }
    }
    m_watcher.addPaths(paths);
}

void ScriptWorkbench::settled()
{
    readProfile();
    rescan();
    for (ScriptDocument* document : m_documents.all()) {
        document->checkDisk();
    }
    watch();
    refreshRun();
    if (!m_hasLogic) {
        if (!m_problems.empty()) {
            setProblems({});
        }
        return;
    }
    if (fingerprint() != m_checkedPrint) {
        scheduleCheck();
    }
}

void ScriptWorkbench::readProfile()
{
    std::string contents;
    if (platform::readFile(toPath(QDir(m_folder).filePath(QStringLiteral("profile.json"))), kMostProfileBytes, contents,
                           platform::WhenBusy::GiveUp)) {
        return;
    }
    const QByteArray bytes(contents.data(), static_cast<qsizetype>(contents.size()));
    if (bytes == m_profileBytes) {
        return;
    }
    const bool first = m_profileBytes.isEmpty();
    m_profileBytes = bytes;
    const input::LoadResult loaded = input::loadProfile(contents);
    if (!loaded.ok()) {
        return;
    }
    m_names = profileNamesOf(loaded.profile);
    m_controlIds.clear();
    for (const input::Control& control : loaded.profile.controls) {
        m_controlIds.append(toQString(control.id));
    }
    if (const QString name = toQString(loaded.profile.meta.name); !name.isEmpty() && name != m_name) {
        m_name = name;
        emit nameChanged();
    }
    if (!first && m_hasLogic) {
        startTypes();
    }
}

void ScriptWorkbench::scheduleCheck()
{
    if (m_hasLogic) {
        m_checkDelay.start();
    }
}

void ScriptWorkbench::startCheck()
{
    if (!m_hasLogic) {
        return;
    }
    if (m_check.state() != QProcess::NotRunning) {
        m_checkAgain = true;
        return;
    }
    const QString program = tool();
    if (!QFileInfo::exists(program)) {
        m_checkNotice = tr("Problems are not checked: gs.exe is missing next to GameScrcpy");
        emit checkingChanged();
        return;
    }
    m_checkAgain = false;
    m_checkedPrint = fingerprint();
    m_check.setWorkingDirectory(m_folder);
    QStringList arguments{QStringLiteral("check"), QDir::toNativeSeparators(m_folder), QStringLiteral("--json")};
    if (const AppSettings* settings = AppSettings::instance();
        settings != nullptr && settings->interfaceLanguage() == QStringLiteral("zh_CN")) {
        arguments << QStringLiteral("--locale") << QStringLiteral("zh-CN");
    }
    m_check.start(program, arguments);
    emit checkingChanged();
}

void ScriptWorkbench::checkFinished()
{
    const QByteArray output = m_check.readAllStandardOutput();
    const QByteArray errors = m_check.readAllStandardError();
    CheckOutcome outcome = parseCheckOutput(output, m_folder);
    if (outcome.ran) {
        m_checkNotice.clear();
        for (ScriptProblem& problem : outcome.problems) {
            problem.message = localizedCheckMessage(problem.code, problem.message);
        }
        setProblems(std::move(outcome.problems));
    } else {
        QString reason = outcome.failure;
        if (reason.isEmpty()) {
            reason = QString::fromUtf8(errors).trimmed().section(u'\n', 0, 0).trimmed();
        }
        m_checkNotice = reason.isEmpty() ? tr("The check did not finish") : tr("The check did not finish: %1").arg(reason);
    }
    emit checkingChanged();
    if (m_checkAgain) {
        scheduleCheck();
    }
}

void ScriptWorkbench::setProblems(std::vector<ScriptProblem> problems)
{
    m_problems = std::move(problems);
    m_problemList.clear();
    for (const ScriptProblem& problem : m_problems) {
        m_problemList.append(QVariantMap{{QStringLiteral("file"), problem.file},
                                         {QStringLiteral("inside"), problem.inside},
                                         {QStringLiteral("line"), problem.line},
                                         {QStringLiteral("column"), problem.column},
                                         {QStringLiteral("severity"), problem.severity},
                                         {QStringLiteral("code"), problem.code},
                                         {QStringLiteral("message"), problem.message},
                                         {QStringLiteral("place"), placeOf(problem.file, problem.line, problem.column)}});
    }
    for (ScriptDocument* document : m_documents.all()) {
        document->setProblems(document->recorded() ? std::vector<ScriptProblem>() : problemsFor(document->path()));
    }
    emit problemsChanged();
}

QVariantMap ScriptDocument::referenceAt(int position) const
{
    if (!script() || m_recorded) {
        return {};
    }
    const std::optional<ReferenceTarget> target = referenceTargetAt(m_text, position);
    if (!target) {
        return {};
    }
    return {{QStringLiteral("name"), target->text},
            {QStringLiteral("kind"),
             target->kind == ReferenceKind::String ? QStringLiteral("string") : QStringLiteral("name")}};
}

void ScriptDocument::findReferences(int position)
{
    if (!script() || m_recorded) {
        return;
    }
    if (const std::optional<ReferenceTarget> target = referenceTargetAt(m_text, position)) {
        m_bench.showReferences(target->text);
    }
}

QStringList ScriptWorkbench::unsavedFiles() const
{
    QStringList files;
    for (const ScriptDocument* document : m_documents.all()) {
        if (document->dirty() && !document->recorded() && !document->readOnly()) {
            files.append(document->path());
        }
    }
    return files;
}

QVariantMap ScriptWorkbench::openScriptTexts() const
{
    QVariantMap texts;
    for (const ScriptDocument* document : m_documents.all()) {
        if (document->script() && !document->recorded()) {
            texts.insert(document->path(), document->text());
        }
    }
    return texts;
}

void ScriptWorkbench::showReferences(const QString& name)
{
    OpenTexts texts;
    for (const ScriptDocument* document : m_documents.all()) {
        if (document->script() && !document->recorded()) {
            texts.emplace(document->path(), document->text().toUtf8());
        }
    }
    const ReferenceSearch search = findLogicReferences(toPath(m_folder), {.text = name.toStdString()}, texts);
    QVariantList places;
    for (const LogicReference& found : search.found) {
        if (places.size() >= kMostShownReferences) {
            break;
        }
        places.append(QVariantMap{{QStringLiteral("file"), found.file},
                                  {QStringLiteral("line"), found.line},
                                  {QStringLiteral("column"), found.column},
                                  {QStringLiteral("place"), placeOf(found.file, found.line, 0)},
                                  {QStringLiteral("text"), found.lineText.trimmed()}});
    }
    const auto count = static_cast<int>(search.found.size());
    m_references = QVariantMap{{QStringLiteral("name"), name},
                               {QStringLiteral("count"), count},
                               {QStringLiteral("more"), count - static_cast<int>(places.size())},
                               {QStringLiteral("places"), places}};
    emit referencesChanged();
}

void ScriptWorkbench::startTypes()
{
    const QString program = tool();
    if (m_types.state() != QProcess::NotRunning || !QFileInfo::exists(program)) {
        return;
    }
    m_types.setWorkingDirectory(m_folder);
    m_types.start(program, {QStringLiteral("types"), QDir::toNativeSeparators(m_folder)});
}

void ScriptWorkbench::refreshPhones()
{
    m_phoneStates = m_devices.logicPhones();
    QVariantList list;
    for (const LogicPhone& phone : m_phoneStates) {
        list.append(QVariantMap{{QStringLiteral("serial"), phone.serial},
                                {QStringLiteral("name"), phone.name},
                                {QStringLiteral("uses"), sameFolder(phone.folder, m_folder)}});
    }
    if (list != m_phoneList) {
        m_phoneList = std::move(list);
        emit phonesChanged();
    }
    if (!m_pinned.isEmpty()) {
        setPhone(m_pinned);
    } else if (std::ranges::find(m_phoneStates, m_phone, &LogicPhone::serial) == m_phoneStates.end()) {
        const auto chosen = std::ranges::find_if(
            m_phoneStates, [this](const LogicPhone& phone) { return sameFolder(phone.folder, m_folder); });
        setPhone(chosen != m_phoneStates.end() ? chosen->serial
                 : m_phoneStates.empty()       ? QString()
                                               : m_phoneStates.front().serial);
    }
    refreshRun();
}

void ScriptWorkbench::refreshRun()
{
    bool canRun = false;
    QString note;
    QString state = QStringLiteral("stopped");
    QString error;
    const auto found = std::ranges::find(m_phoneStates, m_phone, &LogicPhone::serial);
    if (found == m_phoneStates.end()) {
        note = tr("No phone is mirroring");
    } else if (!m_task.isEmpty()) {
        canRun = true;
        note = tr("Run the task on %1").arg(found->name);
        if (m_devices.taskRunning(m_task, m_phone)) {
            state = QStringLiteral("running");
        }
    } else {
        const bool uses = sameFolder(found->folder, m_folder);
        if (!uses) {
            note = tr("%1 uses another profile; choose this profile on it to run the logic").arg(found->name);
        } else if (!m_hasLogic) {
            note = tr("Add logic to run it");
        } else {
            canRun = true;
            note = tr("Restart the logic on %1").arg(found->name);
        }
        if (uses && found->started) {
            state = found->halted ? QStringLiteral("failed") : QStringLiteral("running");
            error = found->error;
        }
    }
    if (canRun == m_canRun && note == m_runNote && state == m_logicState && error == m_lastError) {
        return;
    }
    m_canRun = canRun;
    m_runNote = note;
    m_logicState = state;
    m_lastError = error;
    emit runChanged();
}

void ScriptWorkbench::refreshThumbnail()
{
    const QImage image = m_phone.isEmpty() ? QImage() : m_devices.thumbnailOf(m_phone);
    if (image.cacheKey() == m_thumbnail.cacheKey()) {
        return;
    }
    m_thumbnail = image;
    emit thumbnailChanged();
}

void ScriptWorkbench::refreshConsole()
{
    m_console.reset(m_phone.isEmpty() ? std::vector<LogicReport>() : m_devices.logicConsole().lines(m_phone, m_folder));
}

void ScriptWorkbench::reported(const QString& serial, const LogicReport& report)
{
    if (serial != m_phone || !sameFolder(report.folder, m_folder)) {
        return;
    }
    if (report.kind != script::ReportKind::Log) {
        refreshPhones();
    }
    m_console.append(report);
}

void ScriptWorkbench::setPhone(const QString& serial)
{
    if (serial == m_phone) {
        return;
    }
    if (m_debugging) {
        static_cast<void>(m_devices.debugLogic(m_phone, false));
        m_debugging = false;
        stopDebugRecord();
        emit debugChanged();
    }
    m_phone = serial;
    emit phoneChanged();
    refreshRun();
    refreshThumbnail();
    refreshConsole();
}

void ScriptWorkbench::setWatching(bool on)
{
    if (on == m_live.isActive()) {
        return;
    }
    if (on) {
        m_live.start();
        refreshLive();
    } else {
        m_live.stop();
    }
}

void ScriptWorkbench::refreshLive()
{
    QVariantMap snapshot;
    if (m_task.isEmpty() && m_logicState == u"running") {
        snapshot = m_devices.logicSnapshot(m_phone);
    }
    const QVariantList components = snapshot.value(QStringLiteral("components")).toList();
    const QVariantList vars = snapshot.value(QStringLiteral("vars")).toList();
    if (components == m_liveComponents && vars == m_liveVars) {
        return;
    }
    m_liveComponents = components;
    m_liveVars = vars;
    emit liveChanged();
}

void ScriptWorkbench::setComponent(const QString& ref, bool locked)
{
    m_component = ref;
    m_locked = locked;
    emit componentChanged();
}

void ScriptWorkbench::setReadOnly(bool on)
{
    if (on == m_readOnly) {
        return;
    }
    m_readOnly = on;
    if (!on) {
        return;
    }
    for (ScriptDocument* document : m_documents.all()) {
        if (document != nullptr) {
            document->markDraft();
        }
    }
}

void ScriptWorkbench::setTask(const QString& id)
{
    if (id == m_task) {
        return;
    }
    m_task = id;
    refreshRun();
    emit runChanged();
}

void ScriptWorkbench::pinPhone(const QString& serial)
{
    m_pinned = serial;
    setPhone(serial);
    emit phoneChanged();
}

}
