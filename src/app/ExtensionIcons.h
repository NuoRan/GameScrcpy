#pragma once

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace gs::app {

inline constexpr std::size_t kMostIconBytes = 32 * 1024;
inline constexpr std::size_t kMostIconsPerExtension = 64;

[[nodiscard]] QString iconPathOfSvg(const QByteArray& svg);
[[nodiscard]] QString extensionIconPath(const std::filesystem::path& folder, std::string_view file);
[[nodiscard]] bool builtinIconName(std::string_view name) noexcept;

}
