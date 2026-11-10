#pragma once

#include "AoaTouchSink.h"
#include "ControlChannel.h"
#include "UhidTouchSink.h"

#include <gs/input/Geometry.h>
#include <gs/protocol/Messages.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace gs::test {

class SinkLog final : public app::ControlSink
{
public:
    bool send(const protocol::TouchFrame& frame) override
    {
        frames.push_back(frame);
        return true;
    }

    bool send(const protocol::Key& key) override
    {
        keys.push_back(key);
        return true;
    }

    bool send(const protocol::Scroll& scroll) override
    {
        scrolls.push_back(scroll);
        return true;
    }

    bool send(const protocol::Text& text) override
    {
        texts.push_back(text);
        return true;
    }

    void detached() override { ++detaches; }

    std::vector<protocol::TouchFrame> frames;
    std::vector<protocol::Key> keys;
    std::vector<protocol::Scroll> scrolls;
    std::vector<protocol::Text> texts;
    int detaches = 0;
};

using UhidMessage = std::variant<protocol::UhidCreate, protocol::UhidInput, protocol::UhidDestroy>;

class WireLog final : public app::UhidWire
{
public:
    bool send(const protocol::UhidCreate& create) override { return keep(create); }
    bool send(const protocol::UhidInput& input) override { return keep(input); }
    bool send(const protocol::UhidDestroy& destroy) override { return keep(destroy); }

    template <typename M>
    [[nodiscard]] std::vector<M> sent() const
    {
        std::vector<M> found;
        for (const UhidMessage& message : messages) {
            if (const auto* typed = std::get_if<M>(&message)) {
                found.push_back(*typed);
            }
        }
        return found;
    }

    std::vector<UhidMessage> messages;
    int refusals = 0;

private:
    bool keep(UhidMessage message)
    {
        if (refusals > 0) {
            --refusals;
            return false;
        }
        messages.push_back(std::move(message));
        return true;
    }
};

struct AoaTransfer {
    std::uint8_t request = 0;
    std::uint16_t value = 0;
    std::uint16_t index = 0;
    std::vector<std::uint8_t> data;
};

struct AoaBench {
    std::mutex mutex;
    std::condition_variable resumed;
    std::vector<AoaTransfer> transfers;
    std::optional<std::uint16_t> version = std::uint16_t{2};
    std::size_t packetSize = 64;
    bool gone = false;
    bool paused = false;

    [[nodiscard]] std::vector<AoaTransfer> taken()
    {
        const std::scoped_lock lock(mutex);
        return transfers;
    }

    [[nodiscard]] std::vector<AoaTransfer> of(std::uint8_t request, std::uint16_t id)
    {
        std::vector<AoaTransfer> found;
        for (const AoaTransfer& transfer : taken()) {
            if (transfer.request == request && transfer.value == id) {
                found.push_back(transfer);
            }
        }
        return found;
    }

    void pause(bool on)
    {
        {
            const std::scoped_lock lock(mutex);
            paused = on;
        }
        resumed.notify_all();
    }

    void unplug()
    {
        {
            const std::scoped_lock lock(mutex);
            gone = true;
        }
        resumed.notify_all();
    }
};

class FakeAoaLink final : public app::AoaLink
{
public:
    explicit FakeAoaLink(std::shared_ptr<AoaBench> bench) noexcept
        : m_bench(std::move(bench))
    {
    }

    [[nodiscard]] std::optional<std::uint16_t> protocol() override
    {
        const std::scoped_lock lock(m_bench->mutex);
        m_bench->transfers.push_back({.request = app::aoa::kGetProtocol});
        return m_bench->gone ? std::nullopt : m_bench->version;
    }

    [[nodiscard]] bool send(std::uint8_t request, std::uint16_t value, std::uint16_t index,
                            std::span<const std::uint8_t> data) override
    {
        std::unique_lock lock(m_bench->mutex);
        m_bench->resumed.wait(lock, [this] { return !m_bench->paused || m_bench->gone; });
        if (m_bench->gone) {
            return false;
        }
        m_bench->transfers.push_back(
            {.request = request, .value = value, .index = index, .data = {data.begin(), data.end()}});
        return true;
    }

    [[nodiscard]] std::size_t packetSize() const override { return m_bench->packetSize; }

    [[nodiscard]] bool gone() const override
    {
        const std::scoped_lock lock(m_bench->mutex);
        return m_bench->gone;
    }

private:
    std::shared_ptr<AoaBench> m_bench;
};

inline protocol::TouchPoint touchAt(protocol::TouchAction action, std::uint8_t pointerId, double x, double y)
{
    const bool lifting = action == protocol::TouchAction::Up || action == protocol::TouchAction::Cancel;
    return {.action = action,
            .pointerId = pointerId,
            .x = input::fixedCoordinate(x),
            .y = input::fixedCoordinate(y),
            .pressure = lifting ? std::uint16_t{0} : std::uint16_t{0xFFFF}};
}

inline protocol::TouchPoint pressAt(std::uint8_t pointerId, double x, double y)
{
    return touchAt(protocol::TouchAction::Down, pointerId, x, y);
}

inline protocol::TouchPoint moveTo(std::uint8_t pointerId, double x, double y)
{
    return touchAt(protocol::TouchAction::Move, pointerId, x, y);
}

inline protocol::TouchPoint liftAt(std::uint8_t pointerId, double x, double y)
{
    return touchAt(protocol::TouchAction::Up, pointerId, x, y);
}

inline protocol::TouchPoint cancelAt(std::uint8_t pointerId, double x, double y)
{
    return touchAt(protocol::TouchAction::Cancel, pointerId, x, y);
}

}
