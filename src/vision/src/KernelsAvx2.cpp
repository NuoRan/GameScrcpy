#include "Kernels.h"

#include <immintrin.h>

namespace gs::vision::detail::avx2 {

namespace {

constexpr int kLaneSums = 2048;

}

void halve(const std::uint8_t* top, const std::uint8_t* bottom, std::uint8_t* out, int width) noexcept
{
    const __m256i ones = _mm256_set1_epi8(1);
    const __m256i two = _mm256_set1_epi16(2);
    int x = 0;
    for (; x + 32 <= width; x += 32) {
        const auto* upper = reinterpret_cast<const __m256i*>(top + 2 * x);
        const auto* lower = reinterpret_cast<const __m256i*>(bottom + 2 * x);
        const __m256i first = _mm256_add_epi16(_mm256_maddubs_epi16(_mm256_loadu_si256(upper), ones),
                                               _mm256_maddubs_epi16(_mm256_loadu_si256(lower), ones));
        const __m256i second = _mm256_add_epi16(_mm256_maddubs_epi16(_mm256_loadu_si256(upper + 1), ones),
                                                _mm256_maddubs_epi16(_mm256_loadu_si256(lower + 1), ones));
        const __m256i packed = _mm256_packus_epi16(_mm256_srli_epi16(_mm256_add_epi16(first, two), 2),
                                                   _mm256_srli_epi16(_mm256_add_epi16(second, two), 2));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + x), _mm256_permute4x64_epi64(packed, 0xD8));
    }
    for (; x < width; ++x) {
        const int sum = top[2 * x] + top[2 * x + 1] + bottom[2 * x] + bottom[2 * x + 1];
        out[x] = static_cast<std::uint8_t>((sum + 2) >> 2);
    }
}

void correlate(const Correlation& job) noexcept
{
    const int chunks = job.patternStride / kPatternAlignment;
    const int flushRows = chunks >= kLaneSums ? 1 : kLaneSums / chunks;
    for (int y = 0; y < job.rows; ++y) {
        std::int64_t* sums = job.sums + y * job.sumStride;
        for (int x = 0; x < job.columns; x += kPositionGroup) {
            std::int64_t totals[kPositionGroup] = {0, 0, 0, 0};
            for (int first = 0; first < job.patternHeight; first += flushRows) {
                const int last = job.patternHeight - first > flushRows ? first + flushRows : job.patternHeight;
                __m256i a0 = _mm256_setzero_si256();
                __m256i a1 = _mm256_setzero_si256();
                __m256i a2 = _mm256_setzero_si256();
                __m256i a3 = _mm256_setzero_si256();
                for (int j = first; j < last; ++j) {
                    const std::int16_t* image = job.image + (y + j) * job.imageStride + x;
                    const std::int16_t* pattern = job.pattern + static_cast<std::ptrdiff_t>(j) * job.patternStride;
                    for (int c = 0; c < job.patternStride; c += kPatternAlignment) {
                        const __m256i p = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pattern + c));
                        const std::int16_t* at = image + c;
                        a0 = _mm256_add_epi32(
                            a0, _mm256_madd_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(at)), p));
                        a1 = _mm256_add_epi32(
                            a1, _mm256_madd_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(at + 1)), p));
                        a2 = _mm256_add_epi32(
                            a2, _mm256_madd_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(at + 2)), p));
                        a3 = _mm256_add_epi32(
                            a3, _mm256_madd_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(at + 3)), p));
                    }
                }
                const __m256i halves = _mm256_hadd_epi32(_mm256_hadd_epi32(a0, a1), _mm256_hadd_epi32(a2, a3));
                const __m128i group =
                    _mm_add_epi32(_mm256_castsi256_si128(halves), _mm256_extracti128_si256(halves, 1));
                totals[0] += _mm_cvtsi128_si32(group);
                totals[1] += _mm_extract_epi32(group, 1);
                totals[2] += _mm_extract_epi32(group, 2);
                totals[3] += _mm_extract_epi32(group, 3);
            }
            const int count = job.columns - x < kPositionGroup ? job.columns - x : kPositionGroup;
            for (int k = 0; k < count; ++k) {
                sums[x + k] = totals[k];
            }
        }
    }
}

void accumulateColumns(const ColumnWindow& job) noexcept
{
    int c = 0;
    for (; c + 32 <= job.columns; c += 32) {
        __m256i sums0 = _mm256_setzero_si256();
        __m256i sums1 = _mm256_setzero_si256();
        __m256i sums2 = _mm256_setzero_si256();
        __m256i sums3 = _mm256_setzero_si256();
        __m256i squares0 = _mm256_setzero_si256();
        __m256i squares1 = _mm256_setzero_si256();
        __m256i squares2 = _mm256_setzero_si256();
        __m256i squares3 = _mm256_setzero_si256();
        for (int r = 0; r < job.rows; ++r) {
            const std::uint8_t* row = job.pixels + r * job.stride + c;
            const __m256i first =
                _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(row)));
            const __m256i second =
                _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(row + 8)));
            const __m256i third =
                _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(row + 16)));
            const __m256i fourth =
                _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(row + 24)));
            sums0 = _mm256_add_epi32(sums0, first);
            sums1 = _mm256_add_epi32(sums1, second);
            sums2 = _mm256_add_epi32(sums2, third);
            sums3 = _mm256_add_epi32(sums3, fourth);
            squares0 = _mm256_add_epi32(squares0, _mm256_mullo_epi32(first, first));
            squares1 = _mm256_add_epi32(squares1, _mm256_mullo_epi32(second, second));
            squares2 = _mm256_add_epi32(squares2, _mm256_mullo_epi32(third, third));
            squares3 = _mm256_add_epi32(squares3, _mm256_mullo_epi32(fourth, fourth));
        }
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.sums + c), sums0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.sums + c + 8), sums1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.sums + c + 16), sums2);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.sums + c + 24), sums3);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.squares + c), squares0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.squares + c + 8), squares1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.squares + c + 16), squares2);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.squares + c + 24), squares3);
    }
    for (; c + 16 <= job.columns; c += 16) {
        __m256i sums0 = _mm256_setzero_si256();
        __m256i sums1 = _mm256_setzero_si256();
        __m256i squares0 = _mm256_setzero_si256();
        __m256i squares1 = _mm256_setzero_si256();
        for (int r = 0; r < job.rows; ++r) {
            const std::uint8_t* row = job.pixels + r * job.stride + c;
            const __m256i first =
                _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(row)));
            const __m256i second =
                _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(row + 8)));
            sums0 = _mm256_add_epi32(sums0, first);
            sums1 = _mm256_add_epi32(sums1, second);
            squares0 = _mm256_add_epi32(squares0, _mm256_mullo_epi32(first, first));
            squares1 = _mm256_add_epi32(squares1, _mm256_mullo_epi32(second, second));
        }
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.sums + c), sums0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.sums + c + 8), sums1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.squares + c), squares0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.squares + c + 8), squares1);
    }
    for (; c + 8 <= job.columns; c += 8) {
        __m256i sums0 = _mm256_setzero_si256();
        __m256i squares0 = _mm256_setzero_si256();
        for (int r = 0; r < job.rows; ++r) {
            const std::uint8_t* row = job.pixels + r * job.stride + c;
            const __m256i values =
                _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(row)));
            sums0 = _mm256_add_epi32(sums0, values);
            squares0 = _mm256_add_epi32(squares0, _mm256_mullo_epi32(values, values));
        }
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.sums + c), sums0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(job.squares + c), squares0);
    }
    for (; c < job.columns; ++c) {
        std::uint32_t sum = 0;
        std::uint32_t squares = 0;
        for (int r = 0; r < job.rows; ++r) {
            const std::uint32_t value = job.pixels[r * job.stride + c];
            sum += value;
            squares += value * value;
        }
        job.sums[c] = sum;
        job.squares[c] = squares;
    }
}

}
