#include "gs/base/BuildInfo.h"

#include <doctest/doctest.h>

TEST_CASE("buildInfo reports the project version configured by CMake")
{
    const gs::BuildInfo info = gs::buildInfo();
    CHECK(info.version == GS_EXPECTED_VERSION);
    CHECK_FALSE(info.revision.empty());
}
