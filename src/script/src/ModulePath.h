#pragma once

#include <gs/base/Expected.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace gs::script::detail {

[[nodiscard]] std::filesystem::path pathOf(std::string_view utf8);
[[nodiscard]] std::optional<std::string> moduleNameProblem(std::string_view name);
[[nodiscard]] bool isTypeScriptModule(std::string_view name);
[[nodiscard]] Expected<std::string, std::string> resolveImport(std::string_view importer,
                                                               std::string_view specifier);
[[nodiscard]] Expected<std::filesystem::path, std::string>
moduleFile(const std::filesystem::path& folder, std::string_view name);

}
