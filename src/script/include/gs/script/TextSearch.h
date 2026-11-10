#pragma once

#include "gs/script/Services.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gs::platform {
struct TextResult;
}

namespace gs::script {

inline constexpr std::size_t kMostFindTextChars = 200;
inline constexpr std::size_t kMostLanguageChars = 35;

struct GreyArea {
    const std::uint8_t* plane = nullptr;
    std::ptrdiff_t stride = 0;
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
};

[[nodiscard]] VisionAnswer answerText(const TextQuery& query, const GreyArea& area, PictureSize picture);
[[nodiscard]] VisionAnswer textAnswer(const TextQuery& query, const platform::TextResult& read, int left, int top,
                                      PictureSize picture);
[[nodiscard]] std::string foldedText(std::string_view text);
[[nodiscard]] std::string_view preferredLanguage(const TextQuery& query) noexcept;

}
