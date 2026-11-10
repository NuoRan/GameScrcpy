#include "gs/vision/Simd.h"

#include "Kernels.h"

#include <immintrin.h>
#include <intrin.h>

namespace gs::vision {

namespace {

constexpr int kOsSavesState = 1 << 27;
constexpr int kAvx = 1 << 28;
constexpr int kAvx2 = 1 << 5;
constexpr unsigned long long kVectorState = 0x6;

bool detectAvx2() noexcept
{
    int registers[4] = {};
    __cpuid(registers, 0);
    if (registers[0] < 7) {
        return false;
    }
    __cpuid(registers, 1);
    if ((registers[2] & kOsSavesState) == 0 || (registers[2] & kAvx) == 0) {
        return false;
    }
    if ((_xgetbv(0) & kVectorState) != kVectorState) {
        return false;
    }
    __cpuidex(registers, 7, 0);
    return (registers[1] & kAvx2) != 0;
}

}

bool avx2Available() noexcept
{
    static const bool available = detectAvx2();
    return available;
}

Simd resolvedSimd(Simd requested) noexcept
{
    return requested != Simd::Scalar && avx2Available() ? Simd::Avx2 : Simd::Scalar;
}

namespace detail {

const Kernels& kernels(Simd simd) noexcept
{
    static constexpr Kernels kScalarKernels{&scalar::halve, &scalar::correlate, &scalar::accumulateColumns};
    static constexpr Kernels kAvx2Kernels{&avx2::halve, &avx2::correlate, &avx2::accumulateColumns};
    return resolvedSimd(simd) == Simd::Avx2 ? kAvx2Kernels : kScalarKernels;
}

}

}
