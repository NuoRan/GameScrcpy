#pragma once

#include "CompanionCipher.h"
#include "CompanionLink.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gs::app {

inline constexpr std::size_t kHttpHeadLimit = 8 * 1024;
inline constexpr std::string_view kMdnsService = "_gs-companion._tcp";

struct HttpRequest {
    std::string method;
    std::string target;
    bool chinese = false;
};

enum class HttpRoute : std::uint8_t { Page, Apk, NotFound };

[[nodiscard]] std::optional<HttpRequest> parseHttpRequest(std::string_view head);
[[nodiscard]] HttpRoute routeOf(const HttpRequest& request, std::string_view token);
[[nodiscard]] std::string companionPage(std::string_view token, bool chinese);
[[nodiscard]] std::vector<std::uint8_t> httpResponse(const HttpRequest& request, const std::vector<std::string>& tokens,
                                                     const CompanionApk* apk);
[[nodiscard]] std::string mdnsInstanceName(const LanId& pc);
[[nodiscard]] std::string lanAddress();

struct LanExpectation {
    std::string serial;
    std::string token;
    LanKey qrKey{};
    std::optional<LanPairing> paired;
    bool announce = false;
};

struct LanArrival {
    std::string serial;
    std::optional<LanPairing> paired;
    std::unique_ptr<CompanionChannel> channel;
    protocol::CompanionWelcome welcome;
};

class CompanionLan final
{
public:
    using Arrived = std::function<void(LanArrival arrival)>;

    CompanionLan(std::shared_ptr<const CompanionApk> apk, Arrived arrived, std::uint16_t port = kLanPort,
                 bool loopbackOnly = false);
    ~CompanionLan();
    CompanionLan(const CompanionLan&) = delete;
    CompanionLan& operator=(const CompanionLan&) = delete;

    [[nodiscard]] bool listening() const noexcept { return m_listener != kNoSocket; }
    [[nodiscard]] std::uint16_t port() const noexcept { return m_port; }
    void expect(std::vector<LanExpectation> expectations);

private:
    static constexpr std::uintptr_t kNoSocket = ~std::uintptr_t{0};

    class Announcer;
    struct Slot;

    void acceptLoop();
    void serve(Slot& slot);

    const std::shared_ptr<const CompanionApk> m_apk;
    const Arrived m_arrived;
    const bool m_loopbackOnly;
    std::uintptr_t m_listener = kNoSocket;
    std::uint16_t m_port = 0;
    std::atomic<bool> m_closing{false};
    std::mutex m_mutex;
    std::vector<LanExpectation> m_expected;
    std::vector<std::shared_ptr<Slot>> m_slots;
    std::unique_ptr<Announcer> m_announcer;
    std::jthread m_acceptor;
};

}
