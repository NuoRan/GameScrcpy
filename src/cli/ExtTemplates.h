#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace gs::cli {

[[nodiscard]] int initExtension(std::string_view kind, const std::filesystem::path& folder,
                                const std::optional<std::string>& id);

}
