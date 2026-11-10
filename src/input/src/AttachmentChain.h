#pragma once

#include "gs/input/Controls.h"
#include "gs/input/Modules.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gs::input::detail {

class AttachmentChain final : public TouchRoute
{
public:
    using Events = std::vector<ModuleEvent>;

    AttachmentChain(PointerAllocator& pointers, TouchFrameBuilder& frames, const Microseconds& clock,
                    ModuleFactory& factory, std::string control, std::size_t inputs);
    AttachmentChain(const AttachmentChain&) = delete;
    AttachmentChain& operator=(const AttachmentChain&) = delete;

    void add(std::string module, std::unique_ptr<AttachmentInstance> instance);
    [[nodiscard]] bool empty() const noexcept { return m_links.empty(); }
    [[nodiscard]] TouchOutput& output() noexcept { return m_out; }

    void input(const ModuleEvent& event, Events& delivered);
    void tick(Microseconds now, Events& delivered);
    [[nodiscard]] std::optional<Microseconds> nextDeadline() const noexcept;
    void reset();

    [[nodiscard]] std::optional<std::uint8_t> down(Vec2 position) override;
    void move(std::uint8_t finger, Vec2 position) override;
    void up(std::uint8_t finger, Vec2 position) override;
    void cancel(std::uint8_t finger, Vec2 position) override;

private:
    struct Link {
        std::string module;
        std::unique_ptr<AttachmentInstance> instance;
        std::optional<Microseconds> wake;
    };

    struct Finger {
        bool component = false;
        std::optional<std::uint8_t> pointer;
        Vec2 at;
        Vec2 wanted;
    };

    void pass(Events batch, std::size_t from, Events& delivered);
    void dispatch(std::vector<FingerCommand> batch, std::size_t from);
    void schedule(const FingerCommand& command);
    void apply(const FingerCommand& command);
    void release(Finger& finger);
    void failed(const Link& link);
    static void forget(Link& link);
    void replay(Microseconds now);
    [[nodiscard]] bool busy(std::size_t finger) const noexcept;
    [[nodiscard]] bool usable(const ModuleEvent& event) const noexcept;
    [[nodiscard]] static bool usable(const FingerCommand& command) noexcept;

    PointerAllocator& m_pointers;
    TouchFrameBuilder& m_frames;
    const Microseconds& m_clock;
    ModuleFactory& m_factory;
    std::string m_control;
    std::size_t m_inputs = 0;
    TouchOutput m_out;
    std::vector<Link> m_links;
    std::array<Finger, kMostModuleFingers> m_fingers{};
    std::vector<FingerCommand> m_held;
};

}
