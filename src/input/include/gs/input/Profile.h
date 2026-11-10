#pragma once

#include <gs/input/Geometry.h>
#include <gs/input/InputSource.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gs::input {

enum class Orientation : std::uint8_t { Landscape, Portrait };
enum class PointerState : std::uint8_t { Free, Captured };
enum class Switch : std::uint8_t { On, Off, Toggle };

enum class AimDirection : std::uint8_t { Mouse, Cursor };
enum class CastTiming : std::uint8_t { Release, Press, Manual };
enum class Claim : std::uint8_t { Lock, Cursor, Both };
enum class CameraRange : std::uint8_t { Screen, Box };

enum class SlotKind : std::uint8_t { Digital, Axis2D, HeldAxis2D };

inline constexpr std::string_view kFormatName = "gamescrcpy.profile";
inline constexpr int kFormatVersion = 3;

inline constexpr std::array<std::string_view, 2> kOrientationNames{"landscape", "portrait"};
inline constexpr std::array<std::string_view, 9> kAnchorNames{"topLeft", "top", "topRight", "left", "center",
                                                              "right", "bottomLeft", "bottom", "bottomRight"};
inline constexpr std::array<std::string_view, 2> kScaleModeNames{"shortSide", "stretch"};
inline constexpr std::array<std::string_view, 2> kSafeAreaNames{"ignore", "respect"};
inline constexpr std::array<std::string_view, 3> kSwitchNames{"on", "off", "toggle"};
inline constexpr std::array<std::string_view, 2> kAimDirectionNames{"mouse", "cursor"};
inline constexpr std::array<std::string_view, 3> kCastTimingNames{"release", "press", "manual"};
inline constexpr std::array<std::string_view, 2> kCameraRangeNames{"screen", "box"};
inline constexpr std::array<std::string_view, 3> kClaimNames{"lock", "cursor", "both"};

enum class GamepadMode : std::uint8_t { Map, Passthrough };
inline constexpr std::array<std::string_view, 2> kGamepadModeNames{"map", "passthrough"};

struct Binding {
    std::optional<Chord> input;
    Claim claim = Claim::Lock;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.optionalChord("input", m.input);
        v.choice("claim", m.claim, kClaimNames);
    }

    friend bool operator==(const Binding&, const Binding&) = default;
};

[[nodiscard]] bool claimable(const std::optional<Chord>& input) noexcept;
[[nodiscard]] bool claimsLocked(Claim claim) noexcept;
[[nodiscard]] bool claimsCursor(Claim claim) noexcept;

struct TapControl {
    std::optional<Binding> press;
    std::uint16_t tapMs = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) { in.binding("press", m.press, SlotKind::Digital); });
        v.integer("tapMs", m.tapMs, 0, 1000);
    }

    friend bool operator==(const TapControl&, const TapControl&) = default;
};

struct HoldControl {
    std::optional<Binding> press;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) { in.binding("press", m.press, SlotKind::Digital); });
    }

    friend bool operator==(const HoldControl&, const HoldControl&) = default;
};

struct LongPressControl {
    std::optional<Binding> press;
    std::uint16_t holdMs = 1000;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) { in.binding("press", m.press, SlotKind::Digital); });
        v.integer("holdMs", m.holdMs, 100, 10000);
    }

    friend bool operator==(const LongPressControl&, const LongPressControl&) = default;
};

struct TurboControl {
    std::optional<Binding> press;
    std::uint8_t rateHz = 10;
    std::uint16_t tapMs = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) { in.binding("press", m.press, SlotKind::Digital); });
        v.integer("rateHz", m.rateHz, 1, 30);
        v.integer("tapMs", m.tapMs, 0, 1000);
    }

    friend bool operator==(const TurboControl&, const TurboControl&) = default;
};

struct SwipeControl {
    std::optional<Binding> press;
    std::vector<Vec2> path;
    std::uint16_t durationMs = 200;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) { in.binding("press", m.press, SlotKind::Digital); });
        v.points("path", m.path, 2, -2.0, 2.0);
        v.integer("durationMs", m.durationMs, 10, 10000);
    }

    friend bool operator==(const SwipeControl&, const SwipeControl&) = default;
};

struct PinchControl {
    std::optional<Binding> zoomIn;
    std::optional<Binding> zoomOut;
    double span = 0.2;
    double step = 0.05;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) {
            in.binding("zoomIn", m.zoomIn, SlotKind::Digital);
            in.binding("zoomOut", m.zoomOut, SlotKind::Digital);
        });
        v.number("span", m.span, 0.01, 1.0);
        v.number("step", m.step, 0.001, 0.5);
    }

    friend bool operator==(const PinchControl&, const PinchControl&) = default;
};

struct WasdControl {
    std::optional<Binding> up;
    std::optional<Binding> down;
    std::optional<Binding> left;
    std::optional<Binding> right;
    std::optional<Binding> walk;
    std::optional<Binding> sprint;
    double radius = 0.12;
    double walkScale = 0.5;
    double sprintScale = 1.6;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) {
            in.binding("up", m.up, SlotKind::Digital);
            in.binding("down", m.down, SlotKind::Digital);
            in.binding("left", m.left, SlotKind::Digital);
            in.binding("right", m.right, SlotKind::Digital);
            in.binding("walk", m.walk, SlotKind::Digital);
            in.binding("sprint", m.sprint, SlotKind::Digital);
        });
        v.number("radius", m.radius, 0.01, 1.0);
        v.number("walkScale", m.walkScale, 0.1, 1.0);
        v.number("sprintScale", m.sprintScale, 1.0, 3.0);
    }

    friend bool operator==(const WasdControl&, const WasdControl&) = default;
};

struct RightWalkControl {
    std::optional<Binding> walkTo;
    std::optional<Binding> stop;
    double radius = 0.12;
    std::uint16_t walkMsPerUnit = 600;
    bool stopOnRelease = true;
    Vec2 hero;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) {
            in.binding("walkTo", m.walkTo, SlotKind::Digital, Claim::Cursor);
            in.binding("stop", m.stop, SlotKind::Digital);
        });
        v.number("radius", m.radius, 0.01, 1.0);
        v.integer("walkMsPerUnit", m.walkMsPerUnit, 50, 5000);
        v.boolean("stopOnRelease", m.stopOnRelease);
        v.vec2("hero", m.hero, -2.0, 2.0);
    }

    friend bool operator==(const RightWalkControl&, const RightWalkControl&) = default;
};

struct PadStickControl {
    std::optional<Binding> stick;
    double radius = 0.12;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) { in.binding("stick", m.stick, SlotKind::Axis2D); });
        v.number("radius", m.radius, 0.01, 1.0);
    }

    friend bool operator==(const PadStickControl&, const PadStickControl&) = default;
};

struct CameraControl {
    std::optional<Binding> look;
    std::optional<Chord> lockKey;
    Vec2 size{0.8, 0.6};
    Vec2 sensitivity{1.0, 1.0};
    std::uint16_t idleRecenterMs = 1000;
    bool crosshair = false;
    CameraRange range = CameraRange::Screen;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) { in.binding("look", m.look, SlotKind::Axis2D); });
        v.optionalChord("lockKey", m.lockKey);
        v.vec2("size", m.size, 0.01, 2.0);
        v.vec2("sensitivity", m.sensitivity, 0.01, 100.0);
        v.integer("idleRecenterMs", m.idleRecenterMs, 0, 60000);
        v.boolean("crosshair", m.crosshair);
        v.choice("range", m.range, kCameraRangeNames);
    }

    friend bool operator==(const CameraControl&, const CameraControl&) = default;
};

struct PadCameraControl {
    std::optional<Binding> look;
    Vec2 size{0.8, 0.6};
    Vec2 sensitivity{1.0, 1.0};
    std::uint16_t idleRecenterMs = 1000;
    CameraRange range = CameraRange::Screen;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) { in.binding("look", m.look, SlotKind::Axis2D); });
        v.vec2("size", m.size, 0.01, 2.0);
        v.vec2("sensitivity", m.sensitivity, 0.01, 100.0);
        v.integer("idleRecenterMs", m.idleRecenterMs, 0, 60000);
        v.choice("range", m.range, kCameraRangeNames);
    }

    friend bool operator==(const PadCameraControl&, const PadCameraControl&) = default;
};

struct FreeLookControl {
    std::optional<Binding> hold;
    std::optional<Binding> look;
    Vec2 sensitivity{1.0, 1.0};
    bool resetOnRelease = true;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) {
            in.binding("hold", m.hold, SlotKind::Digital);
            in.binding("look", m.look, SlotKind::HeldAxis2D);
        });
        v.vec2("sensitivity", m.sensitivity, 0.01, 100.0);
        v.boolean("resetOnRelease", m.resetOnRelease);
    }

    friend bool operator==(const FreeLookControl&, const FreeLookControl&) = default;
};

struct SkillControl {
    std::optional<Binding> press;
    std::optional<Binding> look;
    std::optional<Binding> cancel;
    std::optional<Binding> castKey;
    double radius = 0.15;
    Vec2 sensitivity{1.0, 1.0};
    bool castOnRelease = true;
    AimDirection direction = AimDirection::Mouse;
    CastTiming cast = CastTiming::Release;
    double castRadius = 0.35;
    Vec2 hero;
    std::optional<Vec2> cancelAt;
    std::string pauses;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) {
            in.binding("press", m.press, SlotKind::Digital);
            in.binding("look", m.look, SlotKind::HeldAxis2D);
            in.binding("cancel", m.cancel, SlotKind::Digital);
            in.binding("cast", m.castKey, SlotKind::Digital, Claim::Cursor);
        });
        v.number("radius", m.radius, 0.01, 1.0);
        v.vec2("sensitivity", m.sensitivity, 0.01, 100.0);
        v.boolean("castOnRelease", m.castOnRelease);
        v.choice("direction", m.direction, kAimDirectionNames);
        v.choice("cast", m.cast, kCastTimingNames);
        v.number("castRadius", m.castRadius, 0.01, 2.0);
        v.vec2("hero", m.hero, -2.0, 2.0);
        v.optionalVec2("cancelAt", m.cancelAt, -2.0, 2.0);
        v.optionalIdentifier("pauses", m.pauses);
    }

    friend bool operator==(const SkillControl&, const SkillControl&) = default;
};

struct DialControl {
    std::optional<Binding> press;
    std::optional<Binding> look;
    double radius = 0.12;
    std::uint8_t cells = 8;
    std::uint16_t firstDeg = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) {
            in.binding("press", m.press, SlotKind::Digital);
            in.binding("look", m.look, SlotKind::HeldAxis2D, Claim::Both);
        });
        v.number("radius", m.radius, 0.01, 1.0);
        v.integer("cells", m.cells, 2, 16);
        v.integer("firstDeg", m.firstDeg, 0, 359);
    }

    friend bool operator==(const DialControl&, const DialControl&) = default;
};

inline constexpr std::size_t kMostMultiPressPoints = 9;

struct MultiPressControl {
    std::optional<Binding> press;
    std::vector<Vec2> points{{0.1, 0.0}};

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) { in.binding("press", m.press, SlotKind::Digital); });
        v.points("points", m.points, 1, -2.0, 2.0);
    }

    friend bool operator==(const MultiPressControl&, const MultiPressControl&) = default;
};

struct TempCursorControl {
    std::optional<Binding> press;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) { in.binding("press", m.press, SlotKind::Digital, Claim::Both); });
    }

    friend bool operator==(const TempCursorControl&, const TempCursorControl&) = default;
};

enum class WheelAction : std::uint8_t { None, Control, Tap, Logic };
inline constexpr std::array<std::string_view, 4> kWheelActionNames{"none", "control", "tap", "logic"};
inline constexpr std::size_t kWheelCellNameChars = 24;
inline constexpr std::size_t kFewestWheelCells = 2;
inline constexpr std::size_t kMostWheelCells = 12;

struct WheelCell {
    std::string name;
    WheelAction action = WheelAction::None;
    std::string control;
    Vec2 at{0.1, 0.0};

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, kWheelCellNameChars);
        v.choice("action", m.action, kWheelActionNames);
        v.optionalIdentifier("control", m.control);
        v.vec2("at", m.at, -2.0, 2.0);
    }

    friend bool operator==(const WheelCell&, const WheelCell&) = default;
};

struct ActionWheelControl {
    std::optional<Binding> press;
    std::optional<Binding> look;
    std::vector<WheelCell> cells = std::vector<WheelCell>(8);

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.group("inputs", [&](auto& in) {
            in.binding("press", m.press, SlotKind::Digital);
            in.binding("look", m.look, SlotKind::HeldAxis2D, Claim::Both);
        });
        v.list("cells", m.cells);
    }

    friend bool operator==(const ActionWheelControl&, const ActionWheelControl&) = default;
};

using PropValue = std::variant<bool, double, std::string, Vec2>;

inline constexpr std::size_t kComponentRefLength = 128;
inline constexpr std::size_t kPropNameLength = 64;
inline constexpr std::size_t kPropTextLength = 255;

struct NamedBinding {
    std::string name;
    std::optional<Binding> binding;

    friend bool operator==(const NamedBinding&, const NamedBinding&) = default;
};

struct NamedValue {
    std::string name;
    PropValue value;

    friend bool operator==(const NamedValue&, const NamedValue&) = default;
};

struct ComponentControl {
    std::string component;
    std::vector<NamedBinding> keys;
    std::vector<NamedValue> props;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.requiredText("component", m.component, kComponentRefLength);
        v.bindingTable("inputs", m.keys);
        v.valueTable("props", m.props);
    }

    friend bool operator==(const ComponentControl&, const ComponentControl&) = default;
};

[[nodiscard]] bool isComponentRef(std::string_view ref) noexcept;
struct Profile;
[[nodiscard]] bool usesComponents(const Profile& profile) noexcept;

inline constexpr std::size_t kModuleRefLength = 128;
inline constexpr std::size_t kMostAttachments = 8;
inline constexpr std::size_t kMostSkippedGlobals = 32;
inline constexpr std::size_t kMostBehaviorSlots = 8;
inline constexpr std::size_t kMostModuleParamBytes = 16 * 1024;

[[nodiscard]] bool isModuleRef(std::string_view ref) noexcept;

struct Attachment {
    std::string module;
    bool enabled = true;
    std::string params;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.requiredText("module", m.module, kModuleRefLength);
        v.flag("enabled", m.enabled);
        v.json("params", m.params);
    }

    friend bool operator==(const Attachment&, const Attachment&) = default;
};

struct SkippedGlobal {
    std::string module;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.requiredText("module", m.module, kModuleRefLength);
    }

    friend bool operator==(const SkippedGlobal&, const SkippedGlobal&) = default;
};

struct BehaviorControl {
    std::string behavior;
    std::vector<NamedBinding> inputs;
    std::string params;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.requiredText("behavior", m.behavior, kModuleRefLength);
        v.slotTable("inputs", m.inputs);
        v.json("params", m.params);
    }

    friend bool operator==(const BehaviorControl&, const BehaviorControl&) = default;
};

using ControlKind = std::variant<TapControl, HoldControl, LongPressControl, TurboControl, SwipeControl, PinchControl,
                                 WasdControl, RightWalkControl, PadStickControl, CameraControl, PadCameraControl,
                                 FreeLookControl, SkillControl, ComponentControl, DialControl, MultiPressControl,
                                 TempCursorControl, ActionWheelControl, BehaviorControl>;
inline constexpr std::array<std::string_view, 19> kControlTypeNames{
    "tap",      "hold",     "longPress", "turbo",     "swipe",    "pinch", "wasd",      "rightWalk", "padStick",
    "camera",   "padCamera", "freeLook", "skill",     "component", "dial", "multiPress", "tempCursor", "actionWheel",
    "behavior"};

[[nodiscard]] bool isMovement(const ControlKind& kind) noexcept;

inline constexpr std::size_t kNameLength = 64;

struct Control {
    std::string id;
    std::string name;
    bool enabled = true;
    std::string when;
    ControlKind kind;
    Anchor anchor = Anchor::Center;
    Vec2 offset;
    std::string component;
    std::vector<NamedBinding> keys;
    std::vector<NamedValue> props;
    std::string parent;
    std::string child;
    bool idFixed = false;
    std::vector<Attachment> attachments;
    std::vector<SkippedGlobal> skipGlobal;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.identifier("id", m.id);
        v.text("name", m.name, kNameLength);
        v.flag("enabled", m.enabled);
        v.condition("when", m.when);
        v.variant("type", m.kind, kControlTypeNames);
        v.choice("anchor", m.anchor, kAnchorNames);
        v.vec2("offset", m.offset, -2.0, 2.0);
        if (!std::holds_alternative<ComponentControl>(m.kind)) {
            v.extension("component", m.component, m.keys, m.props);
        }
        v.optionalIdentifier("parent", m.parent);
        v.optionalText("child", m.child, kPropNameLength);
        v.flag("idFixed", m.idFixed);
        v.list("attachments", m.attachments);
        v.list("skipGlobal", m.skipGlobal);
    }

    friend bool operator==(const Control&, const Control&) = default;
};

[[nodiscard]] const std::string& componentRef(const Control& control) noexcept;
[[nodiscard]] const std::vector<NamedBinding>& componentKeys(const Control& control) noexcept;
[[nodiscard]] const std::vector<NamedValue>& componentProps(const Control& control) noexcept;
[[nodiscard]] std::vector<std::string_view> inputSlots(std::string_view type);

using VarValue = std::variant<bool, double, std::string>;

inline constexpr std::size_t kVarTextLength = 255;

[[nodiscard]] bool truthy(const VarValue& value) noexcept;

struct Variable {
    std::string id;
    std::string name;
    VarValue initial = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.identifier("id", m.id);
        v.text("name", m.name, kNameLength);
        v.value("initial", m.initial, kVarTextLength);
    }

    friend bool operator==(const Variable&, const Variable&) = default;
};

struct Meta {
    std::string name;
    std::string app;
    Orientation orientation = Orientation::Landscape;
    std::array<std::uint16_t, 2> reference{0, 0};

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, 64);
        v.text("app", m.app, 255);
        v.choice("orientation", m.orientation, kOrientationNames);
        v.integerPair("reference", m.reference, 0, 65535);
    }

    friend bool operator==(const Meta&, const Meta&) = default;
};

struct ScreenSettings {
    ScaleMode scaleMode = ScaleMode::ShortSide;
    SafeArea safeArea = SafeArea::Ignore;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.choice("scaleMode", m.scaleMode, kScaleModeNames);
        v.choice("safeArea", m.safeArea, kSafeAreaNames);
        if constexpr (requires { v.retired("confineCursor"); }) {
            v.retired("confineCursor");
        }
    }

    friend bool operator==(const ScreenSettings&, const ScreenSettings&) = default;
};

struct Profile {
    Meta meta;
    ScreenSettings screen;
    std::vector<Control> controls;
    std::vector<Variable> vars;
    GamepadMode gamepad = GamepadMode::Map;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.constantText("format", kFormatName);
        v.constantInteger("version", kFormatVersion);
        v.object("meta", m.meta);
        v.object("screen", m.screen);
        v.choice("gamepad", m.gamepad, kGamepadModeNames);
        v.list("controls", m.controls);
        v.list("vars", m.vars);
    }

    friend bool operator==(const Profile&, const Profile&) = default;
};

enum class Severity : std::uint8_t { Error, Warning, Info };

enum class DiagnosticCode : std::uint8_t {
    TooLarge,
    MalformedJson,
    WrongFormat,
    WrongVersion,
    MissingField,
    WrongType,
    OutOfRange,
    InvalidValue,
    InvalidInput,
    InputKindMismatch,
    UnknownField,
    DefaultUsed,
    DuplicateId,
    UnknownReference,
    TooFewPoints,
    Conflict,
    PatchBreaksProfile,
    Converted,
};

struct Diagnostic {
    Severity severity = Severity::Error;
    DiagnosticCode code = DiagnosticCode::InvalidValue;
    std::string path;
    std::string detail;

    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

struct LoadResult {
    Profile profile;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] bool converted() const noexcept;
};

[[nodiscard]] LoadResult loadProfile(std::string_view json);

[[nodiscard]] std::string saveProfile(const Profile& profile);

[[nodiscard]] std::string controlSettings(const Control& control);

[[nodiscard]] std::string upgradeProfile(std::string_view json, std::vector<Diagnostic>* notes = nullptr);

[[nodiscard]] std::vector<Chord> boundInputs(const Profile& profile);

struct ConflictingBinding {
    std::size_t index = 0;
    std::string path;

    friend bool operator==(const ConflictingBinding&, const ConflictingBinding&) = default;
};

struct InputConflict {
    Chord input;
    std::vector<ConflictingBinding> bindings;

    friend bool operator==(const InputConflict&, const InputConflict&) = default;
};

[[nodiscard]] std::vector<InputConflict> inputConflicts(const Profile& profile);

[[nodiscard]] std::vector<InputSource> cursorClaims(const Profile& profile);

struct CursorClaim {
    Chord chord;
    std::size_t index = 0;
};

[[nodiscard]] std::vector<CursorClaim> cursorClaimChords(const Profile& profile);

}
