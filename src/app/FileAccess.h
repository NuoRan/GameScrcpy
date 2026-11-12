#pragma once

#include "LogicServices.h"

#include <gs/script/Services.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

class QJSEngine;
class QJsonObject;
class QQmlEngine;

namespace gs::app {

class ExtensionHost;

struct FileReplies {
    std::function<void(std::uint64_t logic, quint64 request, const QString& json)> answer;
    std::function<void(std::uint64_t logic, quint64 request, const QString& message)> refuse;
};

struct DeclaredFolder {
    QString name;
    QString purpose;
};

struct FileOutcome {
    QString json;
    QString problem;
};

class FileAccess final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap prompt READ prompt NOTIFY promptChanged FINAL)
    Q_PROPERTY(QVariantMap folders READ folderMap NOTIFY foldersChanged FINAL)

public:
    using Chooser = std::function<QString(const QVariantMap& prompt)>;

    static constexpr std::string_view kOwnLogic = "mine";
    static constexpr qsizetype kMostFileBytes = qsizetype{64} << 20;
    static constexpr qsizetype kChunkBytes = qsizetype{4} << 20;
    static constexpr std::size_t kMostHandles = 64;
    static constexpr std::size_t kMostWaitingPrompts = 8;
    static constexpr std::size_t kMostEntries = 4096;

    explicit FileAccess(std::filesystem::path foldersFile, QObject* parent = nullptr);
    ~FileAccess() override;
    FileAccess(const FileAccess&) = delete;
    FileAccess& operator=(const FileAccess&) = delete;

    static FileAccess* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static FileAccess* instance() noexcept { return s_instance; }

    void offerServices();
    void setReplies(FileReplies replies);
    void setPermitted(const QStringList& pickers, std::map<QString, std::vector<DeclaredFolder>> folders);
    void setExtensionNames(std::map<QString, QString> names);
    void setChooser(Chooser chooser);
    void setLimits(qsizetype mostFileBytes, qsizetype chunkBytes);
    void follow(ExtensionHost& extensions);

    void request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json);
    [[nodiscard]] script::ServiceAnswer call(const LogicCaller& caller, std::string_view service,
                                             std::string_view json);
    void drop(const LogicCaller& caller, quint64 id);
    void endLogic(std::uint64_t logic);

    [[nodiscard]] QVariantMap prompt() const;
    [[nodiscard]] QVariantMap folderMap() const;
    [[nodiscard]] std::optional<std::filesystem::path> folderOf(const QString& owner, const QString& name) const;
    [[nodiscard]] std::size_t handleCount() const;

    Q_INVOKABLE void choose(const QString& ticket, const QString& fileOrUrl);
    Q_INVOKABLE void cancelPrompt(const QString& ticket);
    Q_INVOKABLE bool forgetFolder(const QString& owner, const QString& name);

signals:
    void promptChanged();
    void foldersChanged();
    void attentionWanted();

private:
    struct Shared;
    enum class PromptKind : std::uint8_t { Open, Save, Folder };

    struct Prompt {
        LogicCaller caller;
        quint64 request = 0;
        PromptKind kind = PromptKind::Open;
        QStringList filters;
        QString suggestedName;
        QString folder;
        QString purpose;
    };

    [[nodiscard]] static script::ServiceAnswer callWith(const std::shared_ptr<Shared>& shared,
                                                        const LogicCaller& caller, std::string_view service,
                                                        std::string_view json);
    [[nodiscard]] static QString ticketOf(const Prompt& prompt);

    void pick(const LogicCaller& caller, quint64 id, const QJsonObject& body);
    void openFolder(const LogicCaller& caller, quint64 id, const QJsonObject& body);
    void readFrom(const LogicCaller& caller, quint64 id, const QJsonObject& body, bool folder);
    void writeTo(const LogicCaller& caller, quint64 id, const QJsonObject& body, bool folder);
    void listIn(const LogicCaller& caller, quint64 id, const QJsonObject& body);
    void removeIn(const LogicCaller& caller, quint64 id, const QJsonObject& body);
    void work(std::uint64_t logic, quint64 request, std::function<FileOutcome()> job);
    void settle(std::uint64_t logic, quint64 request, const FileOutcome& outcome) const;
    [[nodiscard]] QString openHandle(const LogicCaller& caller, bool folder, const QString& name,
                                     const std::filesystem::path& path);
    [[nodiscard]] std::size_t handlesOf(std::uint64_t logic) const;
    [[nodiscard]] bool allowed(const Prompt& prompt) const;
    void enqueue(Prompt prompt);
    void showNextPrompt();
    [[nodiscard]] QString requesterOf(const LogicCaller& caller) const;
    void loadFolders();
    void saveFolders() const;
    void answer(std::uint64_t logic, quint64 request, const QString& json) const;
    void refuse(std::uint64_t logic, quint64 request, const QString& message) const;

    static inline FileAccess* s_instance = nullptr;

    std::shared_ptr<Shared> m_shared;
    std::filesystem::path m_foldersFile;
    FileReplies m_replies;
    Chooser m_chooser;
    std::map<QString, QString> m_extensionNames;
    std::map<QString, std::map<QString, std::filesystem::path>> m_folders;
    std::deque<Prompt> m_prompts;
    QThreadPool m_pool;
    std::uint64_t m_lastHandle = 0;
    bool m_offered = false;
};

}
