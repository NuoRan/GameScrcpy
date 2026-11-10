#pragma once

#include <gs/base/Expected.h>
#include <gs/script/Services.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::cli {

inline constexpr int kSuccess = 0;
inline constexpr int kProblems = 1;
inline constexpr int kUsage = 2;

inline constexpr std::string_view kProfileFile = "profile.json";
inline constexpr std::string_view kApiTypesFile = "types/gamescrcpy.d.ts";
inline constexpr std::string_view kProfileTypesFile = "types/profile.d.ts";
inline constexpr std::string_view kExtensionTypesFile = "types/extensions.d.ts";

struct ProjectProfile {
    std::vector<script::ControlInfo> controls;
    std::vector<script::StateInfo> states;
};

[[nodiscard]] std::string utf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path pathOf(std::string_view text);
[[nodiscard]] std::optional<std::string> readText(const std::filesystem::path& file);
[[nodiscard]] bool writeText(const std::filesystem::path& file, std::string_view contents);
[[nodiscard]] bool isProfileFolder(const std::filesystem::path& folder);
[[nodiscard]] std::optional<std::string> entryOf(const std::filesystem::path& folder);
[[nodiscard]] Expected<ProjectProfile, std::vector<std::string>> loadProjectProfile(const std::filesystem::path& file);
[[nodiscard]] std::string apiDeclarations();

}
