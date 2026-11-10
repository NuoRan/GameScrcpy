#include "Wire.h"

#include <algorithm>
#include <utility>

namespace gs::exthost::detail {

namespace {

constexpr std::size_t kMostNameBytes = 64;
constexpr std::uint32_t kLastType = static_cast<std::uint32_t>(ValueType::Json);

}

void WireWriter::text(std::string_view value)
{
    u32(static_cast<std::uint32_t>(value.size()));
    m_bytes.append(value);
}

void WireWriter::value(const Value& value)
{
    u32(static_cast<std::uint32_t>(value.type));
    u32(value.flag ? 1U : 0U);
    f64(value.number);
    f64(value.x);
    f64(value.y);
    f64(value.width);
    f64(value.height);
    text(value.text);
}

void WireWriter::contact(const Contact& contact)
{
    u32(contact.id);
    u32(static_cast<std::uint32_t>(contact.action));
    f64(contact.x);
    f64(contact.y);
    f64(contact.naturalX);
    f64(contact.naturalY);
    f64(contact.pressure);
}

void WireWriter::info(const ModuleInfo& info)
{
    u32(static_cast<std::uint32_t>(info.touchMethods.size()));
    for (const TouchMethodInfo& method : info.touchMethods) {
        text(method.name);
        u32(static_cast<std::uint32_t>(method.maxContacts));
        u32(method.canCancel ? 1U : 0U);
        u32(method.needsSession ? 1U : 0U);
        u32(method.keys ? 1U : 0U);
        u32(method.text ? 1U : 0U);
        u32(method.mouse ? 1U : 0U);
        u32(method.gamepad ? 1U : 0U);
    }
    u32(static_cast<std::uint32_t>(info.nodes.size()));
    for (const NodeInfo& node : info.nodes) {
        text(node.name);
        u32(static_cast<std::uint32_t>(node.params.size()));
        for (const ValueType type : node.params) {
            u32(static_cast<std::uint32_t>(type));
        }
        u32(static_cast<std::uint32_t>(node.returns));
    }
    u32(static_cast<std::uint32_t>(info.attachments.size()));
    for (const AttachmentInfo& attachment : info.attachments) {
        text(attachment.name);
    }
    u32(static_cast<std::uint32_t>(info.behaviors.size()));
    for (const BehaviorInfo& behavior : info.behaviors) {
        text(behavior.name);
        u32(static_cast<std::uint32_t>(behavior.slotKinds.size()));
        for (const SlotKind slot : behavior.slotKinds) {
            u32(static_cast<std::uint32_t>(slot));
        }
        u32(static_cast<std::uint32_t>(behavior.fingers));
        u32(behavior.frames ? 1U : 0U);
    }
    u32(static_cast<std::uint32_t>(info.jobs.size()));
    for (const JobInfo& job : info.jobs) {
        text(job.name);
    }
}

void WireWriter::event(const InputEvent& event)
{
    u32(static_cast<std::uint32_t>(event.kind));
    u32(event.slot);
    f64(event.x);
    f64(event.y);
    u64(event.at);
}

void WireWriter::command(const TouchCommand& command)
{
    u32(static_cast<std::uint32_t>(command.kind));
    u32(command.finger);
    f64(command.x);
    f64(command.y);
    u64(command.at);
}

void WireWriter::answer(const StepOutput& answer)
{
    u32(static_cast<std::uint32_t>(answer.status));
    u64(answer.wakeAt);
    const std::size_t commands = std::min(answer.commands.size(), kMostStepCommands);
    u32(static_cast<std::uint32_t>(commands));
    for (std::size_t index = 0; index < commands; ++index) {
        command(answer.commands[index]);
    }
    const std::size_t events = std::min(answer.events.size(), kMostStepEvents);
    u32(static_cast<std::uint32_t>(events));
    for (std::size_t index = 0; index < events; ++index) {
        event(answer.events[index]);
    }
}

bool WireReader::text(std::string& out, std::size_t most)
{
    std::uint32_t size = 0;
    if (!u32(size) || size > most || m_bytes.size() - m_at < size) {
        return false;
    }
    out.assign(m_bytes.substr(m_at, size));
    m_at += size;
    return true;
}

bool WireReader::value(Value& out)
{
    std::uint32_t type = 0;
    std::uint32_t flag = 0;
    if (!u32(type) || type > kLastType || !u32(flag)) {
        return false;
    }
    out.type = static_cast<ValueType>(type);
    out.flag = flag != 0;
    return f64(out.number) && f64(out.x) && f64(out.y) && f64(out.width) && f64(out.height) &&
           text(out.text, kMostTextBytes);
}

bool WireReader::contact(Contact& out)
{
    std::uint32_t action = 0;
    if (!u32(out.id) || !u32(action) || action > static_cast<std::uint32_t>(TouchAction::Cancel)) {
        return false;
    }
    out.action = static_cast<TouchAction>(action);
    return f64(out.x) && f64(out.y) && f64(out.naturalX) && f64(out.naturalY) && f64(out.pressure);
}

bool WireReader::info(ModuleInfo& out)
{
    std::uint32_t methods = 0;
    if (!u32(methods) || methods > kMostTouchMethods) {
        return false;
    }
    for (std::uint32_t index = 0; index < methods; ++index) {
        TouchMethodInfo method;
        std::uint32_t contacts = 0;
        std::uint32_t cancel = 0;
        std::uint32_t session = 0;
        std::uint32_t keys = 0;
        std::uint32_t typing = 0;
        std::uint32_t mouse = 0;
        std::uint32_t pad = 0;
        if (!text(method.name, kMostNameBytes) || !u32(contacts) || contacts < 1 || contacts > kMostContacts ||
            !u32(cancel) || !u32(session) || !u32(keys) || !u32(typing) || !u32(mouse) || !u32(pad)) {
            return false;
        }
        method.maxContacts = static_cast<int>(contacts);
        method.canCancel = cancel != 0;
        method.needsSession = session != 0;
        method.keys = keys != 0;
        method.text = typing != 0;
        method.mouse = mouse != 0;
        method.gamepad = pad != 0;
        out.touchMethods.push_back(std::move(method));
    }
    std::uint32_t nodes = 0;
    if (!u32(nodes) || nodes > kMostNodes) {
        return false;
    }
    for (std::uint32_t index = 0; index < nodes; ++index) {
        NodeInfo node;
        std::uint32_t params = 0;
        if (!text(node.name, kMostNameBytes) || !u32(params) || params > kMostParams) {
            return false;
        }
        for (std::uint32_t param = 0; param < params; ++param) {
            std::uint32_t type = 0;
            if (!u32(type) || type < 1 || type > kLastType) {
                return false;
            }
            node.params.push_back(static_cast<ValueType>(type));
        }
        std::uint32_t returns = 0;
        if (!u32(returns) || returns > kLastType) {
            return false;
        }
        node.returns = static_cast<ValueType>(returns);
        out.nodes.push_back(std::move(node));
    }
    std::uint32_t attachments = 0;
    if (!u32(attachments) || attachments > kMostModuleAttachments) {
        return false;
    }
    for (std::uint32_t index = 0; index < attachments; ++index) {
        AttachmentInfo attachment;
        if (!text(attachment.name, kMostNameBytes)) {
            return false;
        }
        out.attachments.push_back(std::move(attachment));
    }
    std::uint32_t behaviors = 0;
    if (!u32(behaviors) || behaviors > kMostModuleBehaviors) {
        return false;
    }
    for (std::uint32_t index = 0; index < behaviors; ++index) {
        BehaviorInfo behavior;
        std::uint32_t slotCount = 0;
        if (!text(behavior.name, kMostNameBytes) || !u32(slotCount) || slotCount > kMostBehaviorSlots) {
            return false;
        }
        for (std::uint32_t slot = 0; slot < slotCount; ++slot) {
            std::uint32_t kind = 0;
            if (!u32(kind) || kind > static_cast<std::uint32_t>(SlotKind::Axis1)) {
                return false;
            }
            behavior.slotKinds.push_back(static_cast<SlotKind>(kind));
        }
        std::uint32_t fingers = 0;
        std::uint32_t frames = 0;
        if (!u32(fingers) || fingers < 1 || fingers > kMostFingers || !u32(frames)) {
            return false;
        }
        behavior.fingers = static_cast<int>(fingers);
        behavior.frames = frames != 0;
        out.behaviors.push_back(std::move(behavior));
    }
    std::uint32_t jobs = 0;
    if (!u32(jobs) || jobs > kMostJobs) {
        return false;
    }
    for (std::uint32_t index = 0; index < jobs; ++index) {
        JobInfo job;
        if (!text(job.name, kMostNameBytes)) {
            return false;
        }
        out.jobs.push_back(std::move(job));
    }
    return true;
}

bool WireReader::event(InputEvent& out)
{
    std::uint32_t kind = 0;
    if (!u32(kind) || kind > static_cast<std::uint32_t>(InputKind::Wheel)) {
        return false;
    }
    out.kind = static_cast<InputKind>(kind);
    return u32(out.slot) && f64(out.x) && f64(out.y) && u64(out.at);
}

bool WireReader::command(TouchCommand& out)
{
    std::uint32_t kind = 0;
    if (!u32(kind) || kind > static_cast<std::uint32_t>(CommandKind::Up)) {
        return false;
    }
    out.kind = static_cast<CommandKind>(kind);
    return u32(out.finger) && f64(out.x) && f64(out.y) && u64(out.at);
}

bool WireReader::answer(StepOutput& out)
{
    std::uint32_t status = 0;
    std::uint32_t commands = 0;
    if (!u32(status) || status > static_cast<std::uint32_t>(StepStatus::Failed) || !u64(out.wakeAt) ||
        !u32(commands) || commands > kMostStepCommands) {
        return false;
    }
    out.status = static_cast<StepStatus>(status);
    for (std::uint32_t index = 0; index < commands; ++index) {
        TouchCommand made;
        if (!command(made)) {
            return false;
        }
        out.commands.push_back(made);
    }
    std::uint32_t events = 0;
    if (!u32(events) || events > kMostStepEvents) {
        return false;
    }
    for (std::uint32_t index = 0; index < events; ++index) {
        InputEvent made;
        if (!event(made)) {
            return false;
        }
        out.events.push_back(made);
    }
    return true;
}

}
