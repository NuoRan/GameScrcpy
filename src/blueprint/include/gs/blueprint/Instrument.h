#pragma once

#include "gs/blueprint/Environment.h"

#include <string>
#include <string_view>
#include <vector>

namespace gs::blueprint {

struct Probe {
    std::string position;
    std::string kind;
    bool async = false;
};

struct Instrumented {
    std::string javascript;
    std::vector<Probe> probes;
    std::vector<std::string> problems;
};

[[nodiscard]] Instrumented instrument(std::string_view source, const Environment& environment);

}
