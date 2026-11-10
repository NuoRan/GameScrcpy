#pragma once

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gs::test {

class ChildProcess
{
public:
    ChildProcess(const std::filesystem::path& program, const std::vector<std::wstring>& arguments)
    {
        std::wstring commandLine = quoted(program.wstring());
        for (const std::wstring& argument : arguments) {
            commandLine += L' ';
            commandLine += quoted(argument);
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof startup;
        PROCESS_INFORMATION child{};
        if (::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &startup, &child) != FALSE) {
            ::CloseHandle(child.hThread);
            m_process = child.hProcess;
        }
    }

    ~ChildProcess()
    {
        kill();
        if (m_process != nullptr) {
            ::CloseHandle(m_process);
        }
    }

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    [[nodiscard]] bool started() const noexcept { return m_process != nullptr; }

    [[nodiscard]] bool running() const noexcept
    {
        return started() && ::WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT;
    }

    [[nodiscard]] bool waitFor(std::chrono::milliseconds limit) const noexcept
    {
        return started() && ::WaitForSingleObject(m_process, static_cast<DWORD>(limit.count())) == WAIT_OBJECT_0;
    }

    void kill() noexcept
    {
        if (running()) {
            ::TerminateProcess(m_process, 1);
        }
        if (started()) {
            ::WaitForSingleObject(m_process, INFINITE);
        }
    }

    [[nodiscard]] std::optional<DWORD> exitCode() const noexcept
    {
        DWORD code = 0;
        if (!started() || ::GetExitCodeProcess(m_process, &code) == FALSE || code == STILL_ACTIVE) {
            return std::nullopt;
        }
        return code;
    }

private:
    static std::wstring quoted(const std::wstring& text) { return L"\"" + text + L"\""; }

    HANDLE m_process = nullptr;
};

}
