#pragma once

#include "gs/adb/AdbClient.h"
#include "gs/platform/UdpSocket.h"
#include "gs/session/ServerHost.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::session {

class ServerPackage
{
public:
    [[nodiscard]] static std::shared_ptr<const ServerPackage> load(const std::filesystem::path& path);

    explicit ServerPackage(std::vector<std::uint8_t> bytes, std::filesystem::path file = {});

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return m_bytes; }
    [[nodiscard]] const std::string& remotePath() const noexcept { return m_remotePath; }
    [[nodiscard]] const std::filesystem::path& file() const noexcept { return m_file; }

private:
    std::vector<std::uint8_t> m_bytes;
    std::string m_remotePath;
    std::filesystem::path m_file;
};

[[nodiscard]] std::string serverCommand(const ServerPackage& package, const LaunchParams& params,
                                        std::string_view extra = {});
[[nodiscard]] std::string serverInput(const LaunchParams& params);

enum class TransportChoice : std::uint8_t {
    AdbTunnel,
    WifiDirect,
    Auto,
};

struct DirectRoute {
    std::uint16_t devicePort = 0;
    std::optional<platform::UdpEndpoint> endpoint;
};

class AdbServerHost final : public ServerHost
{
public:
    static constexpr int kMinimumSdk = 28;

    AdbServerHost(adb::AdbClient client, std::string serial, std::shared_ptr<const ServerPackage> package,
                  TransportChoice choice = TransportChoice::AdbTunnel, DirectRoute direct = {});
    ~AdbServerHost() override;

    AdbServerHost(const AdbServerHost&) = delete;
    AdbServerHost& operator=(const AdbServerHost&) = delete;

    [[nodiscard]] Expected<void, SessionFailure> start(const LaunchParams& params, Events& events,
                                                       std::stop_token stop) override;
    [[nodiscard]] Expected<platform::TcpStream, SessionFailure> connect() override;
    [[nodiscard]] std::unique_ptr<Transport> transport(std::chrono::milliseconds handshakeTimeout) override;
    [[nodiscard]] Expected<void, SessionFailure> restore(bool afterFailure, std::chrono::steady_clock::time_point deadline,
                                                         std::stop_token stop) override;
    void stop(std::chrono::milliseconds grace) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}
