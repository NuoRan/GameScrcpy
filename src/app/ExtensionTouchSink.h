#pragma once

#include "ControlChannel.h"
#include "TouchMethods.h"

#include <gs/exthost/Module.h>
#include <gs/protocol/Messages.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>

namespace gs::app {

class TouchPort
{
public:
    virtual ~TouchPort() = default;

    [[nodiscard]] virtual SinkCapabilities capabilities() const = 0;
    [[nodiscard]] virtual bool frame(const exthost::TouchFrame& frame) = 0;
    [[nodiscard]] virtual bool key(const protocol::Key& key) = 0;
    [[nodiscard]] virtual bool text(std::string_view) { return false; }
    [[nodiscard]] virtual bool mouse(const exthost::MouseEvent&) { return false; }
    [[nodiscard]] virtual bool gamepad(const exthost::PadEvent&) { return false; }
    [[nodiscard]] virtual bool working() const = 0;
    virtual void close() = 0;
};

[[nodiscard]] exthost::PadEvent padEventOf(const input::PadPassState& state) noexcept;

class ModuleTouchPort final : public TouchPort
{
public:
    ModuleTouchPort(std::shared_ptr<exthost::Module> module, std::size_t method, std::string_view device,
                    std::string_view options = {});
    ~ModuleTouchPort() override;
    ModuleTouchPort(const ModuleTouchPort&) = delete;
    ModuleTouchPort& operator=(const ModuleTouchPort&) = delete;

    [[nodiscard]] SinkCapabilities capabilities() const override { return m_capabilities; }
    [[nodiscard]] bool frame(const exthost::TouchFrame& frame) override;
    [[nodiscard]] bool key(const protocol::Key& key) override;
    [[nodiscard]] bool text(std::string_view utf8) override;
    [[nodiscard]] bool mouse(const exthost::MouseEvent& mouse) override;
    [[nodiscard]] bool gamepad(const exthost::PadEvent& pad) override;
    [[nodiscard]] bool working() const override;
    void close() override;

    void lose() noexcept { m_lost.store(true, std::memory_order_release); }
    [[nodiscard]] std::uint64_t session() const noexcept { return m_session; }

private:
    std::shared_ptr<exthost::Module> m_module;
    SinkCapabilities m_capabilities;
    bool m_typing = false;
    bool m_mouse = false;
    std::uint64_t m_session = 0;
    std::atomic<bool> m_lost{false};
    std::atomic<bool> m_closed{false};
};

class ExtensionTouchSink final : public ExtensionTouch
{
public:
    ExtensionTouchSink(ControlSink& injection, std::shared_ptr<TouchPort> port, std::function<void()> fellBack);
    ~ExtensionTouchSink() override;
    ExtensionTouchSink(const ExtensionTouchSink&) = delete;
    ExtensionTouchSink& operator=(const ExtensionTouchSink&) = delete;

    [[nodiscard]] bool send(const protocol::TouchFrame& frame) override;
    [[nodiscard]] bool send(const protocol::Key& key) override;
    [[nodiscard]] bool send(const protocol::Scroll& scroll) override;
    [[nodiscard]] bool send(const protocol::Text& text) override;
    [[nodiscard]] SinkCapabilities capabilities() const override;
    void detached() override;
    [[nodiscard]] bool padExternal(const input::PadPassState& state) override;
    void setVideo(std::uint16_t session, int rotation) override;
    [[nodiscard]] bool fellBack() const override;
    [[nodiscard]] bool mouse(const exthost::MouseEvent& mouse);

    [[nodiscard]] int ignoredContacts() const;

private:
    struct Slot {
        bool held = false;
        std::uint8_t pointerId = 0;
        std::uint16_t x = 0;
        std::uint16_t y = 0;
    };

    [[nodiscard]] exthost::Contact contactOf(std::size_t slot, exthost::TouchAction action, std::uint16_t x,
                                             std::uint16_t y, double pressure) const;
    [[nodiscard]] bool injectAfterFallback(const protocol::TouchFrame& frame);
    void fallBack();
    void liftAll();

    mutable std::mutex m_mutex;
    ControlSink& m_injection;
    std::shared_ptr<TouchPort> m_port;
    std::function<void()> m_fellBack;
    SinkCapabilities m_capabilities;
    std::array<Slot, exthost::kMostContacts> m_slots{};
    std::uint16_t m_videoSession = 0;
    int m_rotation = 0;
    bool m_fallen = false;
    bool m_closed = false;
    std::uint32_t m_injected = 0;
    int m_ignored = 0;
};

}
