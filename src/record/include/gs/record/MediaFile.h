#pragma once

#include <gs/protocol/Codec.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <vector>

namespace gs::record {

inline constexpr std::uint8_t kRecordedAac = 0x41;
inline constexpr std::size_t kAudioSessionSize = 6;

[[nodiscard]] std::vector<std::uint8_t> packetBytes(const protocol::MediaPacket& packet);
[[nodiscard]] bool isRecordedAac(const protocol::AudioSessionInfo& info) noexcept;
[[nodiscard]] std::vector<std::uint8_t> aacSessionPayload(std::uint32_t sampleRate, std::uint8_t channels);
[[nodiscard]] protocol::AudioSessionInfo aacSessionInfo(std::uint32_t sampleRate, std::uint8_t channels) noexcept;
[[nodiscard]] std::optional<protocol::MediaPacket> decodeRecorded(protocol::MediaChannel channel,
                                                                  std::span<const std::uint8_t> bytes);

class MediaHead
{
public:
    void see(const protocol::MediaPacket& packet);
    void clear() noexcept;
    [[nodiscard]] bool empty() const noexcept { return m_session.empty(); }
    [[nodiscard]] std::vector<protocol::MediaPacket> packets(protocol::MediaChannel channel) const;

private:
    std::vector<std::uint8_t> m_session;
    std::vector<std::uint8_t> m_config;
};

class MediaReader
{
public:
    MediaReader(const std::filesystem::path& file, protocol::MediaChannel channel);

    [[nodiscard]] bool opened() const noexcept { return m_opened; }
    [[nodiscard]] std::optional<protocol::MediaHeader> peek();
    [[nodiscard]] std::optional<protocol::MediaPacket> next();
    void skip();
    void seek(std::uint64_t position);
    [[nodiscard]] std::uint64_t position() const noexcept { return m_position; }
    [[nodiscard]] bool truncated() const noexcept { return m_truncated; }

private:
    std::ifstream m_file;
    protocol::MediaChannel m_channel;
    std::uint64_t m_size = 0;
    std::uint64_t m_position = 0;
    std::optional<protocol::MediaHeader> m_peeked;
    std::vector<std::uint8_t> m_buffer;
    bool m_opened = false;
    bool m_truncated = false;
};

}
