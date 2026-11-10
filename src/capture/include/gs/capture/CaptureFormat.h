#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gs::capture {

enum class Subtype : std::uint8_t { Nv12, Yuy2, Mjpg, H264, Other };

struct CaptureFormat {
    int width = 0;
    int height = 0;
    std::uint32_t rateNumerator = 0;
    std::uint32_t rateDenominator = 1;
    Subtype subtype = Subtype::Other;
    std::uint32_t index = 0;

    [[nodiscard]] double framesPerSecond() const noexcept;

    friend bool operator==(const CaptureFormat&, const CaptureFormat&) = default;
};

[[nodiscard]] std::string_view subtypeName(Subtype subtype) noexcept;
[[nodiscard]] std::optional<Subtype> subtypeOfName(std::string_view name) noexcept;
[[nodiscard]] bool decodable(Subtype subtype) noexcept;
[[nodiscard]] std::string formatName(const CaptureFormat& format);
[[nodiscard]] std::optional<CaptureFormat> parseFormatName(std::string_view name);
[[nodiscard]] bool sameMode(const CaptureFormat& a, const CaptureFormat& b) noexcept;
[[nodiscard]] std::optional<CaptureFormat> chooseFormat(std::span<const CaptureFormat> formats,
                                                        std::string_view manual = {});

}
