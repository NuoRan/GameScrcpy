#include "gs/input/InputSource.h"
#include "gs/input/ProfilePatch.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
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
        {"id": "jump", "type": "tap", "anchor": "bottomRight", "offset": [-0.20, -0.42],
         "inputs": {"press": "Space"}}
    ],
    "vars": [{"id": "bag"}]
})";

std::string patch(std::string_view body)
{
    return R"({"format": "gamescrcpy.profile-patch", "version": 1)" + std::string(body.empty() ? "" : ", ") +
           std::string(body) + "}";
}

bool reports(const PatchOutcome& outcome, DiagnosticCode code, std::string_view path)
{
    return std::ranges::any_of(outcome.diagnostics,
                               [&](const Diagnostic& d) { return d.code == code && d.path == path; });
}

std::string listed(const PatchOutcome& outcome)
{
    std::string text;
    for (const Diagnostic& d : outcome.diagnostics) {
        text += d.path + " " + d.detail + "; ";
    }
    return text;
}

Profile loaded(const PatchOutcome& outcome)
{
    const LoadResult result = loadProfile(outcome.profile);
    REQUIRE(result.ok());
    return result.profile;
}

std::vector<std::string> controlIds(const Profile& profile)
{
    std::vector<std::string> ids;
    for (const Control& control : profile.controls) {
        ids.push_back(control.id);
    }
    return ids;
}

std::string pressOf(const Profile& profile, std::string_view id)
{
    const auto found = std::ranges::find(profile.controls, id, &Control::id);
    REQUIRE(found != profile.controls.end());
    const std::optional<Binding>* press = std::visit(
        [](const auto& kind) -> const std::optional<Binding>* {
            if constexpr (requires { kind.press; }) {
                return &kind.press;
            } else {
                return nullptr;
            }
        },
        found->kind);
    REQUIRE(press != nullptr);
    REQUIRE(press->has_value());
    return chordName((*press)->input);
}

}

TEST_CASE("a patch removes, changes and adds controls and variables by id and merges meta (PRF-006)")
{
    const PatchOutcome outcome = applyPatch(kBase, patch(R"(
        "meta": {"name": "Shooter lean"},
        "controls": {
            "add": [{"id": "lean", "type": "hold", "anchor": "right", "offset": [-0.1, 0],
                     "inputs": {"press": "KeyQ"}}],
            "change": {"fire": {"inputs": {"press": "Mouse.Right"}, "when": null}},
            "remove": ["jump"]
        },
        "vars": {"add": [{"id": "scope", "initial": true}]}
    )"));
    INFO(listed(outcome));
    REQUIRE(outcome.applied);
    CHECK(outcome.diagnostics.empty());
    const Profile profile = loaded(outcome);
    CHECK(profile.meta.name == "Shooter lean");
    CHECK(profile.meta.app == "com.example.shooter");
    CHECK(controlIds(profile) == std::vector<std::string>{"move", "fire", "lean"});
    CHECK(pressOf(profile, "fire") == "Mouse.Right");
    CHECK(profile.controls[1].when.empty());
    REQUIRE(profile.vars.size() == 2);
    CHECK(profile.vars[1].initial == VarValue{true});
    CHECK(outcome.touched ==
          std::vector<std::string>{"controls/jump", "controls/fire", "controls/lean", "vars/scope"});
}

TEST_CASE("an older patch's states section still edits the variables, and its commands are reported unknown")
{
    const PatchOutcome outcome = applyPatch(kBase, patch(R"(
        "states": {"add": [{"id": "scope"}]},
        "commands": {"remove": ["bagKey"]}
    )"));
    INFO(listed(outcome));
    REQUIRE(outcome.applied);
    CHECK(reports(outcome, DiagnosticCode::UnknownField, "$.commands"));
    const Profile profile = loaded(outcome);
    REQUIRE(profile.vars.size() == 2);
    CHECK(profile.vars[1].id == "scope");
}

TEST_CASE("a version 2 profile is brought up to version 3 before a patch is applied to it (ADR-0091)")
{
    constexpr std::string_view kOld = R"({"format": "gamescrcpy.profile", "version": 2,
        "controls": [{"id": "fire", "type": "button", "inputs": {"press": "Mouse.Left"}}],
        "commands": [{"input": "Escape", "command": "android.back"}],
        "states": [{"id": "bag"}]})";
    const PatchOutcome outcome =
        applyPatch(kOld, patch(R"("controls": {"change": {"fire": {"inputs": {"press": "KeyF"}}}})"));
    INFO(listed(outcome));
    REQUIRE(outcome.applied);
    const Profile profile = loaded(outcome);
    CHECK(std::holds_alternative<HoldControl>(profile.controls[0].kind));
    CHECK(pressOf(profile, "fire") == "KeyF");
    CHECK(profile.vars.size() == 1);
    CHECK(outcome.profile.find("\"version\": 3") != std::string::npos);
}

TEST_CASE("ids a profile does not have are reported and skipped, and the id of an element cannot change")
{
    const PatchOutcome outcome = applyPatch(kBase, patch(R"(
        "controls": {
            "remove": ["ghost"],
            "change": {"phantom": {"tapMs": 40}, "jump": {"id": "hop", "tapMs": 40}},
            "add": [{"id": "fire", "type": "hold", "inputs": {"press": "KeyJ"}}]
        },
        "extra": true
    )"));
    INFO(listed(outcome));
    REQUIRE(outcome.applied);
    CHECK(reports(outcome, DiagnosticCode::UnknownReference, "$.controls.remove[0]"));
    CHECK(reports(outcome, DiagnosticCode::UnknownReference, "$.controls.change.phantom"));
    CHECK(reports(outcome, DiagnosticCode::InvalidValue, "$.controls.change.jump.id"));
    CHECK(reports(outcome, DiagnosticCode::DuplicateId, "$.controls.add[0].id"));
    CHECK(reports(outcome, DiagnosticCode::UnknownField, "$.extra"));
    CHECK(std::ranges::all_of(outcome.diagnostics, [](const Diagnostic& d) { return d.severity == Severity::Warning; }));
    const Profile profile = loaded(outcome);
    CHECK(controlIds(profile) == std::vector<std::string>{"move", "fire", "jump"});
    CHECK(std::get<TapControl>(profile.controls[2].kind).tapMs == 40);
    CHECK(pressOf(profile, "fire") == "Mouse.Left");
    CHECK(outcome.touched == std::vector<std::string>{"controls/jump"});
}

TEST_CASE("a patch whose result would not be a valid profile is not applied")
{
    const PatchOutcome outcome = applyPatch(kBase, patch(R"("vars": {"remove": ["bag"]})"));
    CHECK_FALSE(outcome.applied);
    CHECK(outcome.profile == kBase);
    CHECK(outcome.touched.empty());
    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(outcome.diagnostics[0].severity == Severity::Error);
    CHECK(outcome.diagnostics[0].code == DiagnosticCode::PatchBreaksProfile);
    CHECK_FALSE(outcome.diagnostics[0].path.empty());
}

TEST_CASE("patches that are not patch documents or have the wrong shape are rejected as a whole")
{
    const auto rejected = [](std::string_view text, DiagnosticCode code, std::string_view path) {
        const PatchOutcome outcome = applyPatch(kBase, text);
        INFO(std::string(text.substr(0, 80)));
        INFO(listed(outcome));
        CHECK_FALSE(outcome.applied);
        CHECK(outcome.profile == kBase);
        CHECK(outcome.touched.empty());
        CHECK(reports(outcome, code, path));
    };
    rejected("{", DiagnosticCode::MalformedJson, "$");
    rejected("[]", DiagnosticCode::WrongType, "$");
    rejected(R"({"version": 1})", DiagnosticCode::MissingField, "$.format");
    rejected(R"({"format": "gamescrcpy.profile", "version": 3})", DiagnosticCode::WrongFormat, "$.format");
    rejected(R"({"format": "gamescrcpy.profile-patch", "version": 2})", DiagnosticCode::WrongVersion, "$.version");
    rejected(patch(R"("controls": [])"), DiagnosticCode::WrongType, "$.controls");
    rejected(patch(R"("meta": "Shooter")"), DiagnosticCode::WrongType, "$.meta");
    rejected(patch(R"("controls": {"remove": "jump"})"), DiagnosticCode::WrongType, "$.controls.remove");
    rejected(patch(R"("controls": {"change": {"fire": 3}})"), DiagnosticCode::WrongType, "$.controls.change.fire");
    rejected(patch(R"("controls": {"add": [{"type": "tap"}]})"), DiagnosticCode::MissingField,
             "$.controls.add[0].id");
    rejected(std::string(1024 * 1024 + 1, ' '), DiagnosticCode::TooLarge, "$");
}

TEST_CASE("patches applied one after another stack, and the later one wins where both change a control")
{
    const PatchOutcome first = applyPatch(kBase, patch(R"("controls": {"change": {"fire": {"inputs": {"press": "KeyF"}}},
                                                                     "add": [{"id": "lean", "type": "hold",
                                                                              "inputs": {"press": "KeyQ"}}]})"));
    REQUIRE(first.applied);
    const PatchOutcome second =
        applyPatch(first.profile, patch(R"("controls": {"change": {"fire": {"inputs": {"press": "KeyG"}}}})"));
    REQUIRE(second.applied);
    const Profile profile = loaded(second);
    CHECK(pressOf(profile, "fire") == "KeyG");
    CHECK(pressOf(profile, "lean") == "KeyQ");
    CHECK(second.touched == std::vector<std::string>{"controls/fire"});
}
