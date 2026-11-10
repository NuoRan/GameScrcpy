#include "McpRelay.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gs::cli {

namespace {

using Json = nlohmann::json;

constexpr int kSuccess = 0;
constexpr int kProblems = 1;
constexpr int kUsage = 2;
constexpr DWORD kChunkBytes = 64 * 1024;
constexpr DWORD kBusyWaitMs = 2000;
constexpr int kBusyAttempts = 5;
constexpr std::size_t kMostLineBytes = std::size_t{32} << 20;
constexpr int kRefused = -32001;
constexpr std::string_view kOffText =
    "GameScrcpy is not accepting AI connections. Open GameScrcpy, turn on Settings > AI connection (MCP) > Allow AI "
    "to connect through MCP, then start this server again.";

struct Shared {
    std::mutex mutex;
    long long outstanding = 0;
    bool inputEnded = false;
};

class Event
{
public:
    Event()
        : m_handle(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
    }
    ~Event()
    {
        if (m_handle != nullptr) {
            CloseHandle(m_handle);
        }
    }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    [[nodiscard]] HANDLE handle() const noexcept { return m_handle; }

private:
    HANDLE m_handle;
};

std::wstring pipePath()
{
    std::array<wchar_t, 256> name{};
    const DWORD length = GetEnvironmentVariableW(L"GS_MCP_PIPE", name.data(), static_cast<DWORD>(name.size()));
    const std::wstring chosen =
        length > 0 && length < name.size() ? std::wstring(name.data(), length) : std::wstring(L"GameScrcpy-MCP");
    return L"\\\\.\\pipe\\" + chosen;
}

HANDLE connectTo(const std::wstring& path)
{
    for (int attempt = 0; attempt < kBusyAttempts; ++attempt) {
        const HANDLE pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            return pipe;
        }
        if (GetLastError() != ERROR_PIPE_BUSY || WaitNamedPipeW(path.c_str(), kBusyWaitMs) == FALSE) {
            break;
        }
    }
    return INVALID_HANDLE_VALUE;
}

DWORD chunkOf(std::size_t size)
{
    return static_cast<DWORD>(std::min<std::size_t>(size, kChunkBytes));
}

bool writeAll(HANDLE handle, const char* data, std::size_t size)
{
    while (size > 0) {
        DWORD written = 0;
        if (WriteFile(handle, data, chunkOf(size), &written, nullptr) == FALSE || written == 0) {
            return false;
        }
        data += written;
        size -= written;
    }
    return true;
}

bool writePipe(HANDLE pipe, const Event& event, const char* data, std::size_t size)
{
    while (size > 0) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = event.handle();
        DWORD written = 0;
        if (WriteFile(pipe, data, chunkOf(size), nullptr, &overlapped) == FALSE && GetLastError() != ERROR_IO_PENDING) {
            return false;
        }
        if (GetOverlappedResult(pipe, &overlapped, &written, TRUE) == FALSE || written == 0) {
            return false;
        }
        data += written;
        size -= written;
    }
    return true;
}

long long expectedReplies(std::string_view line)
{
    const Json message = Json::parse(line, nullptr, false);
    if (!message.is_object() || !message.contains("method")) {
        return 0;
    }
    if (message.contains("id")) {
        return 1;
    }
    const Json& method = message["method"];
    return method.is_string() && method.get_ref<const std::string&>() == "notifications/cancelled" ? -1 : 0;
}

class Replies
{
public:
    long long count(const char* data, std::size_t size)
    {
        long long answered = 0;
        for (std::size_t index = 0; index < size; ++index) {
            const char c = data[index];
            if (c != '\n') {
                if (m_head.size() < kSniffBytes) {
                    m_head += c;
                } else {
                    m_long = true;
                }
                continue;
            }
            answered += (m_long || answers(m_head)) ? 1 : 0;
            m_head.clear();
            m_long = false;
        }
        return answered;
    }

private:
    static constexpr std::size_t kSniffBytes = 4096;

    static bool answers(std::string_view line)
    {
        const Json message = Json::parse(line, nullptr, false);
        return !message.is_object() || !message.contains("method");
    }

    std::string m_head;
    bool m_long = false;
};

void reportOff()
{
    std::cerr << "gs mcp: " << kOffText << '\n';
}

int refuse()
{
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    std::string buffer;
    std::vector<char> chunk(kChunkBytes);
    for (;;) {
        for (std::size_t newline = buffer.find('\n'); newline != std::string::npos; newline = buffer.find('\n')) {
            const Json message = Json::parse(std::string_view(buffer).substr(0, newline), nullptr, false);
            buffer.erase(0, newline + 1);
            if (message.is_object() && message.contains("method") && message.contains("id")) {
                const Json reply = {{"jsonrpc", "2.0"},
                                    {"id", message.at("id")},
                                    {"error", {{"code", kRefused}, {"message", kOffText}}}};
                const std::string text = reply.dump(-1, ' ', false, Json::error_handler_t::replace) + "\n";
                static_cast<void>(writeAll(output, text.data(), text.size()));
                reportOff();
                return kProblems;
            }
        }
        DWORD read = 0;
        if (buffer.size() > kMostLineBytes ||
            ReadFile(input, chunk.data(), kChunkBytes, &read, nullptr) == FALSE || read == 0) {
            reportOff();
            return kProblems;
        }
        buffer.append(chunk.data(), read);
    }
}

void forward(HANDLE pipe, std::shared_ptr<Shared> shared)
{
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const Event event;
    std::vector<char> chunk(kChunkBytes);
    std::string line;
    for (;;) {
        DWORD read = 0;
        if (ReadFile(input, chunk.data(), kChunkBytes, &read, nullptr) == FALSE || read == 0) {
            break;
        }
        long long expected = 0;
        for (DWORD index = 0; index < read; ++index) {
            const char c = chunk[index];
            if (c != '\n') {
                if (line.size() <= kMostLineBytes) {
                    line += c;
                }
                continue;
            }
            expected += expectedReplies(line);
            line.clear();
        }
        {
            const std::scoped_lock lock(shared->mutex);
            shared->outstanding += expected;
        }
        if (!writePipe(pipe, event, chunk.data(), read)) {
            break;
        }
    }
    bool done = false;
    {
        const std::scoped_lock lock(shared->mutex);
        shared->inputEnded = true;
        done = shared->outstanding <= 0;
    }
    if (done) {
        CancelIoEx(pipe, nullptr);
    }
}

int relay(HANDLE pipe)
{
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    auto shared = std::make_shared<Shared>();
    std::thread(forward, pipe, shared).detach();
    const Event event;
    std::vector<char> chunk(kChunkBytes);
    Replies replies;
    for (;;) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = event.handle();
        DWORD read = 0;
        if (ReadFile(pipe, chunk.data(), kChunkBytes, nullptr, &overlapped) == FALSE && GetLastError() != ERROR_IO_PENDING) {
            break;
        }
        if (GetOverlappedResult(pipe, &overlapped, &read, TRUE) == FALSE || read == 0) {
            break;
        }
        if (!writeAll(output, chunk.data(), read)) {
            break;
        }
        const long long answered = replies.count(chunk.data(), read);
        const std::scoped_lock lock(shared->mutex);
        shared->outstanding -= answered;
        if (shared->inputEnded && shared->outstanding <= 0) {
            return kSuccess;
        }
    }
    const std::scoped_lock lock(shared->mutex);
    if (shared->inputEnded) {
        return kSuccess;
    }
    std::cerr << "gs mcp: GameScrcpy closed the connection\n";
    return kProblems;
}

}

int runMcpRelay(const std::vector<std::wstring_view>& args)
{
    if (!args.empty()) {
        std::cerr << "usage: gs mcp\n";
        return kUsage;
    }
    const HANDLE pipe = connectTo(pipePath());
    if (pipe == INVALID_HANDLE_VALUE) {
        return refuse();
    }
    return relay(pipe);
}

}
