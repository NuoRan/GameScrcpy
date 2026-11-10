#include "ProfileForm.h"

#include "KeyLabels.h"
#include "TranslateNoop.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>
#include <utility>

namespace gs::app {

namespace form {

namespace {

constexpr std::array kAnchorChoices{
    Choice{.value = "topLeft", .text = GS_TRANSLATE_NOOP("ProfileForm", "Top left")},
    Choice{.value = "top", .text = GS_TRANSLATE_NOOP("ProfileForm", "Top")},
    Choice{.value = "topRight", .text = GS_TRANSLATE_NOOP("ProfileForm", "Top right")},
    Choice{.value = "left", .text = GS_TRANSLATE_NOOP("ProfileForm", "Left")},
    Choice{.value = "center", .text = GS_TRANSLATE_NOOP("ProfileForm", "Center")},
    Choice{.value = "right", .text = GS_TRANSLATE_NOOP("ProfileForm", "Right")},
    Choice{.value = "bottomLeft", .text = GS_TRANSLATE_NOOP("ProfileForm", "Bottom left")},
    Choice{.value = "bottom", .text = GS_TRANSLATE_NOOP("ProfileForm", "Bottom")},
    Choice{.value = "bottomRight", .text = GS_TRANSLATE_NOOP("ProfileForm", "Bottom right")},
};

constexpr std::array kCastChoices{
    Choice{.value = "release", .text = GS_TRANSLATE_NOOP("ProfileForm", "Release")},
    Choice{.value = "press", .text = GS_TRANSLATE_NOOP("ProfileForm", "Press")},
    Choice{.value = "manual", .text = GS_TRANSLATE_NOOP("ProfileForm", "Manual")},
};

constexpr std::array kCameraRangeChoices{
    Choice{.value = "screen", .text = GS_TRANSLATE_NOOP("ProfileForm", "Whole picture")},
    Choice{.value = "box", .text = GS_TRANSLATE_NOOP("ProfileForm", "Within the area")},
};

constexpr std::array kClaimChoices{
    Choice{.value = "lock", .text = GS_TRANSLATE_NOOP("ProfileForm", "While the view is locked")},
    Choice{.value = "cursor", .text = GS_TRANSLATE_NOOP("ProfileForm", "In cursor mode")},
    Choice{.value = "both", .text = GS_TRANSLATE_NOOP("ProfileForm", "Both")},
};

constexpr Field claimField(std::string_view key, std::string_view path, int group = 0)
{
    return Field{.key = key,
                 .path = path,
                 .widget = Widget::Select,
                 .measure = Measure::Choice,
                 .title = GS_TRANSLATE_NOOP("ProfileForm", "When it applies"),
                 .choices = kClaimChoices,
                 .shown = Shown::MouseBound,
                 .group = group};
}

constexpr const char* kPlace = GS_TRANSLATE_NOOP("ProfileForm", "Position and size");
constexpr const char* kDrag = GS_TRANSLATE_NOOP("ProfileForm", "Drag on the picture too");

constexpr Field kX{.key = "x",
                   .path = "offset",
                   .component = 0,
                   .widget = Widget::Box,
                   .measure = Measure::PositionX,
                   .title = kPlace,
                   .hint = kDrag,
                   .prefix = GS_TRANSLATE_NOOP("ProfileForm", "X"),
                   .unit = Unit::Percent,
                   .step = 0.1,
                   .decimals = 1,
                   .group = 1};
constexpr Field kY{.key = "y",
                   .path = "offset",
                   .component = 1,
                   .widget = Widget::Box,
                   .measure = Measure::PositionY,
                   .title = kPlace,
                   .hint = kDrag,
                   .prefix = GS_TRANSLATE_NOOP("ProfileForm", "Y"),
                   .unit = Unit::Percent,
                   .step = 0.1,
                   .decimals = 1,
                   .group = 1};
constexpr Field kRadius{.key = "radius",
                        .path = "radius",
                        .widget = Widget::Box,
                        .measure = Measure::Length,
                        .title = kPlace,
                        .hint = kDrag,
                        .prefix = GS_TRANSLATE_NOOP("ProfileForm", "R"),
                        .unit = Unit::Pixels,
                        .group = 1};
constexpr Field kWidth{.key = "width",
                       .path = "size",
                       .component = 0,
                       .widget = Widget::Box,
                       .measure = Measure::ExtentX,
                       .title = kPlace,
                       .hint = kDrag,
                       .prefix = GS_TRANSLATE_NOOP("ProfileForm", "W"),
                       .unit = Unit::Percent,
                       .group = 1};
constexpr Field kHeight{.key = "height",
                        .path = "size",
                        .component = 1,
                        .widget = Widget::Box,
                        .measure = Measure::ExtentY,
                        .title = kPlace,
                        .hint = kDrag,
                        .prefix = GS_TRANSLATE_NOOP("ProfileForm", "H"),
                        .unit = Unit::Percent,
                        .group = 1};
constexpr Field kAnchor{.key = "anchor",
                        .path = "anchor",
                        .widget = Widget::Select,
                        .measure = Measure::Anchor,
                        .title = GS_TRANSLATE_NOOP("ProfileForm", "Anchor"),
                        .info = GS_TRANSLATE_NOOP("ProfileForm",
                                                  "The corner or edge of the picture that the component keeps its "
                                                  "distance to. When the picture has another size or shape, such as "
                                                  "on another phone or a tablet, the component stays as far from that "
                                                  "corner or edge as it is now, like the game's own buttons do. Pick "
                                                  "the side the game's button sits on: a skill button at the bottom "
                                                  "right uses Bottom right. Dragging the component picks the nearest "
                                                  "one by itself."),
                        .choices = kAnchorChoices,
                        .group = 1};
constexpr Field kSensitivityX{.key = "sensitivityX",
                              .path = "sensitivity",
                              .component = 0,
                              .widget = Widget::Slider,
                              .title = GS_TRANSLATE_NOOP("ProfileForm", "Horizontal sensitivity"),
                              .unit = Unit::Times,
                              .sliderFrom = 0.1,
                              .sliderTo = 10.0,
                              .logarithmic = true,
                              .step = 0.05,
                              .decimals = 2,
                              .group = 2};
constexpr Field kSensitivityY{.key = "sensitivityY",
                              .path = "sensitivity",
                              .component = 1,
                              .widget = Widget::Slider,
                              .title = GS_TRANSLATE_NOOP("ProfileForm", "Vertical sensitivity"),
                              .unit = Unit::Times,
                              .sliderFrom = 0.1,
                              .sliderTo = 10.0,
                              .logarithmic = true,
                              .step = 0.05,
                              .decimals = 2,
                              .group = 2};
constexpr Field kTapTime{.key = "tapMs",
                         .path = "tapMs",
                         .title = GS_TRANSLATE_NOOP("ProfileForm", "Touch time"),
                         .unit = Unit::Milliseconds,
                         .step = 10,
                         .special = GS_TRANSLATE_NOOP("ProfileForm", "Auto"),
                         .group = 2};
constexpr Field kRange{.key = "range",
                       .path = "range",
                       .widget = Widget::Segmented,
                       .measure = Measure::Choice,
                       .title = GS_TRANSLATE_NOOP("ProfileForm", "Slide over"),
                       .choices = kCameraRangeChoices,
                       .group = 2};
constexpr Field kIdleRecenter{.key = "idleRecenterMs",
                              .path = "idleRecenterMs",
                              .title = GS_TRANSLATE_NOOP("ProfileForm", "Recenter after idle"),
                              .unit = Unit::Milliseconds,
                              .step = 100,
                              .special = GS_TRANSLATE_NOOP("ProfileForm", "Never"),
                              .group = 2};

constexpr Field heroField(std::string_view key, int component, Measure measure, const char* prefix, Shown shown)
{
    return Field{.key = key,
                 .path = "hero",
                 .component = component,
                 .widget = Widget::Box,
                 .measure = measure,
                 .title = GS_TRANSLATE_NOOP("ProfileForm", "Hero position"),
                 .hint = kDrag,
                 .prefix = prefix,
                 .unit = Unit::Percent,
                 .step = 0.1,
                 .decimals = 1,
                 .shown = shown,
                 .group = 3};
}

constexpr Field inputField(std::string_view key, std::string_view path, const char* title, bool half = false)
{
    return Field{.key = key,
                 .path = path,
                 .widget = Widget::Key,
                 .measure = Measure::Input,
                 .title = title,
                 .group = 0,
                 .half = half};
}

constexpr const char* kKeyTitle = GS_TRANSLATE_NOOP("ProfileForm", "Key");
constexpr Field kPressKey = inputField("key.press", "inputs.press.input", kKeyTitle);

constexpr std::array kTapFields{
    kPressKey,
    claimField("claim.press", "inputs.press.claim"),
    kX,
    kY,
    kAnchor,
    kTapTime,
};

constexpr std::array kComponentFields{
    kX,
    kY,
    kAnchor,
};

constexpr std::array kHoldFields{
    kPressKey,
    claimField("claim.press", "inputs.press.claim"),
    kX,
    kY,
    kAnchor,
};

constexpr std::array kLongPressFields{
    kPressKey,
    claimField("claim.press", "inputs.press.claim"),
    kX,
    kY,
    kAnchor,
    Field{.key = "holdMs",
          .path = "holdMs",
          .widget = Widget::Slider,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Hold for"),
          .unit = Unit::Milliseconds,
          .sliderFrom = 100,
          .sliderTo = 5000,
          .step = 50,
          .group = 2},
};

constexpr std::array kTurboFields{
    kPressKey,
    claimField("claim.press", "inputs.press.claim"),
    kX,
    kY,
    kAnchor,
    Field{.key = "rateHz",
          .path = "rateHz",
          .widget = Widget::Slider,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Rate"),
          .unit = Unit::PerSecond,
          .sliderFrom = 1,
          .sliderTo = 30,
          .group = 2},
    kTapTime,
};

constexpr std::array kWasdFields{
    inputField("key.up", "inputs.up.input", GS_TRANSLATE_NOOP("ProfileForm", "Up"), true),
    inputField("key.down", "inputs.down.input", GS_TRANSLATE_NOOP("ProfileForm", "Down"), true),
    inputField("key.left", "inputs.left.input", GS_TRANSLATE_NOOP("ProfileForm", "Left"), true),
    inputField("key.right", "inputs.right.input", GS_TRANSLATE_NOOP("ProfileForm", "Right"), true),
    inputField("key.walk", "inputs.walk.input", GS_TRANSLATE_NOOP("ProfileForm", "Walk"), true),
    inputField("key.sprint", "inputs.sprint.input", GS_TRANSLATE_NOOP("ProfileForm", "Sprint"), true),
    claimField("claim.up", "inputs.up.claim"),
    claimField("claim.down", "inputs.down.claim"),
    claimField("claim.left", "inputs.left.claim"),
    claimField("claim.right", "inputs.right.claim"),
    claimField("claim.walk", "inputs.walk.claim"),
    claimField("claim.sprint", "inputs.sprint.claim"),
    kX,
    kY,
    kRadius,
    kAnchor,
    Field{.key = "sprintScale",
          .path = "sprintScale",
          .widget = Widget::Slider,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "How far sprint pushes"),
          .hint = GS_TRANSLATE_NOOP("ProfileForm", "The dashed ring on the picture"),
          .unit = Unit::Times,
          .sliderFrom = 1.0,
          .sliderTo = 3.0,
          .step = 0.05,
          .decimals = 2,
          .group = 2},
};

constexpr std::array kRightWalkFields{
    inputField("key.walkTo", "inputs.walkTo.input", GS_TRANSLATE_NOOP("ProfileForm", "Walk to"), true),
    inputField("key.stop", "inputs.stop.input", GS_TRANSLATE_NOOP("ProfileForm", "Stop"), true),
    claimField("claim.walkTo", "inputs.walkTo.claim"),
    claimField("claim.stop", "inputs.stop.claim"),
    kX,
    kY,
    kRadius,
    kAnchor,
    Field{.key = "walkMsPerUnit",
          .path = "walkMsPerUnit",
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Walk time per screen height"),
          .unit = Unit::Milliseconds,
          .step = 50,
          .group = 2},
    Field{.key = "stopOnRelease",
          .path = "stopOnRelease",
          .widget = Widget::Switch,
          .measure = Measure::Flag,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Stop on release"),
          .group = 2},
    heroField("heroX", 0, Measure::HeroX, GS_TRANSLATE_NOOP("ProfileForm", "X"), Shown::Always),
    heroField("heroY", 1, Measure::HeroY, GS_TRANSLATE_NOOP("ProfileForm", "Y"), Shown::Always),
};

constexpr std::array kPadStickFields{
    kX,
    kY,
    kRadius,
    kAnchor,
};

constexpr std::array kCameraFields{
    kX,
    kY,
    kWidth,
    kHeight,
    kAnchor,
    kSensitivityX,
    kSensitivityY,
    kRange,
    kIdleRecenter,
    Field{.key = "crosshair",
          .path = "crosshair",
          .widget = Widget::Switch,
          .measure = Measure::Flag,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Crosshair"),
          .group = 3},
};

constexpr std::array kPadCameraFields{
    kX,
    kY,
    kWidth,
    kHeight,
    kAnchor,
    kSensitivityX,
    kSensitivityY,
    kRange,
    kIdleRecenter,
};

constexpr std::array kFreeLookFields{
    inputField("key.hold", "inputs.hold.input", kKeyTitle),
    claimField("claim.hold", "inputs.hold.claim"),
    kX,
    kY,
    kAnchor,
    kSensitivityX,
    kSensitivityY,
    Field{.key = "resetOnRelease",
          .path = "resetOnRelease",
          .widget = Widget::Switch,
          .measure = Measure::Flag,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Recenter when released"),
          .group = 3},
};

constexpr std::array kSkillFields{
    kPressKey,
    Field{.key = "cancel",
          .path = "inputs.cancel.input",
          .widget = Widget::Key,
          .measure = Measure::Input,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Cancel cast"),
          .group = 0},
    Field{.key = "castKey",
          .path = "inputs.cast.input",
          .widget = Widget::Key,
          .measure = Measure::Input,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Cast key"),
          .group = 0},
    kX,
    kY,
    kRadius,
    kAnchor,
    Field{.key = "cast",
          .path = "cast",
          .widget = Widget::Segmented,
          .measure = Measure::Choice,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Cast timing"),
          .choices = kCastChoices,
          .group = 2},
    claimField("claim.press", "inputs.press.claim", 2),
    claimField("claim.cast", "inputs.cast.claim", 2),
    claimField("claim.cancel", "inputs.cancel.claim", 2),
    Field{.key = "castRadius",
          .path = "castRadius",
          .widget = Widget::Slider,
          .measure = Measure::Length,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Cast radius"),
          .hint = kDrag,
          .unit = Unit::Pixels,
          .sliderFrom = 0.05,
          .sliderTo = 1.0,
          .shown = Shown::CursorAim,
          .group = 3},
    heroField("heroX", 0, Measure::HeroX, GS_TRANSLATE_NOOP("ProfileForm", "X"), Shown::CursorAim),
    heroField("heroY", 1, Measure::HeroY, GS_TRANSLATE_NOOP("ProfileForm", "Y"), Shown::CursorAim),
    Field{.key = "pauses",
          .path = "pauses",
          .widget = Widget::Switch,
          .measure = Measure::Pauses,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Pause walking while casting"),
          .group = 3},
};

constexpr std::array kSwipeFields{
    kPressKey,
    claimField("claim.press", "inputs.press.claim"),
    kX,
    kY,
    kAnchor,
    Field{.key = "durationMs",
          .path = "durationMs",
          .widget = Widget::Slider,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Swipe time"),
          .unit = Unit::Milliseconds,
          .sliderFrom = 50,
          .sliderTo = 2000,
          .step = 10,
          .group = 2},
};

constexpr std::array kPinchFields{
    inputField("key.zoomIn", "inputs.zoomIn.input", GS_TRANSLATE_NOOP("ProfileForm", "Zoom in"), true),
    inputField("key.zoomOut", "inputs.zoomOut.input", GS_TRANSLATE_NOOP("ProfileForm", "Zoom out"), true),
    claimField("claim.zoomIn", "inputs.zoomIn.claim"),
    claimField("claim.zoomOut", "inputs.zoomOut.claim"),
    kX,
    kY,
    kAnchor,
    Field{.key = "span",
          .path = "span",
          .measure = Measure::Length,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Finger distance"),
          .unit = Unit::Pixels,
          .group = 2,
          .half = true},
    Field{.key = "step",
          .path = "step",
          .measure = Measure::Length,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Zoom step"),
          .unit = Unit::Pixels,
          .group = 2,
          .half = true},
};

constexpr std::array kDialFields{
    kPressKey,
    claimField("claim.press", "inputs.press.claim"),
    kX,
    kY,
    kRadius,
    kAnchor,
    Field{.key = "cells",
          .path = "cells",
          .widget = Widget::Slider,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Cells"),
          .sliderFrom = 2,
          .sliderTo = 16,
          .group = 2},
    Field{.key = "firstDeg",
          .path = "firstDeg",
          .widget = Widget::Slider,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Direction of the first cell"),
          .unit = Unit::Degrees,
          .sliderFrom = 0,
          .sliderTo = 359,
          .group = 2},
};

constexpr std::array kMultiPressFields{
    kPressKey,
    claimField("claim.press", "inputs.press.claim"),
    kX,
    kY,
    kAnchor,
    Field{.key = "places",
          .path = "points",
          .measure = Measure::Places,
          .title = GS_TRANSLATE_NOOP("ProfileForm", "Places"),
          .hint = GS_TRANSLATE_NOOP("ProfileForm", "Drag the dots on the picture"),
          .step = 1,
          .group = 2,
          .half = true},
};

constexpr std::array kTempCursorFields{
    kPressKey,
    claimField("claim.press", "inputs.press.claim"),
    kX,
    kY,
    kAnchor,
};

constexpr std::array kActionWheelFields{
    kPressKey,
    claimField("claim.press", "inputs.press.claim"),
    kX,
    kY,
    kAnchor,
};

constexpr std::array kBehaviorFields{
    kX,
    kY,
    kAnchor,
};

class Collector
{
public:
    Collector(std::vector<Declared>& out, std::string prefix, bool absent = false)
        : m_out(out)
        , m_prefix(std::move(prefix))
        , m_absent(absent)
    {
    }

    void constantText(std::string_view name, std::string_view value) { add(name, std::string(value)); }
    void constantInteger(std::string_view name, int value) { add(name, static_cast<double>(value)); }
    void text(std::string_view name, const std::string& field, std::size_t) { add(name, field); }
    void optionalText(std::string_view name, const std::string& field, std::size_t) { add(name, field); }
    void requiredText(std::string_view name, const std::string& field, std::size_t) { add(name, field); }
    void identifier(std::string_view name, const std::string& field) { add(name, field); }
    void optionalIdentifier(std::string_view name, const std::string& field) { add(name, field); }
    void identifierList(std::string_view name, const std::vector<std::string>&) { add(name, {}); }
    void boolean(std::string_view name, const bool& field) { add(name, field); }
    void flag(std::string_view name, const bool& field) { add(name, field); }
    void condition(std::string_view name, const std::string& field) { add(name, field); }
    void number(std::string_view name, const double& field, double min, double max) { add(name, field, min, max); }
    void vec2(std::string_view name, const input::Vec2& field, double min, double max) { add(name, field, min, max); }
    void chord(std::string_view name, const input::Chord& field) { add(name, input::chordName(field)); }

    template <typename E, std::size_t N>
    void choice(std::string_view name, const E& field, const std::array<std::string_view, N>& names)
    {
        add(name, std::string(names[static_cast<std::size_t>(field)]));
    }

    template <typename E, std::size_t N>
    void optionalChoice(std::string_view name, const std::optional<E>& field, const std::array<std::string_view, N>& names)
    {
        add(name, field ? Value{std::string(names[static_cast<std::size_t>(*field)])} : Value{});
    }

    template <typename T>
    void integer(std::string_view name, const T& field, std::int64_t min, std::int64_t max)
    {
        add(name, static_cast<double>(field), static_cast<double>(min), static_cast<double>(max), true);
    }

    void optionalVec2(std::string_view name, const std::optional<input::Vec2>& field, double min, double max)
    {
        add(name, field ? Value{*field} : Value{}, min, max);
    }

    void points(std::string_view name, const std::vector<input::Vec2>&, std::size_t, double min, double max)
    {
        add(name, {}, min, max);
    }

    void integerPair(std::string_view name, const std::array<std::uint16_t, 2>&, std::int64_t min, std::int64_t max)
    {
        add(name, {}, static_cast<double>(min), static_cast<double>(max));
    }

    void optionalChord(std::string_view name, const std::optional<input::Chord>& field)
    {
        add(name, field ? Value{input::chordName(*field)} : Value{});
    }

    void binding(std::string_view name, const std::optional<input::Binding>& field, input::SlotKind,
                 input::Claim = input::Claim::Lock)
    {
        Collector nested(m_out, pathOf(name) + ".", m_absent || !field);
        const input::Binding standard;
        input::Binding::visit(nested, field ? *field : standard);
    }

    void bindingTable(std::string_view name, const std::vector<input::NamedBinding>& field)
    {
        for (const input::NamedBinding& entry : field) {
            binding(std::string(name) + "." + entry.name, entry.binding, input::SlotKind::Digital);
        }
    }

    void slotTable(std::string_view name, const std::vector<input::NamedBinding>& field) { bindingTable(name, field); }

    void json(std::string_view name, const std::string& field) { add(name, field); }

    void valueTable(std::string_view name, const std::vector<input::NamedValue>& field)
    {
        for (const input::NamedValue& entry : field) {
            const std::string path = std::string(name) + "." + entry.name;
            std::visit([&](const auto& held) { add(path, Value{held}, -2.0, 2.0); }, entry.value);
        }
    }

    void extension(std::string_view name, const std::string& component, const std::vector<input::NamedBinding>& keys,
                   const std::vector<input::NamedValue>& props)
    {
        add(name, component);
        bindingTable("inputs", keys);
        valueTable("props", props);
    }

    template <typename T>
    void object(std::string_view name, const T&)
    {
        add(name, {});
    }

    template <typename T>
    void list(std::string_view name, const std::vector<T>&)
    {
        add(name, {});
    }

    template <typename Fn>
    void group(std::string_view name, Fn&& visitFields)
    {
        Collector nested(m_out, pathOf(name) + ".", m_absent);
        visitFields(nested);
    }

    template <typename... Alternatives, std::size_t N>
    void variant(std::string_view name, const std::variant<Alternatives...>& field,
                 const std::array<std::string_view, N>& names)
    {
        add(name, std::string(names[field.index()]));
        std::visit([this](const auto& alternative) { std::decay_t<decltype(alternative)>::visit(*this, alternative); },
                   field);
    }

private:
    [[nodiscard]] std::string pathOf(std::string_view name) const { return m_prefix + std::string(name); }

    void add(std::string_view name, Value value, double min = 0.0, double max = 0.0, bool whole = false)
    {
        m_out.push_back({.key = pathOf(name),
                         .minimum = min,
                         .maximum = max,
                         .whole = whole,
                         .value = m_absent ? Value{} : std::move(value)});
    }

    std::vector<Declared>& m_out;
    std::string m_prefix;
    bool m_absent = false;
};

void identity(std::vector<Excluded>& out)
{
    out.push_back({.path = "id", .reason = Reason::Identity});
    out.push_back({.path = "idFixed", .reason = Reason::Identity});
    out.push_back({.path = "name", .reason = Reason::Identity});
    out.push_back({.path = "type", .reason = Reason::Identity});
    out.push_back({.path = "enabled", .reason = Reason::FixedRow});
    out.push_back({.path = "when", .reason = Reason::BehaviorPage});
    out.push_back({.path = "parent", .reason = Reason::Canvas});
    out.push_back({.path = "child", .reason = Reason::Canvas});
    out.push_back({.path = "attachments", .reason = Reason::FixedRow});
    out.push_back({.path = "skipGlobal", .reason = Reason::FixedRow});
}

void slotKey(std::vector<Excluded>& out, std::string_view slot, Reason input)
{
    out.push_back({.path = "inputs." + std::string(slot) + ".input", .reason = input});
}

void claimKey(std::vector<Excluded>& out, std::string_view slot)
{
    out.push_back({.path = "inputs." + std::string(slot) + ".claim", .reason = Reason::Inapplicable});
}

const Declared* declared(const std::vector<Declared>& values, std::string_view key)
{
    const auto found = std::ranges::find(values, key, &Declared::key);
    return found != values.end() ? &*found : nullptr;
}

double numberIn(const Value& value, int component)
{
    if (const auto* number = std::get_if<double>(&value)) {
        return *number;
    }
    if (const auto* pair = std::get_if<input::Vec2>(&value)) {
        return component == 1 ? pair->y : pair->x;
    }
    return 0.0;
}

input::Vec2 pairIn(const Value& value)
{
    const auto* pair = std::get_if<input::Vec2>(&value);
    return pair != nullptr ? *pair : input::Vec2{};
}

double rounded(double value, int decimals)
{
    const double scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}

QString nameOf(std::string_view name)
{
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

QString unitText(Unit unit)
{
    switch (unit) {
    case Unit::Percent:
        return text(GS_TRANSLATE_NOOP("ProfileForm", "%"));
    case Unit::Pixels:
        return text(GS_TRANSLATE_NOOP("ProfileForm", "px"));
    case Unit::Times:
        return text(GS_TRANSLATE_NOOP("ProfileForm", "×"));
    case Unit::Milliseconds:
        return text(GS_TRANSLATE_NOOP("ProfileForm", "ms"));
    case Unit::PerSecond:
        return text(GS_TRANSLATE_NOOP("ProfileForm", "/s"));
    case Unit::Degrees:
        return text(GS_TRANSLATE_NOOP("ProfileForm", "°"));
    case Unit::None:
        break;
    }
    return {};
}

QString widgetName(Widget widget)
{
    switch (widget) {
    case Widget::Box:
        return QStringLiteral("box");
    case Widget::Slider:
        return QStringLiteral("slider");
    case Widget::Number:
        return QStringLiteral("number");
    case Widget::Segmented:
        return QStringLiteral("segmented");
    case Widget::Select:
        return QStringLiteral("select");
    case Widget::Switch:
        return QStringLiteral("switch");
    case Widget::Key:
        return QStringLiteral("key");
    }
    return {};
}

QVariantList optionsOf(std::span<const Choice> choices)
{
    QVariantList options;
    for (const Choice& choice : choices) {
        options.append(QVariantMap{{QStringLiteral("value"), nameOf(choice.value)},
                                   {QStringLiteral("text"), text(choice.text)}});
    }
    return options;
}

const input::Control* controlOf(const Context& context)
{
    return context.index < context.profile.controls.size() ? &context.profile.controls[context.index] : nullptr;
}

std::optional<input::Chord> chordAt(const std::vector<Declared>& values, std::string_view path)
{
    const std::string binding = bindingOf(path);
    const Declared* found = declared(values, binding.empty() ? std::string("input") : binding + ".input");
    const auto* name = found != nullptr ? std::get_if<std::string>(&found->value) : nullptr;
    return name != nullptr ? input::parseChord(*name) : std::nullopt;
}

bool isShown(const Field& field, const input::Control* control, const std::vector<Declared>& values)
{
    if (field.shown == Shown::MouseBound) {
        return input::claimable(chordAt(values, field.path));
    }
    if (control == nullptr) {
        return field.shown == Shown::Always;
    }
    switch (field.shown) {
    case Shown::Always:
        return true;
    case Shown::CursorAim: {
        const auto* skill = std::get_if<input::SkillControl>(&control->kind);
        return skill != nullptr && skill->direction == input::AimDirection::Cursor;
    }
    case Shown::MouseBound:
        break;
    }
    return false;
}

struct Scale {
    double factor = 1.0;
    double low = 0.0;
    double high = 0.0;
};

Scale scaleOf(const Field& field, const Declared* found, const Context& context)
{
    const double low = found != nullptr ? found->minimum : 0.0;
    const double high = found != nullptr ? found->maximum : 0.0;
    switch (field.measure) {
    case Measure::PositionX:
    case Measure::PositionY:
    case Measure::HeroX:
    case Measure::HeroY:
        return {.factor = 100.0, .low = 0.0, .high = 100.0};
    case Measure::ExtentX:
    case Measure::ExtentY: {
        const input::Vec2 unit = context.mapper.extent({1.0, 1.0});
        const double factor = 100.0 * (field.measure == Measure::ExtentX ? unit.x : unit.y);
        return {.factor = factor,
                .low = std::ceil(rounded(low * factor, field.decimals)),
                .high = std::min(100.0, std::floor(rounded(high * factor, field.decimals)))};
    }
    case Measure::Length: {
        const double factor = context.mapper.radius(1.0).x * context.width;
        return {.factor = factor,
                .low = std::ceil(rounded(low * factor, field.decimals)),
                .high = std::floor(rounded(high * factor, field.decimals))};
    }
    case Measure::Places:
        return {.factor = 1.0, .low = 2.0, .high = static_cast<double>(input::kMostMultiPressPoints + 1)};
    default:
        return {.factor = 1.0, .low = low, .high = high};
    }
}

double shownNumber(const Field& field, const input::Control& control, const Declared* found, const Context& context)
{
    const Value value = found != nullptr ? found->value : Value{};
    switch (field.measure) {
    case Measure::PositionX:
    case Measure::PositionY: {
        const input::Vec2 at = context.mapper.position(control.anchor, control.offset);
        return (field.measure == Measure::PositionX ? at.x : at.y) * 100.0;
    }
    case Measure::HeroX:
    case Measure::HeroY: {
        const input::Vec2 at = context.mapper.position(input::Anchor::Center, pairIn(value));
        return (field.measure == Measure::HeroX ? at.x : at.y) * 100.0;
    }
    case Measure::ExtentX:
    case Measure::ExtentY: {
        const input::Vec2 extent = context.mapper.extent(pairIn(value));
        return (field.measure == Measure::ExtentX ? extent.x : extent.y) * 100.0;
    }
    case Measure::Length:
        return context.mapper.radius(numberIn(value, -1)).x * context.width;
    case Measure::Places: {
        const auto* multi = std::get_if<input::MultiPressControl>(&control.kind);
        return multi != nullptr ? static_cast<double>(multi->points.size() + 1) : 2.0;
    }
    default:
        return numberIn(value, field.component);
    }
}

QVariantMap numberMap(const Field& field, double shown, const Declared* found, const Context& context)
{
    const Scale scale = scaleOf(field, found, context);
    const bool sliding = field.widget == Widget::Slider;
    const double from = sliding ? std::max(scale.low, rounded(field.sliderFrom * scale.factor, field.decimals)) : scale.low;
    const double to = sliding ? std::min(scale.high, rounded(field.sliderTo * scale.factor, field.decimals)) : scale.high;
    const double value = rounded(shown, field.decimals);
    const bool special = *field.special != '\0' && value == 0.0;
    return {{QStringLiteral("value"), value},
            {QStringLiteral("minimum"), scale.low},
            {QStringLiteral("maximum"), scale.high},
            {QStringLiteral("from"), from},
            {QStringLiteral("to"), to},
            {QStringLiteral("logarithmic"), field.logarithmic},
            {QStringLiteral("step"), field.step},
            {QStringLiteral("decimals"), field.decimals},
            {QStringLiteral("unit"), unitText(field.unit)},
            {QStringLiteral("word"), special ? text(field.special) : QString()}};
}

QVariantMap fieldMap(const Field& field, const input::Control* control, const std::vector<Declared>& values,
                     const Context& context)
{
    QVariantMap map{{QStringLiteral("key"), nameOf(field.key)},
                    {QStringLiteral("widget"), widgetName(field.widget)},
                    {QStringLiteral("title"), text(field.title)},
                    {QStringLiteral("hint"), text(field.hint)},
                    {QStringLiteral("info"), text(field.info)},
                    {QStringLiteral("prefix"), text(field.prefix)},
                    {QStringLiteral("half"), field.half},
                    {QStringLiteral("enabled"), true}};
    const Declared* found = declared(values, field.path);
    switch (field.widget) {
    case Widget::Box:
    case Widget::Slider:
    case Widget::Number:
        if (control != nullptr) {
            map.insert(numberMap(field, shownNumber(field, *control, found, context), found, context));
        } else if (found != nullptr) {
            map.insert(numberMap(field, numberIn(found->value, field.component), found, context));
        }
        break;
    case Widget::Segmented:
    case Widget::Select: {
        map.insert(QStringLiteral("options"), optionsOf(field.choices));
        const auto* chosen = found != nullptr ? std::get_if<std::string>(&found->value) : nullptr;
        map.insert(QStringLiteral("value"), chosen != nullptr ? QString::fromStdString(*chosen) : QString());
        if (field.shown == Shown::MouseBound) {
            if (const std::optional<input::Chord> chord = chordAt(values, field.path)) {
                QStringList caps;
                for (const KeyLabel& cap : keyCaps(*chord)) {
                    caps.append(cap.text);
                }
                map.insert(QStringLiteral("hint"), caps.join(QStringLiteral(" + ")));
            }
        }
        break;
    }
    case Widget::Switch:
        if (field.measure == Measure::Pauses) {
            const auto* target = found != nullptr ? std::get_if<std::string>(&found->value) : nullptr;
            const bool walking = std::ranges::any_of(context.profile.controls,
                                                     [](const input::Control& c) { return input::isMovement(c.kind); });
            map.insert(QStringLiteral("value"), target != nullptr && !target->empty());
            map.insert(QStringLiteral("enabled"), walking || (target != nullptr && !target->empty()));
        } else {
            const auto* on = found != nullptr ? std::get_if<bool>(&found->value) : nullptr;
            map.insert(QStringLiteral("value"), on != nullptr && *on);
        }
        break;
    case Widget::Key:
        break;
    }
    return map;
}

QVariantMap boxRow(const Field& first)
{
    return {{QStringLiteral("key"), nameOf(first.key)},
            {QStringLiteral("widget"), QStringLiteral("boxes")},
            {QStringLiteral("title"), text(first.title)},
            {QStringLiteral("hint"), text(first.hint)},
            {QStringLiteral("half"), false},
            {QStringLiteral("boxes"), QVariantList{}}};
}

QVariantMap whenField(const Context& context, const std::string& when)
{
    QVariantList options{QVariantMap{{QStringLiteral("value"), QString()},
                                     {QStringLiteral("text"), QCoreApplication::translate("ProfileForm", "Always")}}};
    for (const input::Variable& variable : context.profile.vars) {
        if (!std::holds_alternative<bool>(variable.initial)) {
            continue;
        }
        const QString id = QString::fromStdString(variable.id);
        const QString name = variable.name.empty() ? id : QString::fromStdString(variable.name);
        options.append(QVariantMap{{QStringLiteral("value"), id},
                                   {QStringLiteral("text"),
                                    QCoreApplication::translate("ProfileForm", "When “%1” is on").arg(name)}});
        options.append(QVariantMap{{QStringLiteral("value"), QStringLiteral("!") + id},
                                   {QStringLiteral("text"),
                                    QCoreApplication::translate("ProfileForm", "When “%1” is off").arg(name)}});
    }
    return {{QStringLiteral("key"), QStringLiteral("when")},
            {QStringLiteral("widget"), QStringLiteral("select")},
            {QStringLiteral("title"), QCoreApplication::translate("ProfileForm", "Applies when")},
            {QStringLiteral("hint"), QString()},
            {QStringLiteral("half"), false},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("options"), options},
            {QStringLiteral("value"), QString::fromStdString(when)}};
}

}

std::span<const Field> fieldsOf(const input::ControlKind& kind)
{
    return std::visit(
        [](const auto& control) -> std::span<const Field> {
            using T = std::decay_t<decltype(control)>;
            if constexpr (std::is_same_v<T, input::TapControl>) {
                return kTapFields;
            } else if constexpr (std::is_same_v<T, input::HoldControl>) {
                return kHoldFields;
            } else if constexpr (std::is_same_v<T, input::LongPressControl>) {
                return kLongPressFields;
            } else if constexpr (std::is_same_v<T, input::TurboControl>) {
                return kTurboFields;
            } else if constexpr (std::is_same_v<T, input::WasdControl>) {
                return kWasdFields;
            } else if constexpr (std::is_same_v<T, input::RightWalkControl>) {
                return kRightWalkFields;
            } else if constexpr (std::is_same_v<T, input::PadStickControl>) {
                return kPadStickFields;
            } else if constexpr (std::is_same_v<T, input::CameraControl>) {
                return kCameraFields;
            } else if constexpr (std::is_same_v<T, input::PadCameraControl>) {
                return kPadCameraFields;
            } else if constexpr (std::is_same_v<T, input::FreeLookControl>) {
                return kFreeLookFields;
            } else if constexpr (std::is_same_v<T, input::SkillControl>) {
                return kSkillFields;
            } else if constexpr (std::is_same_v<T, input::SwipeControl>) {
                return kSwipeFields;
            } else if constexpr (std::is_same_v<T, input::ComponentControl>) {
                return kComponentFields;
            } else if constexpr (std::is_same_v<T, input::DialControl>) {
                return kDialFields;
            } else if constexpr (std::is_same_v<T, input::MultiPressControl>) {
                return kMultiPressFields;
            } else if constexpr (std::is_same_v<T, input::TempCursorControl>) {
                return kTempCursorFields;
            } else if constexpr (std::is_same_v<T, input::ActionWheelControl>) {
                return kActionWheelFields;
            } else if constexpr (std::is_same_v<T, input::BehaviorControl>) {
                return kBehaviorFields;
            } else {
                static_assert(std::is_same_v<T, input::PinchControl>);
                return kPinchFields;
            }
        },
        kind);
}

std::vector<Excluded> excludedOf(const input::ControlKind& kind)
{
    std::vector<Excluded> out;
    identity(out);
    std::visit(
        [&out](const auto& control) {
            using T = std::decay_t<decltype(control)>;
            if constexpr (std::is_same_v<T, input::TapControl> || std::is_same_v<T, input::HoldControl> ||
                          std::is_same_v<T, input::LongPressControl> || std::is_same_v<T, input::TurboControl>) {
            } else if constexpr (std::is_same_v<T, input::WasdControl>) {
                out.push_back({.path = "walkScale", .reason = Reason::Deferred});
            } else if constexpr (std::is_same_v<T, input::RightWalkControl>) {
            } else if constexpr (std::is_same_v<T, input::PadStickControl>) {
                slotKey(out, "stick", Reason::Deferred);
                claimKey(out, "stick");
            } else if constexpr (std::is_same_v<T, input::CameraControl>) {
                slotKey(out, "look", Reason::Deferred);
                claimKey(out, "look");
                out.push_back({.path = "lockKey", .reason = Reason::Canvas});
            } else if constexpr (std::is_same_v<T, input::PadCameraControl>) {
                slotKey(out, "look", Reason::Deferred);
                claimKey(out, "look");
            } else if constexpr (std::is_same_v<T, input::FreeLookControl>) {
                slotKey(out, "look", Reason::Deferred);
                claimKey(out, "look");
            } else if constexpr (std::is_same_v<T, input::SkillControl>) {
                slotKey(out, "look", Reason::Deferred);
                claimKey(out, "look");
                out.push_back({.path = "sensitivity", .reason = Reason::Deferred});
                out.push_back({.path = "direction", .reason = Reason::Deferred});
                out.push_back({.path = "castOnRelease", .reason = Reason::Deferred});
                out.push_back({.path = "cancelAt", .reason = Reason::Canvas});
            } else if constexpr (std::is_same_v<T, input::SwipeControl>) {
                out.push_back({.path = "path", .reason = Reason::Canvas});
            } else if constexpr (std::is_same_v<T, input::ComponentControl>) {
                out.push_back({.path = "component", .reason = Reason::Identity});
            } else if constexpr (std::is_same_v<T, input::DialControl>) {
                slotKey(out, "look", Reason::Deferred);
                claimKey(out, "look");
            } else if constexpr (std::is_same_v<T, input::ActionWheelControl>) {
                slotKey(out, "look", Reason::Deferred);
                claimKey(out, "look");
                out.push_back({.path = "cells", .reason = Reason::BehaviorPage});
            } else if constexpr (std::is_same_v<T, input::MultiPressControl> ||
                                 std::is_same_v<T, input::TempCursorControl>) {
            } else if constexpr (std::is_same_v<T, input::BehaviorControl>) {
                out.push_back({.path = "behavior", .reason = Reason::Identity});
                out.push_back({.path = "params", .reason = Reason::FixedRow});
            } else {
                static_assert(std::is_same_v<T, input::PinchControl>);
            }
        },
        kind);
    if (!std::holds_alternative<input::ComponentControl>(kind)) {
        out.push_back({.path = "component", .reason = Reason::Identity});
    }
    return out;
}

std::vector<Declared> declaredOf(const input::Control& control)
{
    std::vector<Declared> out;
    Collector collector(out, {});
    input::Control::visit(collector, control);
    return out;
}

const Field* find(const Context& context, std::string_view key)
{
    const input::Control* control = controlOf(context);
    if (control == nullptr) {
        return nullptr;
    }
    const std::span<const Field> fields = fieldsOf(control->kind);
    const auto found = std::ranges::find(fields, key, &Field::key);
    return found != fields.end() ? &*found : nullptr;
}

const Declared* find(const std::vector<Declared>& values, std::string_view key)
{
    return declared(values, key);
}

std::string bindingOf(std::string_view path)
{
    const std::size_t dot = path.rfind('.');
    return dot == std::string_view::npos ? std::string() : std::string(path.substr(0, dot));
}

bool numeric(const Field& field)
{
    return field.widget == Widget::Box || field.widget == Widget::Slider || field.widget == Widget::Number;
}

QString text(const char* source)
{
    return source == nullptr || *source == '\0' ? QString() : QCoreApplication::translate("ProfileForm", source);
}

QVariantList build(const Context& context, const KeyFieldMaker& keyField)
{
    const input::Control* control = controlOf(context);
    if (control == nullptr) {
        return {};
    }
    const std::vector<Declared> values = declaredOf(*control);
    const std::span<const Field> fields = fieldsOf(control->kind);
    QVariantList groups;
    QVariantList current;
    std::optional<int> group;
    std::optional<QVariantMap> boxes;
    const auto flushBoxes = [&] {
        if (boxes) {
            if (boxes->value(QStringLiteral("placed")).toBool() && !boxes->value(QStringLiteral("sized")).toBool()) {
                boxes->insert(QStringLiteral("title"), QCoreApplication::translate("ProfileForm", "Position"));
            }
            current.append(*boxes);
            boxes.reset();
        }
    };
    const auto flushGroup = [&] {
        flushBoxes();
        if (!current.isEmpty()) {
            groups.append(QVariantMap{{QStringLiteral("fields"), current}});
            current.clear();
        }
    };
    for (const Field& field : fields) {
        if (!isShown(field, control, values)) {
            continue;
        }
        if (group && *group != field.group) {
            flushGroup();
        }
        group = field.group;
        if (field.widget == Widget::Box) {
            if (!boxes) {
                boxes = boxRow(field);
                if (field.title == kPlace) {
                    boxes->insert(QStringLiteral("placed"), true);
                }
            }
            QVariantList list = boxes->value(QStringLiteral("boxes")).toList();
            list.append(fieldMap(field, control, values, context));
            boxes->insert(QStringLiteral("boxes"), list);
            if (field.title == kPlace && (field.measure == Measure::ExtentX || field.measure == Measure::ExtentY ||
                                          field.measure == Measure::Length)) {
                boxes->insert(QStringLiteral("sized"), true);
            }
            continue;
        }
        flushBoxes();
        if (field.widget == Widget::Key) {
            const Declared* found = declared(values, field.path);
            const auto* name = found != nullptr ? std::get_if<std::string>(&found->value) : nullptr;
            current.append(keyField(field, name != nullptr ? input::parseChord(*name) : std::nullopt));
        } else {
            current.append(fieldMap(field, control, values, context));
        }
        if (const auto* turbo = std::get_if<input::TurboControl>(&control->kind);
            turbo != nullptr && std::string_view(field.path) == "rateHz") {
            const int rate = turbo->rateHz;
            current.append(QVariantMap{
                {QStringLiteral("key"), QStringLiteral("rhythm")},
                {QStringLiteral("widget"), QStringLiteral("rhythm")},
                {QStringLiteral("title"), QCoreApplication::translate("ProfileForm", "Rhythm")},
                {QStringLiteral("hint"), QCoreApplication::translate("ProfileForm", "Hold 1 s · %n tap(s)", nullptr, rate)},
                {QStringLiteral("half"), false},
                {QStringLiteral("enabled"), true},
                {QStringLiteral("value"), rate}});
        }
    }
    flushGroup();
    return groups;
}

QVariantList behavior(const Context& context)
{
    const input::Control* control = controlOf(context);
    if (control == nullptr) {
        return {};
    }
    const std::vector<Declared> values = declaredOf(*control);
    const Declared* when = declared(values, "when");
    const auto* condition = when != nullptr ? std::get_if<std::string>(&when->value) : nullptr;
    return {QVariantMap{{QStringLiteral("fields"),
                         QVariantList{whenField(context, condition != nullptr ? *condition : std::string())}}}};
}

}

ProfileForm::ProfileForm(QObject* parent)
    : QAbstractListModel(parent)
{
}

void ProfileForm::setEditor(ProfileEditor* editor)
{
    if (editor == m_editor) {
        return;
    }
    disconnect(m_watch);
    m_editor = editor;
    if (editor != nullptr) {
        m_watch = connect(editor, &ProfileEditor::changed, this, [this] { reload(false); });
    }
    reload(true);
    emit editorChanged();
}

void ProfileForm::setKeyId(const QString& keyId)
{
    if (keyId == m_keyId) {
        return;
    }
    m_keyId = keyId;
    reload(true);
    emit keyIdChanged();
}

void ProfileForm::setBehavior(bool behavior)
{
    if (behavior == m_behavior) {
        return;
    }
    m_behavior = behavior;
    reload(true);
    emit behaviorChanged();
}

int ProfileForm::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ProfileForm::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= m_rows.size()) {
        return {};
    }
    const Row& row = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case FieldsRole:
        return row.fields;
    case FirstRole:
        return row.first;
    case LastRole:
        return row.last;
    case SeparatedRole:
        return row.separated;
    default:
        return {};
    }
}

QHash<int, QByteArray> ProfileForm::roleNames() const
{
    return {{FieldsRole, "fields"}, {FirstRole, "first"}, {LastRole, "last"}, {SeparatedRole, "separated"}};
}

std::vector<ProfileForm::Row> ProfileForm::rowsNow() const
{
    std::vector<Row> rows;
    if (m_editor == nullptr || m_keyId.isEmpty()) {
        return rows;
    }
    const QVariantList groups = m_behavior ? m_editor->behavior(m_keyId) : m_editor->form(m_keyId);
    for (qsizetype g = 0; g < groups.size(); ++g) {
        const QVariantList fields = groups[g].toMap().value(QStringLiteral("fields")).toList();
        const std::size_t start = rows.size();
        for (qsizetype f = 0; f < fields.size(); ++f) {
            const QVariantMap field = fields[f].toMap();
            const bool half = field.value(QStringLiteral("half")).toBool();
            QVariantList cells{field};
            if (half && f + 1 < fields.size() && fields[f + 1].toMap().value(QStringLiteral("half")).toBool()) {
                cells.append(fields[++f]);
            }
            QStringList keys;
            for (const QVariant& cell : cells) {
                keys.append(cell.toMap().value(QStringLiteral("key")).toString());
            }
            rows.push_back({.key = QString::number(g) + u':' + keys.join(u','), .fields = cells});
        }
        if (rows.size() > start) {
            rows[start].first = true;
            rows[start].separated = g > 0;
            rows.back().last = true;
        }
    }
    return rows;
}

void ProfileForm::reload(bool reset)
{
    std::vector<Row> rows = rowsNow();
    const bool same = !reset && rows.size() == m_rows.size() &&
                      std::ranges::equal(rows, m_rows, {}, &Row::key, &Row::key);
    if (!same) {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
        return;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].fields == m_rows[i].fields) {
            continue;
        }
        m_rows[i] = std::move(rows[i]);
        const QModelIndex changed = index(static_cast<int>(i));
        emit dataChanged(changed, changed, {FieldsRole});
    }
}

}
