#include "gs/esp32/Diagnosis.h"

#include <gs_esp32_protocol.h>

#include <algorithm>
#include <cctype>
#include <string_view>

namespace gs::esp32 {

namespace {

constexpr std::string_view kOldFirmwareGreeting = "qinetouch";
constexpr std::string_view kDownloadMode = "waiting for download";
constexpr std::string_view kRomBoot = "esp-rom";
constexpr std::string_view kResetReason = "rst:";
constexpr std::string_view kBoot = "boot:";
constexpr std::size_t kMostReason = 32;

struct Heard {
    std::string text;
    std::string folded;
};

Heard readHeard(std::span<const std::uint8_t> bytes)
{
    Heard heard;
    heard.text.reserve(bytes.size());
    heard.folded.reserve(bytes.size());
    for (const std::uint8_t byte : bytes) {
        const bool shown = byte == '\r' || byte == '\n' || byte == '\t' || (byte >= 0x20 && byte < 0x7F);
        heard.text.push_back(shown ? static_cast<char>(byte) : '\0');
        heard.folded.push_back(shown ? static_cast<char>(std::tolower(static_cast<unsigned char>(byte))) : '\0');
    }
    return heard;
}

std::size_t countOf(std::string_view text, std::string_view needle) noexcept
{
    std::size_t found = 0;
    for (std::size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + 1)) {
        ++found;
    }
    return found;
}

bool oldTouchPackets(std::span<const std::uint8_t> bytes) noexcept
{
    if (bytes.empty() || bytes.size() % GS_ESP32_LEGACY_PACKET != 0) {
        return false;
    }
    for (std::size_t at = 0; at < bytes.size(); at += GS_ESP32_LEGACY_PACKET) {
        if (bytes[at] != GS_ESP32_LEGACY_MAGIC || bytes[at + 1] > 1 || bytes[at + 2] > GS_ESP32_CONTACT_ID_MAX) {
            return false;
        }
        const auto coordinate = [&bytes, at](std::size_t offset) {
            return static_cast<std::uint32_t>(bytes[at + offset]) |
                   (static_cast<std::uint32_t>(bytes[at + offset + 1]) << 8U) |
                   (static_cast<std::uint32_t>(bytes[at + offset + 2]) << 16U) |
                   (static_cast<std::uint32_t>(bytes[at + offset + 3]) << 24U);
        };
        if (coordinate(3) > static_cast<std::uint32_t>(GS_ESP32_COORD_MAX) ||
            coordinate(7) > static_cast<std::uint32_t>(GS_ESP32_COORD_MAX) || bytes[at + 11] > 16) {
            return false;
        }
    }
    return true;
}

std::string resetReasonOf(const Heard& heard)
{
    const std::size_t at = heard.folded.rfind(kResetReason);
    if (at == std::string::npos) {
        return {};
    }
    std::size_t start = at + kResetReason.size();
    std::size_t end = heard.text.find_first_of(",\r\n", start);
    if (end == std::string::npos) {
        end = heard.text.size();
    }
    end = std::min(end, start + kMostReason);
    while (end > start && (heard.text[end - 1] == ' ' || heard.text[end - 1] == '\0')) {
        --end;
    }
    while (start < end && heard.text[start] == ' ') {
        ++start;
    }
    return heard.text.substr(start, end - start);
}

}

BoardTrouble diagnoseHeard(std::span<const std::uint8_t> heard)
{
    BoardTrouble trouble;
    trouble.bytes = heard.size();
    if (heard.empty()) {
        trouble.problem = BoardProblem::Silent;
        return trouble;
    }
    const Heard text = readHeard(heard);
    const bool old = text.folded.find(kOldFirmwareGreeting) != std::string::npos || oldTouchPackets(heard);
    if (old) {
        trouble.problem = BoardProblem::OldFirmware;
        return trouble;
    }
    if (text.folded.find(kDownloadMode) != std::string::npos) {
        trouble.problem = BoardProblem::DownloadMode;
        return trouble;
    }
    const auto boots = static_cast<int>(countOf(text.folded, kRomBoot));
    const bool booting =
        boots > 0 || text.folded.find(kResetReason) != std::string::npos || text.folded.find(kBoot) != std::string::npos;
    if (booting) {
        trouble.problem = BoardProblem::Restarting;
        trouble.restarts = std::max(boots, 1);
        trouble.resetReason = resetReasonOf(text);
        return trouble;
    }
    trouble.problem = BoardProblem::WrongPort;
    return trouble;
}

const char* boardProblemName(BoardProblem problem) noexcept
{
    switch (problem) {
    case BoardProblem::OldFirmware:
        return "the old QineTouch firmware";
    case BoardProblem::Restarting:
        return "the board keeps restarting";
    case BoardProblem::DownloadMode:
        return "the board is in download mode";
    case BoardProblem::WrongPort:
        return "the bytes do not look like the ESP32 touch box: wrong port or speed";
    case BoardProblem::Silent:
        return "the port sent nothing";
    case BoardProblem::None:
        break;
    }
    return "no problem";
}

}
