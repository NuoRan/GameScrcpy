#include <gs/media/VideoDecoder.h>
#include <gs/protocol/Codec.h>
#include <gs/protocol/Constants.h>
#include <gs/vision/Image.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using gs::vision::Image;
using gs::vision::ImageView;
using gs::vision::Rect;

constexpr int kSuccess = 0;
constexpr int kProblems = 1;
constexpr int kUsage = 2;

constexpr std::string_view kUsageText =
    "usage: gs_vision_fixtures dump <recording.media> <folder>\n"
    "       gs_vision_fixtures write <video folder> <vision folder>\n"
    "       gs_vision_fixtures check <video folder> <vision folder>\n";

struct FrameFixture {
    std::string_view recording;
    int index = 0;
    std::string_view name;
};

struct CropFixture {
    std::string_view frame;
    Rect area;
    std::string_view name;
};

constexpr FrameFixture kFrames[] = {
    {"emulator_h265.media", 0, "frame_h265_top"},       {"emulator_av1.media", 0, "frame_av1_top"},
    {"emulator_h264.media", 10, "frame_h264_scrolled"}, {"emulator_av1.media", 40, "frame_av1_bottom"},
    {"emulator_av1.media", 70, "frame_av1_landscape"},  {"emulator_h264.media", 48, "frame_h264_rotating"},
};

constexpr CropFixture kCrops[] = {
    {"frame_h265_top", {20, 130, 136, 30}, "template_search_bar"},
    {"frame_h265_top", {16, 372, 200, 36}, "template_notifications_row"},
    {"frame_h265_top", {18, 434, 104, 34}, "template_battery_row"},
    {"frame_h265_top", {236, 38, 38, 38}, "template_profile"},
    {"frame_h265_top", {16, 186, 168, 40}, "template_network_row"},
    {"frame_h265_top", {21, 378, 20, 22}, "template_bell"},
    {"frame_av1_bottom", {16, 420, 180, 40}, "template_wallpaper_row"},
    {"frame_av1_bottom", {16, 600, 120, 36}, "template_location_row"},
};

struct Collector final : gs::media::FrameSink {
    void onFrame(gs::media::VideoFrame frame) override
    {
        const AVFrame* picture = frame.avFrame();
        frames.push_back(
            Image::copyOf(ImageView(picture->data[0], frame.width(), frame.height(), picture->linesize[0])));
    }

    std::vector<Image> frames;
};

std::optional<std::vector<Image>> decode(const fs::path& recording)
{
    std::ifstream file(recording, std::ios::binary);
    if (!file) {
        std::cerr << "cannot read " << recording.string() << '\n';
        return std::nullopt;
    }
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    gs::media::VideoDecoder decoder;
    Collector collector;
    for (std::size_t offset = 0; offset < bytes.size();) {
        const auto packet = gs::protocol::decodeMediaPacket(gs::protocol::MediaChannel::Video,
                                                            std::span(bytes).subspan(offset));
        if (!packet) {
            std::cerr << recording.string() << ": broken packet at byte " << offset << '\n';
            return std::nullopt;
        }
        offset += gs::protocol::kMediaHeaderSize + packet->header.size;
        const auto pushed = decoder.push(*packet, collector);
        if (!pushed) {
            std::cerr << recording.string() << ": " << pushed.error().message << '\n';
            return std::nullopt;
        }
    }
    return std::move(collector.frames);
}

std::string pgm(ImageView image)
{
    std::string bytes = std::format("P5\n{} {}\n255\n", image.width(), image.height());
    for (int y = 0; y < image.height(); ++y) {
        bytes.append(reinterpret_cast<const char*>(image.row(y)), static_cast<std::size_t>(image.width()));
    }
    return bytes;
}

std::optional<std::string> readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool writeFile(const fs::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

int dump(const fs::path& recording, const fs::path& folder)
{
    const std::optional<std::vector<Image>> frames = decode(recording);
    if (!frames) {
        return kProblems;
    }
    std::error_code error;
    fs::create_directories(folder, error);
    for (std::size_t index = 0; index < frames->size(); ++index) {
        const Image& frame = (*frames)[index];
        const fs::path target = folder / std::format("frame_{:03}_{}x{}.pgm", index, frame.width(), frame.height());
        if (!writeFile(target, pgm(frame.view()))) {
            std::cerr << "cannot write " << target.string() << '\n';
            return kProblems;
        }
    }
    std::cout << frames->size() << " frames written to " << folder.string() << '\n';
    return kSuccess;
}

std::optional<std::map<std::string, std::string>> fixtures(const fs::path& videoFolder)
{
    std::map<std::string, std::vector<Image>> decoded;
    std::map<std::string, Image> named;
    std::map<std::string, std::string> files;
    for (const FrameFixture& fixture : kFrames) {
        const std::string recording(fixture.recording);
        if (!decoded.contains(recording)) {
            std::optional<std::vector<Image>> frames = decode(videoFolder / recording);
            if (!frames) {
                return std::nullopt;
            }
            decoded.emplace(recording, std::move(*frames));
        }
        const std::vector<Image>& frames = decoded.at(recording);
        if (fixture.index < 0 || static_cast<std::size_t>(fixture.index) >= frames.size()) {
            std::cerr << recording << " has no frame " << fixture.index << '\n';
            return std::nullopt;
        }
        const Image& frame = frames[static_cast<std::size_t>(fixture.index)];
        named.emplace(std::string(fixture.name), Image::copyOf(frame.view()));
        files.emplace(std::string(fixture.name) + ".pgm", pgm(frame.view()));
    }
    for (const CropFixture& crop : kCrops) {
        const auto source = named.find(std::string(crop.frame));
        if (source == named.end()) {
            std::cerr << "unknown frame " << crop.frame << '\n';
            return std::nullopt;
        }
        const ImageView pixels = source->second.view().crop(crop.area);
        if (pixels.width() != crop.area.width || pixels.height() != crop.area.height) {
            std::cerr << crop.name << " lies outside " << crop.frame << '\n';
            return std::nullopt;
        }
        files.emplace(std::string(crop.name) + ".pgm", pgm(pixels));
    }
    return files;
}

int write(const fs::path& videoFolder, const fs::path& visionFolder)
{
    const auto files = fixtures(videoFolder);
    if (!files) {
        return kProblems;
    }
    std::error_code error;
    fs::create_directories(visionFolder, error);
    std::size_t total = 0;
    for (const auto& [name, bytes] : *files) {
        if (!writeFile(visionFolder / name, bytes)) {
            std::cerr << "cannot write " << (visionFolder / name).string() << '\n';
            return kProblems;
        }
        total += bytes.size();
        std::cout << name << ' ' << bytes.size() << '\n';
    }
    std::cout << files->size() << " files, " << total << " bytes\n";
    return kSuccess;
}

int check(const fs::path& videoFolder, const fs::path& visionFolder)
{
    const auto files = fixtures(videoFolder);
    if (!files) {
        return kProblems;
    }
    int stale = 0;
    for (const auto& [name, bytes] : *files) {
        const std::optional<std::string> stored = readFile(visionFolder / name);
        if (!stored || *stored != bytes) {
            std::cout << name << ": differs from what the recordings decode to\n";
            ++stale;
        }
    }
    std::cout << files->size() << " fixtures, " << stale << " stale\n";
    return stale == 0 ? kSuccess : kProblems;
}

}

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << kUsageText;
        return kUsage;
    }
    const std::string_view command = argv[1];
    const fs::path first(argv[2]);
    const fs::path second(argv[3]);
    if (command == "dump") {
        return dump(first, second);
    }
    if (command == "write") {
        return write(first, second);
    }
    if (command == "check") {
        return check(first, second);
    }
    std::cerr << kUsageText;
    return kUsage;
}
