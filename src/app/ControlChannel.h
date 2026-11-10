#pragma once

#include <gs/input/Geometry.h>
#include <gs/input/PadReports.h>
#include <gs/protocol/Messages.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

namespace gs::session {
class Session;
}

namespace gs::app {

enum class TouchBackend : std::uint8_t { Injection, Uhid, Aoa, None };

[[nodiscard]] constexpr TouchBackend touchBackendOf(TouchBackend chosen, std::optional<TouchBackend> forced) noexcept
{
    return forced.value_or(chosen);
}

struct SinkCapabilities {
    int maxContacts = static_cast<int>(protocol::TouchFrame::kMaxPoints);
    bool keys = true;
    bool cancel = true;
    bool gamepad = false;

    friend bool operator==(const SinkCapabilities&, const SinkCapabilities&) = default;
};

enum class PadRoute : std::uint8_t { None, Uhid, Aoa, Missing, External };

struct PadLink {
    PadRoute route = PadRoute::None;
    std::uint32_t epoch = 0;
    bool reports = false;

    friend bool operator==(const PadLink&, const PadLink&) = default;
};

class ControlSink
{
public:
    virtual ~ControlSink() = default;

    [[nodiscard]] virtual bool send(const protocol::TouchFrame& frame) = 0;
    [[nodiscard]] virtual bool send(const protocol::Key& key) = 0;
    [[nodiscard]] virtual bool send(const protocol::Scroll& scroll) = 0;
    [[nodiscard]] virtual bool send(const protocol::Text& text) = 0;
    [[nodiscard]] virtual SinkCapabilities capabilities() const { return {}; }
    virtual void detached() {}
    [[nodiscard]] virtual bool padFeature(const protocol::UhidFeature&) { return false; }
    [[nodiscard]] virtual bool padCreate(const protocol::UhidCreate&) { return false; }
    [[nodiscard]] virtual bool padInput(const protocol::UhidInput&) { return false; }
    [[nodiscard]] virtual bool padDestroy(const protocol::UhidDestroy&) { return false; }
    [[nodiscard]] virtual bool padExternal(const input::PadPassState&) { return false; }
};

class SessionControlSink final : public ControlSink
{
public:
    explicit SessionControlSink(session::Session& session) noexcept
        : m_session(session)
    {
    }

    [[nodiscard]] bool send(const protocol::TouchFrame& frame) override;
    [[nodiscard]] bool send(const protocol::Key& key) override;
    [[nodiscard]] bool send(const protocol::Scroll& scroll) override;
    [[nodiscard]] bool send(const protocol::Text& text) override;
    [[nodiscard]] bool padFeature(const protocol::UhidFeature& feature) override;
    [[nodiscard]] bool padCreate(const protocol::UhidCreate& create) override;
    [[nodiscard]] bool padInput(const protocol::UhidInput& input) override;
    [[nodiscard]] bool padDestroy(const protocol::UhidDestroy& destroy) override;

private:
    session::Session& m_session;
};

class NoTouchSink final : public ControlSink
{
public:
    explicit NoTouchSink(ControlSink& keys) noexcept
        : m_keys(keys)
    {
    }

    [[nodiscard]] bool send(const protocol::TouchFrame&) override { return true; }
    [[nodiscard]] bool send(const protocol::Key& key) override { return m_keys.send(key); }
    [[nodiscard]] bool send(const protocol::Scroll&) override { return true; }
    [[nodiscard]] bool send(const protocol::Text& text) override { return m_keys.send(text); }
    [[nodiscard]] SinkCapabilities capabilities() const override { return m_keys.capabilities(); }
    [[nodiscard]] bool padFeature(const protocol::UhidFeature& feature) override { return m_keys.padFeature(feature); }
    [[nodiscard]] bool padCreate(const protocol::UhidCreate& create) override { return m_keys.padCreate(create); }
    [[nodiscard]] bool padInput(const protocol::UhidInput& input) override { return m_keys.padInput(input); }
    [[nodiscard]] bool padDestroy(const protocol::UhidDestroy& destroy) override { return m_keys.padDestroy(destroy); }

private:
    ControlSink& m_keys;
};

struct InputScreen {
    std::uint16_t videoSession = 0;
    input::ScreenGeometry geometry;
    double refreshHz = 60.0;
};

class OperationObserver
{
public:
    virtual ~OperationObserver() = default;

    virtual void touched(const protocol::TouchFrame& frame) = 0;
    virtual void keyed(const protocol::Key& key) = 0;
    virtual void scrolled(const protocol::Scroll& scroll) = 0;
    virtual void typed(std::size_t characters) = 0;
};

class ControlChannel
{
public:
    using TouchObserver = std::function<void(const protocol::TouchFrame& frame, std::int64_t sentUs)>;

    void attach(ControlSink& sink);
    void detach();
    void setTouchObserver(TouchObserver observer);
    void setOperationObserver(std::shared_ptr<OperationObserver> observer);
    void setVideo(std::uint16_t videoSession, std::uint16_t width, std::uint16_t height);
    void setRefreshHz(double hz);

    [[nodiscard]] std::optional<InputScreen> screen() const;
    [[nodiscard]] SinkCapabilities capabilities() const;
    [[nodiscard]] std::uint64_t revision() const noexcept { return m_revision.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t pacing() const noexcept { return m_pacing.load(std::memory_order_acquire); }
    [[nodiscard]] double refreshHz() const;

    [[nodiscard]] bool send(const protocol::TouchFrame& frame);
    [[nodiscard]] bool send(const protocol::Key& key);
    [[nodiscard]] bool scroll(input::Vec2 position, std::int16_t horizontal, std::int16_t vertical);
    [[nodiscard]] bool type(std::string_view utf8);

    [[nodiscard]] bool setPadLink(PadLink link);
    [[nodiscard]] PadLink padLink() const;
    [[nodiscard]] bool padFeature(const protocol::UhidFeature& feature);
    [[nodiscard]] bool padCreate(const protocol::UhidCreate& create);
    [[nodiscard]] bool padInput(const protocol::UhidInput& input);
    [[nodiscard]] bool padDestroy(const protocol::UhidDestroy& destroy);
    [[nodiscard]] bool externalPad() const;
    [[nodiscard]] bool padExternal(const input::PadPassState& state);

private:
    void changed() noexcept { m_revision.fetch_add(1, std::memory_order_acq_rel); }

    mutable std::mutex m_mutex;
    ControlSink* m_sink = nullptr;
    TouchObserver m_touchObserver;
    std::shared_ptr<OperationObserver> m_operations;
    std::optional<InputScreen> m_screen;
    double m_refreshHz = 60.0;
    std::atomic<std::uint64_t> m_revision{1};
    std::atomic<std::uint64_t> m_pacing{1};
    PadLink m_padLink;
};

}
