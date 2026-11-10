#include <gs/platform/RunningMarker.h>

#include <doctest/doctest.h>

#include <windows.h>

#include <string>

TEST_CASE("the running marker is seen in the session and global namespaces while held, and gone after (ADR-0089)")
{
    using gs::platform::RunningMarker;
    using gs::platform::runningMarkerPresent;

    const std::wstring name = L"GameScrcpyTest-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring global = L"Global\\" + name;
    CHECK_FALSE(runningMarkerPresent(name));
    CHECK_FALSE(runningMarkerPresent(global));
    {
        const RunningMarker marker(name);
        CHECK(marker.held());
        CHECK(runningMarkerPresent(name));
        CHECK(runningMarkerPresent(global));
        const RunningMarker again(name);
        CHECK(again.held());
    }
    CHECK_FALSE(runningMarkerPresent(name));
    CHECK_FALSE(runningMarkerPresent(global));
}
