#pragma once

#include <QString>
#include <QStringView>
#include <QVariantList>

#include <cstddef>
#include <string_view>
#include <vector>

namespace gs::input {
struct Profile;
}

namespace gs::app {

struct ApiEntry {
    QString label;
    QString owner;
    QString name;
    QString kind;
    QString detail;
    QString documentation;
    QString example;
};

class ScriptApi
{
public:
    ScriptApi(std::string_view completions, std::string_view declarations);

    [[nodiscard]] static const ScriptApi& installed();
    [[nodiscard]] const std::vector<ApiEntry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] const ApiEntry* find(QStringView label) const;
    [[nodiscard]] QString documentationOf(QStringView label) const;
    [[nodiscard]] bool isNamespace(QStringView name) const;
    [[nodiscard]] QVariantList list() const;

private:
    std::vector<ApiEntry> m_entries;
};

struct ProfileName {
    QString id;
    QString detail;
    QString shape;
};

struct ProfileNames {
    std::vector<ProfileName> controls;
    std::vector<ProfileName> states;
};

[[nodiscard]] ProfileNames profileNamesOf(const input::Profile& profile);

struct CompletionItem {
    QString label;
    QString kind;
    QString detail;
    QString documentation;
    QString insert;
    int from = 0;
    int caret = 0;
};

inline constexpr std::size_t kMostCompletions = 50;

[[nodiscard]] std::vector<CompletionItem> completionsAt(const ScriptApi& api, const ProfileNames& profile,
                                                        QStringView line, int column, int state, bool asked);

}
