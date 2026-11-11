#include "TransportPhones.h"

#include <gs/platform/Crypto.h>
#include <gs/protocol/Constants.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <format>
#include <span>
#include <utility>

namespace gs::app {

namespace {

constexpr std::chrono::milliseconds kOpenWait{20'000};

std::atomic<std::uint64_t> g_lastStream{0};

session::SessionFailure failure(session::FailureKind kind, std::string detail)
{
    session::SessionFailure result;
    result.kind = kind;
    result.detail = std::move(detail);
    return result;
}

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

session::StreamSend sendThrough(const std::weak_ptr<exthost::Module>& carrier, std::uint64_t stream,
                                std::span<const std::uint8_t> bytes)
{
    const std::shared_ptr<exthost::Module> alive = carrier.lock();
    if (alive == nullptr) {
        return session::StreamSend::Failed;
    }
    switch (alive->sendStream(stream, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()))) {
    case exthost::StreamSent::Taken:
        return session::StreamSend::Taken;
    case exthost::StreamSent::Busy:
        return session::StreamSend::Busy;
    case exthost::StreamSent::Failed:
        break;
    }
    return session::StreamSend::Failed;
}

class ExtensionPhoneStreams final : public session::PhoneStreams
{
public:
    ExtensionPhoneStreams(PhoneRoute route, std::shared_ptr<TransportChanges> changes,
                          std::shared_ptr<const session::ServerPackage> package)
        : m_route(std::move(route)), m_changes(std::move(changes)), m_package(std::move(package))
    {
    }

    Expected<std::unique_ptr<session::ByteStream>, session::SessionFailure> open(session::StreamChannel channel,
                                                                                const session::LaunchParams& params,
                                                                                std::chrono::milliseconds timeout,
                                                                                std::stop_token stop) override
    {
        m_seen = m_changes->generation(m_route.key);
        const std::shared_ptr<exthost::Module> module = m_route.module.lock();
        if (module == nullptr || !module->working()) {
            return makeUnexpected(failure(session::FailureKind::ServerEnded,
                                          std::format("{} is off, so the phone cannot be reached", m_route.through)));
        }
        const std::uint64_t id = ++g_lastStream;
        const std::weak_ptr<exthost::Module> carrier = module;
        auto stream = std::make_shared<session::TransportStream>(
            [carrier, id](std::span<const std::uint8_t> bytes) { return sendThrough(carrier, id, bytes); },
            [carrier, id] {
                if (const std::shared_ptr<exthost::Module> alive = carrier.lock()) {
                    alive->closeStream(id);
                }
            });
        const std::weak_ptr<session::TransportStream> carried = stream;
        exthost::StreamEvents events;
        events.opened = [carried] {
            if (const std::shared_ptr<session::TransportStream> alive = carried.lock()) {
                alive->opened();
            }
        };
        events.data = [carried](std::string_view bytes) {
            if (const std::shared_ptr<session::TransportStream> alive = carried.lock()) {
                alive->deliver(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
            }
        };
        events.writable = [carried] {
            if (const std::shared_ptr<session::TransportStream> alive = carried.lock()) {
                alive->writable();
            }
        };
        events.closed = [carried](std::string reason) {
            if (const std::shared_ptr<session::TransportStream> alive = carried.lock()) {
                alive->closed(std::move(reason));
            }
        };
        const exthost::StreamRequest request{.phone = m_route.phone,
                                             .channel = std::string(session::streamChannelName(channel)),
                                             .session = streamSession(params, m_package.get())};
        if (!module->openStream(m_route.transport, id, request, std::move(events))) {
            return makeUnexpected(failure(session::FailureKind::ConnectFailed,
                                          std::format("{} did not start a connection to the phone", m_route.through)));
        }
        return session::awaitStream(std::move(stream), m_route.through, std::max(timeout, kOpenWait), stop);
    }

    Expected<void, session::SessionFailure> awaitChange(std::chrono::steady_clock::time_point deadline,
                                                        std::stop_token stop) override
    {
        if (!m_changes->waitPast(m_route.key, m_seen, deadline, stop)) {
            return makeUnexpected(failure(session::FailureKind::ConnectionLost,
                                          std::format("the phone did not come back through {}", m_route.through)));
        }
        const std::shared_ptr<exthost::Module> module = m_route.module.lock();
        if (module == nullptr || !module->working()) {
            return makeUnexpected(failure(session::FailureKind::ServerEnded,
                                          std::format("{} stopped, so the phone cannot be reached", m_route.through)));
        }
        return {};
    }

private:
    PhoneRoute m_route;
    std::shared_ptr<TransportChanges> m_changes;
    std::shared_ptr<const session::ServerPackage> m_package;
    std::uint64_t m_seen = 0;
};

}

void TransportChanges::bump(std::string_view key)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (const auto found = m_generations.find(key); found != m_generations.end()) {
            ++found->second;
        } else {
            m_generations.emplace(std::string(key), 1);
        }
    }
    m_changed.notify_all();
}

std::uint64_t TransportChanges::generation(std::string_view key) const
{
    const std::scoped_lock lock(m_mutex);
    const auto found = m_generations.find(key);
    return found == m_generations.end() ? 0 : found->second;
}

bool TransportChanges::waitPast(std::string_view key, std::uint64_t seen, std::chrono::steady_clock::time_point deadline,
                                const std::stop_token& stop)
{
    std::unique_lock lock(m_mutex);
    return m_changed.wait_until(lock, stop, deadline, [&] {
        const auto found = m_generations.find(key);
        return (found == m_generations.end() ? 0 : found->second) != seen;
    });
}

std::string transportKey(std::string_view extension, std::string_view transport)
{
    return std::string(extension) + "/" + std::string(transport);
}

std::string streamSession(const session::LaunchParams& params, const session::ServerPackage* package)
{
    nlohmann::json server = nullptr;
    if (package != nullptr) {
        server = nlohmann::json{{"file", utf8(package->file())},
                                {"path", package->remotePath()},
                                {"command", session::serverCommand(*package, params)},
                                {"stdin", session::serverInput(params)}};
    }
    return nlohmann::json{{"session", params.socketName},
                          {"token", platform::toHex(params.token)},
                          {"protocol", protocol::kProtocolVersion},
                          {"socket", params.socketName},
                          {"lingerMs", params.linger.count()},
                          {"log", params.logLevel},
                          {"server", std::move(server)}}
        .dump();
}

std::shared_ptr<session::PhoneStreams> makePhoneStreams(PhoneRoute route, std::shared_ptr<TransportChanges> changes,
                                                        std::shared_ptr<const session::ServerPackage> package)
{
    return std::make_shared<ExtensionPhoneStreams>(std::move(route), std::move(changes), std::move(package));
}

}
