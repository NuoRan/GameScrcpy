#pragma once

#include <gs/base/Expected.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gs::platform {

struct TextBox {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    friend bool operator==(const TextBox&, const TextBox&) = default;
};

struct TextWord {
    std::string text;
    TextBox box;

    friend bool operator==(const TextWord&, const TextWord&) = default;
};

struct TextLine {
    std::string text;
    TextBox box;
    std::vector<TextWord> words;

    friend bool operator==(const TextLine&, const TextLine&) = default;
};

struct TextResult {
    std::string language;
    std::vector<TextLine> lines;
};

enum class TextProblem : std::uint8_t { NoLanguage, Failed };

[[nodiscard]] Expected<TextResult, TextProblem> readText(const std::uint8_t* grey, int width, int height,
                                                         std::ptrdiff_t stride, std::string_view language);
[[nodiscard]] std::vector<std::string> textLanguages();
[[nodiscard]] bool joinsWithSpace(char32_t before, char32_t after) noexcept;

class TextLanguages
{
public:
    using Done = std::function<void(std::vector<std::string>)>;

    explicit TextLanguages(Done done);
    ~TextLanguages();
    TextLanguages(const TextLanguages&) = delete;
    TextLanguages& operator=(const TextLanguages&) = delete;

private:
    std::jthread m_thread;
};

}
