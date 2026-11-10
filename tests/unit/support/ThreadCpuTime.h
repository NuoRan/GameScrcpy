#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

#if defined(_WIN32)
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread();
extern "C" __declspec(dllimport) int __stdcall QueryThreadCycleTime(void* thread, unsigned long long* cycles);
#endif

namespace gs::testing {

class ThreadCpuTime
{
public:
    ThreadCpuTime() noexcept
    {
        for (int attempt = 0; attempt < 8; ++attempt) {
            const std::uint64_t before = cycles();
            const auto start = std::chrono::steady_clock::now();
            volatile std::uint64_t churn = static_cast<std::uint64_t>(attempt) + 1;
            auto stop = start;
            do {
                for (int pass = 0; pass < 512; ++pass) {
                    churn = churn * 6364136223846793005ULL + 1442695040888963407ULL;
                }
                stop = std::chrono::steady_clock::now();
            } while (stop - start < std::chrono::milliseconds(2));
            const std::uint64_t after = cycles();
            const double spentUs = std::chrono::duration<double, std::micro>(stop - start).count();
            if (before > 0 && after > before && spentUs > 0.0) {
                const double perUs = static_cast<double>(after - before) / spentUs;
                m_cyclesPerMicrosecond = std::max(m_cyclesPerMicrosecond, perUs);
            }
        }
    }

    [[nodiscard]] bool usable() const noexcept { return m_cyclesPerMicrosecond > 0.0; }

    [[nodiscard]] std::uint64_t now() const noexcept { return cycles(); }

    [[nodiscard]] double micros(std::uint64_t from, std::uint64_t to) const noexcept
    {
        if (m_cyclesPerMicrosecond <= 0.0 || to <= from) {
            return 0.0;
        }
        return static_cast<double>(to - from) / m_cyclesPerMicrosecond;
    }

    [[nodiscard]] double microsNow() const noexcept { return micros(0, cycles()); }

private:
    static std::uint64_t cycles() noexcept
    {
#if defined(_WIN32)
        unsigned long long counted = 0;
        if (::QueryThreadCycleTime(::GetCurrentThread(), &counted) == 0) {
            return 0;
        }
        return static_cast<std::uint64_t>(counted);
#else
        return 0;
#endif
    }

    double m_cyclesPerMicrosecond = 0.0;
};

}
