#include "gs/input/Profile.h"
#include "gs/input/ProfileDiff.h"
#include "gs/input/ProfilePatch.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>

using namespace gs::input;

namespace {

constexpr std::string_view kMapped = R"({"format": "gamescrcpy.profile", "version": 3,
    "controls": [{"id": "jump", "type": "tap", "anchor": "bottomRight", "offset": [-0.2, -0.4],
                  "inputs": {"press": "Space"}}]})";

constexpr std::string_view kHanded = R"({"format": "gamescrcpy.profile", "version": 3, "gamepad": "passthrough",
    "controls": [{"id": "jump", "type": "tap", "anchor": "bottomRight", "offset": [-0.2, -0.4],
                  "inputs": {"press": "Space"}}]})";

Profile loaded(std::string_view json)
{
    const LoadResult result = loadProfile(json);
    REQUIRE(result.ok());
    return result.profile;
}

bool reports(const LoadResult& result, DiagnosticCode code, std::string_view path)
{
    return std::ranges::any_of(result.diagnostics, [&](const Diagnostic& d) {
        return d.severity == Severity::Error && d.code == code && d.path == path;
    });
}

std::string patch(std::string_view body)
{
    return R"({"format": "gamescrcpy.profile-patch", "version": 1, )" + std::string(body) + "}";
}

}

TEST_CASE("a profile maps the gamepad unless it says passthrough, and only writes the setting then (ADR-0103)")
{
    const Profile mapped = loaded(kMapped);
    CHECK(mapped.gamepad == GamepadMode::Map);
    CHECK(saveProfile(mapped).find("gamepad") == std::string::npos);

    const Profile handed = loaded(kHanded);
    CHECK(handed.gamepad == GamepadMode::Passthrough);
    const std::string saved = saveProfile(handed);
    CHECK(saved.find(R"("gamepad": "passthrough")") != std::string::npos);
    CHECK(saved.find(R"("gamepad")") < saved.find(R"("controls")"));
    CHECK(loaded(saved) == handed);

    Profile toggled = handed;
    toggled.gamepad = GamepadMode::Map;
    CHECK(saveProfile(toggled).find("gamepad") == std::string::npos);
    CHECK(loaded(saveProfile(toggled)) == mapped);

    const Profile spelled = loaded(R"({"format": "gamescrcpy.profile", "version": 3, "gamepad": "map"})");
    CHECK(spelled.gamepad == GamepadMode::Map);
    CHECK(saveProfile(spelled).find("gamepad") == std::string::npos);
}

TEST_CASE("a gamepad setting that is neither map nor passthrough is an error at its path (ADR-0103)")
{
    const LoadResult unknown = loadProfile(R"({"format": "gamescrcpy.profile", "version": 3, "gamepad": "both"})");
    CHECK_FALSE(unknown.ok());
    CHECK(reports(unknown, DiagnosticCode::InvalidValue, "$.gamepad"));

    const LoadResult typed = loadProfile(R"({"format": "gamescrcpy.profile", "version": 3, "gamepad": true})");
    CHECK_FALSE(typed.ok());
    CHECK(reports(typed, DiagnosticCode::InvalidValue, "$.gamepad"));
}

TEST_CASE("a patch sets or clears the gamepad mode and a diff carries it exactly (ADR-0103)")
{
    const PatchOutcome on = applyPatch(kMapped, patch(R"("gamepad": "passthrough")"));
    REQUIRE(on.applied);
    CHECK(on.diagnostics.empty());
    CHECK(loaded(on.profile).gamepad == GamepadMode::Passthrough);

    const PatchOutcome off = applyPatch(kHanded, patch(R"("gamepad": null)"));
    REQUIRE(off.applied);
    CHECK(loaded(off.profile).gamepad == GamepadMode::Map);

    const PatchOutcome wrong = applyPatch(kMapped, patch(R"("gamepad": "sideways")"));
    CHECK_FALSE(wrong.applied);

    const ProfileDiff handing = diffProfiles(loaded(kMapped), loaded(kHanded));
    CHECK(handing.exact);
    CHECK_FALSE(handing.empty);
    const PatchOutcome replayed = applyPatch(kMapped, handing.patch);
    REQUIRE(replayed.applied);
    CHECK(loaded(replayed.profile) == loaded(kHanded));

    const ProfileDiff mapping = diffProfiles(loaded(kHanded), loaded(kMapped));
    CHECK(mapping.exact);
    CHECK(mapping.patch.find(R"("gamepad": null)") != std::string::npos);
    const PatchOutcome back = applyPatch(kHanded, mapping.patch);
    REQUIRE(back.applied);
    CHECK(loaded(back.profile) == loaded(kMapped));

    const ProfileDiff same = diffProfiles(loaded(kHanded), loaded(kHanded));
    CHECK(same.empty);
}
