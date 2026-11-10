#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::app {

inline constexpr std::size_t kMostLogicFileBytes = std::size_t{2} << 20;
inline constexpr int kMostShownReferences = 1000;
inline constexpr int kMostListedReferences = 1000;

enum class ReferenceKind : std::uint8_t { String, Name };

struct ReferenceQuery {
    std::string text;
    bool strings = true;
    bool names = true;
};

struct LogicReference {
    QString file;
    ReferenceKind kind = ReferenceKind::String;
    int line = 0;
    int column = 0;
    int length = 0;
    std::size_t start = 0;
    std::size_t end = 0;
    QString lineText;
};

struct ReferenceSearch {
    std::vector<LogicReference> found;
    QStringList unread;
};

struct ReferenceTarget {
    QString text;
    ReferenceKind kind = ReferenceKind::Name;
};

struct LiteralRename {
    std::string text;
    int changed = 0;
};

struct ControlUse {
    QString file;
    int line = 0;
    int column = 0;
    int length = 0;
    std::size_t start = 0;
    std::size_t end = 0;
    std::size_t useStart = 0;
    std::size_t useEnd = 0;
    QString lineText;
};

struct ControlUseSearch {
    std::vector<ControlUse> found;
    QStringList unread;
};

struct ControlRewrite {
    std::string text;
    int changed = 0;
};

using OpenTexts = std::map<QString, QByteArray>;

[[nodiscard]] std::optional<std::string> literalValue(std::string_view literal);
[[nodiscard]] std::string quotedLiteral(std::string_view value, char quote);
[[nodiscard]] QStringList logicFilesIn(const std::filesystem::path& folder);
[[nodiscard]] std::vector<LogicReference> referencesIn(std::string_view source, const QString& file,
                                                       const ReferenceQuery& query);
[[nodiscard]] ReferenceSearch findLogicReferences(const std::filesystem::path& folder, const ReferenceQuery& query,
                                                  const OpenTexts& open = {});
[[nodiscard]] std::optional<ReferenceTarget> referenceTargetAt(const QString& text, int position);
[[nodiscard]] LiteralRename renameLiterals(std::string_view source, std::string_view from, std::string_view to);
[[nodiscard]] std::vector<ControlUse> controlUsesIn(std::string_view source, const QString& file,
                                                    std::string_view id);
[[nodiscard]] ControlUseSearch findControlUses(const std::filesystem::path& folder, std::string_view id,
                                                const OpenTexts& open = {});
[[nodiscard]] ControlRewrite renameControlUses(std::string_view source, std::string_view from, std::string_view to);
[[nodiscard]] ControlRewrite removeControlUses(std::string_view source, std::string_view id);
[[nodiscard]] bool listedPath(const QStringList& paths, const QString& path);

}
