#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <cstdint>
#include <optional>
#include <vector>

namespace gs::blueprint {
struct Graph;
struct GraphNode;
}

namespace gs::input {
struct Profile;
}

namespace gs::app {

inline constexpr qsizetype kDraftMostDiffLines = 400;
inline constexpr qsizetype kDraftDiffContext = 3;

struct DraftLine {
    enum class Kind : std::uint8_t { Context, Added, Removed };

    Kind kind = Kind::Context;
    QString text;

    friend bool operator==(const DraftLine&, const DraftLine&) = default;
};

struct DraftHunk {
    int beforeStart = 0;
    int beforeCount = 0;
    int afterStart = 0;
    int afterCount = 0;
    std::vector<DraftLine> lines;

    friend bool operator==(const DraftHunk&, const DraftHunk&) = default;
};

struct DraftTextDiff {
    std::vector<DraftHunk> hunks;
    int added = 0;
    int removed = 0;
    int leftOut = 0;
    bool binary = false;

    [[nodiscard]] bool empty() const noexcept { return hunks.empty() && !binary; }
    friend bool operator==(const DraftTextDiff&, const DraftTextDiff&) = default;
};

[[nodiscard]] QStringList draftLinesOf(const QString& text);
[[nodiscard]] DraftTextDiff draftTextDiff(const std::optional<QString>& before, const std::optional<QString>& after,
                                          qsizetype context = kDraftDiffContext,
                                          qsizetype mostLines = kDraftMostDiffLines);
[[nodiscard]] QVariantList draftDiffList(const DraftTextDiff& diff);

enum class DraftChange : std::uint8_t { None, Added, Changed, Removed };

[[nodiscard]] QString draftChangeName(DraftChange change);
[[nodiscard]] QHash<QString, QString> draftControlChanges(const input::Profile& live, const input::Profile& draft);

[[nodiscard]] QString draftNodeSignature(const blueprint::GraphNode& node);

struct DraftGraphChanges {
    QHash<QString, QString> marks;
    QVariantList removed;
    int added = 0;
    int changed = 0;
    int removedCount = 0;

    [[nodiscard]] bool empty() const noexcept { return added == 0 && changed == 0 && removedCount == 0; }
};

[[nodiscard]] DraftGraphChanges draftGraphChanges(const blueprint::Graph& live, const blueprint::Graph& draft);

}
