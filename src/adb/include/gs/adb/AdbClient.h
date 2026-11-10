#pragma once

#include "gs/adb/DeviceList.h"
#include "gs/adb/ForwardList.h"
#include "gs/adb/ShellProtocol.h"
#include "gs/adb/WirelessServices.h"
#include "gs/base/Expected.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace gs::platform {
class TcpStream;
}

namespace gs::adb {

enum class AdbErrorKind : std::uint8_t {
    ServerUnavailable,
    ServerTooOld,
    Rejected,
    Protocol,
    Io,
    LaunchFailed,
};

struct AdbError {
    AdbErrorKind kind = AdbErrorKind::Io;
    std::string message;
};

template <typename T>
using AdbResult = Expected<T, AdbError>;

struct RemoteFileStat {
    std::uint32_t mode = 0;
    std::uint32_t size = 0;
    std::uint32_t mtime = 0;
};

using TransferProgress = std::function<void(std::uint64_t done, std::uint64_t total)>;

struct RemoteEntry {
    std::string name;
    bool directory = false;
    std::uint64_t size = 0;
    std::int64_t modifiedSeconds = 0;
    std::uint32_t mode = 0;
};

class ShellProcess
{
public:
    ShellProcess(ShellProcess&&) noexcept;
    ShellProcess& operator=(ShellProcess&&) noexcept;
    ~ShellProcess();

    [[nodiscard]] bool writeStdin(std::span<const std::uint8_t> bytes);
    [[nodiscard]] bool closeStdin();

    [[nodiscard]] std::optional<ShellPacket> read();

    void interrupt() noexcept;

private:
    friend class AdbClient;
    explicit ShellProcess(std::unique_ptr<platform::TcpStream> stream);

    std::unique_ptr<platform::TcpStream> m_stream;
    ShellPacketParser m_parser;
};

class AdbClient
{
public:
    using TransferProgress = adb::TransferProgress;
    using RemoteEntry = adb::RemoteEntry;

    static constexpr std::uint16_t kDefaultPort = 5037;
    static constexpr int kMinimumServerVersion = 39;

    explicit AdbClient(std::uint16_t port = kDefaultPort) noexcept : m_port(port) {}

    [[nodiscard]] static std::uint16_t portFromSetting(std::string_view setting) noexcept;

    [[nodiscard]] std::uint16_t port() const noexcept { return m_port; }

    [[nodiscard]] AdbResult<int> serverVersion() const;
    [[nodiscard]] AdbResult<std::vector<DeviceEntry>> devices() const;
    [[nodiscard]] AdbResult<std::vector<std::string>> features(std::string_view serial) const;

    [[nodiscard]] AdbResult<std::string> connect(std::string_view address, std::stop_token stop = {}) const;
    [[nodiscard]] AdbResult<void> disconnect(std::string_view address) const;
    [[nodiscard]] AdbResult<std::string> pair(std::string_view address, std::string_view code,
                                              std::stop_token stop = {}) const;
    [[nodiscard]] AdbResult<std::vector<WirelessService>> wirelessServices() const;

    [[nodiscard]] AdbResult<ShellResult> shell(std::string_view serial, std::string_view command) const;
    [[nodiscard]] AdbResult<ShellProcess> spawn(std::string_view serial, std::string_view command) const;

    [[nodiscard]] AdbResult<void> push(std::string_view serial, std::span<const std::uint8_t> content,
                                       std::string_view remotePath, std::uint32_t permissions = 0644) const;
    [[nodiscard]] AdbResult<std::optional<RemoteFileStat>> stat(std::string_view serial,
                                                                std::string_view remotePath) const;

    [[nodiscard]] AdbResult<std::vector<RemoteEntry>> list(std::string_view serial, std::string_view remoteDir) const;
    [[nodiscard]] AdbResult<void> pushFile(std::string_view serial, const std::filesystem::path& local,
                                           std::string_view remote, TransferProgress progress = {},
                                           std::stop_token stop = {}) const;
    [[nodiscard]] AdbResult<void> pullFile(std::string_view serial, std::string_view remote,
                                           const std::filesystem::path& local, TransferProgress progress = {},
                                           std::stop_token stop = {}) const;
    [[nodiscard]] AdbResult<void> removePaths(std::string_view serial, std::span<const std::string> remotePaths) const;
    [[nodiscard]] AdbResult<void> renamePath(std::string_view serial, std::string_view from, std::string_view to) const;
    [[nodiscard]] AdbResult<void> makeFolders(std::string_view serial, std::span<const std::string> remoteDirs) const;

    [[nodiscard]] static std::string removeCommand(std::span<const std::string> remotePaths);
    [[nodiscard]] static std::string renameCommand(std::string_view from, std::string_view to);
    [[nodiscard]] static std::string makeFoldersCommand(std::span<const std::string> remoteDirs);

    [[nodiscard]] AdbResult<std::uint16_t> forward(std::string_view serial, std::string_view remote) const;
    [[nodiscard]] AdbResult<void> removeForward(std::string_view serial, std::uint16_t localPort) const;
    [[nodiscard]] AdbResult<void> reverse(std::string_view serial, std::string_view remote,
                                          std::uint16_t localPort) const;
    [[nodiscard]] AdbResult<void> removeReverse(std::string_view serial, std::string_view remote) const;
    [[nodiscard]] AdbResult<std::vector<ForwardEntry>> forwards() const;
    [[nodiscard]] AdbResult<std::vector<ForwardEntry>> reverses(std::string_view serial) const;

private:
    std::uint16_t m_port;
};

}
