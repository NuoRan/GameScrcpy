#include "FileAccess.h"

#include "ExtensionHost.h"
#include "LogicUploads.h"

#include <gs/base/Log.h>
#include <gs/ext/Archive.h>
#include <gs/ext/Manifest.h>
#include <gs/platform/AtomicFile.h>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaObject>
#include <QStandardPaths>
#include <QUrl>
#include <QVariant>
#include <QVariantList>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kPick = "files.pick";
constexpr std::string_view kPickRead = "files.pick.read";
constexpr std::string_view kPickWrite = "files.pick.write";
constexpr std::string_view kPickClose = "files.pick.close";
constexpr std::string_view kFolder = "files.folder";
constexpr std::string_view kFolderRead = "files.folder.read";
constexpr std::string_view kFolderWrite = "files.folder.write";
constexpr std::string_view kFolderList = "files.folder.list";
constexpr std::string_view kFolderRemove = "files.folder.remove";
constexpr std::string_view kFolderClose = "files.folder.close";
constexpr std::string_view kPart = "files.part";
constexpr std::array<std::string_view, 3> kCalls{kPart, kPickClose, kFolderClose};
constexpr std::array<std::string_view, 8> kRequests{kPick,        kPickRead,    kPickWrite, kFolder,
                                                    kFolderRead, kFolderWrite, kFolderList, kFolderRemove};
constexpr std::string_view kFoldersFormat = "gamescrcpy.logic-folders";
constexpr int kFoldersVersion = 1;
constexpr std::size_t kMostFoldersBytes = 1024 * 1024;
constexpr qsizetype kMostFilters = 8;
constexpr qsizetype kMostExtensions = 16;
constexpr qsizetype kMostFilterName = 60;
constexpr qsizetype kMostSuggestedName = 255;
constexpr qsizetype kMostWaitingWrites = qsizetype{1} << 20;

QString fromView(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString compactJson(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject objectOf(const QByteArray& json)
{
    QJsonParseError problem;
    const QJsonDocument document = QJsonDocument::fromJson(json, &problem);
    return problem.error == QJsonParseError::NoError && document.isObject() ? document.object() : QJsonObject();
}

script::ServiceAnswer refused(const QString& message)
{
    const std::string full = message.toStdString();
    const std::size_t space = full.find(' ');
    return {"null", full.substr(0, space), space == std::string::npos ? std::string() : full.substr(space + 1)};
}

QString pathText(const fs::path& path)
{
    return QString::fromStdWString(path.wstring());
}

fs::path pathOf(const QString& text)
{
    return fs::path(text.toStdWString());
}

QString ownerOf(const LogicCaller& caller)
{
    return caller.extension.isEmpty() ? fromView(FileAccess::kOwnLogic) : caller.extension;
}

bool validFolderName(const QString& name)
{
    return !name.isEmpty() && name.size() <= 32 && std::ranges::all_of(name, [](QChar c) {
               const char16_t code = c.unicode();
               return (code >= u'a' && code <= u'z') || (code >= u'0' && code <= u'9') || code == u'-';
           });
}

bool validExtension(const QString& extension)
{
    return !extension.isEmpty() && extension.size() <= 16 && std::ranges::all_of(extension, [](QChar c) {
               const char16_t code = c.unicode();
               return (code >= u'a' && code <= u'z') || (code >= u'A' && code <= u'Z') ||
                      (code >= u'0' && code <= u'9') || code == u'_' || code == u'-';
           });
}

bool safeName(const QString& name)
{
    const QByteArray utf8 = name.toUtf8();
    return !name.contains(u'/') &&
           ext::safeArchivePath(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

bool validOwner(const QString& owner)
{
    return owner == fromView(FileAccess::kOwnLogic) || ext::validExtensionId(owner.toStdString());
}

QString noPick()
{
    return QStringLiteral("GS-API-160 this extension may not open files: its manifest does not ask for the permission "
                          "\"files.pick\"");
}

QString noFolder(const QString& name)
{
    return QStringLiteral("GS-API-160 this extension may not open the folder %1: its manifest does not name it under "
                          "the permission \"files.paths\"")
        .arg(name);
}

QString invalidWith(const QString& what)
{
    return QStringLiteral("GS-API-161 ") + what;
}

QString failedWith(const QString& what)
{
    return QStringLiteral("GS-API-162 ") + what;
}

QString overLimit(const QString& what)
{
    return QStringLiteral("GS-API-163 ") + what;
}

QString closedHandle(bool folder)
{
    return folder ? QStringLiteral("GS-API-164 the folder is closed") : QStringLiteral("GS-API-164 the file is closed");
}

QString notChosen(const QString& folder)
{
    return QStringLiteral("GS-API-164 no folder was chosen for %1").arg(folder);
}

QString reasonOf(const std::error_code& error)
{
    if (platform::fileMissing(error)) {
        return QStringLiteral("it or its folder is not there");
    }
    if (platform::fileBusy(error)) {
        return QStringLiteral("another program is using it");
    }
    if (error == std::errc::permission_denied) {
        return QStringLiteral("Windows does not allow it");
    }
    if (error == std::errc::no_space_on_device) {
        return QStringLiteral("the disk is full");
    }
    return QStringLiteral("error %1").arg(error.value());
}

qsizetype utf8Boundary(const QByteArray& bytes)
{
    const qsizetype size = bytes.size();
    qsizetype start = size;
    for (qsizetype back = 1; back <= 4 && back <= size; ++back) {
        if ((static_cast<unsigned char>(bytes.at(size - back)) & 0xC0) != 0x80) {
            start = size - back;
            break;
        }
    }
    if (start == size) {
        return size;
    }
    const auto lead = static_cast<unsigned char>(bytes.at(start));
    const qsizetype need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    return start + need > size && start > 0 ? start : size;
}

std::optional<fs::path> inside(const fs::path& root, const QString& relative, QString& problem)
{
    QString written = relative;
    if (written.endsWith(u'/')) {
        written.chop(1);
    }
    const QByteArray utf8 = written.toUtf8();
    if (!ext::safeArchivePath(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())))) {
        problem = invalidWith(QStringLiteral("\"%1\" is not a path inside the folder: write it with / between names, "
                                             "without .., a drive or characters Windows does not allow")
                                  .arg(relative));
        return std::nullopt;
    }
    std::error_code error;
    const fs::path base = fs::canonical(root, error);
    if (error) {
        problem = failedWith(QStringLiteral("the folder is not there any more"));
        return std::nullopt;
    }
    const fs::path target = fs::weakly_canonical(base / pathOf(written), error);
    if (error) {
        problem = failedWith(QStringLiteral("%1 cannot be reached").arg(relative));
        return std::nullopt;
    }
    const fs::path rest = target.lexically_relative(base);
    if (rest.empty() || rest.is_absolute() || rest == "." || *rest.begin() == "..") {
        problem = invalidWith(QStringLiteral("%1 leads out of the folder").arg(relative));
        return std::nullopt;
    }
    return target;
}

FileOutcome readPiece(const fs::path& file, const QString& shown, qint64 offset, bool text, qsizetype chunk,
                      qsizetype most)
{
    std::error_code error;
    if (fs::is_directory(file, error)) {
        return {{}, failedWith(QStringLiteral("%1 is a folder").arg(shown))};
    }
    QFile handle(pathText(file));
    if (!handle.open(QIODevice::ReadOnly)) {
        return {{}, failedWith(fs::exists(file, error)
                                   ? QStringLiteral("%1 cannot be read: another program may be using it, or Windows "
                                                    "does not allow it")
                                         .arg(shown)
                                   : QStringLiteral("%1 does not exist").arg(shown))};
    }
    const qint64 size = handle.size();
    if (size > most) {
        return {{}, overLimit(QStringLiteral("%1 is larger than a file may be (64 MiB)").arg(shown))};
    }
    if (offset > size || !handle.seek(offset)) {
        return {{}, failedWith(QStringLiteral("%1 changed while it was read").arg(shown))};
    }
    const qint64 wanted = std::min<qint64>(chunk, size - offset);
    const QByteArray bytes = handle.read(wanted);
    if (bytes.size() != wanted) {
        return {{}, failedWith(QStringLiteral("%1 cannot be read to its end").arg(shown))};
    }
    QJsonObject piece{
        {QStringLiteral("size"), static_cast<double>(size)},
        {QStringLiteral("modified"), static_cast<double>(QFileInfo(pathText(file)).lastModified().toMSecsSinceEpoch())}};
    if (text) {
        const qsizetype cut = offset + bytes.size() < size ? utf8Boundary(bytes) : bytes.size();
        piece.insert(QStringLiteral("text"), QString::fromUtf8(bytes.first(cut)));
        piece.insert(QStringLiteral("next"), static_cast<double>(offset + cut));
    } else {
        piece.insert(QStringLiteral("data"), QString::fromLatin1(bytes.toBase64()));
    }
    return {compactJson(piece), {}};
}

FileOutcome writeWhole(const fs::path& file, const QString& shown, const QByteArray& bytes, bool makeParents)
{
    std::error_code error;
    if (fs::is_directory(file, error)) {
        return {{}, failedWith(QStringLiteral("%1 is a folder").arg(shown))};
    }
    if (makeParents && !fs::is_directory(file.parent_path(), error)) {
        fs::create_directories(file.parent_path(), error);
        if (error) {
            return {{}, failedWith(QStringLiteral("the folder of %1 cannot be made: %2").arg(shown, reasonOf(error)))};
        }
    }
    error = platform::writeFileAtomically(file,
                                          std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (error) {
        return {{}, failedWith(QStringLiteral("%1 cannot be written: %2").arg(shown, reasonOf(error)))};
    }
    return {QStringLiteral("null"), {}};
}

FileOutcome listFolder(const fs::path& folder, const QString& relative, std::size_t most)
{
    const QString shown = relative.isEmpty() ? QStringLiteral("the folder") : relative;
    const QFileInfo info(pathText(folder));
    if (!info.exists()) {
        return {{}, failedWith(QStringLiteral("%1 is not there").arg(shown))};
    }
    if (!info.isDir()) {
        return {{}, failedWith(QStringLiteral("%1 is a file, not a folder").arg(shown))};
    }
    const QFileInfoList found = QDir(pathText(folder)).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                                                                     QDir::Name | QDir::IgnoreCase);
    QJsonArray entries;
    for (const QFileInfo& entry : found) {
        if (static_cast<std::size_t>(entries.size()) >= most) {
            break;
        }
        const QString name = entry.fileName();
        const bool isFolder = entry.isDir();
        entries.append(QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("path"), relative.isEmpty() ? name : relative + u'/' + name},
            {QStringLiteral("folder"), isFolder},
            {QStringLiteral("size"), isFolder ? 0.0 : static_cast<double>(entry.size())},
            {QStringLiteral("modified"), static_cast<double>(entry.lastModified().toMSecsSinceEpoch())}});
    }
    return {QString::fromUtf8(QJsonDocument(entries).toJson(QJsonDocument::Compact)), {}};
}

FileOutcome removeEntry(const fs::path& target, const QString& shown)
{
    std::error_code error;
    const fs::file_status status = fs::symlink_status(target, error);
    if (!fs::exists(status)) {
        return {QStringLiteral("false"), {}};
    }
    if (fs::is_directory(status) && !fs::is_empty(target, error)) {
        return {{}, failedWith(QStringLiteral("%1 is a folder with something in it").arg(shown))};
    }
    error.clear();
    const bool removed = fs::remove(target, error);
    if (error) {
        return {{}, failedWith(QStringLiteral("%1 cannot be removed: %2").arg(shown, reasonOf(error)))};
    }
    return {removed ? QStringLiteral("true") : QStringLiteral("false"), {}};
}

}

struct FileAccess::Shared {
    struct Handle {
        std::uint64_t logic = 0;
        QString owner;
        bool folder = false;
        QString name;
        fs::path path;
    };

    mutable std::mutex mutex;
    FileAccess* owner = nullptr;
    std::set<QString> pickers;
    std::map<QString, std::vector<DeclaredFolder>> declared;
    std::map<QString, Handle> handles;
    qsizetype mostFileBytes = kMostFileBytes;
    qsizetype chunkBytes = kChunkBytes;
    LogicUploads uploads{kMostFileBytes + kMostWaitingWrites};

    void post(std::function<void(FileAccess&)> work) const
    {
        const std::scoped_lock lock(mutex);
        if (owner == nullptr) {
            return;
        }
        FileAccess* target = owner;
        QMetaObject::invokeMethod(target, [target, work = std::move(work)] { work(*target); }, Qt::QueuedConnection);
    }

    [[nodiscard]] bool mayPick(const QString& extension) const
    {
        const std::scoped_lock lock(mutex);
        return extension.isEmpty() || pickers.contains(extension);
    }

    [[nodiscard]] bool mayStage(const QString& extension) const
    {
        const std::scoped_lock lock(mutex);
        const auto found = declared.find(extension);
        return extension.isEmpty() || pickers.contains(extension) || (found != declared.end() && !found->second.empty());
    }

    [[nodiscard]] std::optional<DeclaredFolder> declaredFolder(const QString& extension, const QString& name) const
    {
        const std::scoped_lock lock(mutex);
        const auto found = declared.find(extension);
        if (found == declared.end()) {
            return std::nullopt;
        }
        const auto folder = std::ranges::find(found->second, name, &DeclaredFolder::name);
        return folder == found->second.end() ? std::nullopt : std::optional<DeclaredFolder>(*folder);
    }

    [[nodiscard]] std::optional<Handle> handleOf(std::uint64_t logic, const QString& handle, bool folder) const
    {
        const std::scoped_lock lock(mutex);
        const auto found = handles.find(handle);
        if (found == handles.end() || found->second.logic != logic || found->second.folder != folder) {
            return std::nullopt;
        }
        return found->second;
    }

    [[nodiscard]] bool keeps(const Handle& handle) const
    {
        if (handle.owner == fromView(kOwnLogic)) {
            return true;
        }
        if (!handle.folder) {
            return pickers.contains(handle.owner);
        }
        const auto found = declared.find(handle.owner);
        return found != declared.end() &&
               std::ranges::find(found->second, handle.name, &DeclaredFolder::name) != found->second.end();
    }
};

FileAccess::FileAccess(std::filesystem::path foldersFile, QObject* parent)
    : QObject(parent)
    , m_shared(std::make_shared<Shared>())
    , m_foldersFile(std::move(foldersFile))
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    m_shared->owner = this;
    m_replies = {
        .answer = [](std::uint64_t logic, quint64 request,
                     const QString& json) { static_cast<void>(LogicServices::instance().answer(logic, request, json)); },
        .refuse = [](std::uint64_t logic, quint64 request,
                     const QString& message) { static_cast<void>(LogicServices::instance().refuse(logic, request, message)); },
    };
    m_pool.setMaxThreadCount(1);
    loadFolders();
}

FileAccess::~FileAccess()
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->owner = nullptr;
    }
    if (m_offered) {
        for (const std::string_view name : kCalls) {
            LogicServices::instance().withdraw(name);
        }
        for (const std::string_view name : kRequests) {
            LogicServices::instance().withdraw(name);
        }
    }
    m_pool.clear();
    static_cast<void>(m_pool.waitForDone());
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

FileAccess* FileAccess::create(QQmlEngine*, QJSEngine*)
{
    if (s_instance == nullptr) {
        auto* made = new FileAccess(std::filesystem::path());
        QJSEngine::setObjectOwnership(made, QJSEngine::JavaScriptOwnership);
        return made;
    }
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void FileAccess::offerServices()
{
    LogicServices& services = LogicServices::instance();
    for (const std::string_view name : kCalls) {
        services.offerCall(std::string(name), [shared = m_shared, name](const LogicCaller& caller, std::string_view json) {
            return callWith(shared, caller, name, json);
        });
    }
    for (const std::string_view name : kRequests) {
        services.offerRequest(
            std::string(name),
            [this, name](const LogicCaller& caller, quint64 id, const QString& json) { request(caller, name, id, json); },
            [this](const LogicCaller& caller, quint64 id) { drop(caller, id); });
    }
    connect(&services, &LogicServices::logicEnded, this, [this](quint64 logic) { endLogic(logic); });
    m_offered = true;
}

void FileAccess::setReplies(FileReplies replies)
{
    m_replies = std::move(replies);
}

void FileAccess::setPermitted(const QStringList& pickers, std::map<QString, std::vector<DeclaredFolder>> folders)
{
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->pickers = std::set<QString>(pickers.begin(), pickers.end());
        m_shared->declared = std::move(folders);
        std::erase_if(m_shared->handles, [this](const auto& entry) { return !m_shared->keeps(entry.second); });
    }
    const bool frontGone = !m_prompts.empty() && !allowed(m_prompts.front());
    for (auto it = m_prompts.begin(); it != m_prompts.end();) {
        if (allowed(*it)) {
            ++it;
            continue;
        }
        refuse(it->caller.logic, it->request, it->kind == PromptKind::Folder ? noFolder(it->folder) : noPick());
        it = m_prompts.erase(it);
    }
    if (frontGone) {
        showNextPrompt();
    }
    emit foldersChanged();
}

void FileAccess::setExtensionNames(std::map<QString, QString> names)
{
    m_extensionNames = std::move(names);
    if (!m_prompts.empty() && !m_chooser) {
        emit promptChanged();
    }
}

void FileAccess::setChooser(Chooser chooser)
{
    m_chooser = std::move(chooser);
}

void FileAccess::setLimits(qsizetype mostFileBytes, qsizetype chunkBytes)
{
    const std::scoped_lock lock(m_shared->mutex);
    m_shared->mostFileBytes = mostFileBytes;
    m_shared->chunkBytes = chunkBytes;
}

void FileAccess::follow(ExtensionHost& extensions)
{
    const auto update = [this, &extensions] {
        QStringList pickers;
        std::map<QString, std::vector<DeclaredFolder>> folders;
        for (const ExtensionHost::Extension& extension : extensions.extensions()) {
            if (!extension.active || !extension.manifest) {
                continue;
            }
            const QString id = QString::fromStdString(extension.manifest->id);
            for (const ext::Permission& permission : extension.manifest->permissions) {
                if (permission.name == ext::kFilesPickPermission) {
                    pickers.append(id);
                } else if (permission.name == ext::kFilesPathsPermission) {
                    std::vector<DeclaredFolder>& declared = folders[id];
                    for (const ext::PermissionFolder& folder : permission.folders) {
                        declared.push_back(
                            {QString::fromStdString(folder.name), QString::fromStdString(folder.purpose)});
                    }
                }
            }
        }
        std::map<QString, QString> names;
        for (const QVariant& entry : extensions.entries()) {
            const QVariantMap fields = entry.toMap();
            names.insert_or_assign(fields.value(QStringLiteral("id")).toString(),
                                   fields.value(QStringLiteral("name")).toString());
        }
        setExtensionNames(std::move(names));
        setPermitted(pickers, std::move(folders));
    };
    update();
    connect(&extensions, &ExtensionHost::extensionsChanged, this, update);
}

void FileAccess::request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json)
{
    const QJsonObject body = objectOf(json.toUtf8());
    if (service == kPick) {
        pick(caller, id, body);
    } else if (service == kFolder) {
        openFolder(caller, id, body);
    } else if (service == kPickRead || service == kFolderRead) {
        readFrom(caller, id, body, service == kFolderRead);
    } else if (service == kPickWrite || service == kFolderWrite) {
        writeTo(caller, id, body, service == kFolderWrite);
    } else if (service == kFolderList) {
        listIn(caller, id, body);
    } else if (service == kFolderRemove) {
        removeIn(caller, id, body);
    } else {
        refuse(caller.logic, id,
               QStringLiteral("GS-API-070 no part of GameScrcpy offers the service %1").arg(fromView(service)));
    }
}

script::ServiceAnswer FileAccess::call(const LogicCaller& caller, std::string_view service, std::string_view json)
{
    return callWith(m_shared, caller, service, json);
}

script::ServiceAnswer FileAccess::callWith(const std::shared_ptr<Shared>& shared, const LogicCaller& caller,
                                           std::string_view service, std::string_view json)
{
    const QJsonObject body = objectOf(QByteArray(json.data(), static_cast<qsizetype>(json.size())));
    if (service == kPart) {
        if (!shared->mayStage(caller.extension)) {
            return refused(noPick());
        }
        qsizetype most = 0;
        {
            const std::scoped_lock lock(shared->mutex);
            most = shared->mostFileBytes;
        }
        switch (shared->uploads.add(caller.logic, body, most)) {
        case LogicUploads::Result::Done:
            return {"null", {}, {}};
        case LogicUploads::Result::Invalid:
            return refused(invalidWith(QStringLiteral("what is written must be text or bytes")));
        case LogicUploads::Result::TooLarge:
            return refused(overLimit(QStringLiteral("a file is at most 64 MiB, and no more than that waits to be "
                                                    "written at once")));
        case LogicUploads::Result::TooMany:
            return refused(overLimit(QStringLiteral("a logic has at most 64 files waiting to be written in parts")));
        }
        return {"null", {}, {}};
    }
    if (service == kPickClose || service == kFolderClose) {
        const QString handle = body.value(u"handle").toString();
        const std::scoped_lock lock(shared->mutex);
        const auto found = shared->handles.find(handle);
        if (found != shared->handles.end() && found->second.logic == caller.logic &&
            found->second.folder == (service == kFolderClose)) {
            shared->handles.erase(found);
        }
        return {"null", {}, {}};
    }
    return {"null", "GS-API-070", "no part of GameScrcpy answers the service " + std::string(service) + " at once"};
}

void FileAccess::drop(const LogicCaller& caller, quint64 id)
{
    for (auto it = m_prompts.begin(); it != m_prompts.end(); ++it) {
        if (it->caller.logic == caller.logic && it->request == id) {
            const bool front = it == m_prompts.begin();
            m_prompts.erase(it);
            if (front) {
                showNextPrompt();
            }
            return;
        }
    }
}

void FileAccess::endLogic(std::uint64_t logic)
{
    const bool front = !m_prompts.empty() && m_prompts.front().caller.logic == logic;
    std::erase_if(m_prompts, [logic](const Prompt& prompt) { return prompt.caller.logic == logic; });
    {
        const std::scoped_lock lock(m_shared->mutex);
        std::erase_if(m_shared->handles, [logic](const auto& entry) { return entry.second.logic == logic; });
    }
    m_shared->uploads.drop(logic);
    if (front) {
        showNextPrompt();
    }
}

QVariantMap FileAccess::prompt() const
{
    if (m_prompts.empty()) {
        return {};
    }
    const Prompt& asked = m_prompts.front();
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString kind = asked.kind == PromptKind::Save     ? QStringLiteral("save")
                         : asked.kind == PromptKind::Folder ? QStringLiteral("folder")
                                                            : QStringLiteral("open");
    QVariantMap shown{{QStringLiteral("open"), true},
                      {QStringLiteral("ticket"), ticketOf(asked)},
                      {QStringLiteral("kind"), kind},
                      {QStringLiteral("own"), asked.caller.extension.isEmpty()},
                      {QStringLiteral("requester"), requesterOf(asked.caller)},
                      {QStringLiteral("filters"), asked.filters},
                      {QStringLiteral("folder"), asked.folder},
                      {QStringLiteral("purpose"), asked.purpose},
                      {QStringLiteral("startUrl"), QUrl::fromLocalFile(documents)}};
    if (!asked.suggestedName.isEmpty()) {
        shown.insert(QStringLiteral("suggestedUrl"), QUrl::fromLocalFile(documents + u'/' + asked.suggestedName));
    }
    return shown;
}

QVariantMap FileAccess::folderMap() const
{
    std::map<QString, std::vector<DeclaredFolder>> declared;
    {
        const std::scoped_lock lock(m_shared->mutex);
        declared = m_shared->declared;
    }
    QVariantMap owners;
    for (const auto& [owner, names] : m_folders) {
        const auto known = declared.find(owner);
        QVariantList entries;
        for (const auto& [name, path] : names) {
            QString purpose;
            if (known != declared.end()) {
                const auto folder = std::ranges::find(known->second, name, &DeclaredFolder::name);
                purpose = folder != known->second.end() ? folder->purpose : QString();
            }
            entries.append(QVariantMap{{QStringLiteral("name"), name},
                                       {QStringLiteral("path"), QDir::toNativeSeparators(pathText(path))},
                                       {QStringLiteral("purpose"), purpose}});
        }
        owners.insert(owner, entries);
    }
    return owners;
}

std::optional<std::filesystem::path> FileAccess::folderOf(const QString& owner, const QString& name) const
{
    const auto found = m_folders.find(owner);
    if (found == m_folders.end()) {
        return std::nullopt;
    }
    const auto folder = found->second.find(name);
    return folder == found->second.end() ? std::nullopt : std::optional<std::filesystem::path>(folder->second);
}

std::size_t FileAccess::handleCount() const
{
    const std::scoped_lock lock(m_shared->mutex);
    return m_shared->handles.size();
}

std::size_t FileAccess::handlesOf(std::uint64_t logic) const
{
    const std::scoped_lock lock(m_shared->mutex);
    return static_cast<std::size_t>(
        std::ranges::count_if(m_shared->handles, [logic](const auto& entry) { return entry.second.logic == logic; }));
}

void FileAccess::choose(const QString& ticket, const QString& fileOrUrl)
{
    if (m_prompts.empty() || ticketOf(m_prompts.front()) != ticket) {
        return;
    }
    const Prompt asked = std::move(m_prompts.front());
    m_prompts.pop_front();
    const QUrl url(fileOrUrl);
    const QString local = url.isLocalFile() ? url.toLocalFile() : fileOrUrl;
    const fs::path path = pathOf(local);
    const bool absolute = !local.isEmpty() && path.is_absolute();
    std::error_code error;
    switch (asked.kind) {
    case PromptKind::Folder:
        if (absolute && fs::is_directory(path, error)) {
            m_folders[ownerOf(asked.caller)].insert_or_assign(asked.folder, path);
            saveFolders();
            emit foldersChanged();
            answer(asked.caller.logic, asked.request, openHandle(asked.caller, true, asked.folder, path));
        } else {
            refuse(asked.caller.logic, asked.request, notChosen(asked.folder));
        }
        break;
    case PromptKind::Open:
        if (absolute && fs::is_regular_file(path, error)) {
            answer(asked.caller.logic, asked.request,
                   openHandle(asked.caller, false, pathText(path.filename()), path));
        } else {
            answer(asked.caller.logic, asked.request, QStringLiteral("null"));
        }
        break;
    case PromptKind::Save:
        if (absolute && !fs::is_directory(path, error) && fs::is_directory(path.parent_path(), error)) {
            answer(asked.caller.logic, asked.request,
                   openHandle(asked.caller, false, pathText(path.filename()), path));
        } else {
            answer(asked.caller.logic, asked.request, QStringLiteral("null"));
        }
        break;
    }
    showNextPrompt();
}

void FileAccess::cancelPrompt(const QString& ticket)
{
    if (m_prompts.empty() || ticketOf(m_prompts.front()) != ticket) {
        return;
    }
    const Prompt asked = std::move(m_prompts.front());
    m_prompts.pop_front();
    if (asked.kind == PromptKind::Folder) {
        refuse(asked.caller.logic, asked.request, notChosen(asked.folder));
    } else {
        answer(asked.caller.logic, asked.request, QStringLiteral("null"));
    }
    showNextPrompt();
}

bool FileAccess::forgetFolder(const QString& owner, const QString& name)
{
    const auto found = m_folders.find(owner);
    if (found == m_folders.end() || found->second.erase(name) == 0) {
        return false;
    }
    if (found->second.empty()) {
        m_folders.erase(found);
    }
    {
        const std::scoped_lock lock(m_shared->mutex);
        std::erase_if(m_shared->handles, [&owner, &name](const auto& entry) {
            return entry.second.folder && entry.second.owner == owner && entry.second.name == name;
        });
    }
    saveFolders();
    emit foldersChanged();
    return true;
}

QString FileAccess::ticketOf(const Prompt& prompt)
{
    return QStringLiteral("%1:%2").arg(prompt.caller.logic).arg(prompt.request);
}

void FileAccess::pick(const LogicCaller& caller, quint64 id, const QJsonObject& body)
{
    if (!m_shared->mayPick(caller.extension)) {
        refuse(caller.logic, id, noPick());
        return;
    }
    const QJsonValue save = body.value(u"save");
    const QJsonValue given = body.value(u"filters");
    const QJsonArray filters = given.toArray();
    if ((!save.isUndefined() && !save.isBool()) || (!given.isUndefined() && !given.isArray()) ||
        filters.size() > kMostFilters) {
        refuse(caller.logic, id,
               invalidWith(QStringLiteral("save is true or false, and filters a list of at most 8 kinds of file")));
        return;
    }
    QStringList kinds;
    for (const QJsonValue& each : filters) {
        const QJsonObject filter = each.toObject();
        const QString name = filter.value(u"name").toString().trimmed();
        const QJsonArray extensions = filter.value(u"extensions").toArray();
        bool valid = each.isObject() && !name.isEmpty() && name.size() <= kMostFilterName && !name.contains(u'(') &&
                     !name.contains(u')') && !extensions.isEmpty() && extensions.size() <= kMostExtensions;
        QStringList patterns;
        for (const QJsonValue& extension : extensions) {
            valid = valid && extension.isString() && validExtension(extension.toString());
            patterns.append(QStringLiteral("*.") + extension.toString());
        }
        if (!valid) {
            refuse(caller.logic, id,
                   invalidWith(QStringLiteral("a filter is { name, extensions }, such as { name: \"Images\", extensions: "
                                              "[\"png\", \"jpg\"] }, with at most 16 extensions of letters and digits")));
            return;
        }
        kinds.append(QStringLiteral("%1 (%2)").arg(name, patterns.join(u' ')));
    }
    const QJsonValue suggested = body.value(u"suggestedName");
    const QString name = suggested.toString();
    if ((!suggested.isUndefined() && !suggested.isString()) || name.size() > kMostSuggestedName ||
        (!name.isEmpty() && !safeName(name))) {
        refuse(caller.logic, id,
               invalidWith(QStringLiteral("suggestedName is the name of a file without its folder, such as "
                                          "\"notes.txt\"")));
        return;
    }
    const std::size_t open = handlesOf(caller.logic);
    if (open >= kMostHandles || m_prompts.size() >= kMostWaitingPrompts) {
        refuse(caller.logic, id,
               overLimit(open >= kMostHandles ? QStringLiteral("a logic can have at most 64 files and folders open")
                                              : QStringLiteral("8 requests for files and folders are waiting already")));
        return;
    }
    enqueue(Prompt{.caller = caller,
                   .request = id,
                   .kind = save.toBool() ? PromptKind::Save : PromptKind::Open,
                   .filters = kinds,
                   .suggestedName = name,
                   .folder = {},
                   .purpose = {}});
}

void FileAccess::openFolder(const LogicCaller& caller, quint64 id, const QJsonObject& body)
{
    const QString name = body.value(u"name").toString();
    if (!validFolderName(name)) {
        refuse(caller.logic, id,
               invalidWith(QStringLiteral("a folder is named with 1 to 32 lowercase letters, digits and hyphens, as "
                                          "the manifest declares it")));
        return;
    }
    const std::optional<DeclaredFolder> declared = m_shared->declaredFolder(caller.extension, name);
    if (!caller.extension.isEmpty() && !declared) {
        refuse(caller.logic, id, noFolder(name));
        return;
    }
    if (handlesOf(caller.logic) >= kMostHandles) {
        refuse(caller.logic, id, overLimit(QStringLiteral("a logic can have at most 64 files and folders open")));
        return;
    }
    if (const std::optional<fs::path> known = folderOf(ownerOf(caller), name)) {
        std::error_code error;
        if (fs::is_directory(*known, error)) {
            answer(caller.logic, id, openHandle(caller, true, name, *known));
            return;
        }
    }
    if (m_prompts.size() >= kMostWaitingPrompts) {
        refuse(caller.logic, id, overLimit(QStringLiteral("8 requests for files and folders are waiting already")));
        return;
    }
    enqueue(Prompt{.caller = caller,
                   .request = id,
                   .kind = PromptKind::Folder,
                   .filters = {},
                   .suggestedName = {},
                   .folder = name,
                   .purpose = declared ? declared->purpose : QString()});
}

void FileAccess::readFrom(const LogicCaller& caller, quint64 id, const QJsonObject& body, bool folder)
{
    const std::optional<Shared::Handle> handle =
        m_shared->handleOf(caller.logic, body.value(u"handle").toString(), folder);
    if (!handle) {
        refuse(caller.logic, id, closedHandle(folder));
        return;
    }
    const QJsonValue at = body.value(u"offset");
    const double offset = at.toDouble(-1);
    const QString relative = body.value(u"path").toString();
    if (!at.isDouble() || !(offset >= 0) || std::floor(offset) != offset || (folder && relative.isEmpty())) {
        refuse(caller.logic, id,
               invalidWith(QStringLiteral("a read names the file and starts at a whole number of bytes from 0")));
        return;
    }
    qsizetype most = 0;
    qsizetype chunk = 0;
    {
        const std::scoped_lock lock(m_shared->mutex);
        most = m_shared->mostFileBytes;
        chunk = m_shared->chunkBytes;
    }
    const bool text = body.value(u"text").toBool();
    const QString shown = folder ? relative : handle->name;
    work(caller.logic, id, [root = handle->path, relative, folder, shown, start = static_cast<qint64>(offset), text, chunk,
                            most] {
        if (!folder) {
            return readPiece(root, shown, start, text, chunk, most);
        }
        QString problem;
        const std::optional<fs::path> target = inside(root, relative, problem);
        if (!target) {
            return FileOutcome{{}, problem};
        }
        return readPiece(*target, shown, start, text, chunk, most);
    });
}

void FileAccess::writeTo(const LogicCaller& caller, quint64 id, const QJsonObject& body, bool folder)
{
    qsizetype most = 0;
    {
        const std::scoped_lock lock(m_shared->mutex);
        most = m_shared->mostFileBytes;
    }
    LogicUploads::Taken payload = m_shared->uploads.take(caller.logic, body, most);
    const std::optional<Shared::Handle> handle =
        m_shared->handleOf(caller.logic, body.value(u"handle").toString(), folder);
    if (!handle) {
        refuse(caller.logic, id, closedHandle(folder));
        return;
    }
    const QString relative = body.value(u"path").toString();
    if (payload.result == LogicUploads::Result::Invalid || (folder && relative.isEmpty())) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("a write names the file and gives text or bytes")));
        return;
    }
    if (payload.result == LogicUploads::Result::TooLarge) {
        refuse(caller.logic, id, overLimit(QStringLiteral("a file is at most 64 MiB")));
        return;
    }
    const QString shown = folder ? relative : handle->name;
    work(caller.logic, id, [root = handle->path, relative, folder, shown, bytes = std::move(payload.bytes)] {
        if (!folder) {
            return writeWhole(root, shown, bytes, false);
        }
        QString problem;
        const std::optional<fs::path> target = inside(root, relative, problem);
        if (!target) {
            return FileOutcome{{}, problem};
        }
        return writeWhole(*target, shown, bytes, true);
    });
}

void FileAccess::listIn(const LogicCaller& caller, quint64 id, const QJsonObject& body)
{
    const std::optional<Shared::Handle> handle =
        m_shared->handleOf(caller.logic, body.value(u"handle").toString(), true);
    if (!handle) {
        refuse(caller.logic, id, closedHandle(true));
        return;
    }
    QString relative = body.value(u"path").toString();
    while (relative.endsWith(u'/')) {
        relative.chop(1);
    }
    work(caller.logic, id, [root = handle->path, relative] {
        if (relative.isEmpty()) {
            std::error_code error;
            const fs::path base = fs::canonical(root, error);
            if (error) {
                return FileOutcome{{}, failedWith(QStringLiteral("the folder is not there any more"))};
            }
            return listFolder(base, relative, kMostEntries);
        }
        QString problem;
        const std::optional<fs::path> target = inside(root, relative, problem);
        if (!target) {
            return FileOutcome{{}, problem};
        }
        return listFolder(*target, relative, kMostEntries);
    });
}

void FileAccess::removeIn(const LogicCaller& caller, quint64 id, const QJsonObject& body)
{
    const std::optional<Shared::Handle> handle =
        m_shared->handleOf(caller.logic, body.value(u"handle").toString(), true);
    if (!handle) {
        refuse(caller.logic, id, closedHandle(true));
        return;
    }
    const QString relative = body.value(u"path").toString();
    if (relative.isEmpty()) {
        refuse(caller.logic, id, invalidWith(QStringLiteral("a removal names a file or a folder inside the folder")));
        return;
    }
    work(caller.logic, id, [root = handle->path, relative] {
        QString problem;
        const std::optional<fs::path> target = inside(root, relative, problem);
        if (!target) {
            return FileOutcome{{}, problem};
        }
        return removeEntry(*target, relative);
    });
}

void FileAccess::work(std::uint64_t logic, quint64 request, std::function<FileOutcome()> job)
{
    m_pool.start([shared = m_shared, logic, request, job = std::move(job)] {
        FileOutcome outcome = job();
        shared->post([logic, request, outcome = std::move(outcome)](FileAccess& self) {
            self.settle(logic, request, outcome);
        });
    });
}

void FileAccess::settle(std::uint64_t logic, quint64 request, const FileOutcome& outcome) const
{
    if (outcome.problem.isEmpty()) {
        answer(logic, request, outcome.json);
    } else {
        refuse(logic, request, outcome.problem);
    }
}

QString FileAccess::openHandle(const LogicCaller& caller, bool folder, const QString& name,
                               const std::filesystem::path& path)
{
    const QString handle = (folder ? QStringLiteral("d%1") : QStringLiteral("f%1")).arg(++m_lastHandle);
    {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->handles.insert_or_assign(
            handle, Shared::Handle{.logic = caller.logic, .owner = ownerOf(caller), .folder = folder, .name = name, .path = path});
    }
    return compactJson(QJsonObject{{QStringLiteral("handle"), handle}, {QStringLiteral("name"), name}});
}

bool FileAccess::allowed(const Prompt& prompt) const
{
    if (prompt.caller.extension.isEmpty()) {
        return true;
    }
    return prompt.kind == PromptKind::Folder ? m_shared->declaredFolder(prompt.caller.extension, prompt.folder).has_value()
                                             : m_shared->mayPick(prompt.caller.extension);
}

void FileAccess::enqueue(Prompt prompt)
{
    m_prompts.push_back(std::move(prompt));
    if (m_prompts.size() == 1) {
        showNextPrompt();
    }
}

void FileAccess::showNextPrompt()
{
    if (m_chooser && !m_prompts.empty()) {
        const QString ticket = ticketOf(m_prompts.front());
        const QString chosen = m_chooser(prompt());
        if (chosen.isEmpty()) {
            cancelPrompt(ticket);
        } else {
            choose(ticket, chosen);
        }
        return;
    }
    emit promptChanged();
    if (!m_prompts.empty()) {
        emit attentionWanted();
    }
}

QString FileAccess::requesterOf(const LogicCaller& caller) const
{
    if (caller.extension.isEmpty()) {
        return {};
    }
    const auto found = m_extensionNames.find(caller.extension);
    return found != m_extensionNames.end() && !found->second.isEmpty() ? found->second : caller.extension;
}

void FileAccess::loadFolders()
{
    if (m_foldersFile.empty()) {
        return;
    }
    std::string contents;
    const std::error_code error = platform::readFile(m_foldersFile, kMostFoldersBytes, contents);
    if (error) {
        if (!platform::fileMissing(error)) {
            log::warning("files", "the folders chosen for logic could not be read (error {})", error.value());
        }
        return;
    }
    QJsonParseError problem;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(contents), &problem);
    const QJsonObject root = document.object();
    if (problem.error != QJsonParseError::NoError || !document.isObject() ||
        root.value(u"format").toString() != fromView(kFoldersFormat) || root.value(u"version").toInt() != kFoldersVersion) {
        log::warning("files", "the folders chosen for logic are in a file GameScrcpy cannot read; starting with none");
        return;
    }
    const QJsonObject owners = root.value(u"owners").toObject();
    for (auto owner = owners.begin(); owner != owners.end(); ++owner) {
        if (!validOwner(owner.key()) || !owner.value().isObject()) {
            continue;
        }
        const QJsonObject names = owner.value().toObject();
        for (auto entry = names.begin(); entry != names.end(); ++entry) {
            const fs::path path = pathOf(entry.value().toString());
            if (!validFolderName(entry.key()) || !entry.value().isString() || !path.is_absolute()) {
                continue;
            }
            m_folders[owner.key()].insert_or_assign(entry.key(), path);
        }
    }
}

void FileAccess::saveFolders() const
{
    if (m_foldersFile.empty()) {
        return;
    }
    QJsonObject owners;
    for (const auto& [owner, names] : m_folders) {
        QJsonObject entries;
        for (const auto& [name, path] : names) {
            entries.insert(name, pathText(path));
        }
        owners.insert(owner, entries);
    }
    const QJsonObject root{{QStringLiteral("format"), fromView(kFoldersFormat)},
                           {QStringLiteral("version"), kFoldersVersion},
                           {QStringLiteral("owners"), owners}};
    std::error_code error;
    std::filesystem::create_directories(m_foldersFile.parent_path(), error);
    error = platform::writeFileAtomically(m_foldersFile, QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString());
    if (error) {
        log::warning("files", "the folders chosen for logic could not be saved ({}error {})",
                     platform::fileBusy(error) ? "another program is using the file, " : "", error.value());
    }
}

void FileAccess::answer(std::uint64_t logic, quint64 request, const QString& json) const
{
    if (m_replies.answer) {
        m_replies.answer(logic, request, json);
    }
}

void FileAccess::refuse(std::uint64_t logic, quint64 request, const QString& message) const
{
    if (m_replies.refuse) {
        m_replies.refuse(logic, request, message);
    }
}

}
