#pragma once

#include "ControlChannel.h"
#include "GamepadPassthrough.h"

#include <gs/input/DeviceEvents.h>
#include <gs/input/MappingEngine.h>
#include <gs/input/Profile.h>
#include <gs/platform/InputCapture.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::app {

[[nodiscard]] input::Chord defaultLockKey();

struct LaneValues {
    std::vector<std::pair<std::string, bool>> states;
    std::vector<std::pair<std::string, bool>> enabled;

    friend bool operator==(const LaneValues&, const LaneValues&) = default;
};

class LaneObserver
{
public:
    virtual ~LaneObserver() = default;

    virtual void synced(const LaneValues& values) = 0;
    virtual void controlPressed(std::string_view id, bool down) = 0;
    virtual void stateChanged(std::string_view id, bool value) = 0;
    virtual void controlEnabledChanged(std::string_view id, bool enabled) = 0;
    virtual void statusChanged(std::string_view, const input::ControlStatus&) {}
    virtual void keyEvent(std::string_view, bool) {}
    virtual void lockChanged(bool) {}
    virtual void profileChanged(const std::shared_ptr<const input::Profile>&) {}
};

class InputHost final : public platform::InputHandler
{
public:
    static constexpr std::uint8_t kEnginePointers = 24;
    static constexpr std::uint8_t kScriptPointers = 8;

    struct Callbacks {
        std::function<void(input::PointerState)> pointer;
        std::function<void(input::EngineNotice)> notice;
        std::function<void(const ControlChannel*, std::vector<bool>, std::uintptr_t)> live;
        std::function<std::uint8_t()> pressedMouse;
        input::CursorLocator cursor;
        std::function<void(const ControlChannel*, std::vector<bool>, std::uintptr_t)> aiming;
        std::function<void(bool)> peek;
        std::function<void(const std::string& control, bool open, int cell)> wheel;
        std::function<void(const input::PadRumble& rumble)> rumble;
        std::function<void(const ControlChannel* channel, PadNotice notice, input::PadFamily family)> padNotice;
    };

    struct Follower {
        std::shared_ptr<ControlChannel> channel;
        std::shared_ptr<const input::Profile> profile;
    };

    explicit InputHost(Callbacks callbacks = {});
    ~InputHost() override;
    InputHost(const InputHost&) = delete;
    InputHost& operator=(const InputHost&) = delete;

    void setChannel(std::shared_ptr<ControlChannel> channel, Microseconds now);
    void setProfile(std::shared_ptr<const input::Profile> profile, Microseconds now, bool revision = false);
    void setLockKey(std::optional<input::Chord> key, Microseconds now);
    void setModules(std::shared_ptr<input::ModuleFactory> modules, Microseconds now);
    void setActive(bool on, Microseconds now);
    void setCameraLock(input::Switch action, Microseconds now);
    void cursor(input::CursorPhase phase, input::Vec2 position, Microseconds now);

    void attach(const std::shared_ptr<ControlChannel>& channel, std::shared_ptr<const input::Profile> profile,
                std::shared_ptr<LaneObserver> observer, Microseconds now);
    void detach(const ControlChannel* channel, const LaneObserver* observer, Microseconds now);
    void watch(const std::shared_ptr<ControlChannel>& channel, std::shared_ptr<const input::Profile> profile,
               std::shared_ptr<LaneObserver> observer, Microseconds now);
    void unwatch(const ControlChannel* channel, const LaneObserver* observer, Microseconds now);
    void watchKeys(const ControlChannel* channel, const LaneObserver* observer, std::vector<input::InputSource> keys);
    void forget(const ControlChannel* channel, Microseconds now);

    bool pressControl(const ControlChannel* channel, std::string_view id, bool down, Microseconds now);
    bool enableControl(const ControlChannel* channel, std::string_view id, bool on, Microseconds now);
    bool retuneControl(const ControlChannel* channel, const input::Profile* basis, const input::Control& control,
                       Microseconds now);
    bool setState(const ControlChannel* channel, std::string_view id, bool value, Microseconds now);
    bool withEngine(const ControlChannel* channel, const std::function<bool(input::MappingEngine&)>& act,
                    Microseconds now);
    void claimKeys(const ControlChannel* channel, std::vector<input::InputSource> keys);
    void handOver(const ControlChannel* channel, std::vector<std::string> ids);
    void typeText(const ControlChannel* channel, std::string_view text);
    void tap(const ControlChannel* channel, std::uint64_t gesture, input::Vec2 at, Microseconds duration,
             Microseconds now);
    void swipe(const ControlChannel* channel, std::uint64_t gesture, input::Vec2 from, input::Vec2 to,
               Microseconds duration, Microseconds now);
    void holdGesture(const ControlChannel* channel, std::uint64_t gesture, input::Vec2 at, Microseconds now);
    void moveGesture(const ControlChannel* channel, std::uint64_t gesture, input::Vec2 to, Microseconds duration,
                     Microseconds now);
    void liftGesture(const ControlChannel* channel, std::uint64_t gesture, Microseconds now);
    void liftAll(const ControlChannel* channel, Microseconds now);
    void androidKey(const ControlChannel* channel, std::uint32_t keycode, bool down);
    bool feed(const ControlChannel* channel, const input::DeviceEvent& event, Microseconds now);
    std::size_t setFollowers(const std::shared_ptr<ControlChannel>& master,
                             const std::vector<Follower>& followers, Microseconds now);
    [[nodiscard]] std::size_t followerCount(const ControlChannel* master) const;
    [[nodiscard]] const ControlChannel* leaderOf(const ControlChannel* follower) const;
    void padLinkChanged(const ControlChannel* channel, Microseconds now);
    void padOutput(const ControlChannel* channel, std::uint16_t id, std::span<const std::uint8_t> data);
    void padStatus(const ControlChannel* channel, std::uint16_t id, bool recognized, bool vibration, Microseconds now);
    [[nodiscard]] GamepadPassthrough& passthrough() noexcept { return m_passthrough; }

    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] bool locked() const noexcept { return m_locked; }
    [[nodiscard]] bool peeking() const noexcept { return m_peeking; }
    [[nodiscard]] std::uint8_t activeTouches() const noexcept;
    [[nodiscard]] std::size_t laneCount() const noexcept { return m_lanes.size(); }

    void key(const platform::RawKey& raw, Microseconds now) override;
    void mouse(const platform::RawMouse& report, Microseconds now) override;
    void gamepad(const platform::PadState& state, Microseconds now) override;
    void wake(Microseconds now) override;
    [[nodiscard]] std::optional<Microseconds> nextWakeup() const override;

private:
    struct ScriptTouch {
        std::uint64_t gesture = 0;
        std::uint8_t pointer = 0;
        std::uint16_t videoSession = 0;
        input::Vec2 from;
        input::Vec2 to;
        Microseconds start{0};
        Microseconds end{0};
        Microseconds step{0};
        Microseconds next{0};
        bool held = false;
    };

    struct ScriptLift {
        protocol::TouchAction action = protocol::TouchAction::Up;
        std::uint8_t pointer = 0;
        std::uint16_t videoSession = 0;
        input::Vec2 position;
    };

    struct Memory {
        bool valid = false;
        LaneValues values;
    };

    struct Watcher {
        std::shared_ptr<LaneObserver> observer;
        std::vector<input::InputSource> keys;
        std::vector<input::InputSource> held;
    };

    class Lane;

    [[nodiscard]] Lane* find(const ControlChannel* channel) const noexcept;
    Lane& laneFor(const std::shared_ptr<ControlChannel>& channel);
    [[nodiscard]] input::MappingEngine* engineOf(Lane& lane, Microseconds now);
    void release(Lane& lane, Microseconds now);
    void dropEngine(Lane& lane, Microseconds now);
    void leave(Lane& lane, Microseconds now);
    void erase(Lane& lane, Microseconds now);
    void changeProfile(Lane& lane, std::shared_ptr<const input::Profile> profile, Microseconds now);
    void connect(Lane& lane, Microseconds now);
    [[nodiscard]] LaneValues valuesOf(Lane& lane, Microseconds now);
    bool remember(Lane& lane, std::string_view id, bool value, bool state);
    bool sendTouch(Lane& lane, protocol::TouchAction action, std::uint8_t pointer, std::uint16_t videoSession,
                   input::Vec2 position);
    void lift(Lane& lane, const ScriptLift& pending, Microseconds now);
    void retryLifts(Lane& lane, Microseconds now);
    [[nodiscard]] std::optional<std::uint8_t> scriptPointer(Lane& lane, Microseconds now);
    void advanceTouches(Lane& lane, Microseconds now);
    void endTouches(Lane& lane, Microseconds now);
    void tellScript(Lane& lane, const input::DeviceEvent& event);
    bool noteKey(std::vector<input::InputSource>& held, input::InputSource source, bool down) const;
    void releaseScriptKeys(Lane& lane);
    [[nodiscard]] static bool usable(const Lane& lane) noexcept;
    [[nodiscard]] std::vector<Lane*> followersOf(const Lane& leader) const;
    [[nodiscard]] bool leads(const Lane& lane) const;
    void unfollow(Lane& lane, Microseconds now);
    void unlock();
    [[nodiscard]] bool heldSinceActivation(const input::DeviceEvent& event);
    void noteHeld(const input::DeviceEvent& event);
    void resumeHeld(input::MappingEngine& engine, Microseconds now) const;
    void refreshHeld(Microseconds now);
    void followPad(Microseconds now);

    template <typename Events>
    void deliverAll(const Events& events, Microseconds now);

    Callbacks m_callbacks;
    std::vector<std::unique_ptr<Lane>> m_lanes;
    Lane* m_target = nullptr;
    std::shared_ptr<const input::Profile> m_profile;
    std::optional<input::Chord> m_lockKey;
    bool m_active = false;
    bool m_locked = false;
    bool m_peeking = false;
    bool m_relocking = false;
    std::uint8_t m_heldMouse = 0;
    std::vector<input::InputSource> m_down;
    std::vector<std::pair<input::InputSource, input::Vec2>> m_pushed;
    input::KeyboardDecoder m_keyboard;
    input::MouseDecoder m_mouse;
    input::GamepadTracker m_gamepad;
    GamepadPassthrough m_passthrough;
    std::shared_ptr<input::ModuleFactory> m_modules;
    std::uint32_t m_seed = 0;
};

}
