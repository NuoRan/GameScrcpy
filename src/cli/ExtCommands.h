#pragma once

#include <string_view>
#include <vector>

namespace gs::cli {

[[nodiscard]] int runExtCommand(const std::vector<std::wstring_view>& args);

}
