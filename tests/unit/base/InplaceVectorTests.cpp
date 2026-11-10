#include "gs/base/InplaceVector.h"

#include <doctest/doctest.h>

#include <numeric>

namespace {

using Small = gs::InplaceVector<int, 4>;

static_assert(Small::capacity() == 4);
static_assert(Small{}.empty());
static_assert(Small{1, 2, 3}.size() == 3);

}

TEST_CASE("InplaceVector keeps insertion order up to its capacity")
{
    Small items;
    for (int i = 1; i <= 4; ++i) {
        items.push_back(i * 10);
    }
    CHECK(items.full());
    CHECK(items.size() == 4);
    CHECK(items[0] == 10);
    CHECK(items[3] == 40);
    CHECK(std::accumulate(items.begin(), items.end(), 0) == 100);
}

TEST_CASE("InplaceVector compares by its live elements only")
{
    Small lhs{1, 2, 3};
    Small rhs{1, 2, 3, 4};
    CHECK_FALSE(lhs == rhs);

    rhs.clear();
    CHECK(rhs.empty());
    rhs.push_back(1);
    rhs.push_back(2);
    rhs.push_back(3);
    CHECK(lhs == rhs);
}
