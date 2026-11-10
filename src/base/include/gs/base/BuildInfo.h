#pragma once

#include <string_view>

namespace gs {

struct BuildInfo {
    std::string_view version;
    std::string_view revision;
};

[[nodiscard]] BuildInfo buildInfo() noexcept;

}
