#include <gs/protocol/Codec.h>
#include <gs/protocol/Constants.h>
#include <gs/record/Operations.h>
#include <gs/record/Recorder.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <thread>
#include <vector>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        return 2;
    }
    std::ifstream file(std::filesystem::path(argv[2]), std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<gs::protocol::MediaPacket> packets;
    for (std::size_t offset = 0; offset < bytes.size();) {
        const auto packet =
            gs::protocol::decodeMediaPacket(gs::protocol::MediaChannel::Video, std::span(bytes).subspan(offset));
        if (!packet) {
            return 1;
        }
        offset += gs::protocol::kMediaHeaderSize + packet->header.size;
        packets.push_back(*packet);
    }
    auto recorder = gs::record::Recorder::start(gs::record::RecorderOptions{
        .folder = std::filesystem::path(argv[1]),
        .device = {"Killed", 15, 288, 640, 0},
        .rewriteEvery = std::chrono::milliseconds(1),
    });
    if (!recorder) {
        return 1;
    }
    for (std::size_t round = 0;; ++round) {
        for (const gs::protocol::MediaPacket& packet : packets) {
            (*recorder)->video(packet);
            (*recorder)->operation(gs::script::SessionText{.length = round});
            (*recorder)->event(gs::script::SessionEventKind::Press, "probe");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
