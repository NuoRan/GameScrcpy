#include "gs/adb/ShellProtocol.h"

#include "Wire.h"

namespace gs::adb {

void ShellPacketParser::feed(std::span<const std::uint8_t> bytes)
{
    if (m_consumed > 0) {
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(m_consumed));
        m_consumed = 0;
    }
    m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
}

std::optional<ShellPacket> ShellPacketParser::next()
{
    if (m_failed || m_buffer.size() - m_consumed < kShellHeaderSize) {
        return std::nullopt;
    }
    const std::uint8_t* header = m_buffer.data() + m_consumed;
    const std::size_t length = detail::loadLe32(header + 1);
    if (length > kMaxPacket) {
        m_failed = true;
        return std::nullopt;
    }
    if (m_buffer.size() - m_consumed < kShellHeaderSize + length) {
        return std::nullopt;
    }
    ShellPacket packet;
    packet.id = header[0];
    packet.data.assign(header + kShellHeaderSize, header + kShellHeaderSize + length);
    m_consumed += kShellHeaderSize + length;
    return packet;
}

std::string shellQuote(std::string_view argument)
{
    std::string quoted;
    quoted.reserve(argument.size() + 2);
    quoted.push_back('\'');
    for (const char c : argument) {
        if (c == '\'') {
            quoted.append("'\\''");
        } else {
            quoted.push_back(c);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

}
