#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace gs::app {

class ComponentLibrary;
class ProfileLibrary;
class TaskLibrary;

struct McpDraftFile {
    QString path;
    QString state;
    int added = 0;
    int removed = 0;
    bool conflict = false;
};

struct McpDraftSetup {
    std::filesystem::path root;
    ProfileLibrary* profiles = nullptr;
    ComponentLibrary* components = nullptr;
    TaskLibrary* tasks = nullptr;
};

struct McpDraftWrite {
    QString path;
    QString text;
};

struct McpDraftResult {
    QString code;
    QString problem;
    QString target;
    QStringList written;
    QStringList warnings;
    QStringList conflicts;

    [[nodiscard]] bool ok() const noexcept { return code.isEmpty(); }
};

struct McpLineChange {
    qsizetype added = 0;
    qsizetype removed = 0;
};

struct McpDraftFacts {
    QString client;
    QString target;
};

struct McpDraftPreview {
    QString client;
    QString target;
    QString name;
    QString profileText;
    QString liveText;
    QString draftFolder;
    QString liveFolder;
    bool fresh = false;
};

[[nodiscard]] QStringList mcpLinesOf(const QString& text);
[[nodiscard]] McpLineChange mcpLineChange(const QString& before, const QString& after);
[[nodiscard]] QString mcpUnifiedDiff(const QString& path, const std::optional<QString>& before,
                                     const std::optional<QString>& after);
[[nodiscard]] QString mcpCheckedPath(const QString& given, QString& problem);
[[nodiscard]] QByteArray mcpEncodedLike(const QString& text, const QByteArray& existing);
[[nodiscard]] std::optional<QString> mcpUtf8(const QByteArray& bytes);
[[nodiscard]] QString mcpTargetOf(const QString& given);
[[nodiscard]] bool mcpSkippedName(const QString& name);

class McpDrafts final : public QObject
{
    Q_OBJECT

public:
    static constexpr qsizetype kMostWrittenFiles = 32;
    static constexpr qsizetype kMostFileBytes = qsizetype{2} << 20;
    static constexpr qsizetype kMostWrittenBytes = qsizetype{8} << 20;
    static constexpr int kMostCopiedFiles = 2000;
    static constexpr std::size_t kMostCopiedFileBytes = std::size_t{64} << 20;
    static constexpr std::uintmax_t kMostCopiedBytes = std::uintmax_t{256} << 20;
    static constexpr auto kKeptFor = std::chrono::hours(24 * 7);

    explicit McpDrafts(McpDraftSetup setup, QObject* parent = nullptr);
    ~McpDrafts() override;
    McpDrafts(const McpDrafts&) = delete;
    McpDrafts& operator=(const McpDrafts&) = delete;

    [[nodiscard]] QStringList targets(const QString& client) const;
    [[nodiscard]] std::vector<McpDraftFacts> pending() const;
    [[nodiscard]] std::optional<McpDraftPreview> preview(const QString& client, const QString& target) const;
    [[nodiscard]] std::vector<McpDraftFile> diff(const QString& client, const QString& target) const;
    [[nodiscard]] QString fileDiff(const QString& client, const QString& target, const QString& path) const;
    bool apply(const QString& client, const QString& target, QString& problem);
    void discard(const QString& client, const QString& target);

    [[nodiscard]] bool has(const QString& client, const QString& target) const;
    [[nodiscard]] std::filesystem::path folderOf(const QString& client, const QString& target) const;
    [[nodiscard]] std::filesystem::path clientFolder(const QString& client) const;
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return m_setup.root; }
    [[nodiscard]] QString nameOf(const QString& target) const;
    [[nodiscard]] bool convertsToFolder(const QString& client, const QString& target) const;
    [[nodiscard]] McpDraftResult problems(const QString& client, const QString& target) const;
    std::filesystem::path open(const QString& client, const QString& target, McpDraftResult& result);
    McpDraftResult write(const QString& client, const QString& target, const std::vector<McpDraftWrite>& files);
    McpDraftResult store(const QString& client, const QString& target, const QString& path, const QByteArray& bytes);
    McpDraftResult apply(const QString& client, const QString& target);
    void discardAll(const QString& client);
    void prune();

signals:
    void draftsChanged(const QString& client);

private:
    struct Meta {
        QString client;
        QString target;
        bool single = false;
        bool fresh = false;
        std::map<QString, QByteArray> copied;
    };

    struct Real {
        std::filesystem::path folder;
        std::filesystem::path file;
        QString name;
        bool fresh = false;
        bool readOnly = false;
    };

    [[nodiscard]] std::filesystem::path metaFile(const QString& client, const QString& target) const;
    [[nodiscard]] std::optional<Meta> readMeta(const QString& client, const QString& target) const;
    bool writeMeta(const Meta& meta) const;
    [[nodiscard]] std::optional<Real> real(const QString& target, McpDraftResult& result) const;
    [[nodiscard]] std::vector<std::pair<QString, std::filesystem::path>> realFiles(const Meta& meta) const;
    void reloadLibrary(const QString& target) const;

    McpDraftSetup m_setup;
};

}
