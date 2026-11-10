#include "gs/record/Operations.h"

#include <gs/script/Resources.h>

#include <cmath>

namespace gs::record {

namespace {

constexpr double kFixedRange = 65535.0;
constexpr double kPlaces = 10'000.0;

double fractionOf(std::uint16_t fixed)
{
    return std::round(fixed / kFixedRange * kPlaces) / kPlaces;
}

script::SessionTouchAction actionOf(protocol::TouchAction action)
{
    switch (action) {
    case protocol::TouchAction::Down:
        return script::SessionTouchAction::Down;
    case protocol::TouchAction::Up:
        return script::SessionTouchAction::Up;
    case protocol::TouchAction::Move:
        return script::SessionTouchAction::Move;
    case protocol::TouchAction::Cancel:
        return script::SessionTouchAction::Cancel;
    }
    return script::SessionTouchAction::Move;
}

}

script::SessionTouch touchOf(const protocol::TouchFrame& frame)
{
    script::SessionTouch touch;
    touch.points.reserve(frame.points.size());
    for (const protocol::TouchPoint& point : frame.points) {
        touch.points.push_back(script::SessionTouchPoint{.pointer = point.pointerId,
                                                         .action = actionOf(point.action),
                                                         .x = fractionOf(point.x),
                                                         .y = fractionOf(point.y)});
    }
    return touch;
}

script::SessionKey keyOf(const protocol::Key& key)
{
    return script::SessionKey{.down = key.action == protocol::KeyAction::Down,
                              .keycode = key.keycode,
                              .metaState = key.metaState,
                              .repeat = key.repeat};
}

script::SessionScroll scrollOf(const protocol::Scroll& scroll)
{
    return script::SessionScroll{.x = fractionOf(scroll.x),
                                 .y = fractionOf(scroll.y),
                                 .horizontal = scroll.hScroll,
                                 .vertical = scroll.vScroll};
}

script::SessionText textOf(std::string_view utf8) noexcept
{
    return script::SessionText{.length = script::characterCount(utf8)};
}

}
