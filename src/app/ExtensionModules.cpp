#include "ExtensionModules.h"

#include <gs/base/JsonNesting.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace gs::app {

static_assert(static_cast<int>(input::ModuleInput::KeyDown) == static_cast<int>(exthost::InputKind::KeyDown));
static_assert(static_cast<int>(input::ModuleInput::Axis2) == static_cast<int>(exthost::InputKind::Axis2));
static_assert(static_cast<int>(input::ModuleInput::Axis1) == static_cast<int>(exthost::InputKind::Axis1));
static_assert(static_cast<int>(input::ModuleInput::Wheel) == static_cast<int>(exthost::InputKind::Wheel));
static_assert(static_cast<int>(input::FingerAction::Move) == static_cast<int>(exthost::CommandKind::Move));
static_assert(static_cast<int>(input::FingerAction::Up) == static_cast<int>(exthost::CommandKind::Up));
static_assert(static_cast<int>(input::BehaviorSlotKind::Axis2) == static_cast<int>(exthost::SlotKind::Axis2));
static_assert(static_cast<int>(input::BehaviorSlotKind::Axis1) == static_cast<int>(exthost::SlotKind::Axis1));

namespace {

using Json = nlohmann::json;
using ModuleEvent = input::ModuleEvent;
using FingerCommand = input::FingerCommand;
using ModuleOutput = input::ModuleOutput;
using ModuleStatus = input::ModuleStatus;

std::uint64_t wireTime(Microseconds time) noexcept
{
    return time.count() > 0 ? static_cast<std::uint64_t>(time.count()) : 0;
}

Microseconds engineTime(std::uint64_t time) noexcept
{
    constexpr auto kLatest = static_cast<std::uint64_t>(std::numeric_limits<Microseconds::rep>::max());
    return Microseconds{static_cast<Microseconds::rep>(std::min(time, kLatest))};
}

exthost::InputEvent wireEvent(const ModuleEvent& event) noexcept
{
    return {.kind = static_cast<exthost::InputKind>(event.kind),
            .slot = event.slot,
            .x = event.x,
            .y = event.y,
            .at = wireTime(event.at)};
}

exthost::TouchCommand wireCommand(const FingerCommand& command) noexcept
{
    return {.kind = static_cast<exthost::CommandKind>(command.action),
            .finger = command.finger,
            .x = command.position.x,
            .y = command.position.y,
            .at = wireTime(command.at)};
}

ModuleStatus finish(const exthost::StepOutput& result, ModuleOutput& out)
{
    switch (result.status) {
    case exthost::StepStatus::Skipped:
        return ModuleStatus::Skipped;
    case exthost::StepStatus::Failed:
        return ModuleStatus::Failed;
    case exthost::StepStatus::Ok:
        break;
    }
    out.commands.clear();
    for (const exthost::TouchCommand& made : result.commands) {
        if (out.commands.full()) {
            break;
        }
        out.commands.push_back({.action = static_cast<input::FingerAction>(made.kind),
                                .finger = made.finger,
                                .position = {made.x, made.y},
                                .at = engineTime(made.at)});
    }
    out.events.clear();
    for (const exthost::InputEvent& made : result.events) {
        if (out.events.full()) {
            break;
        }
        out.events.push_back({.kind = static_cast<input::ModuleInput>(made.kind),
                              .slot = made.slot,
                              .x = made.x,
                              .y = made.y,
                              .at = engineTime(made.at)});
    }
    out.wakeAt = result.wakeAt != 0 ? std::optional<Microseconds>(engineTime(result.wakeAt)) : std::nullopt;
    out.fresh = result.fresh;
    return ModuleStatus::Ok;
}

class HostInstance
{
public:
    HostInstance(std::shared_ptr<exthost::Module> host, std::uint64_t id) noexcept
        : m_host(std::move(host))
        , m_id(id)
    {
    }

    ~HostInstance() { m_host->closeInstance(m_id); }

    HostInstance(const HostInstance&) = delete;
    HostInstance& operator=(const HostInstance&) = delete;

    ModuleStatus run(exthost::StepInput step, ModuleOutput& out)
    {
        step.wakeAt = out.wakeAt ? wireTime(*out.wakeAt) : 0;
        return finish(m_host->step(m_id, step), out);
    }

    void reset() { m_host->resetInstance(m_id); }

private:
    std::shared_ptr<exthost::Module> m_host;
    std::uint64_t m_id = 0;
};

class HostAttachment final : public input::AttachmentInstance
{
public:
    HostAttachment(std::shared_ptr<exthost::Module> host, std::uint64_t id)
        : m_instance(std::move(host), id)
    {
    }

    ModuleStatus input(const ModuleEvent& event, ModuleOutput& out) override
    {
        return m_instance.run(
            {.call = exthost::StepCall::Input, .event = wireEvent(event), .command = {}, .now = wireTime(event.at)},
            out);
    }

    ModuleStatus output(const FingerCommand& command, ModuleOutput& out) override
    {
        return m_instance.run({.call = exthost::StepCall::Output,
                               .event = {},
                               .command = wireCommand(command),
                               .now = wireTime(command.at)},
                              out);
    }

    ModuleStatus wake(Microseconds now, ModuleOutput& out) override
    {
        out.wakeAt.reset();
        return m_instance.run({.call = exthost::StepCall::Wake, .event = {}, .command = {}, .now = wireTime(now)}, out);
    }

    void reset() override { m_instance.reset(); }

private:
    HostInstance m_instance;
};

class HostBehavior final : public input::BehaviorInstance
{
public:
    HostBehavior(std::shared_ptr<exthost::Module> host, std::uint64_t id)
        : m_instance(std::move(host), id)
    {
    }

    ModuleStatus input(const ModuleEvent& event, ModuleOutput& out) override
    {
        return m_instance.run(
            {.call = exthost::StepCall::Input, .event = wireEvent(event), .command = {}, .now = wireTime(event.at)},
            out);
    }

    ModuleStatus frame(Microseconds now, ModuleOutput& out) override
    {
        return m_instance.run({.call = exthost::StepCall::Frame, .event = {}, .command = {}, .now = wireTime(now)},
                              out);
    }

    ModuleStatus wake(Microseconds now, ModuleOutput& out) override
    {
        out.wakeAt.reset();
        return m_instance.run({.call = exthost::StepCall::Wake, .event = {}, .command = {}, .now = wireTime(now)}, out);
    }

    void reset() override { m_instance.reset(); }

private:
    HostInstance m_instance;
};

}

bool moduleFits(const std::vector<std::string>& appliesTo, std::string_view type) noexcept
{
    return appliesTo.empty() ||
           std::ranges::any_of(appliesTo, [type](const std::string& each) { return each == "*" || each == type; });
}

std::string moduleParams(const std::vector<ext::DeclaredParam>& declared, std::string_view chosen,
                         input::Vec2 position, input::Vec2 unit)
{
    const Json given = chosen.empty() || !jsonNestingWithin(chosen, kMostJsonNesting)
                           ? Json::object()
                           : Json::parse(chosen, nullptr, false);
    Json params = Json::object();
    for (const ext::DeclaredParam& param : declared) {
        std::string value;
        if (given.is_object()) {
            if (const auto found = given.find(param.key); found != given.end()) {
                std::string text = found->dump(-1, ' ', false, Json::error_handler_t::replace);
                if (ext::declaredProblem(param, text).empty()) {
                    value = std::move(text);
                }
            }
        }
        if (value.empty()) {
            value = ext::declaredDefault(param);
        }
        Json parsed = Json::parse(value, nullptr, false);
        params[param.key] = parsed.is_discarded() ? Json() : std::move(parsed);
    }
    params["@position"] = Json::array({position.x, position.y});
    params["@unit"] = Json::array({unit.x, unit.y});
    return params.dump(-1, ' ', false, Json::error_handler_t::replace);
}

ExtensionModules::ExtensionModules(std::vector<ModuleEntry> entries, Notify notify,
                                   std::vector<input::GlobalAttachment> globals)
    : m_entries(std::move(entries))
    , m_notify(std::move(notify))
    , m_globals(std::move(globals))
{
}

const ModuleEntry* ExtensionModules::find(std::string_view module, exthost::InstanceKind kind) const
{
    const auto found = std::ranges::find_if(m_entries, [module, kind](const ModuleEntry& entry) {
        return entry.kind == kind && entry.module == module && entry.host != nullptr;
    });
    return found != m_entries.end() ? &*found : nullptr;
}

std::uint64_t ExtensionModules::open(const ModuleEntry& entry, const input::ModuleRequest& request)
{
    const std::string params = moduleParams(entry.params, request.params, request.position, request.unit);
    const std::uint64_t id = entry.host->openInstance(entry.kind, entry.index, params, request.seed);
    if (id == 0 && m_notify) {
        m_notify({.kind = input::ModuleTroubleKind::Missing,
                  .extension = entry.extension,
                  .module = entry.module,
                  .control = std::string(request.control)});
    }
    return id;
}

std::unique_ptr<input::AttachmentInstance> ExtensionModules::attachment(const input::ModuleRequest& request)
{
    const ModuleEntry* entry = find(request.module, exthost::InstanceKind::Attachment);
    if (entry == nullptr || !moduleFits(entry->appliesTo, request.type)) {
        return nullptr;
    }
    const std::uint64_t id = open(*entry, request);
    if (id == 0) {
        return nullptr;
    }
    return std::make_unique<HostAttachment>(entry->host, id);
}

std::unique_ptr<input::BehaviorInstance> ExtensionModules::behavior(const input::ModuleRequest& request,
                                                                    input::BehaviorShape& shape)
{
    const ModuleEntry* entry = find(request.module, exthost::InstanceKind::Behavior);
    if (entry == nullptr) {
        return nullptr;
    }
    const std::uint64_t id = open(*entry, request);
    if (id == 0) {
        return nullptr;
    }
    shape = entry->shape;
    return std::make_unique<HostBehavior>(entry->host, id);
}

void ExtensionModules::trouble(const input::ModuleTrouble& trouble)
{
    if (trouble.kind == input::ModuleTroubleKind::Missing || !m_notify) {
        return;
    }
    m_notify({.kind = trouble.kind,
              .extension = std::string(input::moduleExtension(trouble.module)),
              .module = std::string(trouble.module),
              .control = std::string(trouble.control)});
}

input::ModuleCatalog ExtensionModules::catalog() const
{
    input::ModuleCatalog catalog;
    for (const ModuleEntry& entry : m_entries) {
        if (entry.kind == exthost::InstanceKind::Attachment) {
            catalog.attachments.push_back({.module = entry.module, .appliesTo = entry.appliesTo});
        } else {
            catalog.behaviors.push_back({.module = entry.module, .shape = entry.shape});
        }
    }
    return catalog;
}

}
