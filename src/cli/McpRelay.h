#pragma once

#include <string_view>
#include <vector>

namespace gs::cli {

[[nodiscard]] int runMcpRelay(const std::vector<std::wstring_view>& args);

}
