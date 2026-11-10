#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::cli {

[[nodiscard]] int newScriptProject(const std::filesystem::path& folder, const std::optional<std::filesystem::path>& profile);
[[nodiscard]] int checkScriptProject(const std::filesystem::path& folder, bool json, const std::string& locale);
[[nodiscard]] int writeProjectTypes(const std::filesystem::path& folder);
[[nodiscard]] int runReplayCommand(const std::vector<std::wstring_view>& args);
[[nodiscard]] int runTestCommand(const std::vector<std::wstring_view>& args);
[[nodiscard]] std::optional<int> runScriptCommand(int argc, wchar_t** argv);

}
