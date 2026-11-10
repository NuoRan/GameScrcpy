#include "gs/input/InputSource.h"
#include "gs/input/ProfileDiff.h"
#include "gs/input/ProfilePatch.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace gs::input;

namespace {

constexpr std::string_view kBase = R"({
    "format": "gamescrcpy.profile", "version": 3,
    "meta": {"name": "Shooter", "app": "com.example.shooter"},
    "controls": [
        {"id": "move", "type": "wasd", "anchor": "bottomLeft", "offset": [0.36, -0.30],
         "inputs": {"up": "KeyW", "down": "KeyS", "left": "KeyA", "right": "KeyD"}},
        {"id": "fire", "when": "!bag", "type": "hold", "anchor": "bottomRight", "offset": [-0.42, -0.30],
         "inputs": {"press": "Mouse.Left"}},
        {"id": "jump", "type": "tap", "anchor": "bottomRight", "offset": [-0.20, -0.42], "enabled": false,
         "inputs": {"press": "Space"}, "tapMs": 40}
    ],
    "vars": [{"id": "bag"}]
})";

Profile loaded(std::string_view json)
{
    const LoadResult result = loadProfile(json);
    REQUIRE(result.ok());
    return result.profile;
}

Profile patched(std::string_view base, const ProfileDiff& diff)
{
    const PatchOutcome outcome = applyPatch(base, diff.patch);
    INFO(diff.patch);
    REQUIRE(outcome.applied);
    return loaded(outcome.profile);
}

Control& control(Profile& profile, std::string_view id)
{
    const auto found = std::ranges::find(profile.controls, id, &Control::id);
    REQUIRE(found != profile.controls.end());
    return *found;
}

std::optional<Binding>& pressSlot(Profile& profile, std::string_view id)
{
    std::optional<Binding>* press = std::visit(
        [](auto& kind) -> std::optional<Binding>* {
            if constexpr (requires { kind.press; }) {
                return &kind.press;
            } else {
                return nullptr;
            }
        },
        control(profile, id).kind);
    REQUIRE(press != nullptr);
    return *press;
}

Binding& press(Profile& profile, std::string_view id)
{
    std::optional<Binding>& slot = pressSlot(profile, id);
    REQUIRE(slot.has_value());
    return *slot;
}

std::vector<std::string> controlIds(const Profile& profile)
{
    std::vector<std::string> ids;
    for (const Control& each : profile.controls) {
        ids.push_back(each.id);
    }
    return ids;
}

Control button(std::string id, std::string_view input)
{
    Control made;
    made.id = std::move(id);
    made.kind = HoldControl{.press = Binding{.input = *parseChord(input)}};
    return made;
}

}

TEST_CASE("a diff turns removed, changed and added controls and variables into a patch (EDT-017)")
{
    const Profile original = loaded(kBase);
    Profile edited = original;
    std::erase_if(edited.controls, [](const Control& each) { return each.id == "jump"; });
    press(edited, "fire").input = *parseChord("KeyF");
    control(edited, "move").offset = {0.4, -0.25};
    edited.controls.push_back(button("lean", "KeyQ"));
    edited.meta.app = "com.example.other";
    edited.screen.safeArea = SafeArea::Respect;
    edited.vars.push_back(Variable{.id = "scope", .name = "Scope", .initial = true});
    edited.vars.push_back(Variable{.id = "kills", .initial = 3.0});

    const ProfileDiff diff = diffProfiles(original, edited);
    INFO(diff.patch);
    CHECK(diff.exact);
    CHECK_FALSE(diff.empty);
    CHECK(diff.patch.find("\"fire\"") != std::string::npos);
    CHECK(diff.patch.find("Mouse.Left") == std::string::npos);
    CHECK(diff.patch.find("\"vars\"") != std::string::npos);
    CHECK(patched(kBase, diff) == edited);
}

TEST_CASE("a diff of an unchanged profile is empty, and a field put back to its default is deleted")
{
    const Profile original = loaded(kBase);
    const ProfileDiff same = diffProfiles(original, original);
    CHECK(same.empty);
    CHECK(same.exact);
    CHECK(patched(kBase, same) == original);

    Profile edited = original;
    control(edited, "jump").enabled = true;
    std::get<TapControl>(control(edited, "jump").kind).tapMs = 0;
    control(edited, "fire").when.clear();
    edited.meta.app.clear();
    const ProfileDiff diff = diffProfiles(original, edited);
    INFO(diff.patch);
    CHECK(diff.exact);
    CHECK(diff.patch.find("null") != std::string::npos);
    const Profile result = patched(kBase, diff);
    CHECK(result == edited);
    CHECK(result.controls[2].enabled);
    CHECK(result.meta.app.empty());
}

TEST_CASE("a diff keeps the order of the edited profile by adding moved elements again")
{
    const Profile original = loaded(kBase);
    Profile edited = original;
    std::ranges::rotate(edited.controls, edited.controls.begin() + 1);
    edited.controls.insert(edited.controls.begin() + 1, button("lean", "KeyQ"));
    const ProfileDiff diff = diffProfiles(original, edited);
    INFO(diff.patch);
    CHECK(diff.exact);
    const Profile result = patched(kBase, diff);
    CHECK(controlIds(result) == std::vector<std::string>{"fire", "lean", "jump", "move"});
    CHECK(result == edited);
}

TEST_CASE("a control that changes its type keeps its id and comes back whole")
{
    const Profile original = loaded(kBase);
    Profile edited = original;
    Control& fire = control(edited, "fire");
    fire.kind = SkillControl{.press = Binding{.input = *parseChord("Mouse.Left")}, .direction = AimDirection::Cursor};
    control(edited, "jump").kind = TurboControl{.press = Binding{.input = *parseChord("Space")}, .rateHz = 15};
    const ProfileDiff diff = diffProfiles(original, edited);
    INFO(diff.patch);
    CHECK(diff.exact);
    CHECK(patched(kBase, diff) == edited);
}

TEST_CASE("a diff carries claims, key-less components and the camera crosshair both ways")
{
    const Profile original = loaded(kBase);
    Profile edited = original;
    press(edited, "fire") = Binding{.input = *parseChord("Mouse.Right"), .claim = Claim::Both};
    pressSlot(edited, "jump").reset();
    Control look;
    look.id = "look";
    look.kind = CameraControl{.lockKey = *parseChord("Backquote"), .crosshair = true};
    edited.controls.push_back(look);

    const ProfileDiff diff = diffProfiles(original, edited);
    INFO(diff.patch);
    CHECK(diff.exact);
    CHECK(diff.patch.find("\"both\"") != std::string::npos);
    CHECK(diff.patch.find("\"crosshair\"") != std::string::npos);
    Profile result = patched(kBase, diff);
    CHECK(result == edited);
    CHECK_FALSE(pressSlot(result, "jump").has_value());
    CHECK(press(result, "fire").claim == Claim::Both);

    const ProfileDiff back = diffProfiles(edited, original);
    INFO(back.patch);
    CHECK(back.exact);
    CHECK(patched(saveProfile(edited), back) == original);
}

TEST_CASE("a patch made from a diff keeps the edits when the profile it was made on is updated")
{
    const Profile original = loaded(kBase);
    Profile edited = original;
    press(edited, "fire").input = *parseChord("KeyF");
    std::erase_if(edited.controls, [](const Control& each) { return each.id == "jump"; });
    edited.controls.push_back(button("lean", "KeyQ"));
    const ProfileDiff diff = diffProfiles(original, edited);
    REQUIRE(diff.exact);

    Profile updated = original;
    control(updated, "fire").offset = {-0.5, -0.35};
    control(updated, "move").offset = {0.3, -0.3};
    updated.controls.push_back(button("map", "KeyM"));
    const std::string updatedText = saveProfile(updated);
    Profile result = patched(updatedText, diff);
    CHECK(controlIds(result) == std::vector<std::string>{"move", "fire", "map", "lean"});
    REQUIRE(press(result, "fire").input.has_value());
    CHECK(chordName(*press(result, "fire").input) == "KeyF");
    CHECK(control(result, "fire").offset == Vec2{-0.5, -0.35});
    CHECK(control(result, "move").offset == Vec2{0.3, -0.3});
}
