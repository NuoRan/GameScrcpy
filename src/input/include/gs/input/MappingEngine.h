#pragma once

#include <gs/base/Clock.h>
#include <gs/base/InplaceVector.h>
#include <gs/input/Controls.h>
#include <gs/input/Modules.h>
#include <gs/input/Profile.h>
#include <gs/input/TouchFrameBuilder.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gs::input {

namespace detail {
class AttachmentChain;
}

enum class EngineNotice : std::uint8_t { NoCamera };

struct ControlStatus {
    bool touching = false;
    Vec2 stick;
    bool aiming = false;
    bool walking = false;
    bool cancelled = false;
    int cell = -1;

    friend bool operator==(const ControlStatus&, const ControlStatus&) = default;
};

[[nodiscard]] std::optional<Slot> slotNamed(std::string_view name) noexcept;

class EngineListener
{
public:
    virtual ~EngineListener() = default;

    virtual void pointerState(PointerState state) = 0;
    virtual void notice(EngineNotice notice) = 0;
    virtual void liveChanged(const std::vector<bool>& controls) = 0;
    virtual void aimingChanged(const std::vector<bool>&) {}
    virtual void controlPressed(std::string_view, bool) {}
    virtual void stateChanged(std::string_view, bool) {}
    virtual void controlEnabledChanged(std::string_view, bool) {}
    virtual void statusChanged(std::string_view, const ControlStatus&) {}
    virtual void peekChanged(bool) {}
    virtual void wheelChanged(std::string_view, bool, int) {}
};

struct EngineConfig {
    ScreenGeometry screen;
    double refreshHz = 60.0;
    std::optional<Microseconds> pacingTick;
    std::uint8_t maxPointers = PointerAllocator::kMaxPointers;
    std::uint32_t seed = 0;
    std::optional<Chord> defaultLockKey;
    std::shared_ptr<ModuleFactory> modules;
};

enum class CursorPhase : std::uint8_t { Down, Move, Up, Cancel };

class MappingEngine
{
public:
    MappingEngine(const Profile& profile, const EngineConfig& config, TouchFrameSink& touches,
                  EngineListener& listener);
    ~MappingEngine();
    MappingEngine(const MappingEngine&) = delete;
    MappingEngine& operator=(const MappingEngine&) = delete;

    void buttonDown(InputSource source, Microseconds now);
    void buttonUp(InputSource source, Microseconds now);
    void resumeHeld(InputSource source, Microseconds now);
    void motion(InputSource source, double dx, double dy, Microseconds now);
    void axis(InputSource source, Vec2 value, Microseconds now);
    void cursor(CursorPhase phase, Vec2 position, Microseconds now);
    void tick(Microseconds now);
    void flush(Microseconds now);

    void resetAll(Microseconds now);
    void setVideoSession(std::uint16_t session) noexcept { m_builder.setVideoSession(session); }
    void setRefreshHz(double hz) noexcept;

    void setCameraLock(Switch action, Microseconds now);
    bool setState(std::string_view id, Switch action, Microseconds now);
    bool setControlEnabled(std::string_view id, Switch action, Microseconds now);
    bool pressControl(std::string_view id, bool down, Microseconds now);
    bool pressSlot(std::string_view id, Slot slot, bool down, Microseconds now);
    bool resetControl(std::string_view id, Microseconds now);
    bool retuneControl(const Control& control, Microseconds now);
    void setHandedOver(const std::vector<std::string>& ids);
    bool pushControl(std::string_view id, Vec2 value, Microseconds now);
    bool turnControl(std::string_view id, Vec2 delta, Microseconds now);
    bool walkControl(std::string_view id, Vec2 point, Microseconds now);
    void setCursorLocator(CursorLocator locator) { *m_locateCursor = std::move(locator); }

    [[nodiscard]] bool cameraLocked() const noexcept { return m_locked; }
    [[nodiscard]] bool peeking() const noexcept { return m_peeking; }
    [[nodiscard]] bool claims(InputSource source) const;
    [[nodiscard]] bool hasCamera() const noexcept { return m_hasCamera; }
    [[nodiscard]] std::optional<bool> state(std::string_view id) const;
    [[nodiscard]] std::optional<bool> controlLive(std::string_view id) const;
    [[nodiscard]] std::optional<bool> controlEnabled(std::string_view id) const;
    [[nodiscard]] std::optional<ControlStatus> controlStatus(std::string_view id) const;
    [[nodiscard]] std::optional<Microseconds> nextWakeup() const;
    [[nodiscard]] std::uint8_t activeTouches() const noexcept { return m_pointers.activeCount(); }

private:
    static constexpr std::size_t kNoState = static_cast<std::size_t>(-1);
    static constexpr std::size_t kMaxPressed = 8;

    using Component = std::variant<ButtonComponent, JoystickComponent, CameraComponent, FreeLookComponent,
                                   AimComponent, SwipeComponent, PinchComponent, DialComponent, MultiPressComponent,
                                   PeekComponent, ActionWheelComponent, BehaviorComponent>;

    struct Lane {
        Slot slot = Slot::Press;
        BehaviorSlotKind kind = BehaviorSlotKind::Key;
        std::optional<std::size_t> entry;
    };

    struct WheelRelease {
        std::size_t control = 0;
        Slot slot = Slot::Press;
        Microseconds at{0};
    };

    struct ControlTarget {
        std::size_t control;
        Slot slot;
    };
    struct LockTarget {};
    using Target = std::variant<ControlTarget, LockTarget>;

    struct Entry {
        Binding binding;
        Target target;
        TriggerState trigger;
        bool held2d = false;
        bool axis2d = false;
        bool mouse = false;
        bool quiet = false;
    };

    struct Condition {
        std::size_t state = kNoState;
        bool expect = true;
    };

    struct ControlState {
        std::string id;
        Component component;
        std::vector<std::size_t> entries;
        Condition when;
        bool enabled = true;
        bool live = false;
        bool camera = false;
        std::uint8_t held = 0;
        std::vector<Lane> lanes;
        std::unique_ptr<detail::AttachmentChain> chain;
        bool handable = false;
        bool handedOver = false;
        std::uint64_t engagedOrder = 0;
    };

    using Pressed = InplaceVector<std::size_t, kMaxPressed>;

    struct Built {
        Component component;
        bool camera = false;
        TriggerConfig press;
    };

    void compile(const Profile& profile);
    [[nodiscard]] Built build(const Control& control, const CoordinateMapper& mapper) const;
    void addControl(const Control& control, const CoordinateMapper& mapper);
    std::size_t addEntry(const Binding& binding, Target target, SlotKind slot, const TriggerConfig& trigger = {});
    void finishEntries();

    [[nodiscard]] Condition conditionOf(const std::string& when) const;
    [[nodiscard]] bool holds(Condition condition) const noexcept;
    [[nodiscard]] bool steersLock(const Entry& entry) const noexcept;
    [[nodiscard]] bool mouseClaimed(const Entry& entry) const noexcept;
    [[nodiscard]] bool entryLive(const Entry& entry) const noexcept;
    void collectPressed(InputSource source, Pressed& out) const;
    bool pressEntries(InputSource source, Microseconds now, bool resuming = false);
    [[nodiscard]] bool resumable(std::size_t entry) const;
    void releaseEntries(InputSource source, Microseconds now);
    void notePressed(std::size_t entry, bool down);
    [[nodiscard]] bool quietFor(std::size_t entry) const noexcept;
    bool takeQuiet(std::size_t entry) noexcept;
    [[nodiscard]] std::optional<std::size_t> controlIndex(std::string_view id) const;

    void deliver(std::size_t entry, TriggerSignal signal, Microseconds now);
    void deliverToControl(std::size_t control, Slot slot, TriggerSignal signal, Microseconds now);
    void runWheelChoice(std::size_t control, int cell, Microseconds now);
    void releaseWheelPresses(Microseconds now);
    void lockAction(Switch action, Microseconds now);
    void applyLock(bool locked, Microseconds now);
    void endUnclaimed(Microseconds now);
    void syncPeek(Microseconds now);
    [[nodiscard]] bool captured() const noexcept { return m_locked && !m_peeking; }
    void changeState(std::size_t index, Switch action, Microseconds now);
    void changeControl(std::size_t index, Switch action, Microseconds now);
    void refresh(Microseconds now);
    void releaseEntry(std::size_t entry, Microseconds now, bool deliverRelease);
    void followCursor(Microseconds now);
    void syncPauses(Microseconds now);
    void settle(Microseconds now);

    [[nodiscard]] Component makeBehavior(const Control& control, const BehaviorControl& behavior, std::size_t index,
                                         const CoordinateMapper& mapper) const;
    void bindBehavior(std::size_t index, const BehaviorControl& behavior);
    void addLanes(std::size_t index, const Control& control);
    void addChain(std::size_t index, const Control& control, const CoordinateMapper& mapper);
    [[nodiscard]] TouchOutput& outOf(std::size_t control) noexcept;
    bool routeInput(std::size_t entry, ModuleInput kind, Vec2 value, Microseconds now);
    void deliverLane(std::size_t control, const ModuleEvent& event, Microseconds now);
    void pressLane(std::size_t control, const Lane& lane, bool down, Microseconds now);
    void resetComponent(std::size_t control);
    void tellTrouble(std::size_t control);
    [[nodiscard]] Microseconds frameTick() const noexcept;

    std::unique_ptr<CursorLocator> m_locateCursor = std::make_unique<CursorLocator>();
    EngineConfig m_config;
    std::optional<CoordinateMapper> m_mapper;
    EngineListener& m_listener;
    TouchFrameSink& m_sink;
    PointerAllocator m_pointers;
    TouchFrameBuilder m_builder;
    TouchOutput m_out;
    Microseconds m_frame;

    std::vector<std::string> m_stateIds;
    std::vector<bool> m_stateValues;
    std::vector<bool> m_live;
    bool m_locked = false;
    bool m_peeking = false;
    std::vector<WheelRelease> m_wheelReleases;
    bool m_hasCamera = false;
    bool m_compiled = false;

    std::vector<ControlState> m_controls;
    std::vector<Entry> m_entries;
    std::array<std::vector<std::size_t>, kSourceCount> m_dispatch{};
    std::array<Pressed, kSourceCount> m_pressed{};
    std::uint8_t m_modifiers = 0;
    std::optional<std::uint8_t> m_cursor;
    Vec2 m_cursorAt;
    std::vector<std::pair<std::size_t, std::size_t>> m_pauses;
    std::vector<bool> m_aiming;
    std::vector<ControlStatus> m_status;
    Microseconds m_now{0};
    std::uint64_t m_engagements = 0;
};

}
