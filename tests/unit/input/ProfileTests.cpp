#include "gs/input/Profile.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <string>
#include <string_view>

using namespace gs::input;

namespace {

constexpr std::string_view kFullProfile = R"json({
  "format": "gamescrcpy.profile",
  "version": 3,
  "meta": { "name": "射击示例", "app": "com.example.shooter", "orientation": "landscape", "reference": [2400, 1080] },
  "screen": { "scaleMode": "shortSide", "safeArea": "respect" },
  "controls": [
    { "id": "move", "type": "wasd", "anchor": "bottomLeft", "offset": [0.36, -0.3],
      "inputs": { "up": "KeyW", "down": "KeyS", "left": "KeyA", "right": "KeyD",
                  "walk": "ControlLeft", "sprint": "ShiftLeft" },
      "radius": 0.14 },
    { "id": "look", "type": "camera", "anchor": "right", "offset": [-0.55, -0.1],
      "inputs": { "look": { "input": "Mouse.Move" } },
      "lockKey": "Backquote", "size": [0.9, 0.7], "sensitivity": [3.0, 2.5], "idleRecenterMs": 0 },
    { "id": "fire", "when": "!bag", "type": "turbo", "anchor": "bottomRight", "offset": [-0.42, -0.3],
      "inputs": { "press": "Mouse.Left" }, "rateHz": 12 },
    { "id": "jump", "type": "tap", "anchor": "bottomRight", "offset": [-0.2, -0.42],
      "inputs": { "press": "Space" }, "tapMs": 40 },
    { "id": "peek", "type": "freeLook", "anchor": "topRight", "offset": [-0.3, 0.3],
      "inputs": { "hold": "Mouse.Middle", "look": "Mouse.Move" }, "resetOnRelease": false },
    { "id": "grenade", "type": "skill", "anchor": "right", "offset": [-0.25, 0.1],
      "inputs": { "press": "KeyG", "look": "Mouse.Move" }, "radius": 0.2, "castOnRelease": false },
    { "id": "slide", "type": "swipe", "offset": [0.1, 0.2],
      "inputs": { "press": "KeyC" }, "path": [[0, 0], [0, 0.15], [0.05, 0.2]], "durationMs": 150 },
    { "id": "map", "type": "pinch", "anchor": "topLeft", "offset": [0.2, 0.2],
      "inputs": { "zoomIn": "Mouse.WheelUp", "zoomOut": "Mouse.WheelDown" }, "span": 0.3, "step": 0.04 },
    { "id": "use", "when": "bag", "type": "hold", "offset": [0, 0.25], "inputs": { "press": "KeyE" } },
    { "id": "horn", "enabled": false, "type": "longPress", "offset": [0.1, 0.25], "inputs": { "press": "KeyH" },
      "holdMs": 1500 },
    { "id": "walk", "type": "rightWalk", "anchor": "bottomLeft", "offset": [0.36, -0.3],
      "inputs": { "walkTo": "Mouse.Right", "stop": "KeyX" }, "walkMsPerUnit": 450, "hero": [0.01, -0.02] },
    { "id": "pad", "type": "padStick", "anchor": "bottomLeft", "offset": [0.36, -0.3],
      "inputs": { "stick": "Gamepad.LeftStick" } },
    { "id": "padLook", "type": "padCamera", "inputs": { "look": "Gamepad.RightStick" }, "sensitivity": [2.0, 2.0] }
  ],
  "vars": [
    { "id": "bag" },
    { "id": "vehicle", "name": "在车上", "initial": true },
    { "id": "kills", "initial": 3 },
    { "id": "mode", "initial": "ranked" }
  ]
})json";

bool has(const LoadResult& result, DiagnosticCode code, std::string_view path)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [&](const Diagnostic& d) { return d.code == code && d.path == path; });
}

std::size_t count(const LoadResult& result, Severity severity)
{
    return static_cast<std::size_t>(std::count_if(result.diagnostics.begin(), result.diagnostics.end(),
                                                  [&](const Diagnostic& d) { return d.severity == severity; }));
}

std::string dump(const LoadResult& result)
{
    std::string text;
    for (const Diagnostic& d : result.diagnostics) {
        if (d.severity != Severity::Info) {
            text += std::to_string(static_cast<int>(d.code)) + " " + d.path + " " + d.detail + "\n";
        }
    }
    return text;
}

Chord chord(std::string_view text)
{
    const auto parsed = parseChord(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

LoadResult loadWithControls(std::string_view controls)
{
    return loadProfile(R"({"format":"gamescrcpy.profile","version":3,"controls":)" + std::string(controls) + "}");
}

}

TEST_CASE("a profile using every component type and variable kind loads completely (PRF-001)")
{
    const LoadResult result = loadProfile(kFullProfile);
    INFO(dump(result));
    REQUIRE(result.ok());
    CHECK(count(result, Severity::Warning) == 0);
    CHECK_FALSE(result.converted());
    const Profile& p = result.profile;

    CHECK(p.meta.name == "射击示例");
    CHECK(p.meta.app == "com.example.shooter");
    CHECK(p.meta.reference == std::array<std::uint16_t, 2>{2400, 1080});
    CHECK(p.screen.safeArea == SafeArea::Respect);
    REQUIRE(p.controls.size() == 13);

    const auto& move = std::get<WasdControl>(p.controls[0].kind);
    CHECK(p.controls[0].anchor == Anchor::BottomLeft);
    CHECK(p.controls[0].offset == Vec2{0.36, -0.3});
    CHECK(p.controls[0].enabled);
    CHECK(p.controls[0].when.empty());
    CHECK(move.up->input == chord("KeyW"));
    CHECK(move.sprint->input == chord("ShiftLeft"));
    CHECK(move.radius == doctest::Approx(0.14));
    CHECK(move.walkScale == doctest::Approx(0.5));

    const auto& look = std::get<CameraControl>(p.controls[1].kind);
    CHECK(look.look->input == chord("Mouse.Move"));
    CHECK(look.lockKey == chord("Backquote"));
    CHECK(look.sensitivity == Vec2{3.0, 2.5});
    CHECK(look.idleRecenterMs == 0);

    CHECK(p.controls[2].when == "!bag");
    const auto& fire = std::get<TurboControl>(p.controls[2].kind);
    CHECK(fire.rateHz == 12);
    CHECK(fire.press->input == chord("Mouse.Left"));
    CHECK(std::get<TapControl>(p.controls[3].kind).tapMs == 40);
    CHECK_FALSE(std::get<FreeLookControl>(p.controls[4].kind).resetOnRelease);
    CHECK(std::get<SkillControl>(p.controls[5].kind).radius == doctest::Approx(0.2));
    const auto& slide = std::get<SwipeControl>(p.controls[6].kind);
    CHECK(slide.path.size() == 3);
    CHECK(slide.durationMs == 150);
    CHECK(p.controls[6].anchor == Anchor::Center);
    CHECK(std::get<PinchControl>(p.controls[7].kind).zoomIn->input == chord("Mouse.WheelUp"));
    CHECK(p.controls[8].when == "bag");
    CHECK(std::holds_alternative<HoldControl>(p.controls[8].kind));
    CHECK_FALSE(p.controls[9].enabled);
    CHECK(std::get<LongPressControl>(p.controls[9].kind).holdMs == 1500);
    const auto& walk = std::get<RightWalkControl>(p.controls[10].kind);
    CHECK(walk.walkTo->input == chord("Mouse.Right"));
    CHECK(walk.walkTo->claim == Claim::Cursor);
    CHECK(walk.stop->input == chord("KeyX"));
    CHECK(walk.walkMsPerUnit == 450);
    CHECK(walk.stopOnRelease);
    CHECK(walk.hero == Vec2{0.01, -0.02});
    CHECK(std::get<PadStickControl>(p.controls[11].kind).stick->input == chord("Gamepad.LeftStick"));
    const auto& padLook = std::get<PadCameraControl>(p.controls[12].kind);
    CHECK(padLook.look->input == chord("Gamepad.RightStick"));
    CHECK(padLook.sensitivity == Vec2{2.0, 2.0});

    REQUIRE(p.vars.size() == 4);
    CHECK(p.vars[0].id == "bag");
    CHECK(p.vars[0].initial == VarValue{false});
    CHECK(p.vars[1].name == "在车上");
    CHECK(p.vars[1].initial == VarValue{true});
    CHECK(p.vars[2].initial == VarValue{3.0});
    CHECK(p.vars[3].initial == VarValue{std::string("ranked")});
    CHECK_FALSE(truthy(p.vars[0].initial));
    CHECK(truthy(p.vars[2].initial));
    CHECK(truthy(p.vars[3].initial));
    CHECK_FALSE(truthy(VarValue{0.0}));
    CHECK_FALSE(truthy(VarValue{std::string()}));
}

TEST_CASE("the components that move are told apart from the rest")
{
    CHECK(isMovement(WasdControl{}));
    CHECK(isMovement(RightWalkControl{}));
    CHECK(isMovement(PadStickControl{}));
    CHECK_FALSE(isMovement(CameraControl{}));
    CHECK_FALSE(isMovement(HoldControl{}));
    CHECK_FALSE(isMovement(SkillControl{}));
}

TEST_CASE("MOBA casting and right-click walk fields load, default and round-trip (INP-022)")
{
    const LoadResult loaded = loadProfile(R"({"format":"gamescrcpy.profile","version":3,
        "screen": {"confineCursor": true},
        "controls": [
            {"id": "move", "type": "rightWalk", "anchor": "bottomLeft", "offset": [0.3, -0.3],
             "inputs": {"walkTo": "Mouse.Right", "stop": "KeyS"}, "walkMsPerUnit": 450, "stopOnRelease": false},
            {"id": "skill1", "type": "skill", "anchor": "bottomRight", "offset": [-0.3, -0.2],
             "inputs": {"press": "KeyQ", "cancel": "Space"}, "direction": "cursor", "cast": "manual",
             "castRadius": 0.5, "hero": [0.02, 0.05], "cancelAt": [0.1, -0.4], "pauses": "move"},
            {"id": "skill2", "type": "skill", "inputs": {"press": "KeyE"}}
        ]})");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    CHECK(count(loaded, Severity::Warning) == 0);
    const Profile& profile = loaded.profile;
    CHECK(profile.screen == ScreenSettings{});
    CHECK(saveProfile(profile).find("confineCursor") == std::string::npos);
    const auto& move = std::get<RightWalkControl>(profile.controls[0].kind);
    CHECK(move.walkTo->input == chord("Mouse.Right"));
    CHECK(move.stop->input == chord("KeyS"));
    CHECK(move.walkMsPerUnit == 450);
    CHECK_FALSE(move.stopOnRelease);
    const auto& skill = std::get<SkillControl>(profile.controls[1].kind);
    CHECK(skill.direction == AimDirection::Cursor);
    CHECK(skill.cast == CastTiming::Manual);
    CHECK(skill.castRadius == 0.5);
    CHECK(skill.hero == Vec2{0.02, 0.05});
    REQUIRE(skill.cancelAt.has_value());
    CHECK(*skill.cancelAt == Vec2{0.1, -0.4});
    CHECK(skill.cancel->input == chord("Space"));
    CHECK(skill.pauses == "move");
    const auto& plain = std::get<SkillControl>(profile.controls[2].kind);
    CHECK(plain.direction == AimDirection::Mouse);
    CHECK(plain.cast == CastTiming::Release);
    CHECK(plain.castRadius == 0.35);
    CHECK(plain.hero == Vec2{});
    CHECK_FALSE(plain.cancelAt.has_value());
    CHECK(plain.pauses.empty());
    const RightWalkControl defaults;
    CHECK(defaults.walkMsPerUnit == 600);
    CHECK(defaults.stopOnRelease);

    const std::string saved = saveProfile(profile);
    CHECK(saved.find("\"castRadius\"") != std::string::npos);
    CHECK(saved.find("\"cancelAt\"") != std::string::npos);
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == profile);
    CHECK(saveProfile(again.profile).find("\"direction\": \"mouse\"") == std::string::npos);
}

TEST_CASE("conflicts group the bindings that fire together on one input, by the same rules as the load check")
{
    const LoadResult loaded = loadProfile(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [
            {"id": "reload", "type": "tap", "inputs": {"press": "KeyR"}},
            {"id": "crouch", "type": "hold", "inputs": {"press": "KeyR"}},
            {"id": "spare", "type": "tap", "enabled": false, "inputs": {"press": "KeyR"}},
            {"id": "lean", "type": "tap", "inputs": {"press": "KeyQ"}},
            {"id": "peek", "type": "longPress", "inputs": {"press": "KeyQ"}},
            {"id": "open", "type": "tap", "when": "bag", "inputs": {"press": "KeyE"}},
            {"id": "use", "type": "tap", "when": "!bag", "inputs": {"press": "KeyE"}}
        ],
        "vars": [{"id": "bag"}]})");
    REQUIRE(loaded.ok());
    const std::vector<InputConflict> conflicts = inputConflicts(loaded.profile);
    REQUIRE(conflicts.size() == 2);
    CHECK(conflicts[0].input == chord("KeyR"));
    REQUIRE(conflicts[0].bindings.size() == 2);
    CHECK(conflicts[0].bindings[0] == ConflictingBinding{0, "$.controls[0].inputs.press"});
    CHECK(conflicts[0].bindings[1] == ConflictingBinding{1, "$.controls[1].inputs.press"});
    CHECK(conflicts[1].input == chord("KeyQ"));
    CHECK(std::ranges::count(loaded.diagnostics, DiagnosticCode::Conflict, &Diagnostic::code) == 2);
}

TEST_CASE("claim sets the lock state a mouse binding answers in, defaults per slot and is saved only when it differs")
{
    const LoadResult loaded = loadWithControls(R"([
        {"id": "move", "type": "rightWalk", "inputs": {"walkTo": "Mouse.Right"}},
        {"id": "fire", "type": "hold", "inputs": {"press": {"input": "Mouse.Left", "claim": "both"}}},
        {"id": "zoom", "type": "tap", "inputs": {"press": {"input": "Mouse.WheelUp", "claim": "cursor"}}},
        {"id": "walker", "type": "rightWalk", "inputs": {"walkTo": {"input": "Mouse.X1", "claim": "lock"}}},
        {"id": "skill", "type": "skill", "inputs": {"press": "KeyQ", "cast": "Mouse.Left"}, "cast": "manual"},
        {"id": "keys", "type": "wasd", "inputs": {"up": "KeyW"}}
    ])");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    CHECK(count(loaded, Severity::Warning) == 0);
    const Profile& profile = loaded.profile;
    CHECK(std::get<RightWalkControl>(profile.controls[0].kind).walkTo->claim == Claim::Cursor);
    CHECK(std::get<WasdControl>(profile.controls[5].kind).up->claim == Claim::Lock);
    CHECK(std::get<HoldControl>(profile.controls[1].kind).press->claim == Claim::Both);
    CHECK(std::get<TapControl>(profile.controls[2].kind).press->claim == Claim::Cursor);
    CHECK(std::get<RightWalkControl>(profile.controls[3].kind).walkTo->claim == Claim::Lock);
    const auto& skill = std::get<SkillControl>(profile.controls[4].kind);
    CHECK(skill.cast == CastTiming::Manual);
    REQUIRE(skill.castKey.has_value());
    CHECK(skill.castKey->input == chord("Mouse.Left"));
    CHECK(skill.castKey->claim == Claim::Cursor);

    const std::string saved = saveProfile(profile);
    CHECK(saved.find(R"("walkTo": "Mouse.Right")") != std::string::npos);
    CHECK(saved.find(R"("cast": "Mouse.Left")") != std::string::npos);
    CHECK(saved.find(R"("claim": "both")") != std::string::npos);
    CHECK(saved.find(R"("claim": "cursor")") != std::string::npos);
    CHECK(saved.find(R"("claim": "lock")") != std::string::npos);
    std::size_t claims = 0;
    for (std::size_t at = saved.find("\"claim\""); at != std::string::npos; at = saved.find("\"claim\"", at + 1)) {
        ++claims;
    }
    CHECK(claims == 3);
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == profile);

    const std::vector<InputSource> claimed = cursorClaims(profile);
    CHECK(std::ranges::find(claimed, chord("Mouse.Right").source) != claimed.end());
    CHECK(std::ranges::find(claimed, chord("Mouse.Left").source) != claimed.end());
    CHECK(std::ranges::find(claimed, chord("Mouse.WheelUp").source) != claimed.end());
    CHECK(std::ranges::find(claimed, chord("Mouse.X1").source) == claimed.end());
}

TEST_CASE("claim on a keyboard, gamepad or mouse-move input and a binding without input are errors")
{
    const LoadResult keyed = loadWithControls(R"([
        {"id": "a", "type": "tap", "inputs": {"press": {"input": "KeyQ", "claim": "cursor"}}},
        {"id": "b", "type": "camera", "inputs": {"look": {"input": "Mouse.Move", "claim": "both"}}},
        {"id": "c", "type": "hold", "inputs": {"press": {"input": "Gamepad.A", "claim": "lock"}}},
        {"id": "d", "type": "tap", "inputs": {"press": {"input": "KeyB", "claim": "lock"}}}
    ])");
    CHECK_FALSE(keyed.ok());
    CHECK(has(keyed, DiagnosticCode::InputKindMismatch, "$.controls[0].inputs.press.claim"));
    CHECK(has(keyed, DiagnosticCode::InputKindMismatch, "$.controls[1].inputs.look.claim"));
    CHECK(has(keyed, DiagnosticCode::InputKindMismatch, "$.controls[2].inputs.press.claim"));
    CHECK(has(keyed, DiagnosticCode::InputKindMismatch, "$.controls[3].inputs.press.claim"));

    const LoadResult empty = loadWithControls(R"([
        {"id": "a", "type": "tap", "inputs": {"press": {}}},
        {"id": "b", "type": "hold", "inputs": {"press": {"claim": "cursor"}}}
    ])");
    CHECK_FALSE(empty.ok());
    CHECK(has(empty, DiagnosticCode::MissingField, "$.controls[0].inputs.press.input"));
    CHECK(has(empty, DiagnosticCode::MissingField, "$.controls[1].inputs.press.input"));
}

TEST_CASE("a component may have no key at all and is then used only by the logic (ADR-0091)")
{
    const LoadResult loaded = loadWithControls(R"([
        {"id": "loot", "type": "tap"},
        {"id": "fire", "type": "turbo", "rateHz": 20},
        {"id": "scope", "type": "hold"}
    ])");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    CHECK(count(loaded, Severity::Warning) == 0);
    const Profile& profile = loaded.profile;
    CHECK_FALSE(std::get<TapControl>(profile.controls[0].kind).press.has_value());
    CHECK(std::get<TurboControl>(profile.controls[1].kind).rateHz == 20);
    CHECK(boundInputs(profile).empty());
    CHECK(inputConflicts(profile).empty());
    CHECK(cursorClaims(profile).empty());

    const std::string saved = saveProfile(profile);
    CHECK(saved.find(R"("rateHz": 20)") != std::string::npos);
    CHECK(saved.find("\"inputs\"") == std::string::npos);
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == profile);
}

TEST_CASE("an instance of a custom component keeps its class, keys and values in file order and round-trips (ADR-0091)")
{
    const LoadResult loaded = loadWithControls(R"([
        {"id": "turn", "type": "component", "component": "examples/in-turn", "offset": [0.1, 0.2],
         "inputs": {"key": "KeyQ"},
         "props": {"third": [0.16, 0], "second": [0.08, -0.04], "gapMs": 120, "label": "下一个", "loop": true}}
    ])");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    CHECK(count(loaded, Severity::Warning) == 0);
    const auto& custom = std::get<ComponentControl>(loaded.profile.controls[0].kind);
    CHECK(custom.component == "examples/in-turn");
    REQUIRE(custom.keys.size() == 1);
    CHECK(custom.keys[0].name == "key");
    REQUIRE(custom.keys[0].binding.has_value());
    CHECK(custom.keys[0].binding->input == chord("KeyQ"));
    REQUIRE(custom.props.size() == 5);
    CHECK(custom.props[0].name == "third");
    CHECK(std::get<Vec2>(custom.props[0].value) == Vec2{0.16, 0});
    CHECK(custom.props[1].name == "second");
    CHECK(std::get<double>(custom.props[2].value) == 120);
    CHECK(std::get<std::string>(custom.props[3].value) == "下一个");
    CHECK(std::get<bool>(custom.props[4].value));
    CHECK(usesComponents(loaded.profile));
    CHECK(boundInputs(loaded.profile).size() == 1);

    const std::string saved = saveProfile(loaded.profile);
    CHECK(saved.find("\"third\"") < saved.find("\"second\""));
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == loaded.profile);
}

TEST_CASE("an instance names its class as examples/<name> or mine/<folder>, and its property names and values are checked")
{
    CHECK(isComponentRef("examples/double-tap"));
    CHECK(isComponentRef("mine/我的连击"));
    CHECK_FALSE(isComponentRef("double-tap"));
    CHECK_FALSE(isComponentRef("other/x"));
    CHECK_FALSE(isComponentRef("mine/"));
    CHECK_FALSE(isComponentRef("mine/.."));
    CHECK_FALSE(isComponentRef("mine/a/b"));
    CHECK_FALSE(isComponentRef("mine/a:b"));

    const LoadResult loaded = loadWithControls(R"([
        {"id": "a", "type": "component", "component": "../escape"},
        {"id": "b", "type": "component", "component": "mine/ok", "inputs": {"bad.key": "KeyB"},
         "props": {"bad.name": 1, "far": [5, 0]}},
        {"id": "c", "type": "component"}
    ])");
    INFO(dump(loaded));
    CHECK_FALSE(loaded.ok());
    CHECK(has(loaded, DiagnosticCode::InvalidValue, "$.controls[0].component"));
    CHECK(has(loaded, DiagnosticCode::InvalidValue, "$.controls[1].inputs.bad.key"));
    CHECK(has(loaded, DiagnosticCode::InvalidValue, "$.controls[1].props.bad.name"));
    CHECK(has(loaded, DiagnosticCode::OutOfRange, "$.controls[1].props.far[0]"));
    CHECK(has(loaded, DiagnosticCode::MissingField, "$.controls[2].component"));
    CHECK_FALSE(usesComponents(loadWithControls(R"([{"id": "jump", "type": "tap"}])").profile));
}

TEST_CASE("a built-in component placed from a class keeps the class, its extra keys and values, and round-trips (ADR-0091)")
{
    const LoadResult loaded = loadWithControls(R"([
        {"id": "scope", "type": "hold", "inputs": {"press": "Mouse.Right", "boost": "KeyB"},
         "component": "mine/quick-scope", "props": {"times": 2, "mark": [0.1, 0]}},
        {"id": "plain", "type": "hold", "inputs": {"press": "KeyP", "extra": "KeyX"}, "props": {"times": 1}},
        {"id": "jump", "type": "tap", "inputs": {"press": "KeyB"}}
    ])");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    const Control& scope = loaded.profile.controls[0];
    REQUIRE(std::get<HoldControl>(scope.kind).press.has_value());
    CHECK(std::get<HoldControl>(scope.kind).press->input == chord("Mouse.Right"));
    CHECK(scope.component == "mine/quick-scope");
    CHECK(componentRef(scope) == "mine/quick-scope");
    REQUIRE(scope.keys.size() == 1);
    CHECK(scope.keys[0].name == "boost");
    REQUIRE(scope.keys[0].binding.has_value());
    CHECK(scope.keys[0].binding->input == chord("KeyB"));
    REQUIRE(componentProps(scope).size() == 2);
    CHECK(std::get<double>(scope.props[0].value) == 2);
    CHECK(std::get<Vec2>(scope.props[1].value) == Vec2{0.1, 0});
    CHECK(usesComponents(loaded.profile));
    CHECK(boundInputs(loaded.profile).size() == 4);
    CHECK(has(loaded, DiagnosticCode::Conflict, "$.controls[2].inputs.press"));
    CHECK(has(loaded, DiagnosticCode::UnknownField, "$.controls[1].inputs.extra"));
    CHECK(has(loaded, DiagnosticCode::UnknownField, "$.controls[1].props"));
    CHECK(loaded.profile.controls[1].keys.empty());
    CHECK(componentRef(loaded.profile.controls[1]).empty());
    CHECK(inputSlots("hold") == std::vector<std::string_view>{"press"});
    CHECK(inputSlots("skill") == std::vector<std::string_view>{"press", "look", "cancel", "cast"});
    CHECK(inputSlots("component").empty());

    const std::string saved = saveProfile(loaded.profile);
    CHECK(saved.find("\"press\"") < saved.find("\"boost\""));
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == loaded.profile);
    const std::string settings = controlSettings(scope);
    CHECK(settings.find(R"("component":"mine/quick-scope")") != std::string::npos);
    CHECK(settings.find(R"("boost":"KeyB")") != std::string::npos);

    const LoadResult bad = loadWithControls(R"([
        {"id": "a", "type": "tap", "component": "../escape"},
        {"id": "b", "type": "tap", "component": ""},
        {"id": "c", "type": "tap", "component": "mine/ok", "inputs": {"bad.key": "KeyB"}}
    ])");
    INFO(dump(bad));
    CHECK_FALSE(bad.ok());
    CHECK(has(bad, DiagnosticCode::InvalidValue, "$.controls[0].component"));
    CHECK(has(bad, DiagnosticCode::InvalidValue, "$.controls[1].component"));
    CHECK(has(bad, DiagnosticCode::InvalidValue, "$.controls[2].inputs.bad.key"));
}

TEST_CASE("a child names its group and its place in the group, and a group that is not there is a warning (ADR-0091)")
{
    const LoadResult loaded = loadWithControls(R"([
        {"id": "volley", "type": "component", "component": "mine/volley"},
        {"id": "volley-cancel", "type": "tap", "parent": "volley", "child": "cancel"},
        {"id": "volley-skills-0", "type": "skill", "parent": "volley", "child": "skills.0"},
        {"id": "lost", "type": "tap", "parent": "nobody", "child": "cancel"},
        {"id": "self", "type": "tap", "parent": "self", "child": "cancel"},
        {"id": "half", "type": "tap", "child": "cancel"},
        {"id": "other", "type": "tap", "parent": "volley"}
    ])");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    CHECK(loaded.profile.controls[1].parent == "volley");
    CHECK(loaded.profile.controls[1].child == "cancel");
    CHECK(loaded.profile.controls[2].child == "skills.0");
    CHECK(has(loaded, DiagnosticCode::UnknownReference, "$.controls[3].parent"));
    CHECK(has(loaded, DiagnosticCode::UnknownReference, "$.controls[4].parent"));
    CHECK(has(loaded, DiagnosticCode::MissingField, "$.controls[5].parent"));
    CHECK(has(loaded, DiagnosticCode::MissingField, "$.controls[6].child"));
    CHECK(count(loaded, Severity::Warning) == 4);
    const std::string saved = saveProfile(loaded.profile);
    CHECK(saved.find(R"("child": "skills.0")") != std::string::npos);
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == loaded.profile);
    CHECK(controlSettings(loaded.profile.controls[1]).find(R"("parent":"volley")") != std::string::npos);

    const LoadResult bad = loadWithControls(R"([{"id": "x", "type": "tap", "parent": "bad id", "child": "a"}])");
    CHECK_FALSE(bad.ok());
    CHECK(has(bad, DiagnosticCode::InvalidValue, "$.controls[0].parent"));
}

TEST_CASE("a camera's crosshair is off by default and saved only when on")
{
    const LoadResult loaded = loadWithControls(R"([
        {"id": "look", "type": "camera", "inputs": {"look": "Mouse.Move"}, "crosshair": true},
        {"id": "plain", "type": "camera", "offset": [0.1, 0]}
    ])");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    CHECK(std::get<CameraControl>(loaded.profile.controls[0].kind).crosshair);
    CHECK_FALSE(std::get<CameraControl>(loaded.profile.controls[1].kind).crosshair);
    const std::string saved = saveProfile(loaded.profile);
    CHECK(saved.find(R"("crosshair": true)") != std::string::npos);
    CHECK(saved.find("\"crosshair\"") == saved.rfind("\"crosshair\""));
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == loaded.profile);
}

TEST_CASE("a direction wheel, a multi-press and a temporary cursor load with their defaults and round-trip (ADR-0094)")
{
    const LoadResult loaded = loadWithControls(R"([
        {"id": "weapons", "type": "dial", "radius": 0.2, "cells": 6, "firstDeg": 30,
         "inputs": {"press": "KeyQ", "look": "Mouse.Move"}},
        {"id": "plainDial", "type": "dial", "inputs": {"press": "KeyZ"}},
        {"id": "peekFire", "type": "multiPress", "points": [[0.1, 0], [0.05, -0.1]], "inputs": {"press": "KeyE"}},
        {"id": "pointer", "type": "tempCursor", "inputs": {"press": "AltLeft"}}
    ])");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    CHECK(count(loaded, Severity::Warning) == 0);
    const auto& weapons = std::get<DialControl>(loaded.profile.controls[0].kind);
    CHECK(weapons.radius == doctest::Approx(0.2));
    CHECK(weapons.cells == 6);
    CHECK(weapons.firstDeg == 30);
    CHECK(weapons.look->input == chord("Mouse.Move"));
    CHECK(weapons.look->claim == Claim::Both);
    const auto& plain = std::get<DialControl>(loaded.profile.controls[1].kind);
    CHECK(plain.cells == 8);
    CHECK(plain.firstDeg == 0);
    CHECK(plain.radius == doctest::Approx(0.12));
    CHECK_FALSE(plain.look.has_value());
    const auto& multi = std::get<MultiPressControl>(loaded.profile.controls[2].kind);
    REQUIRE(multi.points.size() == 2);
    CHECK(multi.points[1] == Vec2{0.05, -0.1});
    const auto& pointer = std::get<TempCursorControl>(loaded.profile.controls[3].kind);
    CHECK(pointer.press->claim == Claim::Both);
    CHECK(inputSlots("dial") == std::vector<std::string_view>{"press", "look"});
    CHECK(inputSlots("tempCursor") == std::vector<std::string_view>{"press"});

    const std::string saved = saveProfile(loaded.profile);
    CHECK(saved.find("\"claim\"") == std::string::npos);
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == loaded.profile);
}

TEST_CASE("an action wheel keeps its cells, defaults to eight empty ones and checks what a cell points at (ADR-0094)")
{
    const LoadResult loaded = loadWithControls(R"([
        {"id": "potion", "type": "tap", "inputs": {"press": "KeyH"}},
        {"id": "items", "type": "actionWheel", "inputs": {"press": "KeyB"},
         "cells": [{"name": "potion", "action": "control", "control": "potion"},
                   {"name": "spot", "action": "tap", "at": [0.2, -0.1]},
                   {"action": "logic"}]},
        {"id": "plainWheel", "type": "actionWheel", "inputs": {"press": "KeyN"}}
    ])");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    CHECK(count(loaded, Severity::Warning) == 0);
    const auto& items = std::get<ActionWheelControl>(loaded.profile.controls[1].kind);
    REQUIRE(items.cells.size() == 3);
    CHECK(items.cells[0].action == WheelAction::Control);
    CHECK(items.cells[0].control == "potion");
    CHECK(items.cells[1].action == WheelAction::Tap);
    CHECK(items.cells[1].at == Vec2{0.2, -0.1});
    CHECK(items.cells[2].action == WheelAction::Logic);
    CHECK(items.cells[2].name.empty());
    const auto& plain = std::get<ActionWheelControl>(loaded.profile.controls[2].kind);
    REQUIRE(plain.cells.size() == 8);
    CHECK(std::ranges::all_of(plain.cells, [](const WheelCell& cell) { return cell.action == WheelAction::None; }));
    CHECK(inputSlots("actionWheel") == std::vector<std::string_view>{"press", "look"});

    const LoadResult again = loadProfile(saveProfile(loaded.profile));
    REQUIRE(again.ok());
    CHECK(again.profile == loaded.profile);

    const LoadResult one = loadWithControls(R"([{"id": "w", "type": "actionWheel", "cells": [{}]}])");
    CHECK_FALSE(one.ok());
    const LoadResult many = loadWithControls(R"([{"id": "w", "type": "actionWheel", "cells": [
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}]}])");
    CHECK_FALSE(many.ok());
    const LoadResult missing = loadWithControls(R"([{"id": "w", "type": "actionWheel",
        "cells": [{"action": "control", "control": "nothing"}, {"action": "control", "control": "w"}]}])");
    REQUIRE(missing.ok());
    CHECK(count(missing, Severity::Warning) == 2);
    const LoadResult longName = loadWithControls(R"([{"id": "w", "type": "actionWheel",
        "cells": [{"name": "abcdefghijklmnopqrstuvwxyz"}, {}]}])");
    CHECK_FALSE(longName.ok());
}

TEST_CASE("two bindings on one mouse button conflict only when their claims overlap")
{
    const LoadResult loaded = loadProfile(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [
            {"id": "walk", "type": "rightWalk", "inputs": {"walkTo": "Mouse.Right"}},
            {"id": "scope", "type": "hold", "inputs": {"press": "Mouse.Right"}},
            {"id": "any", "type": "tap", "inputs": {"press": {"input": "Mouse.Middle", "claim": "both"}}},
            {"id": "aimed", "type": "tap", "inputs": {"press": "Mouse.Middle"}},
            {"id": "roam", "type": "tap", "inputs": {"press": {"input": "Mouse.Middle", "claim": "cursor"}}},
            {"id": "side", "type": "tap", "inputs": {"press": {"input": "Mouse.X1", "claim": "cursor"}}},
            {"id": "other", "type": "hold", "inputs": {"press": {"input": "Mouse.X1", "claim": "both"}}}
        ]})");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    const std::vector<InputConflict> conflicts = inputConflicts(loaded.profile);
    REQUIRE(conflicts.size() == 2);
    CHECK(conflicts[0].input == chord("Mouse.Middle"));
    REQUIRE(conflicts[0].bindings.size() == 3);
    CHECK(conflicts[0].bindings[0].path == "$.controls[2].inputs.press");
    CHECK(conflicts[0].bindings[1].path == "$.controls[3].inputs.press");
    CHECK(conflicts[0].bindings[2].path == "$.controls[4].inputs.press");
    CHECK(conflicts[1].input == chord("Mouse.X1"));
    CHECK(conflicts[1].bindings.size() == 2);
    CHECK(std::ranges::count(loaded.diagnostics, DiagnosticCode::Conflict, &Diagnostic::code) == 3);
    CHECK_FALSE(has(loaded, DiagnosticCode::Conflict, "$.controls[1].inputs.press"));
}

TEST_CASE("a skill's pauses must name a movement component, and a look binding is ignored when it follows the cursor")
{
    const LoadResult missing = loadWithControls(
        R"([{"id": "skill", "type": "skill", "inputs": {"press": "KeyQ"}, "pauses": "nowhere"}])");
    CHECK(has(missing, DiagnosticCode::UnknownReference, "$.controls[0].pauses"));
    CHECK_FALSE(missing.ok());

    const LoadResult notStick = loadWithControls(
        R"([{"id": "fire", "type": "hold", "inputs": {"press": "KeyF"}},
            {"id": "skill", "type": "skill", "inputs": {"press": "KeyQ"}, "pauses": "fire"}])");
    CHECK(has(notStick, DiagnosticCode::UnknownReference, "$.controls[1].pauses"));

    for (const std::string_view type : {"wasd", "rightWalk", "padStick"}) {
        const LoadResult moving = loadWithControls(R"([{"id": "move", "type": ")" + std::string(type) +
                                                   R"("}, {"id": "skill", "type": "skill", "pauses": "move"}])");
        INFO(type);
        CHECK(moving.ok());
    }

    const LoadResult looking = loadWithControls(
        R"([{"id": "skill", "type": "skill", "inputs": {"press": "KeyQ", "look": "Mouse.Move"}, "direction": "cursor"}])");
    CHECK(looking.ok());
    CHECK(has(looking, DiagnosticCode::InvalidValue, "$.controls[0].inputs.look"));

    const LoadResult wrong = loadWithControls(
        R"([{"id": "skill", "type": "skill", "inputs": {"press": "KeyQ"}, "cast": "later", "cancelAt": [3, 0]}])");
    CHECK_FALSE(wrong.ok());
    CHECK(has(wrong, DiagnosticCode::InvalidValue, "$.controls[0].cast"));
    CHECK(has(wrong, DiagnosticCode::OutOfRange, "$.controls[0].cancelAt[0]"));
}

TEST_CASE("missing fields take the documented defaults (PRF-002)")
{
    const LoadResult result = loadWithControls(R"([{ "id": "move", "type": "wasd" }, { "id": "long", "type": "longPress" },
                                                   { "id": "fast", "type": "turbo" }])");
    REQUIRE(result.ok());
    const auto& move = std::get<WasdControl>(result.profile.controls[0].kind);
    CHECK(move.radius == doctest::Approx(0.12));
    CHECK(move.sprintScale == doctest::Approx(1.6));
    CHECK_FALSE(move.up.has_value());
    CHECK(std::get<LongPressControl>(result.profile.controls[1].kind).holdMs == 1000);
    CHECK(std::get<TurboControl>(result.profile.controls[2].kind).rateHz == 10);
    CHECK(result.profile.controls[0].anchor == Anchor::Center);
    CHECK(has(result, DiagnosticCode::DefaultUsed, "$.controls[0].radius"));
    CHECK(has(result, DiagnosticCode::DefaultUsed, "$.meta"));
}

TEST_CASE("controls and variables carry an optional name of up to 64 characters")
{
    const LoadResult loaded = loadProfile(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [{"id": "fire", "name": "开火", "type": "hold"}, {"id": "move", "type": "wasd"}],
        "vars": [{"id": "bag", "name": "背包"}]})");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    CHECK(loaded.profile.controls[0].name == "开火");
    CHECK(loaded.profile.controls[1].name.empty());
    CHECK(loaded.profile.vars[0].name == "背包");

    const std::string saved = saveProfile(loaded.profile);
    std::size_t names = 0;
    for (std::size_t at = saved.find("\"name\""); at != std::string::npos; at = saved.find("\"name\"", at + 1)) {
        ++names;
    }
    CHECK(names == 2);
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == loaded.profile);

    std::string longest;
    for (int i = 0; i < 64; ++i) {
        longest += "键";
    }
    CHECK(loadWithControls(R"([{"id": "a", "type": "tap", "name": ")" + longest + R"("}])").ok());
    const LoadResult tooLong = loadWithControls(R"([{"id": "a", "type": "tap", "name": ")" + longest + R"(键"}])");
    CHECK_FALSE(tooLong.ok());
    CHECK(has(tooLong, DiagnosticCode::OutOfRange, "$.controls[0].name"));
}

TEST_CASE("a control's code name stays its own once set by hand, and is written only then")
{
    const LoadResult loaded = loadProfile(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [{"id": "holdF", "type": "hold", "inputs": {"press": "KeyF"}},
                     {"id": "holdG", "type": "hold", "idFixed": true, "inputs": {"press": "KeyG"}}]})");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    CHECK_FALSE(loaded.profile.controls[0].idFixed);
    CHECK(loaded.profile.controls[1].idFixed);

    const std::string saved = saveProfile(loaded.profile);
    CHECK(saved.find(R"("idFixed": true)") != std::string::npos);
    std::size_t written = 0;
    for (std::size_t at = saved.find("\"idFixed\""); at != std::string::npos; at = saved.find("\"idFixed\"", at + 1)) {
        ++written;
    }
    CHECK(written == 1);
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == loaded.profile);

    const LoadResult bad = loadWithControls(R"([{"id": "a", "type": "tap", "idFixed": "yes"}])");
    CHECK_FALSE(bad.ok());
    CHECK(has(bad, DiagnosticCode::WrongType, "$.controls[0].idFixed"));
}

TEST_CASE("a variable starts as a switch, a number or a text, and anything else is an error")
{
    const LoadResult loaded = loadProfile(R"({"format":"gamescrcpy.profile","version":3,
        "vars": [{"id": "on", "initial": true}, {"id": "count", "initial": 2.5}, {"id": "label", "initial": "甲"},
                 {"id": "off"}]})");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    const std::string saved = saveProfile(loaded.profile);
    CHECK(saved.find(R"("initial": true)") != std::string::npos);
    CHECK(saved.find(R"("initial": 2.5)") != std::string::npos);
    CHECK(saved.find(R"("initial": "甲")") != std::string::npos);
    CHECK(saved.find(R"("initial": false)") == std::string::npos);
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == loaded.profile);

    const LoadResult bad = loadProfile(R"({"format":"gamescrcpy.profile","version":3,
        "vars": [{"id": "list", "initial": [1]}, {"id": "map", "initial": {}}]})");
    CHECK_FALSE(bad.ok());
    CHECK(has(bad, DiagnosticCode::WrongType, "$.vars[0].initial"));
    CHECK(has(bad, DiagnosticCode::WrongType, "$.vars[1].initial"));
}

TEST_CASE("unknown fields are reported and ignored (PRF-002)")
{
    const LoadResult result = loadProfile(R"({"format":"gamescrcpy.profile","version":3,"colour":"red",
        "controls":[{"id":"a","type":"tap","inputs":{"pres":"KeyA"},"radiuss":1}]})");
    CHECK(result.ok());
    CHECK(has(result, DiagnosticCode::UnknownField, "$.colour"));
    CHECK(has(result, DiagnosticCode::UnknownField, "$.controls[0].inputs.pres"));
    CHECK(has(result, DiagnosticCode::UnknownField, "$.controls[0].radiuss"));
}

TEST_CASE("fields that belong to another component or an older format are unknown in version 3")
{
    const LoadResult result = loadWithControls(R"([
        { "id": "fire", "type": "hold", "tapMs": 40, "logicOnly": true },
        { "id": "jump", "type": "tap", "inputs": { "press": { "input": "Space", "trigger": "tap" } } },
        { "id": "move", "type": "wasd", "inputs": { "walkTo": "Mouse.Right" }, "mode": "smooth" }
    ])");
    INFO(dump(result));
    CHECK(result.ok());
    CHECK(has(result, DiagnosticCode::UnknownField, "$.controls[0].tapMs"));
    CHECK(has(result, DiagnosticCode::UnknownField, "$.controls[0].logicOnly"));
    CHECK(has(result, DiagnosticCode::UnknownField, "$.controls[1].inputs.press.trigger"));
    CHECK(has(result, DiagnosticCode::UnknownField, "$.controls[2].inputs.walkTo"));
    CHECK(has(result, DiagnosticCode::UnknownField, "$.controls[2].mode"));
    const LoadResult top = loadProfile(R"({"format":"gamescrcpy.profile","version":3,
        "commands":[{"input":"Escape","command":"android.back"}],"states":[{"id":"bag"}],"controls":[]})");
    CHECK(top.ok());
    CHECK(has(top, DiagnosticCode::UnknownField, "$.commands"));
    CHECK(has(top, DiagnosticCode::UnknownField, "$.states"));
}

TEST_CASE("errors name the offending JSON path (PRF-002)")
{
    struct Case {
        std::string_view controls;
        DiagnosticCode code;
        std::string_view path;
    };
    const Case cases[] = {
        {R"([{"type":"tap"}])", DiagnosticCode::MissingField, "$.controls[0].id"},
        {R"([{"id":"bad id","type":"tap"}])", DiagnosticCode::InvalidValue, "$.controls[0].id"},
        {R"([{"id":"a","type":"lever"}])", DiagnosticCode::InvalidValue, "$.controls[0].type"},
        {R"([{"id":"a","type":"button"}])", DiagnosticCode::InvalidValue, "$.controls[0].type"},
        {R"([{"id":"a","type":"tap","anchor":"middle"}])", DiagnosticCode::InvalidValue, "$.controls[0].anchor"},
        {R"([{"id":"a","type":"wasd","radius":"big"}])", DiagnosticCode::WrongType, "$.controls[0].radius"},
        {R"([{"id":"a","type":"padStick","radius":3}])", DiagnosticCode::OutOfRange, "$.controls[0].radius"},
        {R"([{"id":"a","type":"tap","tapMs":2.5}])", DiagnosticCode::WrongType, "$.controls[0].tapMs"},
        {R"([{"id":"a","type":"longPress","holdMs":20}])", DiagnosticCode::OutOfRange, "$.controls[0].holdMs"},
        {R"([{"id":"a","type":"turbo","rateHz":99}])", DiagnosticCode::OutOfRange, "$.controls[0].rateHz"},
        {R"([{"id":"a","type":"hold","offset":[0,9]}])", DiagnosticCode::OutOfRange, "$.controls[0].offset[1]"},
        {R"([{"id":"a","type":"tap","inputs":{"press":"Key_W"}}])", DiagnosticCode::InvalidInput,
         "$.controls[0].inputs.press"},
        {R"([{"id":"a","type":"hold","inputs":{"press":"Mouse.Move"}}])", DiagnosticCode::InputKindMismatch,
         "$.controls[0].inputs.press"},
        {R"([{"id":"a","type":"camera","inputs":{"look":"KeyW"}}])", DiagnosticCode::InputKindMismatch,
         "$.controls[0].inputs.look"},
        {R"([{"id":"a","type":"padStick","inputs":{"stick":"Gamepad.A"}}])", DiagnosticCode::InputKindMismatch,
         "$.controls[0].inputs.stick"},
        {R"([{"id":"a","type":"swipe","path":[[0,0]]}])", DiagnosticCode::TooFewPoints, "$.controls[0].path"},
        {R"([{"id":"a","type":"swipe"}])", DiagnosticCode::MissingField, "$.controls[0].path"},
        {R"([{"id":"a","type":"dial","cells":1}])", DiagnosticCode::OutOfRange, "$.controls[0].cells"},
        {R"([{"id":"a","type":"dial","firstDeg":360}])", DiagnosticCode::OutOfRange, "$.controls[0].firstDeg"},
        {R"([{"id":"a","type":"dial","inputs":{"look":"KeyW"}}])", DiagnosticCode::InputKindMismatch,
         "$.controls[0].inputs.look"},
        {R"([{"id":"a","type":"multiPress","points":[]}])", DiagnosticCode::TooFewPoints, "$.controls[0].points"},
        {R"([{"id":"a","type":"multiPress","points":[[0,0],[0,0],[0,0],[0,0],[0,0],[0,0],[0,0],[0,0],[0,0],[0,0]]}])",
         DiagnosticCode::OutOfRange, "$.controls[0].points"},
        {R"([{"id":"a","type":"tap"},{"id":"a","type":"hold"}])", DiagnosticCode::DuplicateId, "$.controls[1].id"},
        {R"([3])", DiagnosticCode::WrongType, "$.controls[0]"},
    };
    for (const Case& c : cases) {
        INFO(c.controls);
        const LoadResult result = loadWithControls(c.controls);
        INFO(dump(result));
        CHECK_FALSE(result.ok());
        CHECK(has(result, c.code, c.path));
    }
}

TEST_CASE("file-level problems are rejected before or at the top level (PRF-002)")
{
    CHECK(has(loadProfile("{"), DiagnosticCode::MalformedJson, "$"));
    CHECK(has(loadProfile("[]"), DiagnosticCode::WrongType, "$"));
    CHECK(has(loadProfile(R"({"version":3})"), DiagnosticCode::MissingField, "$.format"));
    CHECK(has(loadProfile(R"({"format":"other","version":3})"), DiagnosticCode::WrongFormat, "$.format"));
    CHECK(has(loadProfile(R"({"format":"gamescrcpy.profile","version":1})"), DiagnosticCode::WrongVersion,
              "$.version"));
    CHECK(has(loadProfile(R"({"format":"gamescrcpy.profile","version":4})"), DiagnosticCode::WrongVersion,
              "$.version"));
    CHECK(has(loadProfile(std::string(100, '[') + std::string(100, ']')), DiagnosticCode::TooLarge, "$"));
    CHECK(has(loadProfile(std::string(1024 * 1024 + 1, ' ')), DiagnosticCode::TooLarge, "$"));
}

TEST_CASE("references between controls and variables must resolve (PRF-002)")
{
    const LoadResult result = loadProfile(R"({"format":"gamescrcpy.profile","version":3,
        "controls":[{"id":"fire","type":"hold"},
                    {"id":"use","when":"nowhere","type":"tap"},
                    {"id":"bad","when":"!!bag","type":"tap"}],
        "vars":[{"id":"bag"},{"id":"fire"}]})");
    INFO(dump(result));
    CHECK(has(result, DiagnosticCode::UnknownReference, "$.controls[1].when"));
    CHECK(has(result, DiagnosticCode::InvalidValue, "$.controls[2].when"));
    CHECK(has(result, DiagnosticCode::DuplicateId, "$.vars[1].id"));
}

TEST_CASE("conflicts are reported for bindings that are live together, not for exclusive states (PRF-003)")
{
    const LoadResult result = loadProfile(R"({"format":"gamescrcpy.profile","version":3,
        "controls":[{"id":"a","type":"hold","inputs":{"press":"KeyF"}},
                    {"id":"b","type":"tap","inputs":{"press":"KeyF"}},
                    {"id":"c","type":"tap","inputs":{"press":"KeyR"}},
                    {"id":"d","type":"tap","inputs":{"press":"Ctrl+KeyF"}},
                    {"id":"walk","when":"!boat","type":"hold","inputs":{"press":"KeyW"}},
                    {"id":"row","when":"boat","type":"hold","inputs":{"press":"KeyW"}},
                    {"id":"spare","enabled":false,"type":"tap","inputs":{"press":"KeyF"}},
                    {"id":"look","type":"camera","lockKey":"KeyE","inputs":{"look":"Mouse.Move"}},
                    {"id":"use","type":"tap","inputs":{"press":"KeyE"}}],
        "vars":[{"id":"boat"}]})");
    INFO(dump(result));
    CHECK(result.ok());
    CHECK(has(result, DiagnosticCode::Conflict, "$.controls[1].inputs.press"));
    CHECK_FALSE(has(result, DiagnosticCode::Conflict, "$.controls[3].inputs.press"));
    CHECK_FALSE(has(result, DiagnosticCode::Conflict, "$.controls[5].inputs.press"));
    CHECK_FALSE(has(result, DiagnosticCode::Conflict, "$.controls[6].inputs.press"));
    CHECK(has(result, DiagnosticCode::Conflict, "$.controls[8].inputs.press"));
    CHECK(count(result, Severity::Warning) == 2);
}

TEST_CASE("always-on two-dimensional bindings conflict, held ones take over without conflicting (PRF-003)")
{
    const LoadResult result = loadWithControls(R"([
        {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}},
        {"id":"look2","type":"camera","inputs":{"look":"Mouse.Move"}},
        {"id":"peek","type":"freeLook","inputs":{"hold":"Mouse.Middle","look":"Mouse.Move"}},
        {"id":"aim","type":"skill","inputs":{"press":"KeyG","look":"Mouse.Move"}},
        {"id":"pad","type":"padCamera","inputs":{"look":"Gamepad.RightStick"}},
        {"id":"pad2","type":"padCamera","inputs":{"look":"Gamepad.RightStick"}}])");
    INFO(dump(result));
    CHECK(has(result, DiagnosticCode::Conflict, "$.controls[1].inputs.look"));
    CHECK_FALSE(has(result, DiagnosticCode::Conflict, "$.controls[2].inputs.look"));
    CHECK_FALSE(has(result, DiagnosticCode::Conflict, "$.controls[3].inputs.look"));
    CHECK(has(result, DiagnosticCode::Conflict, "$.controls[5].inputs.look"));
}

TEST_CASE("mutated profiles are rejected or loaded, never crash")
{
    std::mt19937 rng{20261001U};
    const std::string seed(kFullProfile);
    std::size_t loaded = 0;
    for (int round = 0; round < 2000; ++round) {
        std::string mutated = seed;
        const int edits = 1 + static_cast<int>(rng() % 4U);
        for (int e = 0; e < edits && !mutated.empty(); ++e) {
            const std::size_t at = rng() % mutated.size();
            switch (rng() % 3U) {
            case 0: mutated[at] = static_cast<char>(rng() % 128U); break;
            case 1: mutated.erase(at, 1 + rng() % 8U); break;
            default: mutated.insert(at, 1, "{}[],:\"0e-"[rng() % 10U]); break;
            }
        }
        const LoadResult result = loadProfile(mutated);
        if (result.ok()) {
            ++loaded;
            CHECK(loadProfile(saveProfile(result.profile)).ok());
        }
    }
    CHECK(loaded > 0);
}

TEST_CASE("saving writes only non-default fields and round-trips losslessly (PRF-004)")
{
    CHECK(saveProfile(Profile{}) == "{\n  \"format\": \"gamescrcpy.profile\",\n  \"version\": 3\n}\n");

    Profile minimal;
    minimal.controls.push_back(Control{.id = "jump", .kind = TapControl{.press = Binding{.input = chord("Space")}}});
    CHECK(saveProfile(minimal) == R"({
  "format": "gamescrcpy.profile",
  "version": 3,
  "controls": [
    {
      "id": "jump",
      "type": "tap",
      "inputs": {
        "press": "Space"
      }
    }
  ]
}
)");

    const LoadResult first = loadProfile(kFullProfile);
    REQUIRE(first.ok());
    const std::string saved = saveProfile(first.profile);
    const LoadResult second = loadProfile(saved);
    INFO(saved);
    REQUIRE(second.ok());
    CHECK(second.profile == first.profile);
    CHECK(saveProfile(second.profile) == saved);
}

TEST_CASE("mutated profiles with claims, cast keys, a crosshair and key-less components save back to what they loaded")
{
    constexpr std::string_view kSeed = R"json({"format": "gamescrcpy.profile", "version": 3,
  "controls": [
    {"id": "move", "type": "rightWalk",
     "inputs": {"walkTo": "Mouse.Right", "stop": {"input": "Mouse.X1", "claim": "both"}}},
    {"id": "look", "type": "camera", "inputs": {"look": "Mouse.Move"}, "lockKey": "Mouse.X2", "crosshair": true},
    {"id": "fire", "type": "turbo", "inputs": {"press": {"input": "Mouse.Left", "claim": "lock"}}, "rateHz": 12},
    {"id": "hold", "type": "longPress", "holdMs": 800},
    {"id": "skill", "type": "skill", "direction": "cursor", "cast": "manual",
     "inputs": {"press": "KeyQ", "cast": {"input": "Mouse.Left", "claim": "cursor"},
                "cancel": {"input": "Mouse.Middle", "claim": "cursor"}}},
    {"id": "zoom", "type": "pinch", "inputs": {"zoomIn": {"input": "Mouse.WheelUp", "claim": "both"}}}
  ],
  "vars": [{"id": "bag", "initial": true}, {"id": "kills", "initial": 7}]
})json";
    const LoadResult seed = loadProfile(kSeed);
    INFO(dump(seed));
    REQUIRE(seed.ok());
    std::mt19937 rng{20261030U};
    std::size_t loaded = 0;
    for (int round = 0; round < 2000; ++round) {
        std::string mutated(kSeed);
        const int edits = 1 + static_cast<int>(rng() % 4U);
        for (int e = 0; e < edits && !mutated.empty(); ++e) {
            const std::size_t at = rng() % mutated.size();
            switch (rng() % 3U) {
            case 0: mutated[at] = static_cast<char>(rng() % 128U); break;
            case 1: mutated.erase(at, 1 + rng() % 8U); break;
            default: mutated.insert(at, 1, "{}[],:\"0e-"[rng() % 10U]); break;
            }
        }
        const LoadResult result = loadProfile(mutated);
        if (!result.ok()) {
            continue;
        }
        ++loaded;
        INFO(mutated);
        const LoadResult again = loadProfile(saveProfile(result.profile));
        CHECK(again.ok());
        CHECK(again.profile == result.profile);
    }
    CHECK(loaded > 0);
}

TEST_CASE("a version 2 profile is converted on load: buttons by trigger, joysticks split, commands dropped (ADR-0091)")
{
    const LoadResult loaded = loadProfile(R"({"format":"gamescrcpy.profile","version":2,
        "controls": [
            {"id": "move", "name": "移动", "type": "joystick", "anchor": "bottomLeft", "offset": [0.3, -0.3],
             "inputs": {"up": "KeyW", "walkTo": "Mouse.Right", "stick": "Gamepad.LeftStick"}, "radius": 0.15,
             "walkMsPerUnit": 450},
            {"id": "fire", "type": "button", "inputs": {"press": {"input": "Mouse.Left", "trigger": "turbo", "rateHz": 12}},
             "tapMs": 30},
            {"id": "jump", "type": "button", "inputs": {"press": {"input": "Space", "trigger": "tap"}}, "tapMs": 40},
            {"id": "scope", "type": "button", "inputs": {"press": "Mouse.Right"}, "tapMs": 50},
            {"id": "lean", "type": "button", "inputs": {"press": {"input": "KeyQ", "trigger": "toggle"}}},
            {"id": "combo", "type": "button", "inputs": {"press": "KeyC"}, "logicOnly": true},
            {"id": "spare", "type": "button", "inputs": {"press": {"trigger": "longPress"}}},
            {"id": "look", "type": "camera", "inputs": {"look": "Mouse.Move"}, "lockKey": "Backquote"},
            {"id": "pad", "type": "camera", "inputs": {"look": "Gamepad.RightStick"}, "lockKey": "KeyL",
             "crosshair": true},
            {"id": "skill", "type": "aim", "inputs": {"press": "KeyE"}, "direction": "cursor", "pauses": "move"},
            {"id": "keys", "type": "joystick", "offset": [0.1, 0.1]}
        ],
        "commands": [{"input": "Escape", "command": "android.back"},
                     {"id": "bagKey", "input": "Tab", "command": "state", "state": "bag", "action": "toggle"}],
        "states": [{"id": "bag", "name": "背包"}, {"id": "car", "initial": true}]})");
    INFO(dump(loaded));
    REQUIRE(loaded.ok());
    CHECK(loaded.converted());
    CHECK(has(loaded, DiagnosticCode::Converted, "$.version"));
    CHECK(has(loaded, DiagnosticCode::Converted, "$.commands[0]"));
    CHECK(has(loaded, DiagnosticCode::Converted, "$.commands[1]"));
    CHECK(has(loaded, DiagnosticCode::Converted, "$.controls[4].inputs.press.trigger"));
    CHECK(has(loaded, DiagnosticCode::Converted, "$.controls[5].logicOnly"));
    CHECK(std::none_of(loaded.diagnostics.begin(), loaded.diagnostics.end(),
                       [](const Diagnostic& d) { return d.code == DiagnosticCode::UnknownField; }));
    const Profile& p = loaded.profile;
    REQUIRE(p.controls.size() == 13);

    CHECK(p.controls[0].id == "move");
    CHECK(p.controls[0].name == "移动");
    const auto& wasd = std::get<WasdControl>(p.controls[0].kind);
    CHECK(wasd.up->input == chord("KeyW"));
    CHECK(wasd.radius == doctest::Approx(0.15));
    CHECK(p.controls[1].id == "move-walk");
    CHECK(p.controls[1].anchor == Anchor::BottomLeft);
    CHECK(p.controls[1].offset == Vec2{0.3, -0.3});
    const auto& walk = std::get<RightWalkControl>(p.controls[1].kind);
    CHECK(walk.walkTo->input == chord("Mouse.Right"));
    CHECK(walk.walkMsPerUnit == 450);
    CHECK(walk.radius == doctest::Approx(0.15));
    CHECK(p.controls[2].id == "move-pad");
    CHECK(std::get<PadStickControl>(p.controls[2].kind).stick->input == chord("Gamepad.LeftStick"));

    const auto& fire = std::get<TurboControl>(p.controls[3].kind);
    CHECK(fire.rateHz == 12);
    CHECK(fire.tapMs == 30);
    CHECK(std::get<TapControl>(p.controls[4].kind).tapMs == 40);
    CHECK(std::holds_alternative<HoldControl>(p.controls[5].kind));
    CHECK(std::get<HoldControl>(p.controls[6].kind).press->input == chord("KeyQ"));
    CHECK_FALSE(std::get<HoldControl>(p.controls[7].kind).press.has_value());
    CHECK_FALSE(std::get<HoldControl>(p.controls[8].kind).press.has_value());
    CHECK(std::get<CameraControl>(p.controls[9].kind).lockKey == chord("Backquote"));
    CHECK(std::get<PadCameraControl>(p.controls[10].kind).look->input == chord("Gamepad.RightStick"));
    const auto& skill = std::get<SkillControl>(p.controls[11].kind);
    CHECK(skill.direction == AimDirection::Cursor);
    CHECK(skill.pauses == "move-walk");
    CHECK(p.controls[12].id == "keys");
    CHECK(std::holds_alternative<WasdControl>(p.controls[12].kind));

    REQUIRE(p.vars.size() == 2);
    CHECK(p.vars[0].name == "背包");
    CHECK(p.vars[1].initial == VarValue{true});

    const std::string saved = saveProfile(p);
    CHECK(saved.find("\"version\": 3") != std::string::npos);
    CHECK(saved.find("\"commands\"") == std::string::npos);
    CHECK(saved.find("\"states\"") == std::string::npos);
    CHECK(saved.find("\"trigger\"") == std::string::npos);
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK_FALSE(again.converted());
    CHECK(again.profile == p);
}

TEST_CASE("upgradeProfile turns a version 2 text into version 3 and leaves anything else alone")
{
    const std::string old = R"({"format":"gamescrcpy.profile","version":2,
        "controls":[{"id":"jump","type":"button","inputs":{"press":{"input":"Space","trigger":"tap"}}}]})";
    std::vector<Diagnostic> notes;
    const std::string upgraded = upgradeProfile(old, &notes);
    CHECK(upgraded.find("\"version\": 3") != std::string::npos);
    CHECK(upgraded.find("\"type\": \"tap\"") != std::string::npos);
    CHECK_FALSE(notes.empty());
    CHECK(loadProfile(upgraded).ok());

    const std::string current(kFullProfile);
    CHECK(upgradeProfile(current) == current);
    CHECK(upgradeProfile("{") == "{");
    CHECK(upgradeProfile(R"({"format":"other","version":2})") == R"({"format":"other","version":2})");
}
