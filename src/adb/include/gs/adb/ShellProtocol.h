#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::adb {

enum class ShellPacketId : std::uint8_t { Stdin = 0, Stdout = 1, Stderr = 2, Exit = 3, CloseStdin = 4 };

inline constexpr std::size_t kShellHeaderSize = 5;

struct ShellPacket {
    std::uint8_t id = 0;
    std::vector<std::uint8_t> data;
};

class ShellPacketParser
{
public:
    static constexpr std::size_t kMaxPacket = 1024 * 1024;

    void feed(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::optional<ShellPacket> next();
    [[nodiscard]] bool failed() const noexcept { return m_failed; }

private:
    std::vector<std::uint8_t> m_buffer;
    std::size_t m_consumed = 0;
    bool m_failed = false;
};

struct ShellResult {
    int exitCode = -1;
    std::string out;
    std::string err;
};

[[nodiscard]] std::string shellQuote(std::string_view argument);

}
