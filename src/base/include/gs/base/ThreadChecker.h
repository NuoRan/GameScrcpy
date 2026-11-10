#pragma once

#include <atomic>
#include <thread>

namespace gs {

class ThreadChecker
{
public:
    ThreadChecker() noexcept
        : m_owner(std::this_thread::get_id())
    {
    }

    ThreadChecker(const ThreadChecker&) = delete;
    ThreadChecker& operator=(const ThreadChecker&) = delete;

    [[nodiscard]] bool isCurrent() const noexcept
    {
        const std::thread::id current = std::this_thread::get_id();
        std::thread::id owner = m_owner.load(std::memory_order_acquire);
        if (owner == std::thread::id{} &&
            m_owner.compare_exchange_strong(owner, current, std::memory_order_acq_rel)) {
            return true;
        }
        return owner == current;
    }

    void detach() noexcept { m_owner.store(std::thread::id{}, std::memory_order_release); }

private:
    mutable std::atomic<std::thread::id> m_owner;
};

namespace detail {

using ThreadCheckFailureHandler = void (*)(const char* file, int line) noexcept;

void threadCheckFailed(const char* file, int line) noexcept;

ThreadCheckFailureHandler setThreadCheckFailureHandler(ThreadCheckFailureHandler handler) noexcept;

}

}

#if defined(NDEBUG)
#define GS_ASSERT_ON_THREAD(checker) static_cast<void>(sizeof(checker))
#else
#define GS_ASSERT_ON_THREAD(checker)                                                               \
    ((checker).isCurrent() ? static_cast<void>(0)                                                  \
                           : ::gs::detail::threadCheckFailed(__FILE__, __LINE__))
#endif
