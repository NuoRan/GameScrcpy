#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace gs::platform {

inline constexpr std::string_view kCrashFolderPrefix = "crash-";
inline constexpr std::size_t kCrashFolderLength = 25;

[[nodiscard]] bool installCrashHandler(const std::filesystem::path& directory, const std::filesystem::path& log);

[[noreturn]] void crashOnPurpose() noexcept;

}
