#pragma once

#include <cstdint>

namespace gs::stability {

struct ProcessSample {
    std::uint64_t handles = 0;
    std::uint64_t threads = 0;
    std::uint64_t privateBytes = 0;
    std::uint64_t workingSet = 0;
};

[[nodiscard]] ProcessSample sampleProcess();

}
