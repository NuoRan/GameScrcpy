#include "gs/input/PointerAllocator.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <vector>

using gs::input::PointerAllocator;

TEST_CASE("a released pointer id is not handed out again right away (MAP-018)")
{
    PointerAllocator pointers;
    CHECK(pointers.acquire() == 0);
    CHECK(pointers.acquire() == 1);
    CHECK(pointers.acquire() == 2);
    pointers.release(1);
    CHECK(pointers.acquire() == 3);
    CHECK(pointers.inUse(2));
    CHECK_FALSE(pointers.inUse(1));
}

TEST_CASE("the allocator stops at the backend's capacity")
{
    PointerAllocator pointers{10};
    for (int i = 0; i < 10; ++i) {
        CHECK(pointers.acquire().has_value());
    }
    CHECK_FALSE(pointers.acquire().has_value());
    CHECK(pointers.activeCount() == 10);
    pointers.release(4);
    CHECK(pointers.acquire() == 4);
    pointers.release(31);
    CHECK(pointers.activeCount() == 10);
}

TEST_CASE("active pointer ids never collide over 100000 random operations (MAP-010)")
{
    std::mt19937 rng{20261001U};
    PointerAllocator pointers;
    std::vector<std::uint8_t> active;
    for (int step = 0; step < 100000; ++step) {
        if (!active.empty() && (rng() % 2U == 0U || active.size() == PointerAllocator::kMaxPointers)) {
            const std::size_t index = rng() % active.size();
            pointers.release(active[index]);
            active.erase(active.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            const auto id = pointers.acquire();
            REQUIRE(id.has_value());
            REQUIRE(std::find(active.begin(), active.end(), *id) == active.end());
            active.push_back(*id);
        }
        REQUIRE(pointers.activeCount() == active.size());
    }
}
