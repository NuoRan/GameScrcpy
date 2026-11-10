#pragma once

#include <gs/base/Clock.h>
#include <gs/input/Geometry.h>
#include <gs/input/Modules.h>
#include <gs/input/PointerAllocator.h>
#include <gs/input/Profile.h>
#include <gs/input/TouchFrameBuilder.h>
#include <gs/input/TriggerState.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gs::input {

enum class Slot : std::uint8_t {
    Press,
    Up,
    Down,
    Left,
    Right,
    Walk,
    Sprint,
    Stick,
    Look,
    Hold,
    ZoomIn,
    ZoomOut,
    Cancel,
    WalkTo,
    Stop,
    Cast
};

using CursorLocator = std::function<std::optional<Vec2>()>;

class TouchRoute
{
public:
    virtual ~TouchRoute() = default;

    [[nodiscard]] virtual std::optional<std::uint8_t> down(Vec2 position) = 0;
    virtual void move(std::uint8_t pointer, Vec2 position) = 0;
    virtual void up(std::uint8_t pointer, Vec2 position) = 0;
    virtual void cancel(std::uint8_t pointer, Vec2 position) = 0;
};

class TouchOutput
{
public:
    TouchOutput(PointerAllocator& pointers, TouchFrameBuilder& frames, TouchRoute* route = nullptr) noexcept
        : m_pointers(pointers)
        , m_frames(frames)
        , m_route(route)
    {
    }

    [[nodiscard]] std::optional<std::uint8_t> down(Vec2 position);
    void move(std::uint8_t pointer, Vec2 position);
    void up(std::uint8_t pointer, Vec2 position);
    void cancel(std::uint8_t pointer, Vec2 position);

private:
    PointerAllocator& m_pointers;
    TouchFrameBuilder& m_frames;
    TouchRoute* m_route = nullptr;
};

struct ButtonConfig {
    Vec2 position;
    Microseconds tapDuration{33'333};
};

class ButtonComponent
{
public:
    explicit ButtonComponent(const ButtonConfig& config) noexcept
        : m_config(config)
    {
    }

    void signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out);
    void tick(Microseconds now, TouchOutput& out);
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept { return m_releaseAt; }
    void reset(TouchOutput& out);
    void retune(const ButtonComponent& other) noexcept { m_config = other.m_config; }
    [[nodiscard]] bool touching() const noexcept { return m_pointer.has_value(); }

private:
    void press(TouchOutput& out);
    void lift(TouchOutput& out);

    ButtonConfig m_config;
    std::optional<std::uint8_t> m_pointer;
    Vec2 m_at;
    std::optional<Microseconds> m_releaseAt;
};

struct JoystickConfig {
    Vec2 center;
    Vec2 radius;
    double walkScale = 0.5;
    double sprintScale = 1.3;
    Vec2 hero{0.5, 0.5};
    Vec2 unit{1.0, 1.0};
    Microseconds walkPerUnit{600'000};
    bool stopOnRelease = true;
    const CursorLocator* locate = nullptr;
};

class JoystickComponent
{
public:
    explicit JoystickComponent(const JoystickConfig& config) noexcept
        : m_config(config)
    {
    }

    void signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out);
    void axis(Slot slot, Vec2 value, Microseconds now, TouchOutput& out);
    void follow(Microseconds now, TouchOutput& out);
    void walkToward(Vec2 point, Microseconds now, TouchOutput& out);
    void tick(Microseconds now, TouchOutput& out);
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept;
    void setFrozen(bool frozen, Microseconds now, TouchOutput& out);
    void reset(TouchOutput& out);
    void retune(const JoystickComponent& other) noexcept { m_config = other.m_config; }
    [[nodiscard]] bool touching() const noexcept { return m_pointer.has_value(); }
    [[nodiscard]] Vec2 deflection() const noexcept { return m_pointer ? m_offset : Vec2{}; }
    [[nodiscard]] bool walking() const noexcept { return m_walking; }

private:
    [[nodiscard]] Vec2 target() const noexcept;
    void update(Microseconds now, TouchOutput& out);
    void moveTo(Vec2 offset, TouchOutput& out);
    bool walkTowardCursor(Microseconds now);
    bool aimWalk(Vec2 point, Microseconds now);
    void stopWalking() noexcept;

    JoystickConfig m_config;
    bool m_up = false;
    bool m_down = false;
    bool m_left = false;
    bool m_right = false;
    bool m_walk = false;
    bool m_sprint = false;
    Vec2 m_stick;
    std::optional<std::uint8_t> m_pointer;
    bool m_landed = false;
    Vec2 m_landing;
    Vec2 m_offset;
    std::optional<Microseconds> m_deadline;
    bool m_walking = false;
    bool m_walkHeld = false;
    Vec2 m_walkDirection;
    Microseconds m_walkPressed{};
    std::optional<Microseconds> m_walkUntil;
    bool m_frozen = false;
};

struct CameraConfig {
    Vec2 anchor;
    Vec2 halfExtent;
    Vec2 perCount;
    Microseconds idleRecenter{1'000'000};
    Microseconds frame{16'667};
    bool wholePicture = false;
};

class CameraComponent
{
public:
    explicit CameraComponent(const CameraConfig& config) noexcept
        : m_config(config)
    {
    }

    void motion(Slot slot, double dx, double dy, Microseconds now, TouchOutput& out);
    void axis(Slot slot, Vec2 value, Microseconds now, TouchOutput& out);
    void tick(Microseconds now, TouchOutput& out);
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept;
    void reset(TouchOutput& out);
    void retune(const CameraComponent& other) noexcept { m_config = other.m_config; }
    [[nodiscard]] bool touching() const noexcept { return m_pointer.has_value() || m_retiring.has_value(); }

private:
    enum class Phase : std::uint8_t {
        Idle,
        Landed,
        Tracking,
        HandingOff
    };

    void apply(TouchOutput& out);
    void handOver(Vec2 from, Microseconds now, TouchOutput& out);
    [[nodiscard]] bool centered() const noexcept;
    [[nodiscard]] Vec2 low() const noexcept;
    [[nodiscard]] Vec2 high() const noexcept;
    [[nodiscard]] std::optional<Microseconds> phaseDeadline() const noexcept;

    CameraConfig m_config;
    Phase m_phase = Phase::Idle;
    std::optional<std::uint8_t> m_pointer;
    std::optional<std::uint8_t> m_retiring;
    Vec2 m_at;
    Vec2 m_retiringAt;
    Vec2 m_pending;
    Microseconds m_lastMotion{0};
    std::optional<Microseconds> m_followUp;
    Vec2 m_velocity;
    Microseconds m_lastStep{0};
    std::optional<Microseconds> m_stepAt;
};

struct FreeLookConfig {
    Vec2 position;
    Vec2 perCount;
    bool resetOnRelease = true;
};

class FreeLookComponent
{
public:
    explicit FreeLookComponent(const FreeLookConfig& config) noexcept
        : m_config(config)
    {
    }

    void signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out);
    void motion(Slot slot, double dx, double dy, Microseconds now, TouchOutput& out);
    void tick(Microseconds now, TouchOutput& out);
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept { return m_followUp; }
    void reset(TouchOutput& out);
    void retune(const FreeLookComponent& other) noexcept { m_config = other.m_config; }

    [[nodiscard]] bool engaged() const noexcept { return m_held; }
    [[nodiscard]] bool touching() const noexcept { return m_pointer.has_value(); }

private:
    FreeLookConfig m_config;
    std::optional<std::uint8_t> m_pointer;
    bool m_held = false;
    Vec2 m_at;
    Vec2 m_pending;
    std::optional<Microseconds> m_followUp;
};

struct AimConfig {
    Vec2 position;
    Vec2 radius;
    Vec2 perCount;
    bool castOnRelease = true;
    bool cursor = false;
    CastTiming cast = CastTiming::Release;
    Vec2 hero{0.5, 0.5};
    Vec2 castExtent{0.35, 0.35};
    std::optional<Vec2> cancelAt;
    Microseconds frame{16'667};
    const CursorLocator* locate = nullptr;
};

class AimComponent
{
public:
    explicit AimComponent(const AimConfig& config) noexcept
        : m_config(config)
    {
    }

    void signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out);
    void motion(Slot slot, double dx, double dy, Microseconds now, TouchOutput& out);
    void follow(Microseconds now, TouchOutput& out);
    bool castByClick(TouchOutput& out);
    void tick(Microseconds now, TouchOutput& out);
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept { return m_followUp; }
    void reset(TouchOutput& out);
    void retune(const AimComponent& other) noexcept { m_config = other.m_config; }
    [[nodiscard]] bool engaged() const noexcept { return m_pointer.has_value() && !m_releaseAfterLanding; }
    [[nodiscard]] bool touching() const noexcept { return m_pointer.has_value(); }
    [[nodiscard]] bool cancelling() const noexcept { return m_cancelling; }
    [[nodiscard]] bool cancelled() const noexcept { return m_cancelled; }
    [[nodiscard]] bool armed() const noexcept
    {
        return m_config.cast == CastTiming::Manual && engaged() && !m_cancelling;
    }

private:
    void release(TouchOutput& out);
    void cast(TouchOutput& out);
    void cancelCast(Microseconds now, TouchOutput& out);
    void clear() noexcept;
    [[nodiscard]] std::optional<Vec2> cursorOffset() const;
    [[nodiscard]] Vec2 position() const noexcept;

    AimConfig m_config;
    std::optional<std::uint8_t> m_pointer;
    Vec2 m_offset;
    bool m_landed = false;
    bool m_releaseAfterLanding = false;
    bool m_cancelling = false;
    bool m_cancelled = false;
    std::optional<Microseconds> m_followUp;
};

struct SwipeConfig {
    std::vector<Vec2> path;
    Microseconds duration{200'000};
    Microseconds frame{16'667};
};

class SwipeComponent
{
public:
    explicit SwipeComponent(SwipeConfig config) noexcept
        : m_config(std::move(config))
    {
    }

    void signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out);
    void tick(Microseconds now, TouchOutput& out);
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept { return m_followUp; }
    void reset(TouchOutput& out);
    void retune(const SwipeComponent& other) { m_config = other.m_config; }
    [[nodiscard]] bool touching() const noexcept { return m_pointer.has_value(); }

private:
    [[nodiscard]] Vec2 pointAt(double progress) const noexcept;

    SwipeConfig m_config;
    std::optional<std::uint8_t> m_pointer;
    Vec2 m_at;
    Microseconds m_startedAt{0};
    std::optional<Microseconds> m_followUp;
};

struct PinchConfig {
    Vec2 center;
    double halfSpan = 0.05;
    double halfStep = 0.01;
    Microseconds duration{100'000};
    Microseconds frame{16'667};
};

class PinchComponent
{
public:
    explicit PinchComponent(const PinchConfig& config) noexcept
        : m_config(config)
    {
    }

    void signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out);
    void tick(Microseconds now, TouchOutput& out);
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept { return m_followUp; }
    void reset(TouchOutput& out);
    void retune(const PinchComponent& other) noexcept { m_config = other.m_config; }
    [[nodiscard]] bool touching() const noexcept { return m_left.has_value() || m_right.has_value(); }

private:
    void place(double half, TouchOutput& out);

    PinchConfig m_config;
    std::optional<std::uint8_t> m_left;
    std::optional<std::uint8_t> m_right;
    double m_from = 0.0;
    double m_to = 0.0;
    double m_half = 0.0;
    Microseconds m_startedAt{0};
    std::optional<Microseconds> m_followUp;
};

struct DialConfig {
    Vec2 center;
    Vec2 radius;
    std::uint8_t cells = 8;
    double firstDeg = 0.0;
};

class DialComponent
{
public:
    static constexpr double kChooseCounts = 12.0;
    static constexpr double kReachCounts = 40.0;

    explicit DialComponent(const DialConfig& config) noexcept
        : m_config(config)
    {
    }

    void signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out);
    void motion(Slot slot, double dx, double dy, Microseconds now, TouchOutput& out);
    void tick(Microseconds, TouchOutput&) {}
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept { return std::nullopt; }
    void reset(TouchOutput& out);
    void retune(const DialComponent& other) noexcept { m_config = other.m_config; }
    [[nodiscard]] bool engaged() const noexcept { return m_pointer.has_value(); }
    [[nodiscard]] bool touching() const noexcept { return m_pointer.has_value(); }
    [[nodiscard]] int cell() const noexcept { return m_pointer ? m_cell : -1; }

private:
    [[nodiscard]] int cellToward(double dx, double dy) const noexcept;
    [[nodiscard]] Vec2 spotOf(int cell) const noexcept;

    DialConfig m_config;
    std::optional<std::uint8_t> m_pointer;
    double m_pushX = 0.0;
    double m_pushY = 0.0;
    int m_cell = -1;
};

struct MultiPressConfig {
    std::vector<Vec2> spots;
};

class MultiPressComponent
{
public:
    explicit MultiPressComponent(MultiPressConfig config) noexcept
        : m_config(std::move(config))
    {
    }

    void signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out);
    void tick(Microseconds, TouchOutput&) {}
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept { return std::nullopt; }
    void reset(TouchOutput& out);
    void retune(const MultiPressComponent& other) { m_config = other.m_config; }
    [[nodiscard]] bool touching() const noexcept { return !m_fingers.empty(); }

private:
    void lift(TouchOutput& out);

    MultiPressConfig m_config;
    std::vector<std::pair<std::uint8_t, Vec2>> m_fingers;
};

class PeekComponent
{
public:
    void signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out);
    void tick(Microseconds, TouchOutput&) {}
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept { return std::nullopt; }
    void reset(TouchOutput&) noexcept { m_held = false; }
    void retune(const PeekComponent&) noexcept {}
    [[nodiscard]] bool touching() const noexcept { return false; }
    [[nodiscard]] bool held() const noexcept { return m_held; }
    void setShowing(bool showing) noexcept { m_showing = showing; }
    [[nodiscard]] bool showing() const noexcept { return m_showing; }

private:
    bool m_held = false;
    bool m_showing = false;
};

struct WheelSlot {
    WheelAction action = WheelAction::None;
    std::string control;
    Vec2 at;
};

struct ActionWheelConfig {
    std::vector<WheelSlot> cells;
    Microseconds tapHold{0};
};

class ActionWheelComponent
{
public:
    explicit ActionWheelComponent(ActionWheelConfig config) noexcept
        : m_config(std::move(config))
    {
    }

    void signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out);
    void motion(Slot slot, double dx, double dy, Microseconds now, TouchOutput& out);
    void tick(Microseconds now, TouchOutput& out);
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept { return m_tapEnd; }
    void reset(TouchOutput& out);
    void retune(const ActionWheelComponent& other) { m_config = other.m_config; }
    [[nodiscard]] bool engaged() const noexcept { return m_open; }
    [[nodiscard]] bool touching() const noexcept { return m_open; }
    [[nodiscard]] int cell() const noexcept { return m_open ? m_cell : -1; }
    [[nodiscard]] std::optional<int> takeChoice() noexcept { return std::exchange(m_choice, std::nullopt); }
    [[nodiscard]] const WheelSlot* slotAt(int cell) const noexcept;
    [[nodiscard]] Microseconds tapHold() const noexcept { return m_config.tapHold; }

private:
    [[nodiscard]] int cellToward(double dx, double dy) const noexcept;
    void close(bool choose, Microseconds now, TouchOutput& out);

    ActionWheelConfig m_config;
    bool m_open = false;
    double m_pushX = 0.0;
    double m_pushY = 0.0;
    int m_cell = -1;
    std::optional<int> m_choice;
    std::optional<std::uint8_t> m_tap;
    Vec2 m_tapAt;
    std::optional<Microseconds> m_tapEnd;
};

struct BehaviorConfig {
    std::string module;
    BehaviorShape shape;
    Microseconds frame{4'167};
};

class BehaviorComponent
{
public:
    BehaviorComponent(BehaviorConfig config, std::unique_ptr<BehaviorInstance> instance) noexcept
        : m_config(std::move(config))
        , m_instance(std::move(instance))
    {
    }

    void signal(Slot slot, TriggerSignal signal, Microseconds now, TouchOutput& out);
    void input(const ModuleEvent& event, Microseconds now, TouchOutput& out);
    void tick(Microseconds now, TouchOutput& out);
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept;
    void reset(TouchOutput& out);
    void retune(const BehaviorComponent&) noexcept {}
    [[nodiscard]] bool touching() const noexcept;
    [[nodiscard]] bool working() const noexcept { return m_instance != nullptr && !m_stopped; }
    [[nodiscard]] const std::string& module() const noexcept { return m_config.module; }
    [[nodiscard]] const BehaviorShape& shape() const noexcept { return m_config.shape; }
    [[nodiscard]] std::optional<ModuleTroubleKind> takeTrouble() noexcept
    {
        return std::exchange(m_trouble, std::nullopt);
    }

private:
    void finish(ModuleStatus status, const ModuleOutput& result, Microseconds now, TouchOutput& out);
    void apply(const FingerCommand& command, TouchOutput& out);
    void lift(TouchOutput& out);
    void plan(Microseconds now);
    void note(const ModuleEvent& event) noexcept;
    [[nodiscard]] bool active() const noexcept;

    BehaviorConfig m_config;
    std::unique_ptr<BehaviorInstance> m_instance;
    std::array<std::optional<std::uint8_t>, kMostModuleFingers> m_fingers{};
    std::array<Vec2, kMostModuleFingers> m_at{};
    std::vector<FingerCommand> m_held;
    std::optional<Microseconds> m_wake;
    std::optional<Microseconds> m_frameAt;
    std::array<bool, kMostBehaviorSlots> m_keys{};
    std::array<Vec2, kMostBehaviorSlots> m_sticks{};
    bool m_stopped = false;
    std::optional<ModuleTroubleKind> m_trouble;
};

}
