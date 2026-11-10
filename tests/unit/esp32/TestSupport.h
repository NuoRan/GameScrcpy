#pragma once

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace esp32test {

using Bytes = std::vector<std::uint8_t>;

inline Bytes textBytes(std::string_view text)
{
    return Bytes(text.begin(), text.end());
}

inline Bytes fromHex(std::string_view text)
{
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    REQUIRE(text.size() % 2 == 0);
    Bytes bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = digit(text[i]);
        const int low = digit(text[i + 1]);
        REQUIRE(high >= 0);
        REQUIRE(low >= 0);
        bytes.push_back(static_cast<std::uint8_t>(high * 16 + low));
    }
    return bytes;
}

inline std::string toHex(const Bytes& bytes)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : bytes) {
        text.push_back(kDigits[byte >> 4U]);
        text.push_back(kDigits[byte & 0x0FU]);
    }
    return text;
}

inline bool eventually(const std::function<bool()>& condition,
                       std::chrono::milliseconds timeout = std::chrono::milliseconds{3000})
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return condition();
}

class TemporaryFolder
{
public:
    TemporaryFolder()
    {
        static int counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("gs-esp32-tests-" + std::to_string(stamp) + "-" + std::to_string(++counter));
        (void)std::filesystem::create_directories(m_path);
    }

    ~TemporaryFolder()
    {
        std::error_code ignored;
        (void)std::filesystem::remove_all(m_path, ignored);
    }

    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

    void write(const std::string& name, const std::string& text) const
    {
        std::ofstream file(m_path / name, std::ios::binary);
        file << text;
    }

    void write(const std::string& name, const Bytes& bytes) const
    {
        std::ofstream file(m_path / name, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

private:
    std::filesystem::path m_path;
};

}
