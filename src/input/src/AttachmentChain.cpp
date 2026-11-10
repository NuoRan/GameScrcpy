#include "AttachmentChain.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gs::input::detail {

namespace {

constexpr Microseconds kSoonestWake{1'000};

Vec2 onPicture(Vec2 position) noexcept
{
    return {std::clamp(position.x, 0.0, 1.0), std::clamp(position.y, 0.0, 1.0)};
}

}

AttachmentChain::AttachmentChain(PointerAllocator& pointers, TouchFrameBuilder& frames, const Microseconds& clock,
                                 ModuleFactory& factory, std::string control, std::size_t inputs)
    : m_pointers(pointers)
    , m_frames(frames)
    , m_clock(clock)
    , m_factory(factory)
    , m_control(std::move(control))
    , m_inputs(inputs)
    , m_out(pointers, frames, this)
{
}

void AttachmentChain::add(std::string module, std::unique_ptr<AttachmentInstance> instance)
{
    if (instance != nullptr && m_links.size() < kMostAttachments) {
        m_links.push_back({.module = std::move(module), .instance = std::move(instance), .wake = std::nullopt});
    }
}

void AttachmentChain::input(const ModuleEvent& event, Events& delivered)
{
    if (usable(event)) {
        pass({event}, 0, delivered);
    }
}

void AttachmentChain::tick(Microseconds now, Events& delivered)
{
    for (std::size_t index = 0; index < m_links.size(); ++index) {
        Link& link = m_links[index];
        if (!link.wake || *link.wake > now) {
            continue;
        }
        link.wake.reset();
        ModuleOutput out;
        const ModuleStatus status = link.instance->wake(now, out);
        if (status != ModuleStatus::Ok) {
            if (status == ModuleStatus::Failed) {
                failed(link);
            }
            forget(link);
            replay(now);
            continue;
        }
        link.wake = out.wakeAt;
        if (link.wake && *link.wake <= now) {
            link.wake = now + kSoonestWake;
        }
        Events events;
        for (const ModuleEvent& made : out.events) {
            if (usable(made)) {
                events.push_back(made);
            }
        }
        std::vector<FingerCommand> commands;
        for (const FingerCommand& made : out.commands) {
            if (usable(made)) {
                commands.push_back(made);
            }
        }
        if (!events.empty()) {
            pass(std::move(events), index + 1, delivered);
        }
        if (!commands.empty()) {
            dispatch(std::move(commands), index + 1);
        }
    }
    while (!m_held.empty() && m_held.front().at <= now) {
        const FingerCommand command = m_held.front();
        m_held.erase(m_held.begin());
        apply(command);
    }
}

std::optional<Microseconds> AttachmentChain::nextDeadline() const noexcept
{
    std::optional<Microseconds> earliest;
    for (const Link& link : m_links) {
        if (link.wake && (!earliest || *link.wake < *earliest)) {
            earliest = link.wake;
        }
    }
    if (!m_held.empty() && (!earliest || m_held.front().at < *earliest)) {
        earliest = m_held.front().at;
    }
    return earliest;
}

void AttachmentChain::reset()
{
    for (Finger& finger : m_fingers) {
        release(finger);
        finger = {};
    }
    m_held.clear();
    for (Link& link : m_links) {
        link.wake.reset();
        link.instance->reset();
    }
}

std::optional<std::uint8_t> AttachmentChain::down(Vec2 position)
{
    for (std::size_t finger = 0; finger < m_fingers.size(); ++finger) {
        if (busy(finger)) {
            continue;
        }
        m_fingers[finger].component = true;
        m_fingers[finger].wanted = position;
        dispatch({{.action = FingerAction::Down,
                   .finger = static_cast<std::uint32_t>(finger),
                   .position = position,
                   .at = m_clock}},
                 0);
        return static_cast<std::uint8_t>(finger);
    }
    return std::nullopt;
}

void AttachmentChain::move(std::uint8_t finger, Vec2 position)
{
    const std::size_t index = finger;
    if (index < m_fingers.size() && m_fingers[index].component) {
        m_fingers[index].wanted = position;
        dispatch({{.action = FingerAction::Move, .finger = finger, .position = position, .at = m_clock}}, 0);
    }
}

void AttachmentChain::up(std::uint8_t finger, Vec2 position)
{
    const std::size_t index = finger;
    if (index < m_fingers.size() && m_fingers[index].component) {
        m_fingers[index].component = false;
        dispatch({{.action = FingerAction::Up, .finger = finger, .position = position, .at = m_clock}}, 0);
    }
}

void AttachmentChain::cancel(std::uint8_t finger, Vec2)
{
    const std::size_t index = finger;
    if (index >= m_fingers.size()) {
        return;
    }
    std::erase_if(m_held, [index](const FingerCommand& command) { return command.finger == index; });
    release(m_fingers[index]);
    m_fingers[index].component = false;
}

void AttachmentChain::pass(Events batch, std::size_t from, Events& delivered)
{
    for (std::size_t index = from; index < m_links.size() && !batch.empty(); ++index) {
        Link& link = m_links[index];
        Events next;
        for (const ModuleEvent& event : batch) {
            ModuleOutput out;
            out.events.push_back(event);
            out.wakeAt = link.wake;
            const ModuleStatus status = link.instance->input(event, out);
            if (status != ModuleStatus::Ok) {
                if (status == ModuleStatus::Failed) {
                    failed(link);
                }
                forget(link);
                next.push_back(event);
                continue;
            }
            link.wake = out.wakeAt;
            for (const ModuleEvent& made : out.events) {
                if (usable(made) && next.size() < kMostModuleEvents) {
                    next.push_back(made);
                }
            }
        }
        batch = std::move(next);
    }
    delivered.insert(delivered.end(), batch.begin(), batch.end());
}

void AttachmentChain::dispatch(std::vector<FingerCommand> batch, std::size_t from)
{
    for (std::size_t index = from; index < m_links.size() && !batch.empty(); ++index) {
        Link& link = m_links[index];
        std::vector<FingerCommand> next;
        for (const FingerCommand& command : batch) {
            ModuleOutput out;
            out.commands.push_back(command);
            out.wakeAt = link.wake;
            const ModuleStatus status = link.instance->output(command, out);
            if (status != ModuleStatus::Ok) {
                if (status == ModuleStatus::Failed) {
                    failed(link);
                }
                forget(link);
                next.push_back(command);
                continue;
            }
            link.wake = out.wakeAt;
            for (const FingerCommand& made : out.commands) {
                if (usable(made) && next.size() < kMostModuleCommands) {
                    next.push_back(made);
                }
            }
        }
        batch = std::move(next);
    }
    for (const FingerCommand& command : batch) {
        schedule(command);
    }
}

void AttachmentChain::schedule(const FingerCommand& command)
{
    if (command.at <= m_clock) {
        apply(command);
        return;
    }
    m_held.insert(std::ranges::upper_bound(m_held, command.at, {}, &FingerCommand::at), command);
}

void AttachmentChain::apply(const FingerCommand& command)
{
    Finger& finger = m_fingers[command.finger];
    const Vec2 at = onPicture(command.position);
    switch (command.action) {
    case FingerAction::Down:
        if (finger.pointer) {
            m_frames.move(*finger.pointer, at);
        } else {
            finger.pointer = m_pointers.acquire();
            if (!finger.pointer) {
                return;
            }
            m_frames.down(*finger.pointer, at);
        }
        finger.at = at;
        break;
    case FingerAction::Move:
        if (finger.pointer) {
            m_frames.move(*finger.pointer, at);
            finger.at = at;
        }
        break;
    case FingerAction::Up:
        if (finger.pointer) {
            m_frames.up(*finger.pointer, at);
            m_pointers.release(*finger.pointer);
            finger.pointer.reset();
        }
        break;
    }
}

void AttachmentChain::release(Finger& finger)
{
    if (finger.pointer) {
        m_frames.cancel(*finger.pointer, finger.at);
        m_pointers.release(*finger.pointer);
        finger.pointer.reset();
    }
}

void AttachmentChain::failed(const Link& link)
{
    m_factory.trouble({.kind = ModuleTroubleKind::Failed, .module = link.module, .control = m_control});
}

void AttachmentChain::forget(Link& link)
{
    link.wake.reset();
    link.instance->reset();
}

void AttachmentChain::replay(Microseconds now)
{
    std::vector<FingerCommand> wanted;
    for (std::size_t finger = 0; finger < m_fingers.size(); ++finger) {
        const Finger& state = m_fingers[finger];
        if (state.component && state.pointer) {
            wanted.push_back({.action = FingerAction::Move,
                              .finger = static_cast<std::uint32_t>(finger),
                              .position = state.wanted,
                              .at = now});
        }
    }
    if (!wanted.empty()) {
        dispatch(std::move(wanted), 0);
    }
}

bool AttachmentChain::busy(std::size_t finger) const noexcept
{
    const Finger& state = m_fingers[finger];
    return state.component || state.pointer.has_value() ||
           std::ranges::any_of(m_held, [finger](const FingerCommand& command) { return command.finger == finger; });
}

bool AttachmentChain::usable(const ModuleEvent& event) const noexcept
{
    return event.slot < m_inputs && std::isfinite(event.x) && std::isfinite(event.y);
}

bool AttachmentChain::usable(const FingerCommand& command) noexcept
{
    return command.finger < kMostModuleFingers && std::isfinite(command.position.x) &&
           std::isfinite(command.position.y);
}

}
