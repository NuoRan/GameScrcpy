#pragma once

#include "gs/script/ScriptHost.h"

#include <gs/base/Clock.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace gs::script::detail {

class LoadMeter
{
public:
    struct Verdict {
        bool spike = false;
        bool sustained = false;
        Microseconds busy{0};
    };

    explicit LoadMeter(const Limits& limits) noexcept;

    [[nodiscard]] Verdict record(Microseconds end, Microseconds duration) noexcept;

private:
    static constexpr std::size_t kBuckets = 16;

    struct Bucket {
        std::int64_t index = -1;
        Microseconds busy{0};
    };

    Microseconds m_spike;
    Microseconds m_window;
    Microseconds m_threshold;
    std::array<Bucket, kBuckets> m_buckets{};
    bool m_sustained = false;
};

}
