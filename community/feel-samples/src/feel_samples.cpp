#include "params.h"

#include <gamescrcpy_ext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using feel::Params;
using feel::Point;

constexpr std::size_t kFingers = GS_EXT_MOST_FINGERS;
constexpr std::uint32_t kDown = GS_EXT_COMMAND_DOWN;
constexpr std::uint32_t kMove = GS_EXT_COMMAND_MOVE;
constexpr std::uint32_t kUp = GS_EXT_COMMAND_UP;
constexpr std::uint32_t kKeyDown = GS_EXT_INPUT_KEY_DOWN;
constexpr std::uint32_t kKeyUp = GS_EXT_INPUT_KEY_UP;
constexpr std::uint32_t kAxis2 = GS_EXT_INPUT_AXIS2;
constexpr std::uint32_t kAxis1 = GS_EXT_INPUT_AXIS1;
constexpr std::uint64_t kGlideStep = 8'000;
constexpr std::uint64_t kSmoothStep = 16'000;
constexpr double kPi = std::numbers::pi;

class Random
{
public:
    explicit Random(std::uint64_t seed) noexcept
        : m_state(seed)
    {
    }

    double next() noexcept
    {
        m_state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t mixed = m_state;
        mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
        mixed ^= mixed >> 31U;
        return static_cast<double>(mixed >> 11U) / 9007199254740992.0;
    }

private:
    std::uint64_t m_state;
};

struct Place {
    Point position{0.5, 0.5};
    Point unit{0.1, 0.1};
};

Place placeOf(const Params& params)
{
    Place place;
    if (const std::optional<Point> position = params.point("@position")) {
        place.position = *position;
    }
    if (const std::optional<Point> unit = params.point("@unit"); unit && unit->x > 0.0 && unit->y > 0.0) {
        place.unit = *unit;
    }
    return place;
}

std::uint64_t microseconds(double milliseconds) noexcept
{
    return milliseconds > 0.0 ? static_cast<std::uint64_t>(milliseconds * 1000.0) : 0;
}

std::uint64_t earliest(std::uint64_t current, std::uint64_t wanted) noexcept
{
    return current == 0 ? wanted : std::min(current, wanted);
}

bool known(const gs_ext_touch_command& command) noexcept
{
    return command.finger < kFingers;
}

void push(gs_ext_output& out, std::uint32_t kind, std::uint32_t finger, Point at, std::uint64_t when) noexcept
{
    if (out.commands == nullptr || out.command_count >= out.command_capacity) {
        return;
    }
    gs_ext_touch_command& made = out.commands[out.command_count++];
    made.kind = kind;
    made.finger = finger;
    made.x = at.x;
    made.y = at.y;
    made.at = when;
}

struct Plain {
    int32_t input(const gs_ext_input_event&, gs_ext_output&) noexcept { return GS_EXT_OK; }
    int32_t output(const gs_ext_touch_command&, gs_ext_output&) noexcept { return GS_EXT_OK; }
    int32_t frame(std::uint64_t, gs_ext_output&) noexcept { return GS_EXT_OK; }
    int32_t wake(std::uint64_t, gs_ext_output&) noexcept { return GS_EXT_OK; }
    void reset() noexcept {}
};

struct RandomLanding : Plain {
    RandomLanding(const Params& params, std::uint64_t seed)
        : place(placeOf(params))
        , radius(std::clamp(params.number("radius", 2.0), 0.0, 100.0) / 100.0)
        , random(seed)
    {
    }

    int32_t output(const gs_ext_touch_command& command, gs_ext_output& out) noexcept
    {
        if (!known(command) || out.command_count < 1) {
            return GS_EXT_OK;
        }
        Point& offset = offsets[command.finger];
        if (command.kind == kDown) {
            const double distance = radius * std::sqrt(random.next());
            const double angle = 2.0 * kPi * random.next();
            offset = {distance * std::cos(angle) * place.unit.x, distance * std::sin(angle) * place.unit.y};
        }
        out.commands[0].x = command.x + offset.x;
        out.commands[0].y = command.y + offset.y;
        if (command.kind == kUp) {
            offset = {};
        }
        return GS_EXT_OK;
    }

    void reset() noexcept { offsets = {}; }

    Place place;
    double radius = 0.0;
    Random random;
    std::array<Point, kFingers> offsets{};
};

struct JoystickGlide : Plain {
    struct Finger {
        bool down = false;
        bool gliding = false;
        Point at;
        Point from;
        Point to;
        std::uint64_t start = 0;
        std::uint64_t end = 0;
    };

    JoystickGlide(const Params& params, std::uint64_t)
        : duration(microseconds(std::clamp(params.number("time", 80.0), 0.0, 2000.0)))
    {
    }

    int32_t output(const gs_ext_touch_command& command, gs_ext_output& out) noexcept
    {
        if (!known(command) || duration == 0) {
            return GS_EXT_OK;
        }
        Finger& finger = fingers[command.finger];
        const Point target{command.x, command.y};
        if (command.kind == kDown) {
            finger = {.down = true, .gliding = false, .at = target, .from = target, .to = target};
        } else if (command.kind == kMove && finger.down) {
            finger.from = finger.at;
            finger.to = target;
            finger.start = command.at;
            finger.end = command.at + duration;
            finger.gliding = true;
            out.command_count = 0;
            out.wake_at = earliest(out.wake_at, command.at + kGlideStep);
        } else if (command.kind == kUp) {
            if (finger.gliding && out.command_capacity >= 2) {
                out.commands[1] = command;
                out.commands[0] = command;
                out.commands[0].kind = kMove;
                out.command_count = 2;
            }
            finger = {};
        }
        return GS_EXT_OK;
    }

    int32_t wake(std::uint64_t now, gs_ext_output& out) noexcept
    {
        std::uint64_t next = 0;
        for (std::uint32_t index = 0; index < kFingers; ++index) {
            Finger& finger = fingers[index];
            if (!finger.gliding) {
                continue;
            }
            const double span = static_cast<double>(finger.end - finger.start);
            const double done = now <= finger.start || span <= 0.0
                                    ? (now <= finger.start ? 0.0 : 1.0)
                                    : std::min(1.0, static_cast<double>(now - finger.start) / span);
            finger.at = {finger.from.x + (finger.to.x - finger.from.x) * done,
                         finger.from.y + (finger.to.y - finger.from.y) * done};
            push(out, kMove, index, finger.at, now);
            if (done >= 1.0) {
                finger.gliding = false;
            } else {
                next = earliest(next, now + kGlideStep);
            }
        }
        out.wake_at = next;
        return GS_EXT_OK;
    }

    void reset() noexcept { fingers = {}; }

    std::uint64_t duration = 0;
    std::array<Finger, kFingers> fingers{};
};

struct JoystickWander : Plain {
    JoystickWander(const Params& params, std::uint64_t seed)
        : place(placeOf(params))
        , amount(std::clamp(params.number("amount", 3.0), 0.0, 100.0) / 100.0)
        , random(seed)
    {
    }

    int32_t output(const gs_ext_touch_command& command, gs_ext_output& out) noexcept
    {
        if (!known(command) || out.command_count < 1) {
            return GS_EXT_OK;
        }
        Point& offset = offsets[command.finger];
        if (command.kind == kDown) {
            offset = {};
        } else if (command.kind == kMove) {
            offset.x += (random.next() - 0.5) * amount * 0.5;
            offset.y += (random.next() - 0.5) * amount * 0.5;
            const double length = std::hypot(offset.x, offset.y);
            if (length > amount && length > 0.0) {
                offset.x *= amount / length;
                offset.y *= amount / length;
            }
        }
        out.commands[0].x = command.x + offset.x * place.unit.x;
        out.commands[0].y = command.y + offset.y * place.unit.y;
        if (command.kind == kUp) {
            offset = {};
        }
        return GS_EXT_OK;
    }

    void reset() noexcept { offsets = {}; }

    Place place;
    double amount = 0.0;
    Random random;
    std::array<Point, kFingers> offsets{};
};

struct CurvedSwipe : Plain {
    struct Start {
        bool down = false;
        Point at;
    };

    CurvedSwipe(const Params& params, std::uint64_t)
        : place(placeOf(params))
        , bend(std::clamp(params.number("bend", 0.3), -1.0, 1.0))
        , length(std::clamp(params.number("length", 50.0), 1.0, 400.0) / 100.0)
    {
    }

    int32_t output(const gs_ext_touch_command& command, gs_ext_output& out) noexcept
    {
        if (!known(command) || out.command_count < 1) {
            return GS_EXT_OK;
        }
        Start& start = starts[command.finger];
        if (command.kind == kDown) {
            start = {.down = true, .at = {command.x, command.y}};
            return GS_EXT_OK;
        }
        if (!start.down) {
            return GS_EXT_OK;
        }
        const double dx = (command.x - start.at.x) / place.unit.x;
        const double dy = (command.y - start.at.y) / place.unit.y;
        const double distance = std::hypot(dx, dy);
        if (distance > 0.0) {
            const double side = bend * distance * std::sin(kPi * std::min(distance / length, 1.0));
            out.commands[0].x = command.x - dy / distance * side * place.unit.x;
            out.commands[0].y = command.y + dx / distance * side * place.unit.y;
        }
        if (command.kind == kUp) {
            start = {};
        }
        return GS_EXT_OK;
    }

    void reset() noexcept { starts = {}; }

    Place place;
    double bend = 0.0;
    double length = 1.0;
    std::array<Start, kFingers> starts{};
};

struct Smoothing : Plain {
    struct Finger {
        bool down = false;
        Point shown;
        Point target;
    };

    Smoothing(const Params& params, std::uint64_t)
        : strength(std::clamp(params.number("strength", 0.5), 0.0, 0.95))
    {
    }

    static bool settled(const Finger& finger) noexcept
    {
        return std::abs(finger.shown.x - finger.target.x) < 1e-4 && std::abs(finger.shown.y - finger.target.y) < 1e-4;
    }

    void ease(Finger& finger) const noexcept
    {
        finger.shown = {finger.shown.x + (finger.target.x - finger.shown.x) * (1.0 - strength),
                        finger.shown.y + (finger.target.y - finger.shown.y) * (1.0 - strength)};
        if (settled(finger)) {
            finger.shown = finger.target;
        }
    }

    int32_t output(const gs_ext_touch_command& command, gs_ext_output& out) noexcept
    {
        if (!known(command) || out.command_count < 1 || strength <= 0.0) {
            return GS_EXT_OK;
        }
        Finger& finger = fingers[command.finger];
        const Point target{command.x, command.y};
        if (command.kind == kDown) {
            finger = {.down = true, .shown = target, .target = target};
            return GS_EXT_OK;
        }
        if (command.kind == kUp || !finger.down) {
            finger = {};
            return GS_EXT_OK;
        }
        finger.target = target;
        ease(finger);
        out.commands[0].x = finger.shown.x;
        out.commands[0].y = finger.shown.y;
        if (!settled(finger)) {
            out.wake_at = earliest(out.wake_at, command.at + kSmoothStep);
        }
        return GS_EXT_OK;
    }

    int32_t wake(std::uint64_t now, gs_ext_output& out) noexcept
    {
        std::uint64_t next = 0;
        for (std::uint32_t index = 0; index < kFingers; ++index) {
            Finger& finger = fingers[index];
            if (!finger.down || settled(finger)) {
                continue;
            }
            ease(finger);
            push(out, kMove, index, finger.shown, now);
            if (!settled(finger)) {
                next = earliest(next, now + kSmoothStep);
            }
        }
        out.wake_at = next;
        return GS_EXT_OK;
    }

    void reset() noexcept { fingers = {}; }

    double strength = 0.0;
    std::array<Finger, kFingers> fingers{};
};

struct LowPass {
    bool ready = false;
    double value = 0.0;

    double filter(double next, double alpha) noexcept
    {
        if (!ready) {
            ready = true;
            value = next;
            return value;
        }
        value += alpha * (next - value);
        return value;
    }
};

struct AdaptiveSmoothing : Plain {
    struct Axis {
        LowPass place;
        LowPass speed;
    };

    struct Finger {
        bool down = false;
        std::uint64_t last = 0;
        Axis x;
        Axis y;
    };

    AdaptiveSmoothing(const Params& params, std::uint64_t)
        : minCutoff(std::clamp(params.number("minCutoff", 1.0), 0.01, 100.0))
        , beta(std::clamp(params.number("beta", 1.0), 0.0, 1000.0))
    {
    }

    static double alphaOf(double cutoff, double seconds) noexcept
    {
        const double tau = 1.0 / (2.0 * kPi * cutoff);
        return 1.0 / (1.0 + tau / seconds);
    }

    double step(Axis& axis, double next, double seconds) const noexcept
    {
        const double previous = axis.place.ready ? axis.place.value : next;
        const double speed = axis.speed.filter((next - previous) / seconds, alphaOf(1.0, seconds));
        return axis.place.filter(next, alphaOf(minCutoff + beta * std::abs(speed), seconds));
    }

    int32_t output(const gs_ext_touch_command& command, gs_ext_output& out) noexcept
    {
        if (!known(command) || out.command_count < 1) {
            return GS_EXT_OK;
        }
        Finger& finger = fingers[command.finger];
        if (command.kind == kDown) {
            finger = {};
            finger.down = true;
            finger.last = command.at;
            static_cast<void>(step(finger.x, command.x, 1.0));
            static_cast<void>(step(finger.y, command.y, 1.0));
            return GS_EXT_OK;
        }
        if (command.kind == kUp || !finger.down) {
            finger = {};
            return GS_EXT_OK;
        }
        const double seconds =
            std::max(0.001, command.at > finger.last ? static_cast<double>(command.at - finger.last) / 1e6 : 0.0);
        finger.last = command.at;
        out.commands[0].x = step(finger.x, command.x, seconds);
        out.commands[0].y = step(finger.y, command.y, seconds);
        return GS_EXT_OK;
    }

    void reset() noexcept { fingers = {}; }

    double minCutoff = 1.0;
    double beta = 1.0;
    std::array<Finger, kFingers> fingers{};
};

struct ResponseCurve : Plain {
    ResponseCurve(const Params& params, std::uint64_t)
        : points(params.points("curve"))
    {
        if (points.size() < 2) {
            points = {{0.0, 0.0}, {1.0, 1.0}};
        }
    }

    [[nodiscard]] double shape(double amount) const noexcept
    {
        const double clamped = std::clamp(amount, 0.0, 1.0);
        if (clamped <= points.front().x) {
            return std::clamp(points.front().y, 0.0, 1.0);
        }
        for (std::size_t index = 1; index < points.size(); ++index) {
            const Point& low = points[index - 1];
            const Point& high = points[index];
            if (clamped <= high.x) {
                const double span = high.x - low.x;
                const double along = span > 0.0 ? (clamped - low.x) / span : 1.0;
                return std::clamp(low.y + (high.y - low.y) * along, 0.0, 1.0);
            }
        }
        return std::clamp(points.back().y, 0.0, 1.0);
    }

    int32_t input(const gs_ext_input_event& event, gs_ext_output& out) noexcept
    {
        if (out.events == nullptr || out.event_count < 1) {
            return GS_EXT_OK;
        }
        if (event.kind == kAxis2) {
            const double amount = std::hypot(event.x, event.y);
            if (amount > 0.0) {
                const double shaped = shape(amount);
                out.events[0].x = event.x / amount * shaped;
                out.events[0].y = event.y / amount * shaped;
            }
        } else if (event.kind == kAxis1) {
            out.events[0].x = shape(event.x);
        }
        return GS_EXT_OK;
    }

    std::vector<Point> points;
};

struct DeadZone : Plain {
    DeadZone(const Params& params, std::uint64_t)
        : size(std::clamp(params.number("size", 0.15), 0.0, 0.95))
    {
    }

    [[nodiscard]] double beyond(double amount) const noexcept
    {
        return amount <= size ? 0.0 : std::min(1.0, (amount - size) / (1.0 - size));
    }

    int32_t input(const gs_ext_input_event& event, gs_ext_output& out) noexcept
    {
        if (out.events == nullptr || out.event_count < 1) {
            return GS_EXT_OK;
        }
        if (event.kind == kAxis2) {
            const double amount = std::hypot(event.x, event.y);
            const double scaled = amount > 0.0 ? beyond(amount) / amount : 0.0;
            out.events[0].x = event.x * scaled;
            out.events[0].y = event.y * scaled;
        } else if (event.kind == kAxis1) {
            out.events[0].x = beyond(event.x);
        }
        return GS_EXT_OK;
    }

    double size = 0.15;
};

struct HoldTime : Plain {
    HoldTime(const Params& params, std::uint64_t seed)
        : minimum(microseconds(std::clamp(params.number("minimum", 80.0), 0.0, 10000.0)))
        , extra(microseconds(std::clamp(params.number("extra", 30.0), 0.0, 10000.0)))
        , random(seed)
    {
    }

    int32_t output(const gs_ext_touch_command& command, gs_ext_output& out) noexcept
    {
        if (!known(command) || out.command_count < 1) {
            return GS_EXT_OK;
        }
        if (command.kind == kDown) {
            held[command.finger] = true;
            until[command.finger] =
                command.at + minimum + static_cast<std::uint64_t>(random.next() * static_cast<double>(extra));
        } else if (command.kind == kUp && held[command.finger]) {
            if (command.at < until[command.finger]) {
                out.commands[0].at = until[command.finger];
            }
            held[command.finger] = false;
        }
        return GS_EXT_OK;
    }

    void reset() noexcept { held = {}; }

    std::uint64_t minimum = 0;
    std::uint64_t extra = 0;
    Random random;
    std::array<bool, kFingers> held{};
    std::array<std::uint64_t, kFingers> until{};
};

struct Circle : Plain {
    Circle(const Params& params, std::uint64_t)
        : place(placeOf(params))
        , radius(std::clamp(params.number("radius", 30.0), 0.0, 400.0) / 100.0)
        , period(std::max<std::uint64_t>(microseconds(params.number("period", 1000.0)), 50'000))
        , clockwise(params.flag("clockwise", true))
    {
    }

    [[nodiscard]] Point at(std::uint64_t now) const noexcept
    {
        const double turns = now > start ? static_cast<double>(now - start) / static_cast<double>(period) : 0.0;
        const double angle = 2.0 * kPi * turns * (clockwise ? 1.0 : -1.0);
        return {place.position.x + radius * place.unit.x * std::cos(angle),
                place.position.y + radius * place.unit.y * std::sin(angle)};
    }

    int32_t input(const gs_ext_input_event& event, gs_ext_output& out) noexcept
    {
        if (event.slot != 0) {
            return GS_EXT_OK;
        }
        if (event.kind == kKeyDown && !holding) {
            holding = true;
            start = event.at;
            last = at(event.at);
            push(out, kDown, 0, last, event.at);
        } else if (event.kind == kKeyUp && holding) {
            holding = false;
            push(out, kUp, 0, last, event.at);
        }
        return GS_EXT_OK;
    }

    int32_t frame(std::uint64_t now, gs_ext_output& out) noexcept
    {
        if (holding) {
            last = at(now);
            push(out, kMove, 0, last, now);
        }
        return GS_EXT_OK;
    }

    void reset() noexcept { holding = false; }

    Place place;
    double radius = 0.3;
    std::uint64_t period = 1'000'000;
    bool clockwise = true;
    bool holding = false;
    std::uint64_t start = 0;
    Point last;
};

template <typename T>
int32_t startWith(void*, const char* params, size_t size, uint64_t seed, void** state)
{
    if (state == nullptr) {
        return GS_EXT_BAD_ARGUMENT;
    }
    try {
        const Params read(params != nullptr ? std::string_view(params, size) : std::string_view());
        *state = new T(read, seed);
        return GS_EXT_OK;
    } catch (...) {
        return GS_EXT_FAILED;
    }
}

template <typename T>
int32_t inputWith(void* state, const gs_ext_input_event* event, gs_ext_output* out)
{
    return event != nullptr && out != nullptr ? static_cast<T*>(state)->input(*event, *out) : GS_EXT_BAD_ARGUMENT;
}

template <typename T>
int32_t outputWith(void* state, const gs_ext_touch_command* command, gs_ext_output* out)
{
    return command != nullptr && out != nullptr ? static_cast<T*>(state)->output(*command, *out)
                                                : GS_EXT_BAD_ARGUMENT;
}

template <typename T>
int32_t frameWith(void* state, uint64_t now, gs_ext_output* out)
{
    return out != nullptr ? static_cast<T*>(state)->frame(now, *out) : GS_EXT_BAD_ARGUMENT;
}

template <typename T>
int32_t wakeWith(void* state, uint64_t now, gs_ext_output* out)
{
    return out != nullptr ? static_cast<T*>(state)->wake(now, *out) : GS_EXT_BAD_ARGUMENT;
}

template <typename T>
void resetWith(void* state)
{
    static_cast<T*>(state)->reset();
}

template <typename T>
void stopWith(void* state)
{
    delete static_cast<T*>(state);
}

template <typename T>
constexpr gs_ext_attachment attachment(const char* name)
{
    return {name, nullptr, &startWith<T>, &inputWith<T>, &outputWith<T>, &wakeWith<T>, &resetWith<T>, &stopWith<T>};
}

const gs_ext_attachment kAttachments[] = {
    attachment<RandomLanding>("randomLanding"),
    attachment<JoystickGlide>("joystickGlide"),
    attachment<JoystickWander>("joystickWander"),
    attachment<CurvedSwipe>("curvedSwipe"),
    attachment<Smoothing>("smoothing"),
    attachment<AdaptiveSmoothing>("adaptiveSmoothing"),
    attachment<ResponseCurve>("responseCurve"),
    attachment<DeadZone>("deadZone"),
    attachment<HoldTime>("holdTime"),
};

const uint32_t kCircleSlots[] = {GS_EXT_SLOT_KEY};

const gs_ext_behavior kBehaviors[] = {
    {"circle", kCircleSlots, 1, 1, 1, nullptr, &startWith<Circle>, &inputWith<Circle>, &frameWith<Circle>,
     &wakeWith<Circle>, &resetWith<Circle>, &stopWith<Circle>},
};

}

GS_EXT_EXPORT int32_t gs_ext_entry(const gs_ext_host* host, gs_ext_module* module)
{
    if (host == nullptr || module == nullptr || host->abi_version != GS_EXT_ABI_VERSION) {
        return GS_EXT_UNSUPPORTED;
    }
    module->abi_version = GS_EXT_ABI_VERSION;
    module->attachment_count = static_cast<uint32_t>(std::size(kAttachments));
    module->attachments = kAttachments;
    module->behavior_count = static_cast<uint32_t>(std::size(kBehaviors));
    module->behaviors = kBehaviors;
    return GS_EXT_OK;
}
