#include "gs/input/KeyHints.h"

#include <algorithm>
#include <array>
#include <string>
#include <variant>

namespace gs::input {

namespace {

constexpr std::array<std::string_view, kMostMultiPressPoints> kPointNames{
    "points.0", "points.1", "points.2", "points.3", "points.4", "points.5", "points.6", "points.7", "points.8"};

constexpr std::array<std::string_view, kMostWheelCells> kCellNames{"cells.0", "cells.1", "cells.2",  "cells.3",
                                                                  "cells.4", "cells.5", "cells.6",  "cells.7",
                                                                  "cells.8", "cells.9", "cells.10", "cells.11"};

const InputSource& mouseMove()
{
    static const InputSource move = *parseSource("Mouse.Move");
    return move;
}

bool initiallyLive(const Profile& profile, const Control& control)
{
    if (!control.enabled) {
        return false;
    }
    if (control.when.empty()) {
        return true;
    }
    const bool negate = control.when.starts_with('!');
    const std::string_view id = negate ? std::string_view(control.when).substr(1) : std::string_view(control.when);
    const auto found = std::ranges::find(profile.vars, id, &Variable::id);
    return found == profile.vars.end() || truthy(found->initial) != negate;
}

void add(std::vector<HintKey>& keys, std::string_view slot, const std::optional<Binding>& binding)
{
    if (binding && binding->input && binding->input->source != mouseMove()) {
        keys.push_back({slot, *binding->input});
    }
}

}

std::vector<KeyHint> keyHints(const Profile& profile, const ScreenGeometry& screen,
                              const std::optional<Chord>& defaultLockKey, bool everyControl)
{
    const CoordinateMapper mapper{screen, profile.screen.scaleMode, profile.screen.safeArea};
    std::vector<KeyHint> hints;
    for (std::size_t i = 0; i < profile.controls.size(); ++i) {
        const Control& control = profile.controls[i];
        KeyHint hint{.control = i,
                     .type = kControlTypeNames[control.kind.index()],
                     .center = mapper.position(control.anchor, control.offset),
                     .extent = {},
                     .live = initiallyLive(profile, control),
                     .keys = {}};
        if (const auto* tap = std::get_if<TapControl>(&control.kind)) {
            add(hint.keys, "press", tap->press);
        } else if (const auto* hold = std::get_if<HoldControl>(&control.kind)) {
            add(hint.keys, "press", hold->press);
        } else if (const auto* longPress = std::get_if<LongPressControl>(&control.kind)) {
            add(hint.keys, "press", longPress->press);
        } else if (const auto* turbo = std::get_if<TurboControl>(&control.kind)) {
            add(hint.keys, "press", turbo->press);
        } else if (const auto* wasd = std::get_if<WasdControl>(&control.kind)) {
            hint.extent = mapper.radius(wasd->radius);
            add(hint.keys, "up", wasd->up);
            add(hint.keys, "down", wasd->down);
            add(hint.keys, "left", wasd->left);
            add(hint.keys, "right", wasd->right);
            add(hint.keys, "walk", wasd->walk);
            add(hint.keys, "sprint", wasd->sprint);
            if (wasd->sprint) {
                hint.sprint = wasd->sprintScale;
            }
        } else if (const auto* walker = std::get_if<RightWalkControl>(&control.kind)) {
            hint.extent = mapper.radius(walker->radius);
            add(hint.keys, "walkTo", walker->walkTo);
            add(hint.keys, "stop", walker->stop);
        } else if (const auto* pad = std::get_if<PadStickControl>(&control.kind)) {
            hint.extent = mapper.radius(pad->radius);
            add(hint.keys, "stick", pad->stick);
        } else if (const auto* camera = std::get_if<CameraControl>(&control.kind)) {
            const Vec2 size = mapper.extent(camera->size);
            hint.extent = {size.x / 2.0, size.y / 2.0};
            if (const std::optional<Chord> key = camera->lockKey ? camera->lockKey : defaultLockKey) {
                hint.keys.push_back({"lockKey", *key});
            }
            add(hint.keys, "look", camera->look);
        } else if (const auto* padCamera = std::get_if<PadCameraControl>(&control.kind)) {
            const Vec2 size = mapper.extent(padCamera->size);
            hint.extent = {size.x / 2.0, size.y / 2.0};
            add(hint.keys, "look", padCamera->look);
        } else if (const auto* peek = std::get_if<FreeLookControl>(&control.kind)) {
            add(hint.keys, "hold", peek->hold);
            add(hint.keys, "look", peek->look);
        } else if (const auto* skill = std::get_if<SkillControl>(&control.kind)) {
            hint.extent = mapper.radius(skill->radius);
            add(hint.keys, "press", skill->press);
            if (skill->direction == AimDirection::Cursor) {
                hint.castCenter = mapper.position(Anchor::Center, skill->hero);
                hint.castExtent = mapper.radius(skill->castRadius);
            } else {
                add(hint.keys, "look", skill->look);
            }
        } else if (const auto* swipe = std::get_if<SwipeControl>(&control.kind)) {
            add(hint.keys, "press", swipe->press);
        } else if (const auto* pinch = std::get_if<PinchControl>(&control.kind)) {
            add(hint.keys, "zoomIn", pinch->zoomIn);
            add(hint.keys, "zoomOut", pinch->zoomOut);
        } else if (const auto* dial = std::get_if<DialControl>(&control.kind)) {
            hint.extent = mapper.radius(dial->radius);
            hint.cells = dial->cells;
            hint.firstDeg = dial->firstDeg;
            add(hint.keys, "press", dial->press);
        } else if (const auto* multi = std::get_if<MultiPressControl>(&control.kind)) {
            add(hint.keys, "press", multi->press);
            for (std::size_t at = 0; at < multi->points.size() && at < kPointNames.size(); ++at) {
                const Vec2 step = mapper.extent(multi->points[at]);
                hint.points.push_back({kPointNames[at], {hint.center.x + step.x, hint.center.y + step.y}});
            }
        } else if (const auto* cursor = std::get_if<TempCursorControl>(&control.kind)) {
            add(hint.keys, "press", cursor->press);
        } else if (const auto* wheel = std::get_if<ActionWheelControl>(&control.kind)) {
            add(hint.keys, "press", wheel->press);
            hint.cells = static_cast<int>(wheel->cells.size());
            for (std::size_t at = 0; at < wheel->cells.size() && at < kCellNames.size(); ++at) {
                if (wheel->cells[at].action == WheelAction::Tap) {
                    const Vec2 step = mapper.extent(wheel->cells[at].at);
                    hint.points.push_back({kCellNames[at], {hint.center.x + step.x, hint.center.y + step.y}});
                }
            }
        } else if (const auto* behavior = std::get_if<BehaviorControl>(&control.kind)) {
            for (const NamedBinding& entry : behavior->inputs) {
                add(hint.keys, entry.name, entry.binding);
            }
        }
        if (const std::string& ref = componentRef(control); !ref.empty()) {
            hint.component = ref;
            for (const NamedBinding& entry : componentKeys(control)) {
                add(hint.keys, entry.name, entry.binding);
            }
            for (const NamedValue& entry : componentProps(control)) {
                if (const auto* point = std::get_if<Vec2>(&entry.value)) {
                    const Vec2 step = mapper.extent(*point);
                    hint.points.push_back({entry.name, {hint.center.x + step.x, hint.center.y + step.y}});
                }
            }
        }
        if (!hint.keys.empty() || everyControl) {
            hints.push_back(std::move(hint));
        }
    }
    return hints;
}

}
