#pragma once

#include <QColor>
#include <QString>
#include <QStringView>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <array>
#include <cstdint>
#include <vector>

class QTextDocument;

namespace gs::app {

enum class ScriptToken : std::uint8_t { Keyword, String, Number, Comment, Type };

inline constexpr std::size_t kScriptTokenKinds = 5;

inline constexpr int kScriptPlain = 0;
inline constexpr int kScriptInComment = 1;
inline constexpr int kScriptInTemplate = 2;

struct ScriptSpan {
    int start = 0;
    int length = 0;
    ScriptToken token = ScriptToken::Keyword;

    friend bool operator==(const ScriptSpan&, const ScriptSpan&) = default;
};

struct ScriptLine {
    std::vector<ScriptSpan> spans;
    int state = kScriptPlain;
    bool open = false;
};

[[nodiscard]] bool identifierStart(QChar c) noexcept;
[[nodiscard]] bool identifierPart(QChar c) noexcept;
[[nodiscard]] ScriptLine scriptLine(QStringView text, int state);
[[nodiscard]] bool insideLiteral(QStringView text, int state, int column);

class ScriptHighlighter final : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit ScriptHighlighter(QTextDocument* document);

    void setColors(const std::array<QColor, kScriptTokenKinds>& colors);

protected:
    void highlightBlock(const QString& text) override;

private:
    std::array<QTextCharFormat, kScriptTokenKinds> m_formats;
};

}
