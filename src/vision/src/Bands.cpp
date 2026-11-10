#include "Bands.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace gs::vision::detail {

namespace {

constexpr int kMostWorkers = 4;

class Pool
{
public:
    Pool()
    {
        const unsigned cores = std::thread::hardware_concurrency();
        const int half = cores > 2u ? static_cast<int>(cores) / 2 - 1 : 0;
        const int count = std::clamp(half, 0, kMostWorkers);
        m_threads.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            m_threads.emplace_back([this] { work(); });
        }
    }

    ~Pool()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_quit = true;
        }
        m_wake.notify_all();
        for (std::thread& thread : m_threads) {
            thread.join();
        }
    }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    [[nodiscard]] int workers() const noexcept { return static_cast<int>(m_threads.size()); }

    void run(int bands, const std::function<void(int)>& job)
    {
        if (m_threads.empty() || bands <= 1) {
            for (int index = 0; index < bands; ++index) {
                job(index);
            }
            return;
        }
        const std::lock_guard serial(m_serial);
        {
            const std::lock_guard lock(m_mutex);
            m_job = &job;
            m_count = bands;
            m_next.store(0, std::memory_order_relaxed);
            m_running.store(workers(), std::memory_order_relaxed);
            ++m_generation;
        }
        m_wake.notify_all();
        for (int index = m_next.fetch_add(1, std::memory_order_relaxed); index < bands;
             index = m_next.fetch_add(1, std::memory_order_relaxed)) {
            job(index);
        }
        std::unique_lock lock(m_mutex);
        m_done.wait(lock, [this] { return m_running.load(std::memory_order_acquire) == 0; });
        m_job = nullptr;
    }

private:
    void work()
    {
        std::uint64_t seen = 0;
        for (;;) {
            const std::function<void(int)>* job = nullptr;
            int count = 0;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this, &seen] { return m_quit || m_generation != seen; });
                if (m_quit) {
                    return;
                }
                seen = m_generation;
                job = m_job;
                count = m_count;
            }
            for (int index = m_next.fetch_add(1, std::memory_order_relaxed); index < count;
                 index = m_next.fetch_add(1, std::memory_order_relaxed)) {
                (*job)(index);
            }
            if (m_running.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                const std::lock_guard lock(m_mutex);
                m_done.notify_all();
            }
        }
    }

    std::vector<std::thread> m_threads;
    std::mutex m_serial;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_done;
    const std::function<void(int)>* m_job = nullptr;
    int m_count = 0;
    std::atomic<int> m_next{0};
    std::atomic<int> m_running{0};
    std::uint64_t m_generation = 0;
    bool m_quit = false;
};

Pool& pool()
{
    static Pool shared;
    return shared;
}

}

int bandRows(int rows, int leastRows) noexcept
{
    if (rows <= 0 || leastRows <= 0 || rows < 2 * leastRows) {
        return 1;
    }
    return std::min(1 + pool().workers(), rows / leastRows);
}

void runBands(int bands, const std::function<void(int)>& job)
{
    if (bands <= 1) {
        job(0);
        return;
    }
    pool().run(bands, job);
}

}
