#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace gs::ext {

inline constexpr std::size_t kMostSchemaBytes = 16 * 1024;
inline constexpr std::size_t kMostSchemaDepth = 16;
inline constexpr std::size_t kMostValueDepth = 64;

[[nodiscard]] std::string schemaProblem(std::string_view schema);
[[nodiscard]] std::string valueProblem(std::string_view schema, std::string_view value);

}
