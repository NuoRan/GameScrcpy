#pragma once

#include <optional>

namespace gs::cli {

[[nodiscard]] std::optional<int> runKeyCommand(int argc, wchar_t** argv);

}
