#pragma once

#include "ControlChannel.h"
#include "HidTouchscreen.h"

#include <gs/protocol/Messages.h>
#include <gs/session/SessionState.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace gs::session {
class Session;
}

namespace gs::app {

inline constexpr std::uint16_t kUhidTouchId = 1;
inline constexpr std::uint16_t kUhidTouchVendor = 0x1209;
inline constexpr std::uint16_t kUhidTouchProduct = 0x0001;
inline constexpr std::string_view kUhidTouchName = "GameScrcpy Touch";

class UhidWire
{
public:
    virtual ~UhidWire() = default;

    [[nodiscard]] virtual bool send(const protocol::UhidCreate& create) = 0;
    [[nodiscard]] virtual bool send(const protocol::UhidInput& input) = 0;
    [[nodiscard]] virtual bool send(const protocol::UhidDestroy& destroy) = 0;
};

class SessionUhidWire final : public UhidWire
{
public:
    explicit SessionUhidWire(session::Session& session) noexcept
        : m_session(session)
    {
    }

    [[nodiscard]] bool send(const protocol::UhidCreate& create) override;
    [[nodiscard]] bool send(const protocol::UhidInput& input) override;
    [[nodiscard]] bool send(const protocol::UhidDestroy& destroy) override;

private:
    session::Session& m_session;
};

class UhidTouchSink final : public ControlSink
{
public:
    UhidTouchSink(ControlSink& injection, UhidWire& wire) noexcept;

    [[nodiscard]] bool send(const protocol::TouchFrame& frame) override;
    [[nodiscard]] bool send(const protocol::Key& key) override;
    [[nodiscard]] bool send(const protocol::Scroll& scroll) override;
    [[nodiscard]] bool send(const protocol::Text& text) override;
    [[nodiscard]] SinkCapabilities capabilities() const override;
    void detached() override;
    [[nodiscard]] bool padFeature(const protocol::UhidFeature& feature) override;
    [[nodiscard]] bool padCreate(const protocol::UhidCreate& create) override;
    [[nodiscard]] bool padInput(const protocol::UhidInput& input) override;
    [[nodiscard]] bool padDestroy(const protocol::UhidDestroy& destroy) override;

    void welcomed(std::uint32_t phone);
    void phaseChanged(session::SessionPhase phase);
    void setVideo(std::uint16_t session, int rotation);

    [[nodiscard]] bool usesUhid() const;
    [[nodiscard]] int ignoredContacts() const;

private:
    [[nodiscard]] bool inject(const protocol::TouchFrame& frame);
    [[nodiscard]] bool touch(const protocol::TouchFrame& frame);
    void create();
    void destroy();
    void forget();

    mutable std::mutex m_mutex;
    ControlSink& m_injection;
    UhidWire& m_wire;
    bool m_welcomed = false;
    bool m_offered = false;
    bool m_created = false;
    bool m_routed = false;
    bool m_detached = false;
    std::uint32_t m_injected = 0;
    TouchscreenEncoder m_encoder;
    std::uint16_t m_videoSession = 0;
    int m_rotation = 0;
};

}
