#include "gs/base/ThreadChecker.h"

#include <cstdio>
#include <cstdlib>

namespace gs::detail {

namespace {

std::atomic<ThreadCheckFailureHandler> g_handler{nullptr};

}

void threadCheckFailed(const char* file, int line) noexcept
{
    if (const ThreadCheckFailureHandler handler = g_handler.load(std::memory_order_acquire)) {
        handler(file, line);
        return;
    }
    std::fprintf(stderr, "%s(%d): called on a thread that does not own the object\n", file, line);
    std::abort();
}

ThreadCheckFailureHandler setThreadCheckFailureHandler(ThreadCheckFailureHandler handler) noexcept
{
    return g_handler.exchange(handler, std::memory_order_acq_rel);
}

}
