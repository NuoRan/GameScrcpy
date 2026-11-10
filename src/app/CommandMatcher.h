#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

#include <span>
#include <vector>

namespace gs::app {

struct TextRange {
    int start = 0;
    int length = 0;

    friend bool operator==(const TextRange&, const TextRange&) = default;
};

struct CommandText {
    QString title;
    QString sourceTitle;
    QStringList keywords;
    QString id;
    QStringList groupTitles;
};

struct CommandMatch {
    bool matched = false;
    int score = 0;
    std::vector<TextRange> highlights;
};

class CommandMatcher
{
public:
    explicit CommandMatcher(QStringView query);

    [[nodiscard]] bool isEmpty() const noexcept { return m_terms.isEmpty(); }
    [[nodiscard]] CommandMatch match(const CommandText& text) const;
    [[nodiscard]] CommandMatch match(std::span<const CommandText> texts) const;
    [[nodiscard]] std::vector<TextRange> highlights(QStringView title) const;

    [[nodiscard]] static bool ranksBefore(const CommandMatch& first, const CommandText& firstText,
                                          const CommandMatch& second,
                                          const CommandText& secondText);

private:
    [[nodiscard]] int score(const CommandText& text) const;

    QStringList m_terms;
};

}
