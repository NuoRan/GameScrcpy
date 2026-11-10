#pragma once

#include <gs/input/Geometry.h>
#include <gs/input/InputSource.h>
#include <gs/input/Profile.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace gs::input {

struct HintKey {
    std::string_view slot;
    Chord input;
};

struct HintPoint {
    std::string_view name;
    Vec2 at;
};

struct KeyHint {
    std::size_t control = 0;
    std::string_view type;
    Vec2 center;
    Vec2 extent;
    bool live = true;
    std::vector<HintKey> keys;
    std::optional<Vec2> castCenter;
    Vec2 castExtent;
    double sprint = 0.0;
    std::string_view component;
    std::vector<HintPoint> points;
    int cells = 0;
    double firstDeg = 0.0;
};

[[nodiscard]] std::vector<KeyHint> keyHints(const Profile& profile, const ScreenGeometry& screen,
                                            const std::optional<Chord>& defaultLockKey, bool everyControl = false);

}
