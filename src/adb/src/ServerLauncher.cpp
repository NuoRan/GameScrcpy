#include "gs/adb/ServerLauncher.h"

#include "HostProtocol.h"

#include <windows.h>

#include <format>
#include <string>

namespace gs::adb {

namespace {

AdbResult<int> checkVersion(int version)
{
    if (version < AdbClient::kMinimumServerVersion) {
        return detail::failure(AdbErrorKind::ServerTooOld,
                               std::format("adb server version {} is older than the required {}", version,
                                           AdbClient::kMinimumServerVersion));
    }
    return version;
}

AdbResult<void> startServer(const std::filesystem::path& adbExecutable, std::uint16_t port,
                            std::chrono::milliseconds timeout)
{
    std::wstring commandLine = std::format(L"\"{}\" -P {} start-server", adbExecutable.native(), port);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(adbExecutable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return detail::failure(AdbErrorKind::LaunchFailed,
                               std::format("cannot run the bundled adb (error {})", GetLastError()));
    }
    CloseHandle(process.hThread);
    const DWORD waited = WaitForSingleObject(process.hProcess, static_cast<DWORD>(timeout.count()));
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    if (waited != WAIT_OBJECT_0) {
        return detail::failure(AdbErrorKind::LaunchFailed, "adb start-server did not finish in time");
    }
    if (exitCode != 0) {
        return detail::failure(AdbErrorKind::LaunchFailed, std::format("adb start-server exited with {}", exitCode));
    }
    return {};
}

}

AdbResult<int> ensureServer(const AdbClient& client, const std::filesystem::path& adbExecutable,
                            std::chrono::milliseconds timeout)
{
    AdbResult<int> running = client.serverVersion();
    if (running) {
        return checkVersion(*running);
    }
    if (running.error().kind != AdbErrorKind::ServerUnavailable) {
        return running;
    }
    if (AdbResult<void> started = startServer(adbExecutable, client.port(), timeout); !started) {
        return makeUnexpected(std::move(started.error()));
    }
    AdbResult<int> version = client.serverVersion();
    if (!version) {
        return version;
    }
    return checkVersion(*version);
}

}
