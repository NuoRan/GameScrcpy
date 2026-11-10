#include "InputHost.h"

#include <gs/platform/Foreground.h>

#include <algorithm>
#include <array>
#include <random>
#include <utility>

namespace gs::app {

namespace {

using Values = std::vector<std::pair<std::string, bool>>;

const input::Profile& noProfile()
{
    static const input::Profile none;
    return none;
}

std::uint8_t buttonBit(input::InputSource source)
{
    static const std::array<std::pair<input::InputSource, std::uint8_t>, 5> buttons{{
        {*input::parseSource("Mouse.Left"), platform::mouse_button::kLeft},
        {*input::parseSource("Mouse.Right"), platform::mouse_button::kRight},
        {*input::parseSource("Mouse.Middle"), platform::mouse_button::kMiddle},
        {*input::parseSource("Mouse.X1"), platform::mouse_button::kX1},
        {*input::parseSource("Mouse.X2"), platform::mouse_button::kX2},
    }};
    for (const auto& [button, bit] : buttons) {
        if (button == source) {
            return bit;
        }
    }
    return 0;
}

input::Switch switchOf(bool on) noexcept
{
    return on ? input::Switch::On : input::Switch::Off;
}

std::optional<bool> lookup(const Values& values, std::string_view id)
{
    const auto found = std::ranges::find_if(values, [&](const auto& entry) { return entry.first == id; });
    return found != values.end() ? std::optional<bool>(found->second) : std::nullopt;
}

void upsert(Values& values, std::string_view id, bool value)
{
    const auto found = std::ranges::find_if(values, [&](const auto& entry) { return entry.first == id; });
    if (found != values.end()) {
        found->second = value;
    } else {
        values.emplace_back(std::string(id), value);
    }
}

void sendKey(ControlChannel& channel, std::uint32_t keycode, bool down)
{
    protocol::Key key;
    key.action = down ? protocol::KeyAction::Down : protocol::KeyAction::Up;
    key.keycode = keycode;
    static_cast<void>(channel.send(key));
}

input::Vec2 along(input::Vec2 from, input::Vec2 to, Microseconds start, Microseconds end, Microseconds now)
{
    if (end <= start || now >= end) {
        return to;
    }
    const double t = now <= start ? 0.0
                                  : static_cast<double>((now - start).count()) / static_cast<double>((end - start).count());
    return {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
}

std::uint32_t freshSeed()
{
    std::random_device device;
    return static_cast<std::uint32_t>(device());
}

}

input::Chord defaultLockKey()
{
    static const input::Chord key = *input::parseChord("Backquote");
    return key;
}

class InputHost::Lane final : public input::TouchFrameSink, public input::EngineListener
{
public:
    Lane(InputHost& owner, std::shared_ptr<ControlChannel> link)
        : host(owner)
        , channel(std::move(link))
    {
    }

    [[nodiscard]] bool target() const noexcept { return host.m_target == this; }
    [[nodiscard]] bool connected() const noexcept { return observer != nullptr && profile == observerProfile; }

    bool trySend(const protocol::TouchFrame& frame) override { return channel->send(frame); }

    void pointerState(input::PointerState state) override
    {
        if (!target()) {
            if (leader != nullptr) {
                followLock(state == input::PointerState::Captured);
            }
            return;
        }
        host.m_locked = state == input::PointerState::Captured;
        if (host.m_callbacks.pointer) {
            host.m_callbacks.pointer(state);
        }
        if (connected() && !restoring) {
            const std::shared_ptr<LaneObserver> watcher = observer;
            watcher->lockChanged(state == input::PointerState::Captured);
        }
        tellWatchers([state](LaneObserver& each) { each.lockChanged(state == input::PointerState::Captured); });
    }

    void followLock(bool locked)
    {
        if (connected() && !restoring) {
            const std::shared_ptr<LaneObserver> watcher = observer;
            watcher->lockChanged(locked);
        }
        tellWatchers([locked](LaneObserver& each) { each.lockChanged(locked); });
    }

    void peekChanged(bool peeking) override
    {
        if (!target() || host.m_peeking == peeking) {
            return;
        }
        host.m_peeking = peeking;
        if (host.m_callbacks.peek) {
            host.m_callbacks.peek(peeking);
        }
    }

    void wheelChanged(std::string_view control, bool open, int cell) override
    {
        if (target() && host.m_callbacks.wheel) {
            host.m_callbacks.wheel(std::string(control), open, cell);
        }
    }

    void notice(input::EngineNotice notice) override
    {
        if (target() && !host.m_relocking && host.m_callbacks.notice) {
            host.m_callbacks.notice(notice);
        }
    }

    void liveChanged(const std::vector<bool>& controls) override
    {
        if (host.m_callbacks.live) {
            host.m_callbacks.live(channel.get(), controls, reinterpret_cast<std::uintptr_t>(profile.get()));
        }
    }

    void aimingChanged(const std::vector<bool>& controls) override
    {
        if (host.m_callbacks.aiming) {
            host.m_callbacks.aiming(channel.get(), controls, reinterpret_cast<std::uintptr_t>(profile.get()));
        }
    }

    void controlPressed(std::string_view id, bool down) override
    {
        if (connected() && !restoring) {
            observer->controlPressed(id, down);
        }
        tellWatchers([id, down](LaneObserver& each) { each.controlPressed(id, down); });
    }

    void stateChanged(std::string_view id, bool value) override
    {
        if (connected() && !restoring) {
            observer->stateChanged(id, value);
        }
        tellWatchers([id, value](LaneObserver& each) { each.stateChanged(id, value); });
    }

    void controlEnabledChanged(std::string_view id, bool enabled) override
    {
        if (connected() && !restoring) {
            observer->controlEnabledChanged(id, enabled);
        }
        tellWatchers([id, enabled](LaneObserver& each) { each.controlEnabledChanged(id, enabled); });
    }

    void statusChanged(std::string_view id, const input::ControlStatus& status) override
    {
        if (connected() && !restoring) {
            observer->statusChanged(id, status);
        }
        tellWatchers([id, &status](LaneObserver& each) { each.statusChanged(id, status); });
    }

    template <typename Call>
    void tellWatchers(const Call& call) const
    {
        if (restoring || watchers.empty()) {
            return;
        }
        std::vector<std::shared_ptr<LaneObserver>> listeners;
        listeners.reserve(watchers.size());
        for (const Watcher& watcher : watchers) {
            listeners.push_back(watcher.observer);
        }
        for (const std::shared_ptr<LaneObserver>& listener : listeners) {
            call(*listener);
        }
    }

    InputHost& host;
    std::shared_ptr<ControlChannel> channel;
    std::shared_ptr<const input::Profile> profile;
    std::shared_ptr<const input::Profile> observerProfile;
    std::shared_ptr<LaneObserver> observer;
    std::vector<Watcher> watchers;
    std::optional<input::MappingEngine> engine;
    std::uint64_t revision = 0;
    std::uint64_t pacing = 0;
    bool restoring = false;
    bool synced = false;
    Memory memory;
    std::vector<input::Control> tunings;
    std::vector<ScriptTouch> touches;
    std::vector<ScriptLift> lifts;
    std::vector<input::InputSource> scriptKeys;
    std::vector<input::InputSource> scriptHeld;
    std::vector<std::string> handedOver;
    Microseconds retryAt{0};
    std::uint8_t usedPointers = 0;
    std::uint8_t nextPointer = 0;
    Lane* leader = nullptr;
};

InputHost::InputHost(Callbacks callbacks)
    : m_callbacks(std::move(callbacks))
    , m_lockKey(defaultLockKey())
    , m_passthrough({.rumble = m_callbacks.rumble, .notice = m_callbacks.padNotice})
    , m_seed(freshSeed())
{
}

InputHost::~InputHost() = default;

InputHost::Lane* InputHost::find(const ControlChannel* channel) const noexcept
{
    if (channel == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(
        m_lanes, [&](const std::unique_ptr<Lane>& lane) { return lane->channel.get() == channel; });
    return found != m_lanes.end() ? found->get() : nullptr;
}

InputHost::Lane& InputHost::laneFor(const std::shared_ptr<ControlChannel>& channel)
{
    if (Lane* lane = find(channel.get())) {
        return *lane;
    }
    m_lanes.push_back(std::make_unique<Lane>(*this, channel));
    m_lanes.back()->profile = m_profile;
    return *m_lanes.back();
}

void InputHost::setChannel(std::shared_ptr<ControlChannel> channel, Microseconds now)
{
    if (m_target != nullptr && m_target->channel == channel) {
        return;
    }
    m_down.clear();
    m_pushed.clear();
    if (Lane* previous = std::exchange(m_target, nullptr)) {
        leave(*previous, now);
        for (Lane* follower : followersOf(*previous)) {
            leave(*follower, now);
        }
    }
    unlock();
    if (channel != nullptr) {
        m_target = &laneFor(channel);
    }
    followPad(now);
}

void InputHost::setProfile(std::shared_ptr<const input::Profile> profile, Microseconds now, bool revision)
{
    if (!revision) {
        m_down.clear();
        m_pushed.clear();
    }
    m_profile = profile;
    if (m_target != nullptr) {
        changeProfile(*m_target, std::move(profile), now);
        connect(*m_target, now);
    }
    refreshHeld(now);
    followPad(now);
}

void InputHost::setLockKey(std::optional<input::Chord> key, Microseconds now)
{
    m_lockKey = key;
    for (const std::unique_ptr<Lane>& lane : m_lanes) {
        dropEngine(*lane, now);
        lane->revision = 0;
    }
    refreshHeld(now);
}

void InputHost::setModules(std::shared_ptr<input::ModuleFactory> modules, Microseconds now)
{
    if (modules == m_modules) {
        return;
    }
    for (const std::unique_ptr<Lane>& lane : m_lanes) {
        dropEngine(*lane, now);
        lane->revision = 0;
    }
    m_modules = std::move(modules);
    refreshHeld(now);
}

void InputHost::refreshHeld(Microseconds now)
{
    if (!m_active || m_target == nullptr || (m_down.empty() && m_pushed.empty())) {
        return;
    }
    static_cast<void>(engineOf(*m_target, now));
    for (Lane* follower : followersOf(*m_target)) {
        static_cast<void>(engineOf(*follower, now));
    }
}

void InputHost::setActive(bool on, Microseconds now)
{
    if (on == m_active) {
        return;
    }
    m_active = on;
    if (!on) {
        m_heldMouse = 0;
        m_down.clear();
        m_pushed.clear();
        if (m_target != nullptr) {
            release(*m_target, now);
            for (Lane* follower : followersOf(*m_target)) {
                release(*follower, now);
            }
        }
    } else {
        m_heldMouse = m_callbacks.pressedMouse ? m_callbacks.pressedMouse() : platform::pressedMouseButtons();
    }
    m_passthrough.setActive(on, now);
}

void InputHost::setCameraLock(input::Switch action, Microseconds now)
{
    input::MappingEngine* engine = m_target != nullptr ? engineOf(*m_target, now) : nullptr;
    if (engine != nullptr) {
        engine->setCameraLock(action, now);
    } else if (action != input::Switch::On) {
        unlock();
    }
    if (m_target == nullptr) {
        return;
    }
    const input::Switch follow = switchOf(m_locked);
    for (Lane* follower : followersOf(*m_target)) {
        if (input::MappingEngine* followed = engineOf(*follower, now)) {
            followed->setCameraLock(follow, now);
        }
    }
}

void InputHost::cursor(input::CursorPhase phase, input::Vec2 position, Microseconds now)
{
    if (m_target == nullptr) {
        return;
    }
    if (input::MappingEngine* engine = engineOf(*m_target, now)) {
        engine->cursor(phase, position, now);
    }
    for (Lane* follower : followersOf(*m_target)) {
        if (input::MappingEngine* engine = engineOf(*follower, now)) {
            engine->cursor(phase, position, now);
        }
    }
}

void InputHost::attach(const std::shared_ptr<ControlChannel>& channel, std::shared_ptr<const input::Profile> profile,
                       std::shared_ptr<LaneObserver> observer, Microseconds now)
{
    if (channel == nullptr || observer == nullptr) {
        return;
    }
    Lane& lane = laneFor(channel);
    if (!lane.target()) {
        changeProfile(lane, profile, now);
    }
    lane.observer = std::move(observer);
    lane.observerProfile = std::move(profile);
    lane.synced = false;
    connect(lane, now);
}

void InputHost::detach(const ControlChannel* channel, const LaneObserver* observer, Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr || lane->observer.get() != observer) {
        return;
    }
    lane->observer.reset();
    lane->observerProfile.reset();
    lane->synced = false;
    endTouches(*lane, now);
    if (!lane->target()) {
        release(*lane, now);
    }
}

void InputHost::watch(const std::shared_ptr<ControlChannel>& channel, std::shared_ptr<const input::Profile> profile,
                      std::shared_ptr<LaneObserver> observer, Microseconds now)
{
    if (channel == nullptr || observer == nullptr) {
        return;
    }
    Lane& lane = laneFor(channel);
    if (!lane.target() && lane.observer == nullptr && lane.watchers.empty() && profile != nullptr) {
        changeProfile(lane, profile, now);
    }
    if (std::ranges::any_of(lane.watchers, [&](const Watcher& each) { return each.observer == observer; })) {
        return;
    }
    lane.watchers.push_back(Watcher{.observer = observer, .keys = {}, .held = {}});
    if (lane.profile != profile) {
        observer->profileChanged(lane.profile);
    }
    observer->synced(valuesOf(lane, now));
}

void InputHost::unwatch(const ControlChannel* channel, const LaneObserver* observer, Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr) {
        return;
    }
    const auto found = std::ranges::find_if(lane->watchers, [&](const Watcher& each) { return each.observer.get() == observer; });
    if (found == lane->watchers.end()) {
        return;
    }
    const Watcher gone = std::move(*found);
    lane->watchers.erase(found);
    for (const input::InputSource source : gone.held) {
        gone.observer->keyEvent(input::sourceName(source), false);
    }
    if (lane->observer == nullptr && lane->watchers.empty() && !lane->target()) {
        release(*lane, now);
    }
}

void InputHost::watchKeys(const ControlChannel* channel, const LaneObserver* observer, std::vector<input::InputSource> keys)
{
    Lane* lane = find(channel);
    if (lane == nullptr) {
        return;
    }
    const auto found = std::ranges::find_if(lane->watchers, [&](const Watcher& each) { return each.observer.get() == observer; });
    if (found == lane->watchers.end()) {
        return;
    }
    std::ranges::sort(keys);
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    found->keys = std::move(keys);
    std::vector<input::InputSource> released;
    std::erase_if(found->held, [&](input::InputSource source) {
        if (std::ranges::binary_search(found->keys, source)) {
            return false;
        }
        released.push_back(source);
        return true;
    });
    const std::shared_ptr<LaneObserver> listener = found->observer;
    for (const input::InputSource source : released) {
        listener->keyEvent(input::sourceName(source), false);
    }
}

bool InputHost::usable(const Lane& lane) noexcept
{
    return lane.connected() || !lane.watchers.empty();
}

void InputHost::forget(const ControlChannel* channel, Microseconds now)
{
    m_passthrough.forget(channel);
    Lane* lane = find(channel);
    if (lane == nullptr) {
        return;
    }
    if (lane->target()) {
        m_target = nullptr;
        leave(*lane, now);
        for (Lane* follower : followersOf(*lane)) {
            leave(*follower, now);
        }
        unlock();
    }
    erase(*lane, now);
    followPad(now);
}

bool InputHost::pressControl(const ControlChannel* channel, std::string_view id, bool down, Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr || !usable(*lane)) {
        return false;
    }
    input::MappingEngine* engine = engineOf(*lane, now);
    return engine != nullptr && engine->pressControl(id, down, now);
}

bool InputHost::enableControl(const ControlChannel* channel, std::string_view id, bool on, Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr || !usable(*lane)) {
        return false;
    }
    if (input::MappingEngine* engine = engineOf(*lane, now)) {
        return engine->setControlEnabled(id, switchOf(on), now);
    }
    return remember(*lane, id, on, false);
}

bool InputHost::retuneControl(const ControlChannel* channel, const input::Profile* basis, const input::Control& control,
                              Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr || lane->profile == nullptr || lane->profile.get() != basis) {
        return false;
    }
    const std::vector<input::Control>& controls = lane->profile->controls;
    const auto original = std::ranges::find(controls, control.id, &input::Control::id);
    if (original == controls.end() || original->kind.index() != control.kind.index()) {
        return false;
    }
    std::erase_if(lane->tunings, [&control](const input::Control& each) { return each.id == control.id; });
    if (*original != control) {
        lane->tunings.push_back(control);
    }
    if (input::MappingEngine* engine = engineOf(*lane, now)) {
        return engine->retuneControl(control, now);
    }
    return true;
}

bool InputHost::setState(const ControlChannel* channel, std::string_view id, bool value, Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr || !usable(*lane)) {
        return false;
    }
    if (input::MappingEngine* engine = engineOf(*lane, now)) {
        return engine->setState(id, switchOf(value), now);
    }
    return remember(*lane, id, value, true);
}

bool InputHost::withEngine(const ControlChannel* channel, const std::function<bool(input::MappingEngine&)>& act,
                           Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr || !usable(*lane)) {
        return false;
    }
    input::MappingEngine* engine = engineOf(*lane, now);
    return engine != nullptr && act(*engine);
}

bool InputHost::feed(const ControlChannel* channel, const input::DeviceEvent& event, Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr || (!lane->target() && !usable(*lane))) {
        return false;
    }
    input::MappingEngine* engine = engineOf(*lane, now);
    if (engine == nullptr) {
        return false;
    }
    input::deliver(event, *engine, now);
    tellScript(*lane, event);
    for (Lane* follower : followersOf(*lane)) {
        if (input::MappingEngine* following = engineOf(*follower, now)) {
            input::deliver(event, *following, now);
            tellScript(*follower, event);
        }
    }
    return true;
}

void InputHost::claimKeys(const ControlChannel* channel, std::vector<input::InputSource> keys)
{
    Lane* lane = find(channel);
    if (lane == nullptr) {
        return;
    }
    std::ranges::sort(keys);
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    lane->scriptKeys = std::move(keys);
    std::vector<input::InputSource> released;
    std::erase_if(lane->scriptHeld, [&](input::InputSource source) {
        if (std::ranges::binary_search(lane->scriptKeys, source)) {
            return false;
        }
        released.push_back(source);
        return true;
    });
    if (lane->connected()) {
        const std::shared_ptr<LaneObserver> observer = lane->observer;
        for (const input::InputSource source : released) {
            observer->keyEvent(input::sourceName(source), false);
        }
    }
}

void InputHost::handOver(const ControlChannel* channel, std::vector<std::string> ids)
{
    Lane* lane = find(channel);
    if (lane == nullptr) {
        return;
    }
    std::ranges::sort(ids);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    lane->handedOver = std::move(ids);
    if (lane->engine) {
        lane->engine->setHandedOver(lane->handedOver);
    }
}

void InputHost::typeText(const ControlChannel* channel, std::string_view text)
{
    Lane* lane = find(channel);
    if (lane == nullptr || !usable(*lane)) {
        return;
    }
    static_cast<void>(lane->channel->type(text));
}

void InputHost::tellScript(Lane& lane, const input::DeviceEvent& event)
{
    if (event.action != input::DeviceAction::Down && event.action != input::DeviceAction::Up) {
        return;
    }
    const bool down = event.action == input::DeviceAction::Down;
    if (lane.connected() && std::ranges::binary_search(lane.scriptKeys, event.source) &&
        noteKey(lane.scriptHeld, event.source, down)) {
        const std::shared_ptr<LaneObserver> observer = lane.observer;
        observer->keyEvent(input::sourceName(event.source), down);
    }
    std::vector<std::shared_ptr<LaneObserver>> told;
    for (Watcher& watcher : lane.watchers) {
        if (std::ranges::binary_search(watcher.keys, event.source) && noteKey(watcher.held, event.source, down)) {
            told.push_back(watcher.observer);
        }
    }
    for (const std::shared_ptr<LaneObserver>& listener : told) {
        listener->keyEvent(input::sourceName(event.source), down);
    }
}

bool InputHost::noteKey(std::vector<input::InputSource>& held, input::InputSource source, bool down) const
{
    const auto found = std::ranges::find(held, source);
    if (down == (found != held.end())) {
        return false;
    }
    if (down) {
        if (source.device == input::Device::Mouse && (!m_locked || m_peeking)) {
            return false;
        }
        held.push_back(source);
    } else {
        held.erase(found);
    }
    return true;
}

void InputHost::releaseScriptKeys(Lane& lane)
{
    const std::vector<input::InputSource> held = std::exchange(lane.scriptHeld, {});
    if (!held.empty() && lane.connected()) {
        const std::shared_ptr<LaneObserver> observer = lane.observer;
        for (const input::InputSource source : held) {
            observer->keyEvent(input::sourceName(source), false);
        }
    }
    std::vector<std::pair<std::shared_ptr<LaneObserver>, input::InputSource>> released;
    for (Watcher& watcher : lane.watchers) {
        for (const input::InputSource source : std::exchange(watcher.held, {})) {
            released.emplace_back(watcher.observer, source);
        }
    }
    for (const auto& [listener, source] : released) {
        listener->keyEvent(input::sourceName(source), false);
    }
}

void InputHost::tap(const ControlChannel* channel, std::uint64_t gesture, input::Vec2 at, Microseconds duration,
                    Microseconds now)
{
    swipe(channel, gesture, at, at, duration, now);
}

void InputHost::swipe(const ControlChannel* channel, std::uint64_t gesture, input::Vec2 from, input::Vec2 to,
                      Microseconds duration, Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr || !usable(*lane)) {
        return;
    }
    const std::optional<InputScreen> screen = lane->channel->screen();
    if (!screen) {
        return;
    }
    const std::optional<std::uint8_t> pointer = scriptPointer(*lane, now);
    if (!pointer) {
        return;
    }
    const bool moves = from != to;
    ScriptTouch touch;
    touch.gesture = gesture;
    touch.pointer = *pointer;
    touch.videoSession = screen->videoSession;
    touch.from = from;
    touch.to = to;
    touch.start = now;
    touch.end = now + std::max(duration, Microseconds{0});
    touch.step = moves ? Microseconds{static_cast<Microseconds::rep>(1'000'000.0 / std::max(screen->refreshHz, 1.0))}
                       : Microseconds{0};
    touch.next = moves ? std::min(now + touch.step, touch.end) : touch.end;
    static_cast<void>(sendTouch(*lane, protocol::TouchAction::Down, touch.pointer, touch.videoSession, from));
    lane->touches.push_back(touch);
}

void InputHost::holdGesture(const ControlChannel* channel, std::uint64_t gesture, input::Vec2 at, Microseconds now)
{
    swipe(channel, gesture, at, at, Microseconds{0}, now);
    if (Lane* lane = find(channel); lane != nullptr && !lane->touches.empty() &&
                                    lane->touches.back().gesture == gesture) {
        lane->touches.back().held = true;
        lane->touches.back().next = Microseconds::max();
    }
}

void InputHost::moveGesture(const ControlChannel* channel, std::uint64_t gesture, input::Vec2 to, Microseconds duration,
                            Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr) {
        return;
    }
    const auto found = std::ranges::find(lane->touches, gesture, &ScriptTouch::gesture);
    const std::optional<InputScreen> screen = lane->channel->screen();
    if (found == lane->touches.end() || !screen) {
        return;
    }
    found->from = along(found->from, found->to, found->start, found->end, now);
    found->to = to;
    found->start = now;
    found->end = now + std::max(duration, Microseconds{0});
    found->step = Microseconds{static_cast<Microseconds::rep>(1'000'000.0 / std::max(screen->refreshHz, 1.0))};
    if (found->end <= now) {
        static_cast<void>(sendTouch(*lane, protocol::TouchAction::Move, found->pointer, found->videoSession, to));
        found->from = to;
        found->next = found->held ? Microseconds::max() : now;
        return;
    }
    found->next = std::min(now + found->step, found->end);
}

void InputHost::liftAll(const ControlChannel* channel, Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr) {
        return;
    }
    const std::vector<ScriptTouch> touches = std::exchange(lane->touches, {});
    for (const ScriptTouch& touch : touches) {
        lift(*lane,
             {.action = protocol::TouchAction::Up,
              .pointer = touch.pointer,
              .videoSession = touch.videoSession,
              .position = along(touch.from, touch.to, touch.start, touch.end, now)},
             now);
    }
}

void InputHost::androidKey(const ControlChannel* channel, std::uint32_t keycode, bool down)
{
    Lane* lane = find(channel);
    if (lane == nullptr || (down && !usable(*lane))) {
        return;
    }
    sendKey(*lane->channel, keycode, down);
}

std::size_t InputHost::setFollowers(const std::shared_ptr<ControlChannel>& master,
                                    const std::vector<Follower>& followers, Microseconds now)
{
    if (master == nullptr) {
        return 0;
    }
    Lane* leader = find(master.get());
    if (leader == nullptr && followers.empty()) {
        return 0;
    }
    if (leader == nullptr) {
        leader = &laneFor(master);
    }
    const bool leading = !followers.empty() && leader->leader == nullptr;
    for (Lane* follower : followersOf(*leader)) {
        const bool kept = leading && std::ranges::any_of(followers, [follower](const Follower& each) {
            return each.channel == follower->channel;
        });
        if (!kept) {
            unfollow(*follower, now);
        }
    }
    if (!leading) {
        return 0;
    }
    std::size_t accepted = 0;
    for (auto each = followers.begin(); each != followers.end(); ++each) {
        const std::shared_ptr<ControlChannel>& channel = each->channel;
        const bool repeated = std::any_of(followers.begin(), each,
                                          [&channel](const Follower& earlier) { return earlier.channel == channel; });
        if (channel == nullptr || channel == master || repeated) {
            continue;
        }
        Lane* lane = find(channel.get());
        if (lane != nullptr && ((lane->leader != nullptr && lane->leader != leader) || leads(*lane))) {
            continue;
        }
        if (lane == nullptr) {
            lane = &laneFor(channel);
        }
        if (!lane->target()) {
            changeProfile(*lane, each->profile, now);
        }
        lane->leader = leader;
        ++accepted;
    }
    return accepted;
}

std::size_t InputHost::followerCount(const ControlChannel* master) const
{
    const Lane* leader = find(master);
    return leader != nullptr ? followersOf(*leader).size() : 0;
}

const ControlChannel* InputHost::leaderOf(const ControlChannel* follower) const
{
    const Lane* lane = find(follower);
    return lane != nullptr && lane->leader != nullptr ? lane->leader->channel.get() : nullptr;
}

std::vector<InputHost::Lane*> InputHost::followersOf(const Lane& leader) const
{
    std::vector<Lane*> found;
    for (const std::unique_ptr<Lane>& lane : m_lanes) {
        if (lane->leader == &leader) {
            found.push_back(lane.get());
        }
    }
    return found;
}

bool InputHost::leads(const Lane& lane) const
{
    return std::ranges::any_of(m_lanes,
                               [&lane](const std::unique_ptr<Lane>& each) { return each->leader == &lane; });
}

void InputHost::unfollow(Lane& lane, Microseconds now)
{
    const Lane* leader = std::exchange(lane.leader, nullptr);
    if (leader != nullptr && leader->target() && !lane.target()) {
        leave(lane, now);
    }
}

void InputHost::padLinkChanged(const ControlChannel* channel, Microseconds now)
{
    m_passthrough.linkChanged(channel, now);
}

void InputHost::padOutput(const ControlChannel* channel, std::uint16_t id, std::span<const std::uint8_t> data)
{
    m_passthrough.output(channel, id, data);
}

void InputHost::padStatus(const ControlChannel* channel, std::uint16_t id, bool recognized, bool vibration,
                          Microseconds now)
{
    m_passthrough.status(channel, id, recognized, vibration, now);
}

void InputHost::followPad(Microseconds now)
{
    const bool passthrough = m_target != nullptr && m_target->profile != nullptr &&
                             m_target->profile->gamepad == input::GamepadMode::Passthrough;
    const bool took = m_passthrough.takesPad();
    m_passthrough.setTarget(m_target != nullptr ? m_target->channel : nullptr, passthrough, now);
    if (!took && m_passthrough.takesPad()) {
        deliverAll(m_gamepad.update({}), now);
    }
}

std::uint8_t InputHost::activeTouches() const noexcept
{
    return m_target != nullptr && m_target->engine ? m_target->engine->activeTouches() : std::uint8_t{0};
}

template <typename Events>
void InputHost::deliverAll(const Events& events, Microseconds now)
{
    if (!m_active || events.empty() || m_target == nullptr) {
        return;
    }
    for (const input::DeviceEvent& event : events) {
        noteHeld(event);
    }
    Lane& target = *m_target;
    if (input::MappingEngine* engine = engineOf(target, now)) {
        for (const input::DeviceEvent& event : events) {
            input::deliver(event, *engine, now);
            tellScript(target, event);
        }
    }
    for (Lane* follower : followersOf(target)) {
        if (input::MappingEngine* engine = engineOf(*follower, now)) {
            for (const input::DeviceEvent& event : events) {
                input::deliver(event, *engine, now);
                tellScript(*follower, event);
            }
        }
    }
}

void InputHost::key(const platform::RawKey& raw, Microseconds now)
{
    if (const std::optional<input::DeviceEvent> event =
            m_keyboard.decode(raw.makeCode, raw.e0, raw.e1, raw.released)) {
        deliverAll(std::array{*event}, now);
    }
}

void InputHost::mouse(const platform::RawMouse& report, Microseconds now)
{
    const input::MouseDecoder::Events events = m_mouse.decode(
        {.dx = report.dx, .dy = report.dy, .buttonFlags = report.buttonFlags, .buttonData = report.buttonData});
    if (m_heldMouse == 0) {
        deliverAll(events, now);
        return;
    }
    input::MouseDecoder::Events passed;
    for (const input::DeviceEvent& event : events) {
        if (!heldSinceActivation(event)) {
            passed.push_back(event);
        }
    }
    deliverAll(passed, now);
}

bool InputHost::heldSinceActivation(const input::DeviceEvent& event)
{
    const std::uint8_t bit = buttonBit(event.source);
    if ((m_heldMouse & bit) == 0) {
        return false;
    }
    if (event.action == input::DeviceAction::Up) {
        m_heldMouse = static_cast<std::uint8_t>(m_heldMouse & ~bit);
    }
    return true;
}

void InputHost::noteHeld(const input::DeviceEvent& event)
{
    switch (event.action) {
    case input::DeviceAction::Down:
        if (std::ranges::find(m_down, event.source) == m_down.end()) {
            m_down.push_back(event.source);
        }
        break;
    case input::DeviceAction::Up:
        std::erase(m_down, event.source);
        break;
    case input::DeviceAction::Axis: {
        const auto found = std::ranges::find_if(
            m_pushed, [&event](const std::pair<input::InputSource, input::Vec2>& pushed) {
                return pushed.first == event.source;
            });
        if (event.value.x == 0.0 && event.value.y == 0.0) {
            if (found != m_pushed.end()) {
                m_pushed.erase(found);
            }
        } else if (found != m_pushed.end()) {
            found->second = event.value;
        } else {
            m_pushed.emplace_back(event.source, event.value);
        }
        break;
    }
    case input::DeviceAction::Motion:
        break;
    }
}

void InputHost::resumeHeld(input::MappingEngine& engine, Microseconds now) const
{
    for (const bool modifiers : {true, false}) {
        for (const input::InputSource source : m_down) {
            if (input::isModifierKey(source) == modifiers) {
                engine.resumeHeld(source, now);
            }
        }
    }
    for (const auto& [source, value] : m_pushed) {
        engine.axis(source, value, now);
    }
}

void InputHost::gamepad(const platform::PadState& state, Microseconds now)
{
    m_passthrough.gamepad(state, now);
    if (m_passthrough.takesPad()) {
        return;
    }
    deliverAll(m_gamepad.update({.connected = state.connected,
                                 .buttons = state.buttons,
                                 .leftTrigger = state.leftTrigger,
                                 .rightTrigger = state.rightTrigger,
                                 .leftX = state.leftX,
                                 .leftY = state.leftY,
                                 .rightX = state.rightX,
                                 .rightY = state.rightY}),
               now);
}

void InputHost::wake(Microseconds now)
{
    for (const std::unique_ptr<Lane>& lane : m_lanes) {
        if (lane->engine) {
            lane->engine->tick(now);
        }
        advanceTouches(*lane, now);
    }
    m_passthrough.wake(now);
}

std::optional<Microseconds> InputHost::nextWakeup() const
{
    std::optional<Microseconds> earliest;
    const auto consider = [&](std::optional<Microseconds> due) {
        if (due && (!earliest || *due < *earliest)) {
            earliest = due;
        }
    };
    for (const std::unique_ptr<Lane>& lane : m_lanes) {
        if (lane->engine) {
            consider(lane->engine->nextWakeup());
        }
        for (const ScriptTouch& touch : lane->touches) {
            if (touch.next != Microseconds::max()) {
                consider(touch.next);
            }
        }
        if (!lane->lifts.empty()) {
            consider(lane->retryAt);
        }
    }
    consider(m_passthrough.nextWakeup());
    return earliest;
}

input::MappingEngine* InputHost::engineOf(Lane& lane, Microseconds now)
{
    const std::uint64_t revision = lane.channel->revision();
    if (lane.engine && revision == lane.revision) {
        if (const std::uint64_t pacing = lane.channel->pacing(); pacing != lane.pacing) {
            lane.pacing = pacing;
            lane.engine->setRefreshHz(lane.channel->refreshHz());
        }
        return &*lane.engine;
    }
    dropEngine(lane, now);
    lane.revision = revision;
    lane.pacing = lane.channel->pacing();
    const std::optional<InputScreen> screen = lane.channel->screen();
    if (!screen) {
        return nullptr;
    }
    input::EngineConfig config;
    config.screen = screen->geometry;
    config.refreshHz = screen->refreshHz;
    config.maxPointers = kEnginePointers;
    config.defaultLockKey = m_lockKey;
    config.seed = m_seed;
    config.modules = m_modules;
    lane.engine.emplace(lane.profile != nullptr ? *lane.profile : noProfile(), config,
                        static_cast<input::TouchFrameSink&>(lane), static_cast<input::EngineListener&>(lane));
    lane.engine->setVideoSession(screen->videoSession);
    if (m_callbacks.cursor) {
        lane.engine->setCursorLocator(m_callbacks.cursor);
    }
    if (lane.memory.valid) {
        lane.restoring = true;
        for (const auto& [id, value] : lane.memory.values.states) {
            lane.engine->setState(id, switchOf(value), now);
        }
        for (const auto& [id, on] : lane.memory.values.enabled) {
            lane.engine->setControlEnabled(id, switchOf(on), now);
        }
        lane.restoring = false;
    }
    lane.memory = {};
    for (const input::Control& control : lane.tunings) {
        lane.engine->retuneControl(control, now);
    }
    lane.engine->setHandedOver(lane.handedOver);
    if (lane.target() && m_locked) {
        m_relocking = true;
        lane.engine->setCameraLock(input::Switch::On, now);
        m_relocking = false;
        if (!lane.engine->cameraLocked()) {
            unlock();
        }
    }
    if (m_active && m_target != nullptr && (&lane == m_target || lane.leader == m_target)) {
        resumeHeld(*lane.engine, now);
    }
    return &*lane.engine;
}

void InputHost::release(Lane& lane, Microseconds now)
{
    releaseScriptKeys(lane);
    if (lane.engine) {
        lane.engine->resetAll(now);
        lane.engine->flush(now);
    }
}

void InputHost::dropEngine(Lane& lane, Microseconds now)
{
    if (!lane.engine) {
        return;
    }
    release(lane, now);
    const input::Profile& profile = lane.profile != nullptr ? *lane.profile : noProfile();
    Memory memory{.valid = true};
    for (const input::Variable& state : profile.vars) {
        if (const std::optional<bool> value = lane.engine->state(state.id)) {
            memory.values.states.emplace_back(state.id, *value);
        }
    }
    for (const input::Control& control : profile.controls) {
        if (const std::optional<bool> on = lane.engine->controlEnabled(control.id)) {
            memory.values.enabled.emplace_back(control.id, *on);
        }
    }
    lane.memory = std::move(memory);
    lane.engine.reset();
}

void InputHost::leave(Lane& lane, Microseconds now)
{
    if (lane.engine && lane.engine->cameraLocked()) {
        lane.engine->setCameraLock(input::Switch::Off, now);
    }
    release(lane, now);
}

void InputHost::erase(Lane& lane, Microseconds now)
{
    endTouches(lane, now);
    release(lane, now);
    lane.engine.reset();
    const Lane* gone = &lane;
    for (const std::unique_ptr<Lane>& each : m_lanes) {
        if (each->leader == gone) {
            each->leader = nullptr;
        }
    }
    std::erase_if(m_lanes, [gone](const std::unique_ptr<Lane>& each) { return each.get() == gone; });
}

void InputHost::changeProfile(Lane& lane, std::shared_ptr<const input::Profile> profile, Microseconds now)
{
    if (lane.profile == profile) {
        return;
    }
    dropEngine(lane, now);
    lane.memory = {};
    lane.tunings.clear();
    lane.profile = std::move(profile);
    lane.revision = 0;
    lane.synced = false;
    if (!lane.watchers.empty()) {
        std::vector<std::shared_ptr<LaneObserver>> listeners;
        for (const Watcher& watcher : lane.watchers) {
            listeners.push_back(watcher.observer);
        }
        const LaneValues values = valuesOf(lane, now);
        for (const std::shared_ptr<LaneObserver>& listener : listeners) {
            listener->profileChanged(lane.profile);
            listener->synced(values);
        }
    }
}

void InputHost::connect(Lane& lane, Microseconds now)
{
    if (!lane.connected() || lane.synced) {
        return;
    }
    lane.synced = true;
    const std::shared_ptr<LaneObserver> observer = lane.observer;
    observer->synced(valuesOf(lane, now));
}

LaneValues InputHost::valuesOf(Lane& lane, Microseconds now)
{
    const input::Profile& profile = lane.profile != nullptr ? *lane.profile : noProfile();
    const input::MappingEngine* engine = engineOf(lane, now);
    LaneValues values;
    for (const input::Variable& state : profile.vars) {
        const std::optional<bool> value =
            engine != nullptr ? engine->state(state.id) : lookup(lane.memory.values.states, state.id);
        values.states.emplace_back(state.id, value.value_or(input::truthy(state.initial)));
    }
    for (const input::Control& control : profile.controls) {
        const std::optional<bool> on =
            engine != nullptr ? engine->controlEnabled(control.id) : lookup(lane.memory.values.enabled, control.id);
        values.enabled.emplace_back(control.id, on.value_or(control.enabled));
    }
    return values;
}

bool InputHost::remember(Lane& lane, std::string_view id, bool value, bool state)
{
    const input::Profile& profile = lane.profile != nullptr ? *lane.profile : noProfile();
    std::optional<bool> initial;
    if (state) {
        const auto found = std::ranges::find_if(profile.vars, [&](const input::Variable& s) { return s.id == id; });
        if (found != profile.vars.end()) {
            initial = input::truthy(found->initial);
        }
    } else {
        const auto found =
            std::ranges::find_if(profile.controls, [&](const input::Control& c) { return c.id == id; });
        if (found != profile.controls.end()) {
            initial = found->enabled;
        }
    }
    if (!initial) {
        return false;
    }
    Values& values = state ? lane.memory.values.states : lane.memory.values.enabled;
    const bool before = lookup(values, id).value_or(*initial);
    lane.memory.valid = true;
    upsert(values, id, value);
    if (before != value && lane.connected()) {
        const std::shared_ptr<LaneObserver> observer = lane.observer;
        if (state) {
            observer->stateChanged(id, value);
        } else {
            observer->controlEnabledChanged(id, value);
        }
    }
    return true;
}

bool InputHost::sendTouch(Lane& lane, protocol::TouchAction action, std::uint8_t pointer,
                          std::uint16_t videoSession, input::Vec2 position)
{
    const bool lifting = action == protocol::TouchAction::Up || action == protocol::TouchAction::Cancel;
    protocol::TouchFrame frame;
    frame.videoSession = videoSession;
    frame.points.push_back(protocol::TouchPoint{.action = action,
                                                .pointerId = pointer,
                                                .x = input::fixedCoordinate(position.x),
                                                .y = input::fixedCoordinate(position.y),
                                                .pressure = lifting ? std::uint16_t{0} : std::uint16_t{0xFFFF}});
    return lane.channel->send(frame);
}

namespace {

constexpr Microseconds kLiftRetry{4000};

std::uint8_t pointerBit(std::uint8_t pointer)
{
    return static_cast<std::uint8_t>(1U << (pointer - InputHost::kEnginePointers));
}

}

void InputHost::lift(Lane& lane, const ScriptLift& pending, Microseconds now)
{
    if (lane.lifts.empty() && sendTouch(lane, pending.action, pending.pointer, pending.videoSession, pending.position)) {
        lane.usedPointers = static_cast<std::uint8_t>(lane.usedPointers & ~pointerBit(pending.pointer));
        return;
    }
    if (lane.lifts.empty()) {
        lane.retryAt = now + kLiftRetry;
    }
    lane.lifts.push_back(pending);
}

void InputHost::retryLifts(Lane& lane, Microseconds now)
{
    while (!lane.lifts.empty()) {
        const ScriptLift& next = lane.lifts.front();
        if (!sendTouch(lane, next.action, next.pointer, next.videoSession, next.position)) {
            lane.retryAt = now + kLiftRetry;
            return;
        }
        lane.usedPointers = static_cast<std::uint8_t>(lane.usedPointers & ~pointerBit(next.pointer));
        lane.lifts.erase(lane.lifts.begin());
    }
}

std::optional<std::uint8_t> InputHost::scriptPointer(Lane& lane, Microseconds now)
{
    if (lane.touches.size() >= kScriptPointers) {
        const ScriptTouch oldest = lane.touches.front();
        lane.touches.erase(lane.touches.begin());
        lift(lane,
             {.action = protocol::TouchAction::Up,
              .pointer = oldest.pointer,
              .videoSession = oldest.videoSession,
              .position = along(oldest.from, oldest.to, oldest.start, oldest.end, now)},
             now);
    }
    for (std::uint8_t i = 0; i < kScriptPointers; ++i) {
        const auto slot = static_cast<std::uint8_t>((lane.nextPointer + i) % kScriptPointers);
        if ((lane.usedPointers & (1U << slot)) == 0) {
            lane.usedPointers = static_cast<std::uint8_t>(lane.usedPointers | (1U << slot));
            lane.nextPointer = static_cast<std::uint8_t>((slot + 1) % kScriptPointers);
            return static_cast<std::uint8_t>(kEnginePointers + slot);
        }
    }
    return std::nullopt;
}

void InputHost::advanceTouches(Lane& lane, Microseconds now)
{
    if (!lane.lifts.empty() && now >= lane.retryAt) {
        retryLifts(lane, now);
    }
    for (auto it = lane.touches.begin(); it != lane.touches.end();) {
        if (it->next > now) {
            ++it;
            continue;
        }
        if (now >= it->end && it->held) {
            if (it->from != it->to) {
                static_cast<void>(sendTouch(lane, protocol::TouchAction::Move, it->pointer, it->videoSession, it->to));
                it->from = it->to;
            }
            it->next = Microseconds::max();
            ++it;
            continue;
        }
        if (now >= it->end) {
            const ScriptLift ending{.action = protocol::TouchAction::Up,
                                    .pointer = it->pointer,
                                    .videoSession = it->videoSession,
                                    .position = it->to};
            it = lane.touches.erase(it);
            lift(lane, ending, now);
            continue;
        }
        static_cast<void>(sendTouch(lane, protocol::TouchAction::Move, it->pointer, it->videoSession,
                                    along(it->from, it->to, it->start, it->end, now)));
        it->next = std::min(now + it->step, it->end);
        ++it;
    }
}

void InputHost::endTouches(Lane& lane, Microseconds now)
{
    const std::vector<ScriptTouch> touches = std::exchange(lane.touches, {});
    for (const ScriptTouch& touch : touches) {
        lift(lane,
             {.action = protocol::TouchAction::Cancel,
              .pointer = touch.pointer,
              .videoSession = touch.videoSession,
              .position = touch.to},
             now);
    }
}

void InputHost::liftGesture(const ControlChannel* channel, std::uint64_t gesture, Microseconds now)
{
    Lane* lane = find(channel);
    if (lane == nullptr) {
        return;
    }
    const auto found = std::ranges::find(lane->touches, gesture, &ScriptTouch::gesture);
    if (found == lane->touches.end()) {
        return;
    }
    const ScriptTouch touch = *found;
    lane->touches.erase(found);
    lift(*lane,
         {.action = protocol::TouchAction::Up,
          .pointer = touch.pointer,
          .videoSession = touch.videoSession,
          .position = along(touch.from, touch.to, touch.start, touch.end, now)},
         now);
}

void InputHost::unlock()
{
    if (std::exchange(m_peeking, false) && m_callbacks.peek) {
        m_callbacks.peek(false);
    }
    if (std::exchange(m_locked, false) && m_callbacks.pointer) {
        m_callbacks.pointer(input::PointerState::Free);
    }
}

}
