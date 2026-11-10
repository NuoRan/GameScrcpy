#pragma once

#include "gs/script/Report.h"

#include <string_view>

namespace gs::script::detail {

[[nodiscard]] SourceLocation locationFromStack(std::string_view stack);

}
