#include "gs/base/SpscQueue.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <thread>

TEST_CASE("SpscQueue is first-in first-out and reports full and empty")
{
    gs::SpscQueue<int, 4> queue;
    CHECK_FALSE(queue.tryPop().has_value());
    for (int i = 0; i < 4; ++i) {
        CHECK(queue.tryPush(i));
    }
    CHECK_FALSE(queue.tryPush(99));
    for (int i = 0; i < 4; ++i) {
        const auto value = queue.tryPop();
        REQUIRE(value.has_value());
        CHECK(*value == i);
    }
    CHECK_FALSE(queue.tryPop().has_value());
}

TEST_CASE("SpscQueue keeps order while its indices wrap around the storage")
{
    gs::SpscQueue<int, 4> queue;
    int next = 0;
    int expected = 0;
    bool ordered = true;
    for (int round = 0; round < 100; ++round) {
        CHECK(queue.tryPush(next++));
        CHECK(queue.tryPush(next++));
        for (int i = 0; i < 2; ++i) {
            const auto value = queue.tryPop();
            ordered = ordered && value.has_value() && *value == expected++;
        }
    }
    CHECK(ordered);
}

TEST_CASE("SpscQueue delivers every item exactly once and in order across two threads")
{
    constexpr std::uint64_t kCount = 2'000'000;
    const auto queue = std::make_unique<gs::SpscQueue<std::uint64_t, 1024>>();

    std::jthread producer([&queue] {
        for (std::uint64_t i = 0; i < kCount; ++i) {
            while (!queue->tryPush(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::uint64_t received = 0;
    bool ordered = true;
    while (received < kCount) {
        if (const auto value = queue->tryPop()) {
            ordered = ordered && *value == received;
            ++received;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();

    CHECK(ordered);
    CHECK(received == kCount);
    CHECK_FALSE(queue->tryPop().has_value());
}
