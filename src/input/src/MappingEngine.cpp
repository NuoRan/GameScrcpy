#include "gs/input/MappingEngine.h"

#include "AttachmentChain.h"

#include <algorithm>
#include <bit>
#include <initializer_list>
#include <set>
#include <type_traits>
#include <utility>

namespace gs::input {

namespace {

Microseconds milliseconds(std::uint32_t value) noexcept
{
    return Microseconds{static_cast<long long>(value) * 1000};
}

Microseconds defaultPacingTick(double refreshHz) noexcept
{
    const double hz = std::clamp(2.0 * refreshHz, 120.0, 480.0);
    return Microseconds{static_cast<long long>(1'000'000.0 / hz)};
}

Vec2 offsetBy(Vec2 base, Vec2 delta) noexcept
{
    return {base.x + delta.x, base.y + delta.y};
}

InputSource mouseLeft()
{
    static const InputSource left = *parseSource("Mouse.Left");
    return left;
}

InputSource mouseMove()
{
    static const InputSource move = *parseSource("Mouse.Move");
    return move;
}

bool switched(Switch action, bool current) noexcept
{
    switch (action) {
    case Switch::On: return true;
    case Switch::Off: return false;
    case Switch::Toggle: return !current;
    }
    return current;
}

template <typename C>
constexpr Slot primarySlot() noexcept
{
    if constexpr (std::is_same_v<C, FreeLookComponent>) {
        return Slot::Hold;
    } else if constexpr (std::is_same_v<C, PinchComponent>) {
        return Slot::ZoomIn;
    } else {
        return Slot::Press;
    }
}

struct LaneName {
    std::string_view name;
    Slot slot;
    BehaviorSlotKind kind;
};

constexpr LaneName kLaneNames[] = {
    {"press", Slot::Press, BehaviorSlotKind::Key},     {"up", Slot::Up, BehaviorSlotKind::Key},
    {"down", Slot::Down, BehaviorSlotKind::Key},       {"left", Slot::Left, BehaviorSlotKind::Key},
    {"right", Slot::Right, BehaviorSlotKind::Key},     {"walk", Slot::Walk, BehaviorSlotKind::Key},
    {"sprint", Slot::Sprint, BehaviorSlotKind::Key},   {"stick", Slot::Stick, BehaviorSlotKind::Axis2},
    {"look", Slot::Look, BehaviorSlotKind::Axis2},     {"hold", Slot::Hold, BehaviorSlotKind::Key},
    {"zoomIn", Slot::ZoomIn, BehaviorSlotKind::Key},   {"zoomOut", Slot::ZoomOut, BehaviorSlotKind::Key},
    {"cancel", Slot::Cancel, BehaviorSlotKind::Key},   {"walkTo", Slot::WalkTo, BehaviorSlotKind::Key},
    {"stop", Slot::Stop, BehaviorSlotKind::Key},       {"cast", Slot::Cast, BehaviorSlotKind::Key},
};

std::optional<Vec2> wheelNotch(InputSource source)
{
    static const InputSource up = *parseSource("Mouse.WheelUp");
    static const InputSource down = *parseSource("Mouse.WheelDown");
    static const InputSource left = *parseSource("Mouse.WheelLeft");
    static const InputSource right = *parseSource("Mouse.WheelRight");
    if (source == up) {
        return Vec2{0.0, -1.0};
    }
    if (source == down) {
        return Vec2{0.0, 1.0};
    }
    if (source == left) {
        return Vec2{-1.0, 0.0};
    }
    if (source == right) {
        return Vec2{1.0, 0.0};
    }
    return std::nullopt;
}

template <typename C>
ControlStatus statusOf(const C& component) noexcept
{
    ControlStatus status{.touching = component.touching()};
    if constexpr (std::is_same_v<C, JoystickComponent>) {
        status.stick = component.deflection();
        status.walking = component.walking();
    } else if constexpr (std::is_same_v<C, AimComponent>) {
        status.aiming = component.engaged() && !component.cancelling();
        status.cancelled = component.cancelled();
    } else if constexpr (std::is_same_v<C, DialComponent>) {
        status.cell = component.cell();
    } else if constexpr (std::is_same_v<C, ActionWheelComponent>) {
        status.cell = component.cell();
    } else if constexpr (std::is_same_v<C, PeekComponent>) {
        status.touching = component.showing();
    }
    return status;
}

template <typename Variant>
bool engagedNow(const Variant& component)
{
    return std::visit(
        [](const auto& c) {
            if constexpr (requires { c.engaged(); }) {
                return c.engaged();
            } else {
                return false;
            }
        },
        component);
}

}

std::optional<Slot> slotNamed(std::string_view name) noexcept
{
    static constexpr std::pair<std::string_view, Slot> kSlots[] = {
        {"press", Slot::Press},   {"up", Slot::Up},           {"down", Slot::Down},       {"left", Slot::Left},
        {"right", Slot::Right},   {"walk", Slot::Walk},       {"sprint", Slot::Sprint},  {"hold", Slot::Hold},
        {"zoomIn", Slot::ZoomIn}, {"zoomOut", Slot::ZoomOut}, {"cancel", Slot::Cancel},  {"walkTo", Slot::WalkTo},
        {"stop", Slot::Stop},     {"cast", Slot::Cast},
    };
    for (const auto& [slotName, slot] : kSlots) {
        if (slotName == name) {
            return slot;
        }
    }
    return std::nullopt;
}

MappingEngine::MappingEngine(const Profile& profile, const EngineConfig& config, TouchFrameSink& touches,
                             EngineListener& listener)
    : m_config(config)
    , m_listener(listener)
    , m_sink(touches)
    , m_pointers(config.maxPointers)
    , m_builder(config.pacingTick.value_or(defaultPacingTick(config.refreshHz)))
    , m_out(m_pointers, m_builder)
    , m_frame(Microseconds{static_cast<long long>(1'000'000.0 / std::max(config.refreshHz, 1.0))})
{
    compile(profile);
}

MappingEngine::~MappingEngine() = default;

void MappingEngine::compile(const Profile& profile)
{
    m_mapper.emplace(m_config.screen, profile.screen.scaleMode, profile.screen.safeArea);
    const CoordinateMapper& mapper = *m_mapper;

    for (const Variable& variable : profile.vars) {
        m_stateIds.push_back(variable.id);
        m_stateValues.push_back(truthy(variable.initial));
    }
    for (const Control& control : profile.controls) {
        addControl(control, mapper);
    }
    for (std::size_t i = 0; i < profile.controls.size(); ++i) {
        const auto* skill = std::get_if<SkillControl>(&profile.controls[i].kind);
        if (skill == nullptr || skill->pauses.empty()) {
            continue;
        }
        const auto stick = std::ranges::find(m_controls, skill->pauses, &ControlState::id);
        if (stick != m_controls.end() && std::holds_alternative<JoystickComponent>(stick->component)) {
            m_pauses.emplace_back(i, static_cast<std::size_t>(stick - m_controls.begin()));
        }
    }

    std::set<Chord> lockKeys;
    for (const Control& control : profile.controls) {
        if (const auto* camera = std::get_if<CameraControl>(&control.kind)) {
            m_hasCamera = true;
            if (const std::optional<Chord> key = camera->lockKey ? camera->lockKey : m_config.defaultLockKey) {
                lockKeys.insert(*key);
            }
        }
    }
    if (!m_hasCamera && m_config.defaultLockKey) {
        lockKeys.insert(*m_config.defaultLockKey);
    }
    for (const Chord& key : lockKeys) {
        addEntry(Binding{.input = key}, LockTarget{}, SlotKind::Digital);
    }

    finishEntries();
    m_live.assign(m_controls.size(), false);
    m_aiming.assign(m_controls.size(), false);
    m_status.assign(m_controls.size(), ControlStatus{});
    refresh(Microseconds{0});
    m_compiled = true;
    m_listener.liveChanged(m_live);
}

MappingEngine::Built MappingEngine::build(const Control& control, const CoordinateMapper& mapper) const
{
    const Vec2 position = mapper.position(control.anchor, control.offset);
    const auto perCount = [&](Vec2 sensitivity) { return mapper.extent({sensitivity.x / 1000.0, sensitivity.y / 1000.0}); };
    const auto stick = [&](double radius, Vec2 hero, std::uint16_t walkMsPerUnit, bool stopOnRelease, double walkScale,
                           double sprintScale) {
        return Built{JoystickComponent{{.center = position,
                                        .radius = mapper.radius(radius),
                                        .walkScale = walkScale,
                                        .sprintScale = sprintScale,
                                        .hero = mapper.position(Anchor::Center, hero),
                                        .unit = mapper.radius(1.0),
                                        .walkPerUnit = milliseconds(walkMsPerUnit),
                                        .stopOnRelease = stopOnRelease,
                                        .locate = m_locateCursor.get()}}};
    };
    const auto twoFrames = m_frame * 2;

    if (const auto* tap = std::get_if<TapControl>(&control.kind)) {
        return {ButtonComponent{{position, tap->tapMs > 0 ? milliseconds(tap->tapMs) : twoFrames}}, false,
                {.trigger = Trigger::Tap}};
    }
    if (std::holds_alternative<HoldControl>(control.kind)) {
        return {ButtonComponent{{position, twoFrames}}, false, {.trigger = Trigger::Hold}};
    }
    if (const auto* longPress = std::get_if<LongPressControl>(&control.kind)) {
        return {ButtonComponent{{position, milliseconds(longPress->holdMs)}}, false, {.trigger = Trigger::Tap}};
    }
    if (const auto* turbo = std::get_if<TurboControl>(&control.kind)) {
        const std::uint8_t rate = std::max<std::uint8_t>(turbo->rateHz, 1);
        const Microseconds tapFor = std::min(turbo->tapMs > 0 ? milliseconds(turbo->tapMs) : twoFrames,
                                             Microseconds{500'000 / rate});
        return {ButtonComponent{{position, tapFor}}, false, {.trigger = Trigger::Turbo, .rateHz = rate}};
    }
    if (const auto* wasd = std::get_if<WasdControl>(&control.kind)) {
        return stick(wasd->radius, {}, RightWalkControl{}.walkMsPerUnit, true, wasd->walkScale, wasd->sprintScale);
    }
    if (const auto* walker = std::get_if<RightWalkControl>(&control.kind)) {
        const WasdControl plain;
        return stick(walker->radius, walker->hero, walker->walkMsPerUnit, walker->stopOnRelease, plain.walkScale,
                     plain.sprintScale);
    }
    if (const auto* pad = std::get_if<PadStickControl>(&control.kind)) {
        const WasdControl plain;
        return stick(pad->radius, {}, RightWalkControl{}.walkMsPerUnit, true, plain.walkScale, plain.sprintScale);
    }
    if (const auto* camera = std::get_if<CameraControl>(&control.kind)) {
        const Vec2 extent = mapper.extent(camera->size);
        return {CameraComponent{{.anchor = position,
                                 .halfExtent = {extent.x / 2.0, extent.y / 2.0},
                                 .perCount = perCount(camera->sensitivity),
                                 .idleRecenter = milliseconds(camera->idleRecenterMs),
                                 .frame = m_frame,
                                 .wholePicture = camera->range == CameraRange::Screen}},
                true};
    }
    if (const auto* padCamera = std::get_if<PadCameraControl>(&control.kind)) {
        const Vec2 extent = mapper.extent(padCamera->size);
        return {CameraComponent{{.anchor = position,
                                 .halfExtent = {extent.x / 2.0, extent.y / 2.0},
                                 .perCount = perCount(padCamera->sensitivity),
                                 .idleRecenter = milliseconds(padCamera->idleRecenterMs),
                                 .frame = m_frame,
                                 .wholePicture = padCamera->range == CameraRange::Screen}}};
    }
    if (const auto* peek = std::get_if<FreeLookControl>(&control.kind)) {
        return {FreeLookComponent{{position, perCount(peek->sensitivity), peek->resetOnRelease}}};
    }
    if (const auto* skill = std::get_if<SkillControl>(&control.kind)) {
        return {AimComponent{{.position = position,
                              .radius = mapper.radius(skill->radius),
                              .perCount = perCount(skill->sensitivity),
                              .castOnRelease = skill->castOnRelease,
                              .cursor = skill->direction == AimDirection::Cursor,
                              .cast = skill->cast,
                              .hero = mapper.position(Anchor::Center, skill->hero),
                              .castExtent = mapper.radius(skill->castRadius),
                              .cancelAt = skill->cancelAt
                                              ? std::optional<Vec2>(offsetBy(position, mapper.extent(*skill->cancelAt)))
                                              : std::nullopt,
                              .frame = m_frame,
                              .locate = m_locateCursor.get()}}};
    }
    if (const auto* swipe = std::get_if<SwipeControl>(&control.kind)) {
        std::vector<Vec2> path;
        for (const Vec2& point : swipe->path) {
            path.push_back(offsetBy(position, mapper.extent(point)));
        }
        return {SwipeComponent{{std::move(path), milliseconds(swipe->durationMs), m_frame}}};
    }
    if (const auto* pinch = std::get_if<PinchControl>(&control.kind)) {
        return {PinchComponent{{.center = position,
                                .halfSpan = mapper.extent({pinch->span / 2.0, 0.0}).x,
                                .halfStep = mapper.extent({pinch->step / 2.0, 0.0}).x,
                                .frame = m_frame}}};
    }
    if (const auto* dial = std::get_if<DialControl>(&control.kind)) {
        return {DialComponent{{.center = position,
                               .radius = mapper.radius(dial->radius),
                               .cells = dial->cells,
                               .firstDeg = static_cast<double>(dial->firstDeg)}}};
    }
    if (const auto* multi = std::get_if<MultiPressControl>(&control.kind)) {
        std::vector<Vec2> spots{position};
        for (const Vec2& point : multi->points) {
            spots.push_back(offsetBy(position, mapper.extent(point)));
        }
        return {MultiPressComponent{{std::move(spots)}}, false, {.trigger = Trigger::Hold}};
    }
    if (std::holds_alternative<TempCursorControl>(control.kind)) {
        return {PeekComponent{}};
    }
    if (const auto* wheel = std::get_if<ActionWheelControl>(&control.kind)) {
        ActionWheelConfig config{.tapHold = twoFrames};
        for (const WheelCell& cell : wheel->cells) {
            config.cells.push_back(
                {.action = cell.action, .control = cell.control, .at = offsetBy(position, mapper.extent(cell.at))});
        }
        return {ActionWheelComponent{std::move(config)}};
    }
    if (std::holds_alternative<BehaviorControl>(control.kind)) {
        return {BehaviorComponent{BehaviorConfig{}, nullptr}};
    }
    return {ButtonComponent{{position, twoFrames}}};
}

void MappingEngine::addControl(const Control& control, const CoordinateMapper& mapper)
{
    const std::size_t index = m_controls.size();
    Built made = build(control, mapper);
    if (const auto* behavior = std::get_if<BehaviorControl>(&control.kind)) {
        made.component = makeBehavior(control, *behavior, index, mapper);
    }
    const TriggerConfig trigger = made.press;
    m_controls.push_back(
        {control.id, std::move(made.component), {}, conditionOf(control.when), control.enabled, false, made.camera});
    m_controls.back().handable =
        std::holds_alternative<TapControl>(control.kind) || std::holds_alternative<HoldControl>(control.kind) ||
        std::holds_alternative<LongPressControl>(control.kind) || std::holds_alternative<TurboControl>(control.kind) ||
        std::holds_alternative<SwipeControl>(control.kind) || std::holds_alternative<MultiPressControl>(control.kind);
    const auto bind = [&](const std::optional<Binding>& binding, Slot slot, SlotKind kind) {
        if (binding && binding->input) {
            addEntry(*binding, ControlTarget{index, slot}, kind);
        }
    };
    const auto press = [&](const std::optional<Binding>& binding) {
        if (binding && binding->input) {
            addEntry(*binding, ControlTarget{index, Slot::Press}, SlotKind::Digital, trigger);
        }
    };

    if (const auto* tap = std::get_if<TapControl>(&control.kind)) {
        press(tap->press);
    } else if (const auto* hold = std::get_if<HoldControl>(&control.kind)) {
        press(hold->press);
    } else if (const auto* longPress = std::get_if<LongPressControl>(&control.kind)) {
        press(longPress->press);
    } else if (const auto* turbo = std::get_if<TurboControl>(&control.kind)) {
        press(turbo->press);
    } else if (const auto* wasd = std::get_if<WasdControl>(&control.kind)) {
        bind(wasd->up, Slot::Up, SlotKind::Digital);
        bind(wasd->down, Slot::Down, SlotKind::Digital);
        bind(wasd->left, Slot::Left, SlotKind::Digital);
        bind(wasd->right, Slot::Right, SlotKind::Digital);
        bind(wasd->walk, Slot::Walk, SlotKind::Digital);
        bind(wasd->sprint, Slot::Sprint, SlotKind::Digital);
    } else if (const auto* walker = std::get_if<RightWalkControl>(&control.kind)) {
        bind(walker->walkTo, Slot::WalkTo, SlotKind::Digital);
        bind(walker->stop, Slot::Stop, SlotKind::Digital);
    } else if (const auto* pad = std::get_if<PadStickControl>(&control.kind)) {
        bind(pad->stick, Slot::Stick, SlotKind::Axis2D);
    } else if (const auto* camera = std::get_if<CameraControl>(&control.kind)) {
        bind(camera->look, Slot::Look, SlotKind::Axis2D);
    } else if (const auto* padCamera = std::get_if<PadCameraControl>(&control.kind)) {
        bind(padCamera->look, Slot::Look, SlotKind::Axis2D);
    } else if (const auto* peek = std::get_if<FreeLookControl>(&control.kind)) {
        bind(peek->hold, Slot::Hold, SlotKind::Digital);
        bind(peek->look, Slot::Look, SlotKind::HeldAxis2D);
    } else if (const auto* skill = std::get_if<SkillControl>(&control.kind)) {
        bind(skill->press, Slot::Press, SlotKind::Digital);
        if (skill->direction != AimDirection::Cursor) {
            bind(skill->look, Slot::Look, SlotKind::HeldAxis2D);
        }
        bind(skill->cancel, Slot::Cancel, SlotKind::Digital);
        if (skill->cast == CastTiming::Manual) {
            const bool bound = skill->castKey && skill->castKey->input;
            addEntry(bound ? *skill->castKey : Binding{.input = Chord{.source = mouseLeft()}, .claim = Claim::Cursor},
                     ControlTarget{index, Slot::Cast}, SlotKind::Digital);
        }
    } else if (const auto* swipe = std::get_if<SwipeControl>(&control.kind)) {
        bind(swipe->press, Slot::Press, SlotKind::Digital);
    } else if (const auto* pinch = std::get_if<PinchControl>(&control.kind)) {
        bind(pinch->zoomIn, Slot::ZoomIn, SlotKind::Digital);
        bind(pinch->zoomOut, Slot::ZoomOut, SlotKind::Digital);
    } else if (const auto* dial = std::get_if<DialControl>(&control.kind)) {
        bind(dial->press, Slot::Press, SlotKind::Digital);
        bind(dial->look ? dial->look : Binding{.input = Chord{.source = mouseMove()}, .claim = Claim::Both}, Slot::Look,
             SlotKind::HeldAxis2D);
    } else if (const auto* multi = std::get_if<MultiPressControl>(&control.kind)) {
        press(multi->press);
    } else if (const auto* cursor = std::get_if<TempCursorControl>(&control.kind)) {
        bind(cursor->press, Slot::Press, SlotKind::Digital);
    } else if (const auto* wheel = std::get_if<ActionWheelControl>(&control.kind)) {
        bind(wheel->press, Slot::Press, SlotKind::Digital);
        bind(wheel->look ? wheel->look : Binding{.input = Chord{.source = mouseMove()}, .claim = Claim::Both},
             Slot::Look, SlotKind::HeldAxis2D);
    } else if (const auto* behavior = std::get_if<BehaviorControl>(&control.kind)) {
        bindBehavior(index, *behavior);
    }
    addLanes(index, control);
    addChain(index, control, mapper);
}

MappingEngine::Component MappingEngine::makeBehavior(const Control& control, const BehaviorControl& behavior,
                                                     std::size_t index, const CoordinateMapper& mapper) const
{
    BehaviorConfig config{.module = behavior.behavior, .shape = {}, .frame = frameTick()};
    if (m_config.modules == nullptr) {
        return BehaviorComponent{std::move(config), nullptr};
    }
    std::unique_ptr<BehaviorInstance> instance;
    if (isModuleRef(behavior.behavior)) {
        instance = m_config.modules->behavior({.module = behavior.behavior,
                                               .control = control.id,
                                               .type = kControlTypeNames[control.kind.index()],
                                               .params = behavior.params,
                                               .position = mapper.position(control.anchor, control.offset),
                                               .unit = mapper.extent({1.0, 1.0}),
                                               .seed = moduleSeed(m_config.seed, index, kMostAttachments)},
                                              config.shape);
    }
    if (instance == nullptr) {
        m_config.modules->trouble(
            {.kind = ModuleTroubleKind::Missing, .module = behavior.behavior, .control = control.id});
        config.shape = {};
        return BehaviorComponent{std::move(config), nullptr};
    }
    if (config.shape.slotList.size() > kMostBehaviorSlots) {
        config.shape.slotList.resize(kMostBehaviorSlots);
    }
    config.shape.fingers =
        static_cast<std::uint8_t>(std::clamp<std::size_t>(config.shape.fingers, 1, kMostModuleFingers));
    return BehaviorComponent{std::move(config), std::move(instance)};
}

void MappingEngine::bindBehavior(std::size_t index, const BehaviorControl& behavior)
{
    const auto* component = std::get_if<BehaviorComponent>(&m_controls[index].component);
    if (component == nullptr) {
        return;
    }
    const std::vector<BehaviorSlot> declared = component->shape().slotList;
    for (const BehaviorSlot& slot : declared) {
        Lane lane{.slot = Slot::Press, .kind = slot.kind, .entry = std::nullopt};
        const auto bound = std::ranges::find(behavior.inputs, slot.name, &NamedBinding::name);
        if (bound != behavior.inputs.end() && bound->binding && bound->binding->input) {
            const bool twoDimensional = kindOf(bound->binding->input->source) == SourceKind::Axis2D;
            if (twoDimensional == (slot.kind == BehaviorSlotKind::Axis2)) {
                lane.entry = addEntry(*bound->binding, ControlTarget{index, Slot::Press},
                                      twoDimensional ? SlotKind::Axis2D : SlotKind::Digital);
            }
        }
        m_controls[index].lanes.push_back(lane);
    }
}

void MappingEngine::addLanes(std::size_t index, const Control& control)
{
    ControlState& state = m_controls[index];
    if (std::holds_alternative<BehaviorComponent>(state.component)) {
        return;
    }
    for (const std::string_view name : inputSlots(kControlTypeNames[control.kind.index()])) {
        const auto known = std::ranges::find(kLaneNames, name, &LaneName::name);
        if (known == std::end(kLaneNames)) {
            state.lanes.push_back({});
            continue;
        }
        Lane lane{.slot = known->slot, .kind = known->kind, .entry = std::nullopt};
        for (const std::size_t entry : state.entries) {
            const auto* target = std::get_if<ControlTarget>(&m_entries[entry].target);
            if (target != nullptr && target->slot == known->slot) {
                lane.entry = entry;
                break;
            }
        }
        state.lanes.push_back(lane);
    }
}

void MappingEngine::addChain(std::size_t index, const Control& control, const CoordinateMapper& mapper)
{
    if (m_config.modules == nullptr) {
        return;
    }
    std::vector<const GlobalAttachment*> globals;
    for (const GlobalAttachment& global : m_config.modules->globals()) {
        if (isModuleRef(global.module) && takesGlobal(control, global)) {
            globals.push_back(&global);
        }
    }
    if (control.attachments.empty() && globals.empty()) {
        return;
    }
    ControlState& state = m_controls[index];
    auto chain = std::make_unique<detail::AttachmentChain>(m_pointers, m_builder, m_now, *m_config.modules,
                                                           control.id, state.lanes.size());
    const Vec2 position = mapper.position(control.anchor, control.offset);
    const Vec2 unit = mapper.extent({1.0, 1.0});
    const auto add = [&](const std::string& module, const std::string& params, std::size_t place) {
        std::unique_ptr<AttachmentInstance> instance =
            m_config.modules->attachment({.module = module,
                                          .control = control.id,
                                          .type = kControlTypeNames[control.kind.index()],
                                          .params = params,
                                          .position = position,
                                          .unit = unit,
                                          .seed = moduleSeed(m_config.seed, index, place)});
        if (instance == nullptr) {
            m_config.modules->trouble({.kind = ModuleTroubleKind::Missing, .module = module, .control = control.id});
            return;
        }
        chain->add(module, std::move(instance));
    };
    for (std::size_t at = 0; at < globals.size(); ++at) {
        add(globals[at]->module, globals[at]->params, kMostAttachments + at);
    }
    for (std::size_t place = 0; place < control.attachments.size() && place < kMostAttachments; ++place) {
        const Attachment& wanted = control.attachments[place];
        if (!wanted.enabled || !isModuleRef(wanted.module)) {
            continue;
        }
        add(wanted.module, wanted.params, place);
    }
    if (!chain->empty()) {
        state.chain = std::move(chain);
    }
}

TouchOutput& MappingEngine::outOf(std::size_t control) noexcept
{
    return m_controls[control].chain ? m_controls[control].chain->output() : m_out;
}

bool MappingEngine::routeInput(std::size_t entry, ModuleInput kind, Vec2 value, Microseconds now)
{
    const auto* target = std::get_if<ControlTarget>(&m_entries[entry].target);
    if (target == nullptr) {
        return false;
    }
    const std::size_t control = target->control;
    ControlState& state = m_controls[control];
    const bool behavior = std::holds_alternative<BehaviorComponent>(state.component);
    if (!state.chain && !behavior) {
        return false;
    }
    const auto lane = std::ranges::find(state.lanes, std::optional<std::size_t>(entry), &Lane::entry);
    if (lane == state.lanes.end()) {
        return behavior;
    }
    ModuleEvent event{.kind = kind,
                      .slot = static_cast<std::uint32_t>(lane - state.lanes.begin()),
                      .x = value.x,
                      .y = value.y,
                      .at = now};
    const bool keyed = kind == ModuleInput::KeyDown || kind == ModuleInput::KeyUp;
    if (const std::optional<Vec2> notch = wheelNotch(m_entries[entry].binding.input->source); notch && keyed) {
        if (kind == ModuleInput::KeyUp) {
            return true;
        }
        event.kind = ModuleInput::Wheel;
        event.x = notch->x;
        event.y = notch->y;
    } else if (keyed && lane->kind == BehaviorSlotKind::Axis1) {
        event.kind = ModuleInput::Axis1;
        event.x = kind == ModuleInput::KeyDown ? 1.0 : 0.0;
        event.y = 0.0;
    }
    if (!state.chain) {
        deliverLane(control, event, now);
        return true;
    }
    detail::AttachmentChain::Events delivered;
    state.chain->input(event, delivered);
    for (const ModuleEvent& each : delivered) {
        deliverLane(control, each, now);
    }
    return true;
}

void MappingEngine::deliverLane(std::size_t control, const ModuleEvent& event, Microseconds now)
{
    ControlState& state = m_controls[control];
    if (event.slot >= state.lanes.size()) {
        return;
    }
    const Lane lane = state.lanes[event.slot];
    if (auto* behavior = std::get_if<BehaviorComponent>(&state.component)) {
        if (state.live) {
            behavior->input(event, now, outOf(control));
            tellTrouble(control);
        }
        return;
    }
    switch (event.kind) {
    case ModuleInput::KeyDown:
        pressLane(control, lane, true, now);
        return;
    case ModuleInput::KeyUp:
        pressLane(control, lane, false, now);
        return;
    case ModuleInput::Wheel:
        pressLane(control, lane, true, now);
        pressLane(control, lane, false, now);
        return;
    case ModuleInput::Axis1:
        pressLane(control, lane, event.x >= 0.5, now);
        return;
    case ModuleInput::Mouse:
    case ModuleInput::Axis2:
        break;
    }
    if (!state.live || lane.kind != BehaviorSlotKind::Axis2) {
        return;
    }
    const Slot slot = lane.slot;
    const double dx = event.x;
    const double dy = event.y;
    const bool moved = event.kind == ModuleInput::Mouse;
    std::visit(
        [&](auto& c) {
            if (moved) {
                if constexpr (requires { c.motion(slot, dx, dy, now, m_out); }) {
                    c.motion(slot, dx, dy, now, outOf(control));
                }
            } else if constexpr (requires { c.axis(slot, Vec2{dx, dy}, now, m_out); }) {
                c.axis(slot, Vec2{dx, dy}, now, outOf(control));
            }
        },
        state.component);
}

void MappingEngine::pressLane(std::size_t control, const Lane& lane, bool down, Microseconds now)
{
    if (lane.kind == BehaviorSlotKind::Axis2) {
        return;
    }
    if (lane.entry) {
        TriggerState& trigger = m_entries[*lane.entry].trigger;
        deliver(*lane.entry, down ? trigger.press(now) : trigger.release(now), now);
        return;
    }
    deliverToControl(control, lane.slot, down ? TriggerSignal::Activate : TriggerSignal::Deactivate, now);
}

void MappingEngine::resetComponent(std::size_t control)
{
    ControlState& state = m_controls[control];
    std::visit([&](auto& component) { component.reset(outOf(control)); }, state.component);
    if (state.chain) {
        state.chain->reset();
    }
}

void MappingEngine::tellTrouble(std::size_t control)
{
    auto* behavior = std::get_if<BehaviorComponent>(&m_controls[control].component);
    if (behavior == nullptr || m_config.modules == nullptr) {
        return;
    }
    if (const std::optional<ModuleTroubleKind> trouble = behavior->takeTrouble()) {
        m_config.modules->trouble({.kind = *trouble, .module = behavior->module(), .control = m_controls[control].id});
    }
}

Microseconds MappingEngine::frameTick() const noexcept
{
    const Microseconds tick = m_config.pacingTick.value_or(defaultPacingTick(m_config.refreshHz));
    return tick.count() > 0 ? tick : defaultPacingTick(m_config.refreshHz);
}

bool MappingEngine::retuneControl(const Control& control, Microseconds now)
{
    const std::optional<std::size_t> index = controlIndex(control.id);
    if (!index || !m_mapper) {
        return false;
    }
    m_now = now;
    ControlState& state = m_controls[*index];
    if (std::holds_alternative<BehaviorComponent>(state.component)) {
        return false;
    }
    Built made = build(control, *m_mapper);
    if (made.component.index() != state.component.index()) {
        return false;
    }
    std::visit(
        [&made](auto& current) {
            using Kind = std::decay_t<decltype(current)>;
            current.retune(std::get<Kind>(made.component));
        },
        state.component);
    for (const std::size_t entry : state.entries) {
        const auto* target = std::get_if<ControlTarget>(&m_entries[entry].target);
        if (target != nullptr && target->slot == Slot::Press) {
            m_entries[entry].trigger.retune(made.press);
        }
    }
    settle(now);
    return true;
}

std::size_t MappingEngine::addEntry(const Binding& binding, Target target, SlotKind slot, const TriggerConfig& trigger)
{
    m_entries.push_back(Entry{.binding = binding,
                              .target = target,
                              .trigger = TriggerState{trigger},
                              .held2d = slot == SlotKind::HeldAxis2D,
                              .axis2d = slot != SlotKind::Digital,
                              .mouse = binding.input->source.device == Device::Mouse});
    const std::size_t index = m_entries.size() - 1;
    if (const auto* control = std::get_if<ControlTarget>(&target)) {
        m_controls[control->control].entries.push_back(index);
    }
    return index;
}

void MappingEngine::finishEntries()
{
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        m_dispatch[denseIndex(m_entries[i].binding.input->source)].push_back(i);
    }
    for (std::vector<std::size_t>& list : m_dispatch) {
        std::stable_sort(list.begin(), list.end(), [this](std::size_t a, std::size_t b) {
            return std::popcount(m_entries[a].binding.input->modifiers) >
                   std::popcount(m_entries[b].binding.input->modifiers);
        });
    }
}

MappingEngine::Condition MappingEngine::conditionOf(const std::string& when) const
{
    if (when.empty()) {
        return {};
    }
    const bool negate = when.starts_with('!');
    const std::string_view id = negate ? std::string_view(when).substr(1) : std::string_view(when);
    const auto found = std::find(m_stateIds.begin(), m_stateIds.end(), id);
    if (found == m_stateIds.end()) {
        return {};
    }
    return {static_cast<std::size_t>(found - m_stateIds.begin()), !negate};
}

bool MappingEngine::holds(Condition condition) const noexcept
{
    return condition.state == kNoState || m_stateValues[condition.state] == condition.expect;
}

bool MappingEngine::steersLock(const Entry& entry) const noexcept
{
    return std::holds_alternative<LockTarget>(entry.target);
}

bool MappingEngine::mouseClaimed(const Entry& entry) const noexcept
{
    return !entry.mouse || steersLock(entry) ||
           (captured() ? claimsLocked(entry.binding.claim) : claimsCursor(entry.binding.claim));
}

bool MappingEngine::entryLive(const Entry& entry) const noexcept
{
    if (std::holds_alternative<LockTarget>(entry.target)) {
        return true;
    }
    if (!mouseClaimed(entry)) {
        return false;
    }
    if (const auto* control = std::get_if<ControlTarget>(&entry.target)) {
        const ControlState& state = m_controls[control->control];
        if (control->slot == Slot::Cast) {
            const auto* aim = std::get_if<AimComponent>(&state.component);
            return state.live && aim != nullptr && aim->armed();
        }
        return state.live;
    }
    return true;
}

void MappingEngine::collectPressed(InputSource source, Pressed& out) const
{
    std::optional<int> best;
    for (const std::size_t index : m_dispatch[denseIndex(source)]) {
        const Entry& entry = m_entries[index];
        const std::uint8_t wanted = entry.binding.input->modifiers;
        if (entry.axis2d || !entryLive(entry) || (m_modifiers & wanted) != wanted) {
            continue;
        }
        const int count = std::popcount(wanted);
        if (best && count < *best) {
            break;
        }
        best = count;
        if (!out.full()) {
            out.push_back(index);
        }
    }
    const auto casting = [this](std::size_t index) {
        const auto* control = std::get_if<ControlTarget>(&m_entries[index].target);
        return control != nullptr && control->slot == Slot::Cast;
    };
    bool cast = false;
    for (std::size_t i = 0; i < out.size(); ++i) {
        cast = cast || casting(out[i]);
    }
    if (cast) {
        Pressed kept;
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (casting(out[i])) {
                kept.push_back(out[i]);
            }
        }
        out = kept;
    }
}

bool MappingEngine::claims(InputSource source) const
{
    return std::ranges::any_of(m_dispatch[denseIndex(source)], [this](std::size_t index) {
        return !m_entries[index].axis2d && entryLive(m_entries[index]);
    });
}

bool MappingEngine::pressEntries(InputSource source, Microseconds now, bool resuming)
{
    Pressed& pressed = m_pressed[denseIndex(source)];
    if (!pressed.empty()) {
        return true;
    }
    collectPressed(source, pressed);
    if (resuming) {
        Pressed kept;
        for (const std::size_t index : pressed) {
            if (resumable(index)) {
                kept.push_back(index);
            }
        }
        pressed = kept;
    }
    const Pressed chosen = pressed;
    for (const std::size_t index : chosen) {
        notePressed(index, true);
    }
    for (const std::size_t index : chosen) {
        const Pressed& current = m_pressed[denseIndex(source)];
        if (std::find(current.begin(), current.end(), index) == current.end()) {
            continue;
        }
        if (quietFor(index)) {
            m_entries[index].quiet = true;
        } else if (!routeInput(index, ModuleInput::KeyDown, {}, now)) {
            deliver(index, m_entries[index].trigger.press(now), now);
        }
    }
    return !chosen.empty();
}

void MappingEngine::releaseEntries(InputSource source, Microseconds now)
{
    const Pressed released = std::exchange(m_pressed[denseIndex(source)], Pressed{});
    for (const std::size_t index : released) {
        notePressed(index, false);
    }
    for (const std::size_t index : released) {
        if (takeQuiet(index)) {
            continue;
        }
        if (!routeInput(index, ModuleInput::KeyUp, {}, now)) {
            deliver(index, m_entries[index].trigger.release(now), now);
        }
    }
}

bool MappingEngine::quietFor(std::size_t entry) const noexcept
{
    const auto* control = std::get_if<ControlTarget>(&m_entries[entry].target);
    return control != nullptr && control->slot == Slot::Press && m_controls[control->control].handedOver;
}

bool MappingEngine::takeQuiet(std::size_t entry) noexcept
{
    return std::exchange(m_entries[entry].quiet, false);
}

void MappingEngine::setHandedOver(const std::vector<std::string>& ids)
{
    for (ControlState& control : m_controls) {
        control.handedOver = control.handable && std::ranges::find(ids, control.id) != ids.end();
    }
}

void MappingEngine::notePressed(std::size_t entry, bool down)
{
    const auto* control = std::get_if<ControlTarget>(&m_entries[entry].target);
    if (control == nullptr) {
        return;
    }
    ControlState& state = m_controls[control->control];
    if (down) {
        if (state.held++ == 0) {
            m_listener.controlPressed(state.id, true);
        }
    } else if (state.held > 0 && --state.held == 0) {
        m_listener.controlPressed(state.id, false);
    }
}

std::optional<std::size_t> MappingEngine::controlIndex(std::string_view id) const
{
    const auto found =
        std::find_if(m_controls.begin(), m_controls.end(), [&](const ControlState& c) { return c.id == id; });
    if (found == m_controls.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - m_controls.begin());
}

void MappingEngine::buttonDown(InputSource source, Microseconds now)
{
    m_now = now;
    m_modifiers = static_cast<std::uint8_t>(m_modifiers | modifierBit(source));
    if (captured() || source != mouseLeft()) {
        static_cast<void>(pressEntries(source, now));
    }
    settle(now);
}

void MappingEngine::resumeHeld(InputSource source, Microseconds now)
{
    m_now = now;
    m_modifiers = static_cast<std::uint8_t>(m_modifiers | modifierBit(source));
    if (captured() || source != mouseLeft()) {
        static_cast<void>(pressEntries(source, now, true));
    }
    settle(now);
}

bool MappingEngine::resumable(std::size_t entry) const
{
    const auto* control = std::get_if<ControlTarget>(&m_entries[entry].target);
    if (control == nullptr) {
        return false;
    }
    const Trigger kind = m_entries[entry].trigger.kind();
    if (kind != Trigger::Hold && kind != Trigger::Turbo) {
        return false;
    }
    const Component& component = m_controls[control->control].component;
    return std::holds_alternative<JoystickComponent>(component) || std::holds_alternative<ButtonComponent>(component) ||
           std::holds_alternative<MultiPressComponent>(component) ||
           std::holds_alternative<FreeLookComponent>(component) || std::holds_alternative<PeekComponent>(component);
}

void MappingEngine::buttonUp(InputSource source, Microseconds now)
{
    m_now = now;
    m_modifiers = static_cast<std::uint8_t>(m_modifiers & ~modifierBit(source));
    releaseEntries(source, now);
    settle(now);
}

void MappingEngine::motion(InputSource source, double dx, double dy, Microseconds now)
{
    m_now = now;
    std::optional<std::size_t> chosen;
    std::uint64_t latest = 0;
    for (const std::size_t index : m_dispatch[denseIndex(source)]) {
        const Entry& entry = m_entries[index];
        if (!entry.axis2d || !entry.held2d || !entryLive(entry)) {
            continue;
        }
        const ControlState& control = m_controls[std::get<ControlTarget>(entry.target).control];
        if (engagedNow(control.component) && (!chosen || control.engagedOrder > latest)) {
            chosen = index;
            latest = control.engagedOrder;
        }
    }
    if (!chosen) {
        for (const std::size_t index : m_dispatch[denseIndex(source)]) {
            const Entry& entry = m_entries[index];
            if (entry.axis2d && !entry.held2d && entryLive(entry)) {
                chosen = index;
                break;
            }
        }
    }
    if (chosen && !routeInput(*chosen, ModuleInput::Mouse, {dx, dy}, now)) {
        const auto& target = std::get<ControlTarget>(m_entries[*chosen].target);
        std::visit(
            [&](auto& c) {
                if constexpr (requires { c.motion(target.slot, dx, dy, now, m_out); }) {
                    c.motion(target.slot, dx, dy, now, outOf(target.control));
                }
            },
            m_controls[target.control].component);
    }
    if (source.device == Device::Mouse) {
        followCursor(now);
    }
    settle(now);
}

void MappingEngine::followCursor(Microseconds now)
{
    if (!*m_locateCursor) {
        return;
    }
    for (std::size_t index = 0; index < m_controls.size(); ++index) {
        if (!m_controls[index].live) {
            continue;
        }
        std::visit(
            [&](auto& c) {
                if constexpr (requires { c.follow(now, m_out); }) {
                    c.follow(now, outOf(index));
                }
            },
            m_controls[index].component);
    }
}

void MappingEngine::syncPauses(Microseconds now)
{
    for (const auto& [aim, stick] : m_pauses) {
        const bool engaged = std::get<AimComponent>(m_controls[aim].component).engaged();
        std::get<JoystickComponent>(m_controls[stick].component).setFrozen(engaged, now, outOf(stick));
    }
}

void MappingEngine::axis(InputSource source, Vec2 value, Microseconds now)
{
    m_now = now;
    for (const std::size_t index : m_dispatch[denseIndex(source)]) {
        const Entry& entry = m_entries[index];
        if (!entry.axis2d || entry.held2d || !entryLive(entry)) {
            continue;
        }
        if (routeInput(index, ModuleInput::Axis2, value, now)) {
            break;
        }
        const auto& target = std::get<ControlTarget>(entry.target);
        std::visit(
            [&](auto& c) {
                if constexpr (requires { c.axis(target.slot, value, now, m_out); }) {
                    c.axis(target.slot, value, now, outOf(target.control));
                }
            },
            m_controls[target.control].component);
        break;
    }
    settle(now);
}

void MappingEngine::deliver(std::size_t entry, TriggerSignal signal, Microseconds now)
{
    if (signal == TriggerSignal::None) {
        return;
    }
    const Target& target = m_entries[entry].target;
    if (const auto* control = std::get_if<ControlTarget>(&target)) {
        deliverToControl(control->control, control->slot, signal, now);
    } else if (signal != TriggerSignal::Deactivate) {
        lockAction(Switch::Toggle, now);
    }
}

void MappingEngine::deliverToControl(std::size_t control, Slot slot, TriggerSignal signal, Microseconds now)
{
    ControlState& state = m_controls[control];
    if (!state.live) {
        return;
    }
    const bool wasEngaged = engagedNow(state.component);
    std::visit(
        [&](auto& c) {
            if constexpr (requires { c.signal(slot, signal, now, m_out); }) {
                c.signal(slot, signal, now, outOf(control));
            }
        },
        state.component);
    if (!wasEngaged && engagedNow(state.component)) {
        state.engagedOrder = ++m_engagements;
    }
    tellTrouble(control);
    if (auto* wheel = std::get_if<ActionWheelComponent>(&m_controls[control].component)) {
        if (const std::optional<int> chosen = wheel->takeChoice()) {
            runWheelChoice(control, *chosen, now);
        }
    }
}

void MappingEngine::runWheelChoice(std::size_t control, int cell, Microseconds now)
{
    const auto& wheel = std::get<ActionWheelComponent>(m_controls[control].component);
    const WheelSlot* slot = wheel.slotAt(cell);
    if (slot == nullptr || slot->action != WheelAction::Control) {
        return;
    }
    const std::optional<std::size_t> target = controlIndex(slot->control);
    if (!target || *target == control || std::holds_alternative<ActionWheelComponent>(m_controls[*target].component)) {
        return;
    }
    const Slot primary = std::visit([](const auto& c) { return primarySlot<std::decay_t<decltype(c)>>(); },
                                    m_controls[*target].component);
    const Microseconds hold = wheel.tapHold();
    deliverToControl(*target, primary, TriggerSignal::Activate, now);
    m_wheelReleases.push_back({.control = *target, .slot = primary, .at = now + hold});
}

void MappingEngine::releaseWheelPresses(Microseconds now)
{
    std::vector<WheelRelease> due;
    std::erase_if(m_wheelReleases, [&](const WheelRelease& release) {
        if (release.at > now) {
            return false;
        }
        due.push_back(release);
        return true;
    });
    for (const WheelRelease& release : due) {
        deliverToControl(release.control, release.slot, TriggerSignal::Deactivate, now);
    }
}

void MappingEngine::lockAction(Switch action, Microseconds now)
{
    const bool locked = switched(action, m_locked);
    if (locked && !m_hasCamera) {
        m_listener.notice(EngineNotice::NoCamera);
        return;
    }
    applyLock(locked, now);
}

void MappingEngine::applyLock(bool locked, Microseconds now)
{
    if (m_locked == locked) {
        return;
    }
    m_locked = locked;
    endUnclaimed(now);
    if (!locked) {
        for (std::size_t index = 0; index < m_controls.size(); ++index) {
            if (m_controls[index].camera) {
                resetComponent(index);
            }
        }
    }
    m_listener.pointerState(locked ? PointerState::Captured : PointerState::Free);
}

void MappingEngine::endUnclaimed(Microseconds now)
{
    std::vector<std::size_t> ended;
    for (Pressed& pressed : m_pressed) {
        Pressed keep;
        for (const std::size_t index : pressed) {
            if (mouseClaimed(m_entries[index])) {
                keep.push_back(index);
            } else {
                ended.push_back(index);
            }
        }
        pressed = keep;
    }
    for (const std::size_t index : ended) {
        notePressed(index, false);
    }
    for (const std::size_t index : ended) {
        if (takeQuiet(index)) {
            continue;
        }
        const auto* target = std::get_if<ControlTarget>(&m_entries[index].target);
        if (target != nullptr && std::holds_alternative<BehaviorComponent>(m_controls[target->control].component)) {
            static_cast<void>(routeInput(index, ModuleInput::KeyUp, {}, now));
            continue;
        }
        const TriggerSignal released = m_entries[index].trigger.release(now);
        if (released == TriggerSignal::Deactivate) {
            deliver(index, released, now);
        }
    }
}

void MappingEngine::syncPeek(Microseconds now)
{
    bool wanted = false;
    if (m_locked) {
        for (const ControlState& control : m_controls) {
            const auto* peek = std::get_if<PeekComponent>(&control.component);
            if (peek != nullptr && control.live && peek->held()) {
                wanted = true;
                break;
            }
        }
    }
    if (wanted != m_peeking) {
        m_peeking = wanted;
        endUnclaimed(now);
        if (wanted) {
            for (std::size_t index = 0; index < m_controls.size(); ++index) {
                if (m_controls[index].camera) {
                    resetComponent(index);
                }
            }
        }
        m_listener.peekChanged(wanted);
    }
    for (ControlState& control : m_controls) {
        if (auto* peek = std::get_if<PeekComponent>(&control.component)) {
            peek->setShowing(m_peeking && peek->held());
        }
    }
}

void MappingEngine::changeState(std::size_t index, Switch action, Microseconds now)
{
    const bool value = switched(action, m_stateValues[index]);
    if (m_stateValues[index] == value) {
        return;
    }
    m_stateValues[index] = value;
    refresh(now);
    m_listener.stateChanged(m_stateIds[index], value);
}

void MappingEngine::changeControl(std::size_t index, Switch action, Microseconds now)
{
    ControlState& control = m_controls[index];
    const bool enabled = switched(action, control.enabled);
    if (control.enabled == enabled) {
        return;
    }
    control.enabled = enabled;
    refresh(now);
    m_listener.controlEnabledChanged(m_controls[index].id, enabled);
}

void MappingEngine::refresh(Microseconds now)
{
    bool changed = false;
    for (std::size_t index = 0; index < m_controls.size(); ++index) {
        ControlState& control = m_controls[index];
        const bool live = control.enabled && holds(control.when);
        const bool lost = control.live && !live;
        control.live = live;
        if (m_live[index] != live) {
            m_live[index] = live;
            changed = true;
        }
        if (lost) {
            for (const std::size_t entry : control.entries) {
                releaseEntry(entry, now, false);
            }
            resetComponent(index);
        }
    }
    if (changed && m_compiled) {
        m_listener.liveChanged(m_live);
    }
}

void MappingEngine::releaseEntry(std::size_t entry, Microseconds now, bool deliverRelease)
{
    Pressed& pressed = m_pressed[denseIndex(m_entries[entry].binding.input->source)];
    Pressed keep;
    bool found = false;
    for (const std::size_t index : pressed) {
        if (index == entry) {
            found = true;
        } else {
            keep.push_back(index);
        }
    }
    pressed = keep;
    if (found) {
        notePressed(entry, false);
    }
    const bool quiet = takeQuiet(entry);
    if (found && deliverRelease && !quiet) {
        deliver(entry, m_entries[entry].trigger.release(now), now);
    }
    if (deliverRelease && !quiet && m_entries[entry].trigger.active()) {
        deliver(entry, TriggerSignal::Deactivate, now);
    }
    m_entries[entry].trigger.reset();
}

void MappingEngine::setCameraLock(Switch action, Microseconds now)
{
    m_now = now;
    lockAction(action, now);
    settle(now);
}

bool MappingEngine::setState(std::string_view id, Switch action, Microseconds now)
{
    const auto found = std::find(m_stateIds.begin(), m_stateIds.end(), id);
    if (found == m_stateIds.end()) {
        return false;
    }
    m_now = now;
    changeState(static_cast<std::size_t>(found - m_stateIds.begin()), action, now);
    settle(now);
    return true;
}

bool MappingEngine::setControlEnabled(std::string_view id, Switch action, Microseconds now)
{
    const std::optional<std::size_t> index = controlIndex(id);
    if (!index) {
        return false;
    }
    m_now = now;
    changeControl(*index, action, now);
    settle(now);
    return true;
}

bool MappingEngine::pressControl(std::string_view id, bool down, Microseconds now)
{
    const std::optional<std::size_t> index = controlIndex(id);
    if (!index) {
        return false;
    }
    m_now = now;
    const Slot slot = std::visit([](const auto& c) { return primarySlot<std::decay_t<decltype(c)>>(); },
                                 m_controls[*index].component);
    deliverToControl(*index, slot, down ? TriggerSignal::Activate : TriggerSignal::Deactivate, now);
    settle(now);
    return true;
}

bool MappingEngine::pressSlot(std::string_view id, Slot slot, bool down, Microseconds now)
{
    const std::optional<std::size_t> index = controlIndex(id);
    if (!index) {
        return false;
    }
    m_now = now;
    deliverToControl(*index, slot, down ? TriggerSignal::Activate : TriggerSignal::Deactivate, now);
    settle(now);
    return true;
}

bool MappingEngine::resetControl(std::string_view id, Microseconds now)
{
    const std::optional<std::size_t> index = controlIndex(id);
    if (!index) {
        return false;
    }
    m_now = now;
    ControlState& control = m_controls[*index];
    for (const std::size_t entry : control.entries) {
        releaseEntry(entry, now, false);
    }
    resetComponent(*index);
    settle(now);
    return true;
}

bool MappingEngine::pushControl(std::string_view id, Vec2 value, Microseconds now)
{
    const std::optional<std::size_t> index = controlIndex(id);
    if (!index) {
        return false;
    }
    m_now = now;
    if (m_controls[*index].live) {
        std::visit(
            [&](auto& c) {
                using C = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<C, JoystickComponent>) {
                    c.axis(Slot::Stick, value, now, outOf(*index));
                } else if constexpr (std::is_same_v<C, CameraComponent>) {
                    c.axis(Slot::Look, value, now, outOf(*index));
                }
            },
            m_controls[*index].component);
    }
    settle(now);
    return true;
}

bool MappingEngine::turnControl(std::string_view id, Vec2 delta, Microseconds now)
{
    const std::optional<std::size_t> index = controlIndex(id);
    if (!index) {
        return false;
    }
    m_now = now;
    if (m_controls[*index].live) {
        std::visit(
            [&](auto& c) {
                if constexpr (requires { c.motion(Slot::Look, delta.x, delta.y, now, m_out); }) {
                    c.motion(Slot::Look, delta.x, delta.y, now, outOf(*index));
                }
            },
            m_controls[*index].component);
    }
    settle(now);
    return true;
}

bool MappingEngine::walkControl(std::string_view id, Vec2 point, Microseconds now)
{
    const std::optional<std::size_t> index = controlIndex(id);
    if (!index) {
        return false;
    }
    m_now = now;
    if (auto* stick = std::get_if<JoystickComponent>(&m_controls[*index].component);
        stick != nullptr && m_controls[*index].live) {
        stick->walkToward(point, now, outOf(*index));
    }
    settle(now);
    return true;
}

std::optional<ControlStatus> MappingEngine::controlStatus(std::string_view id) const
{
    const std::optional<std::size_t> index = controlIndex(id);
    if (!index) {
        return std::nullopt;
    }
    return m_status[*index];
}

std::optional<bool> MappingEngine::controlEnabled(std::string_view id) const
{
    const std::optional<std::size_t> index = controlIndex(id);
    if (!index) {
        return std::nullopt;
    }
    return m_controls[*index].enabled;
}

std::optional<bool> MappingEngine::state(std::string_view id) const
{
    const auto found = std::find(m_stateIds.begin(), m_stateIds.end(), id);
    if (found == m_stateIds.end()) {
        return std::nullopt;
    }
    return m_stateValues[static_cast<std::size_t>(found - m_stateIds.begin())];
}

std::optional<bool> MappingEngine::controlLive(std::string_view id) const
{
    const auto found =
        std::find_if(m_controls.begin(), m_controls.end(), [&](const ControlState& c) { return c.id == id; });
    if (found == m_controls.end()) {
        return std::nullopt;
    }
    return found->live;
}

void MappingEngine::tick(Microseconds now)
{
    m_now = now;
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const auto due = m_entries[i].trigger.nextDeadline();
        if (due && *due <= now) {
            deliver(i, m_entries[i].trigger.tick(now), now);
        }
    }
    settle(now);
}

void MappingEngine::flush(Microseconds now)
{
    m_builder.flush(now, m_sink);
}

void MappingEngine::setRefreshHz(double hz) noexcept
{
    if (!(hz > 0.0)) {
        return;
    }
    m_config.refreshHz = hz;
    if (!m_config.pacingTick) {
        m_builder.setTick(defaultPacingTick(hz));
    }
}

void MappingEngine::cursor(CursorPhase phase, Vec2 position, Microseconds now)
{
    m_now = now;
    switch (phase) {
    case CursorPhase::Down:
        if (m_cursor) {
            m_out.move(*m_cursor, position);
        } else if (!captured() && pressEntries(mouseLeft(), now)) {
            m_cursorAt = position;
            settle(now);
            return;
        } else {
            m_cursor = m_out.down(position);
        }
        break;
    case CursorPhase::Move:
        if (m_cursor) {
            m_out.move(*m_cursor, position);
        }
        break;
    case CursorPhase::Up:
        if (m_cursor) {
            m_out.up(*std::exchange(m_cursor, std::nullopt), position);
        } else if (!captured()) {
            releaseEntries(mouseLeft(), now);
        }
        break;
    case CursorPhase::Cancel:
        if (m_cursor) {
            m_out.cancel(*std::exchange(m_cursor, std::nullopt), m_cursorAt);
        } else if (!captured()) {
            releaseEntries(mouseLeft(), now);
        }
        break;
    }
    if (phase != CursorPhase::Cancel) {
        m_cursorAt = position;
    }
    settle(now);
}

void MappingEngine::settle(Microseconds now)
{
    syncPeek(now);
    syncPauses(now);
    releaseWheelPresses(now);
    m_builder.flush(now, m_sink);
    for (int round = 0; round < 8; ++round) {
        bool ran = false;
        for (std::size_t index = 0; index < m_controls.size(); ++index) {
            if (detail::AttachmentChain* chain = m_controls[index].chain.get()) {
                if (const std::optional<Microseconds> due = chain->nextDeadline(); due && *due <= now) {
                    detail::AttachmentChain::Events delivered;
                    chain->tick(now, delivered);
                    for (const ModuleEvent& event : delivered) {
                        deliverLane(index, event, now);
                    }
                    ran = true;
                }
            }
            std::visit(
                [&](auto& c) {
                    const auto due = c.nextDeadline();
                    if (due && *due <= now) {
                        c.tick(now, outOf(index));
                        ran = true;
                    }
                },
                m_controls[index].component);
            tellTrouble(index);
        }
        if (!ran) {
            break;
        }
        syncPauses(now);
        m_builder.flush(now, m_sink);
    }
    bool changed = false;
    for (std::size_t index = 0; index < m_controls.size(); ++index) {
        const ControlStatus status =
            std::visit([](const auto& component) { return statusOf(component); }, m_controls[index].component);
        const auto* aim = std::get_if<AimComponent>(&m_controls[index].component);
        const bool aiming = aim != nullptr && aim->engaged();
        if (m_aiming[index] != aiming) {
            m_aiming[index] = aiming;
            changed = true;
        }
        if (m_status[index] != status) {
            m_status[index] = status;
            m_listener.statusChanged(m_controls[index].id, status);
            if (std::holds_alternative<ActionWheelComponent>(m_controls[index].component)) {
                m_listener.wheelChanged(m_controls[index].id, status.touching, status.cell);
            }
        }
    }
    if (changed) {
        m_listener.aimingChanged(m_aiming);
    }
}

void MappingEngine::resetAll(Microseconds now)
{
    m_now = now;
    m_modifiers = 0;
    for (Pressed& pressed : m_pressed) {
        const Pressed held = std::exchange(pressed, Pressed{});
        for (const std::size_t index : held) {
            notePressed(index, false);
        }
    }
    for (Entry& entry : m_entries) {
        entry.trigger.reset();
        entry.quiet = false;
    }
    for (std::size_t index = 0; index < m_controls.size(); ++index) {
        resetComponent(index);
    }
    m_wheelReleases.clear();
    if (m_cursor) {
        m_out.cancel(*std::exchange(m_cursor, std::nullopt), m_cursorAt);
    }
    settle(now);
}

std::optional<Microseconds> MappingEngine::nextWakeup() const
{
    std::optional<Microseconds> earliest;
    const auto consider = [&](std::optional<Microseconds> due) {
        if (due && (!earliest || *due < *earliest)) {
            earliest = due;
        }
    };
    for (const Entry& entry : m_entries) {
        consider(entry.trigger.nextDeadline());
    }
    for (const ControlState& control : m_controls) {
        std::visit([&](const auto& c) { consider(c.nextDeadline()); }, control.component);
        if (control.chain) {
            consider(control.chain->nextDeadline());
        }
    }
    for (const WheelRelease& release : m_wheelReleases) {
        consider(release.at);
    }
    consider(m_builder.nextWakeup());
    return earliest;
}

}
