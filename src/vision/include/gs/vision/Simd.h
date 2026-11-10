#pragma once

#include <cstdint>

namespace gs::vision {

enum class Simd : std::uint8_t {
    Automatic,
    Scalar,
    Avx2,
};

[[nodiscard]] bool avx2Available() noexcept;

[[nodiscard]] Simd resolvedSimd(Simd requested) noexcept;

}
