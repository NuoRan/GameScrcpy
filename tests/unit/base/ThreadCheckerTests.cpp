#include "gs/base/ThreadChecker.h"

#include <doctest/doctest.h>

#include <atomic>
#include <thread>

namespace {

std::atomic<int> g_failures{0};

void countFailure(const char*, int) noexcept
{
    g_failures.fetch_add(1);
}

bool isCurrentOnAnotherThread(const gs::ThreadChecker& checker)
{
    bool current = true;
    std::thread([&] { current = checker.isCurrent(); }).join();
    return current;
}

}

TEST_CASE("ThreadChecker accepts only its owning thread")
{
    const gs::ThreadChecker checker;
    CHECK(checker.isCurrent());
    CHECK_FALSE(isCurrentOnAnotherThread(checker));
}

TEST_CASE("a detached ThreadChecker is adopted by the next thread that uses it")
{
    gs::ThreadChecker checker;
    checker.detach();
    bool adopted = false;
    std::thread([&] { adopted = checker.isCurrent(); }).join();
    CHECK(adopted);
    CHECK_FALSE(checker.isCurrent());
}

#if !defined(NDEBUG)
TEST_CASE("GS_ASSERT_ON_THREAD reports calls from a thread that does not own the object")
{
    const auto previous = gs::detail::setThreadCheckFailureHandler(&countFailure);
    g_failures = 0;
    const gs::ThreadChecker checker;

    GS_ASSERT_ON_THREAD(checker);
    CHECK(g_failures == 0);
    std::thread([&] { GS_ASSERT_ON_THREAD(checker); }).join();
    CHECK(g_failures == 1);

    gs::detail::setThreadCheckFailureHandler(previous);
}
#endif
