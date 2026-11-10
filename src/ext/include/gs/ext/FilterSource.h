#pragma once

#include "gs/ext/Diagnostics.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::ext {

enum class ParamType : std::uint8_t { Float, Int, Bool };
enum class PassFormat : std::uint8_t { Rgba8, Rgba16f };

inline constexpr std::size_t kMaxFilterSourceBytes = 1024 * 1024;
inline constexpr std::size_t kMaxFilterParams = 64;
inline constexpr std::size_t kMaxFilterPasses = 16;
inline constexpr std::size_t kMaxFilterTextures = 8;
inline constexpr double kMaxPassScale = 4.0;
inline constexpr std::array<std::string_view, 8> kFilterBuiltins{"Original",   "Source",       "Linear",     "Point",
                                                                 "SourceSize", "OriginalSize", "OutputSize", "Frame"};

struct FilterParam {
    std::string name;
    ParamType type = ParamType::Float;
    double defaultValue = 0.0;
    std::optional<double> min;
    std::optional<double> max;
    std::optional<double> step;
    std::size_t line = 0;

    friend bool operator==(const FilterParam&, const FilterParam&) = default;
};

struct FilterPass {
    std::string name;
    double scale = 1.0;
    PassFormat format = PassFormat::Rgba8;
    std::size_t line = 0;

    friend bool operator==(const FilterPass&, const FilterPass&) = default;
};

struct FilterTexture {
    std::string name;
    std::string file;
    std::size_t line = 0;

    friend bool operator==(const FilterTexture&, const FilterTexture&) = default;
};

struct FilterSource {
    std::vector<FilterParam> params;
    std::vector<FilterPass> passes;
    std::vector<FilterTexture> textures;
    std::string body;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool usable() const noexcept { return !hasErrors(diagnostics); }
};

[[nodiscard]] FilterSource parseFilterSource(std::string_view hlsl);
[[nodiscard]] std::size_t constantBufferSize(const std::vector<FilterParam>& params) noexcept;
[[nodiscard]] std::string passEntryPoint(const FilterPass& pass);
[[nodiscard]] std::string filterShaderText(const FilterSource& source, std::string_view fileName);

}
