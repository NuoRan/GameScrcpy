#include "gs/platform/AtomicFile.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <new>
#include <thread>
#include <vector>

namespace gs::platform {

namespace {

using namespace std::chrono_literals;

constexpr std::array<std::chrono::milliseconds, 5> kBusyWaits{50ms, 100ms, 150ms, 250ms, 450ms};

std::error_code lastError() noexcept
{
    return {static_cast<int>(::GetLastError()), std::system_category()};
}

std::error_code busy() noexcept
{
    return {ERROR_SHARING_VIOLATION, std::system_category()};
}

class FileHandle
{
public:
    explicit FileHandle(HANDLE handle) noexcept
        : m_handle(handle)
    {
    }
    ~FileHandle() { close(); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    [[nodiscard]] bool valid() const noexcept { return m_handle != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const noexcept { return m_handle; }

    bool close() noexcept
    {
        const bool closed = !valid() || ::CloseHandle(m_handle) != FALSE;
        m_handle = INVALID_HANDLE_VALUE;
        return closed;
    }

private:
    HANDLE m_handle;
};

std::error_code writeAll(HANDLE file, std::string_view contents) noexcept
{
    constexpr std::size_t kChunk = 1u << 20;
    while (!contents.empty()) {
        const auto size = static_cast<DWORD>(std::min(contents.size(), kChunk));
        DWORD written = 0;
        if (::WriteFile(file, contents.data(), size, &written, nullptr) == FALSE) {
            return lastError();
        }
        contents.remove_prefix(written);
    }
    return {};
}

bool renameUnsupported(const std::error_code& error) noexcept
{
    const auto code = static_cast<DWORD>(error.value());
    return code == ERROR_INVALID_PARAMETER || code == ERROR_NOT_SUPPORTED || code == ERROR_INVALID_FUNCTION;
}

std::error_code renameOver(HANDLE file, const std::filesystem::path& target)
{
    std::error_code error;
    const std::filesystem::path destination = std::filesystem::absolute(target, error);
    if (error) {
        return error;
    }
    const std::wstring& name = destination.native();
    const std::size_t bytes = name.size() * sizeof(wchar_t);
    std::vector<std::byte> buffer(offsetof(FILE_RENAME_INFO, FileName) + bytes + sizeof(wchar_t));
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(bytes);
    std::memcpy(info->FileName, name.data(), bytes);
    if (::SetFileInformationByHandle(file, FileRenameInfoEx, info, static_cast<DWORD>(buffer.size())) == FALSE) {
        return lastError();
    }
    return {};
}

std::error_code moveOver(const std::filesystem::path& from, const std::filesystem::path& to) noexcept
{
    if (::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
        return {};
    }
    const std::error_code error = lastError();
    if (error.value() == ERROR_ACCESS_DENIED) {
        const DWORD attributes = ::GetFileAttributesW(to.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) == 0) {
            return busy();
        }
    }
    return error;
}

std::error_code writeOnce(const std::filesystem::path& target, const std::filesystem::path& temporary,
                          std::string_view contents)
{
    FileHandle file(::CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) {
        return lastError();
    }
    std::error_code error = writeAll(file.get(), contents);
    if (!error && ::FlushFileBuffers(file.get()) == FALSE) {
        error = lastError();
    }
    bool moved = false;
    if (!error) {
        error = renameOver(file.get(), target);
        moved = !error;
        if (error && renameUnsupported(error)) {
            error = {};
        }
    }
    if (!file.close() && !error) {
        error = lastError();
    }
    if (!error && !moved) {
        error = moveOver(temporary, target);
    }
    if (error && !moved) {
        ::DeleteFileW(temporary.c_str());
    }
    return error;
}

std::error_code replaceOnce(const std::filesystem::path& from, const std::filesystem::path& to)
{
    FileHandle file(::CreateFileW(from.c_str(), GENERIC_WRITE | DELETE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) {
        return lastError();
    }
    if (::FlushFileBuffers(file.get()) == FALSE) {
        return lastError();
    }
    const std::error_code renamed = renameOver(file.get(), to);
    if (!renamed || !renameUnsupported(renamed)) {
        return renamed;
    }
    file.close();
    return moveOver(from, to);
}

std::error_code removeOnce(const std::filesystem::path& file)
{
    if (::DeleteFileW(file.c_str()) != FALSE) {
        return {};
    }
    const std::error_code error = lastError();
    if (error.value() == ERROR_FILE_NOT_FOUND || error.value() == ERROR_PATH_NOT_FOUND) {
        return {};
    }
    return error;
}

std::error_code readOnce(const std::filesystem::path& file, std::size_t mostBytes, std::string& contents)
{
    contents.clear();
    FileHandle handle(::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!handle.valid()) {
        return lastError();
    }
    LARGE_INTEGER size{};
    if (::GetFileSizeEx(handle.get(), &size) == FALSE) {
        return lastError();
    }
    if (size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) > mostBytes) {
        return std::make_error_code(std::errc::file_too_large);
    }
    contents.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t done = 0;
    while (done < contents.size()) {
        constexpr std::size_t kChunk = 1u << 20;
        const auto wanted = static_cast<DWORD>(std::min(contents.size() - done, kChunk));
        DWORD got = 0;
        if (::ReadFile(handle.get(), contents.data() + done, wanted, &got, nullptr) == FALSE) {
            return lastError();
        }
        if (got == 0) {
            break;
        }
        done += got;
    }
    contents.resize(done);
    return {};
}

template <typename Attempt>
std::error_code patiently(Attempt attempt)
{
    std::error_code error = attempt();
    for (const std::chrono::milliseconds wait : kBusyWaits) {
        if (!fileBusy(error)) {
            break;
        }
        std::this_thread::sleep_for(wait); // source-rule-allow R9: waiting for another program to let go of the file
        error = attempt();
    }
    return error;
}

}

bool fileBusy(const std::error_code& error) noexcept
{
    if (error.category() != std::system_category()) {
        return false;
    }
    const auto code = static_cast<DWORD>(error.value());
    return code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION;
}

bool fileMissing(const std::error_code& error) noexcept
{
    if (error.category() != std::system_category()) {
        return false;
    }
    const auto code = static_cast<DWORD>(error.value());
    return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
}

std::error_code readFile(const std::filesystem::path& file, std::size_t mostBytes, std::string& contents,
                         WhenBusy busy) noexcept
{
    try {
        if (busy == WhenBusy::GiveUp) {
            return readOnce(file, mostBytes, contents);
        }
        return patiently([&] { return readOnce(file, mostBytes, contents); });
    } catch (const std::bad_alloc&) {
        contents.clear();
        return std::make_error_code(std::errc::not_enough_memory);
    }
}

std::error_code writeFileAtomically(const std::filesystem::path& target, std::string_view contents) noexcept
{
    try {
        std::filesystem::path temporary = target;
        temporary += L".tmp";
        return patiently([&] { return writeOnce(target, temporary, contents); });
    } catch (const std::bad_alloc&) {
        return std::make_error_code(std::errc::not_enough_memory);
    }
}

std::error_code flushFile(const std::filesystem::path& file) noexcept
{
    FileHandle handle(::CreateFileW(file.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!handle.valid()) {
        return lastError();
    }
    if (::FlushFileBuffers(handle.get()) == FALSE) {
        return lastError();
    }
    return {};
}

std::error_code flushTree(const std::filesystem::path& directory) noexcept
{
    try {
        std::error_code error;
        for (auto at = std::filesystem::recursive_directory_iterator(directory, error);
             !error && at != std::filesystem::recursive_directory_iterator(); at.increment(error)) {
            std::error_code kind;
            if (!at->is_regular_file(kind)) {
                continue;
            }
            if (const std::error_code flushed = flushFile(at->path())) {
                return flushed;
            }
        }
        return error;
    } catch (const std::bad_alloc&) {
        return std::make_error_code(std::errc::not_enough_memory);
    }
}

std::error_code replaceFile(const std::filesystem::path& from, const std::filesystem::path& to) noexcept
{
    try {
        return patiently([&] { return replaceOnce(from, to); });
    } catch (const std::bad_alloc&) {
        return std::make_error_code(std::errc::not_enough_memory);
    }
}

std::error_code removeFile(const std::filesystem::path& file) noexcept
{
    return patiently([&] { return removeOnce(file); });
}

}
