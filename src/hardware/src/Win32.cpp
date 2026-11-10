#include "Links.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>

namespace gs::hardware::detail {

std::string narrow(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(size, 0)), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    }
    return out;
}

std::wstring widen(std::string_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(size, 0)), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    }
    return out;
}

std::string systemMessage(std::uint32_t error)
{
    std::array<wchar_t, 512> buffer{};
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
                                        buffer.data(), static_cast<DWORD>(buffer.size()), nullptr);
    std::wstring text(buffer.data(), length);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
        text.pop_back();
    }
    if (text.empty()) {
        return "error " + std::to_string(error);
    }
    return narrow(text);
}

bool removalError(std::uint32_t error) noexcept
{
    switch (error) {
    case ERROR_ACCESS_DENIED:
    case ERROR_BAD_COMMAND:
    case ERROR_GEN_FAILURE:
    case ERROR_FILE_NOT_FOUND:
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_DEVICE_REMOVED:
    case ERROR_NO_SUCH_DEVICE:
    case ERROR_OPERATION_ABORTED:
        return true;
    default:
        return false;
    }
}

std::uint16_t hexAfter(std::string_view text, std::string_view key) noexcept
{
    const auto upper = [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); };
    for (std::size_t at = 0; at + key.size() + 4 <= text.size(); ++at) {
        bool same = true;
        for (std::size_t i = 0; i < key.size() && same; ++i) {
            same = upper(text[at + i]) == key[i];
        }
        if (!same) {
            continue;
        }
        unsigned value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const char c = upper(text[at + key.size() + i]);
            if (c >= '0' && c <= '9') {
                value = value * 16 + static_cast<unsigned>(c - '0');
            } else if (c >= 'A' && c <= 'F') {
                value = value * 16 + static_cast<unsigned>(c - 'A' + 10);
            } else {
                return 0;
            }
        }
        return static_cast<std::uint16_t>(value);
    }
    return 0;
}

}
