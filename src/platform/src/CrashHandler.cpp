#include "gs/platform/CrashHandler.h"

#include <gs/base/BuildInfo.h>

#include <windows.h>

#include <dbghelp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string_view>
#include <system_error>

namespace gs::platform {

namespace {

constexpr DWORD kCppException = 0xE06D7363;
constexpr DWORD kTerminated = 0xE0475301;
constexpr DWORD kPureCall = 0xE0475302;
constexpr DWORD kInvalidParameter = 0xE0475303;
constexpr DWORD kAborted = 0xE0475304;
constexpr DWORD kWriteTimeoutMs = 60'000;
constexpr std::size_t kPathCapacity = 2048;
constexpr std::size_t kCopyChunk = std::size_t{1} << 16;

template <typename Char, std::size_t N>
class FixedText
{
public:
    bool add(std::basic_string_view<Char> text) noexcept
    {
        if (text.size() >= N - m_size) {
            m_full = true;
            return false;
        }
        std::ranges::copy(text, m_text.begin() + static_cast<std::ptrdiff_t>(m_size));
        m_size += text.size();
        m_text[m_size] = Char{};
        return true;
    }

    bool add(Char c) noexcept { return add(std::basic_string_view<Char>(&c, 1)); }

    bool number(std::uint64_t value, unsigned base, std::size_t width) noexcept
    {
        std::array<Char, 20> digits{};
        std::size_t count = 0;
        do {
            const auto digit = static_cast<unsigned>(value % base);
            digits[count++] = static_cast<Char>(digit < 10 ? '0' + digit : 'A' + digit - 10);
            value /= base;
        } while (value != 0);
        while (count < width && count < digits.size()) {
            digits[count++] = static_cast<Char>('0');
        }
        std::reverse(digits.begin(), digits.begin() + static_cast<std::ptrdiff_t>(count));
        return add(std::basic_string_view<Char>(digits.data(), count));
    }

    [[nodiscard]] bool ok() const noexcept { return !m_full; }
    [[nodiscard]] const Char* c_str() const noexcept { return m_text.data(); }
    [[nodiscard]] std::basic_string_view<Char> view() const noexcept { return {m_text.data(), m_size}; }

private:
    std::array<Char, N> m_text{};
    std::size_t m_size = 0;
    bool m_full = false;
};

using Path = FixedText<wchar_t, kPathCapacity>;
using Summary = FixedText<char, 1024>;

class FileHandle
{
public:
    explicit FileHandle(HANDLE handle) noexcept
        : m_handle(handle)
    {
    }
    ~FileHandle()
    {
        if (valid()) {
            ::CloseHandle(m_handle);
        }
    }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    [[nodiscard]] bool valid() const noexcept { return m_handle != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const noexcept { return m_handle; }

private:
    HANDLE m_handle;
};

struct Request {
    EXCEPTION_POINTERS* exception = nullptr;
    DWORD thread = 0;
};

Path g_directory;
Path g_log;
Path g_logName;
HANDLE g_requested = nullptr;
HANDLE g_written = nullptr;
Request g_request;
std::atomic<DWORD> g_writer{0};
std::atomic<DWORD> g_crashing{0};

FileHandle created(const Path& path) noexcept
{
    return FileHandle(
        ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
}

std::string_view exceptionName(DWORD code) noexcept
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "integer division by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_IN_PAGE_ERROR:
        return "in-page error";
    case EXCEPTION_BREAKPOINT:
        return "breakpoint";
    case kCppException:
        return "C++ exception";
    case kTerminated:
        return "std::terminate";
    case kPureCall:
        return "pure virtual call";
    case kInvalidParameter:
        return "invalid parameter";
    case kAborted:
        return "abort";
    default:
        return {};
    }
}

void writeDump(const Path& file, const Request& request) noexcept
{
    const FileHandle out = created(file);
    if (!out.valid()) {
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION exception{
        .ThreadId = request.thread, .ExceptionPointers = request.exception, .ClientPointers = FALSE};
    ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), out.get(),
                        static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                        request.exception != nullptr ? &exception : nullptr, nullptr, nullptr);
}

void copyLog(const Path& target) noexcept
{
    const FileHandle in(::CreateFileW(g_log.c_str(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!in.valid()) {
        return;
    }
    const FileHandle out = created(target);
    if (!out.valid()) {
        return;
    }
    std::array<char, kCopyChunk> chunk{};
    for (;;) {
        DWORD read = 0;
        if (::ReadFile(in.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr) == FALSE ||
            read == 0) {
            return;
        }
        DWORD written = 0;
        if (::WriteFile(out.get(), chunk.data(), read, &written, nullptr) == FALSE || written != read) {
            return;
        }
    }
}

void addModule(Summary& text, std::uintptr_t address) noexcept
{
    HMODULE module = nullptr;
    if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(address), &module) == FALSE) {
        return;
    }
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = ::GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return;
    }
    const std::wstring_view full(path.data(), length);
    const std::wstring_view name = full.substr(full.find_last_of(L"\\/") + 1);
    std::array<char, MAX_PATH * 3> utf8{};
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, nullptr);
    if (size <= 0) {
        return;
    }
    text.add(' ');
    text.add(std::string_view(utf8.data(), static_cast<std::size_t>(size)));
    text.add("+0x");
    text.number(address - reinterpret_cast<std::uintptr_t>(module), 16, 0);
}

void writeSummary(const Path& target, const SYSTEMTIME& time, const Request& request) noexcept
{
    Summary text;
    const BuildInfo build = buildInfo();
    text.add("version ");
    text.add(build.version);
    text.add(" (");
    text.add(build.revision);
    text.add(")\ntime ");
    text.number(time.wYear, 10, 4);
    text.add('-');
    text.number(time.wMonth, 10, 2);
    text.add('-');
    text.number(time.wDay, 10, 2);
    text.add(' ');
    text.number(time.wHour, 10, 2);
    text.add(':');
    text.number(time.wMinute, 10, 2);
    text.add(':');
    text.number(time.wSecond, 10, 2);
    text.add('.');
    text.number(time.wMilliseconds, 10, 3);
    text.add(" UTC\n");
    const EXCEPTION_RECORD* record = request.exception != nullptr ? request.exception->ExceptionRecord : nullptr;
    if (record != nullptr) {
        text.add("exception 0x");
        text.number(record->ExceptionCode, 16, 8);
        if (const std::string_view name = exceptionName(record->ExceptionCode); !name.empty()) {
            text.add(' ');
            text.add(name);
        }
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            const ULONG_PTR kind = record->ExceptionInformation[0];
            text.add(kind == 0 ? " reading 0x" : kind == 1 ? " writing 0x" : " executing 0x");
            text.number(record->ExceptionInformation[1], 16, 16);
        }
        const auto address = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
        text.add("\naddress 0x");
        text.number(address, 16, 16);
        addModule(text, address);
        text.add('\n');
    }
    text.add("thread ");
    text.number(request.thread, 10, 0);
    text.add('\n');
    const FileHandle out = created(target);
    if (out.valid()) {
        DWORD written = 0;
        ::WriteFile(out.get(), text.view().data(), static_cast<DWORD>(text.view().size()), &written, nullptr);
    }
}

void writeReport(const Request& request) noexcept
{
    SYSTEMTIME time{};
    ::GetSystemTime(&time);
    FixedText<wchar_t, 32> name;
    for (const char c : kCrashFolderPrefix) {
        name.add(static_cast<wchar_t>(c));
    }
    name.number(time.wYear, 10, 4);
    name.number(time.wMonth, 10, 2);
    name.number(time.wDay, 10, 2);
    name.add(L'-');
    name.number(time.wHour, 10, 2);
    name.number(time.wMinute, 10, 2);
    name.number(time.wSecond, 10, 2);
    name.add(L'-');
    name.number(time.wMilliseconds, 10, 3);

    Path folder;
    folder.add(g_directory.view());
    folder.add(L'\\');
    folder.add(name.view());
    if (!folder.ok() ||
        (::CreateDirectoryW(folder.c_str(), nullptr) == FALSE && ::GetLastError() != ERROR_ALREADY_EXISTS)) {
        return;
    }
    Path file = folder;
    file.add(L'\\');
    file.add(name.view());
    file.add(L".dmp");
    if (file.ok()) {
        writeDump(file, request);
    }
    file = folder;
    file.add(L'\\');
    file.add(g_logName.view());
    if (file.ok()) {
        copyLog(file);
    }
    file = folder;
    file.add(L"\\crash.txt");
    if (file.ok()) {
        writeSummary(file, time, request);
    }
}

DWORD WINAPI writeReports(void*)
{
    if (::WaitForSingleObject(g_requested, INFINITE) == WAIT_OBJECT_0) {
        writeReport(g_request);
    }
    ::SetEvent(g_written);
    return 0;
}

LONG WINAPI onException(EXCEPTION_POINTERS* exception)
{
    const DWORD self = ::GetCurrentThreadId();
    DWORD first = 0;
    if (!g_crashing.compare_exchange_strong(first, self)) {
        if (first != self && self != g_writer.load()) {
            ::SuspendThread(::GetCurrentThread());
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    g_request = Request{.exception = exception, .thread = self};
    ::SetEvent(g_requested);
    ::WaitForSingleObject(g_written, kWriteTimeoutMs);
    return EXCEPTION_CONTINUE_SEARCH;
}

[[noreturn]] void raiseCrash(DWORD code) noexcept
{
    ::RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    std::_Exit(EXIT_FAILURE);
}

void onTerminate()
{
    raiseCrash(kTerminated);
}

void __cdecl onPureCall()
{
    raiseCrash(kPureCall);
}

void __cdecl onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, std::uintptr_t)
{
    raiseCrash(kInvalidParameter);
}

void __cdecl onAbort(int)
{
    raiseCrash(kAborted);
}

}

bool installCrashHandler(const std::filesystem::path& directory, const std::filesystem::path& log)
{
    if (g_requested != nullptr) {
        return false;
    }
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::absolute(directory, error);
    if (!error) {
        std::filesystem::create_directories(folder, error);
    }
    if (error) {
        return false;
    }
    const std::filesystem::path logFile = std::filesystem::absolute(log, error);
    if (error || !g_directory.add(folder.native()) || !g_log.add(logFile.native()) ||
        !g_logName.add(logFile.filename().native())) {
        return false;
    }
    g_requested = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_written = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_requested == nullptr || g_written == nullptr) {
        return false;
    }
    DWORD writer = 0;
    const HANDLE thread = ::CreateThread(nullptr, 0, &writeReports, nullptr, 0, &writer);
    if (thread == nullptr) {
        return false;
    }
    ::SetThreadDescription(thread, L"gs-crash-report");
    ::CloseHandle(thread);
    g_writer.store(writer);
    ::SetUnhandledExceptionFilter(&onException);
    std::set_terminate(&onTerminate);
    ::_set_purecall_handler(&onPureCall);
    ::_set_invalid_parameter_handler(&onInvalidParameter);
    std::signal(SIGABRT, &onAbort);
    return true;
}

void crashOnPurpose() noexcept
{
    volatile int* nowhere = nullptr;
    *nowhere = 0;
    std::_Exit(EXIT_FAILURE);
}

}
