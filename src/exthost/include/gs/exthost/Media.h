#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace gs::exthost {

enum class MediaKind : std::uint8_t { Video, Audio };
enum class PixelFormat : std::uint32_t { None, Bgra, Nv12 };
enum class SampleFormat : std::uint32_t { None, S16, F32 };

inline constexpr std::uint32_t kMostFrameSide = 4096;
inline constexpr std::uint64_t kMostFramePixels = 10'000'000;
inline constexpr std::uint32_t kSoundRate = 48'000;
inline constexpr std::uint32_t kMostChannels = 8;
inline constexpr std::uint32_t kMostSoundFrames = 48'000;
inline constexpr std::uint32_t kMostSinkFps = 60;
inline constexpr std::size_t kMostMediaSources = 8;
inline constexpr std::size_t kMostSinks = 8;
inline constexpr std::size_t kMostExporters = 8;
inline constexpr std::size_t kMostStreams = 16;
inline constexpr std::size_t kMostSourceIdBytes = 64;
inline constexpr std::size_t kMostExportPathBytes = 4096;

struct FrameView {
    PixelFormat format = PixelFormat::None;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::uint32_t chromaStride = 0;
    const std::uint8_t* data = nullptr;
    const std::uint8_t* chroma = nullptr;
    std::uint64_t timeUs = 0;
};

struct SoundView {
    SampleFormat format = SampleFormat::None;
    std::uint32_t channels = 0;
    std::uint32_t rate = 0;
    std::uint32_t frames = 0;
    const void* data = nullptr;
    std::uint64_t timeUs = 0;
};

struct SoundBlock {
    std::vector<float> samples;
    std::uint32_t channels = 0;
    std::uint32_t rate = 0;
    std::uint64_t timeUs = 0;
};

[[nodiscard]] std::string checkFrame(const FrameView& frame);
[[nodiscard]] std::string checkSound(const SoundView& sound);
[[nodiscard]] std::size_t packedBytes(PixelFormat format, std::uint32_t width, std::uint32_t height) noexcept;
[[nodiscard]] bool validSourceId(std::string_view id) noexcept;

class MediaStream
{
public:
    virtual ~MediaStream() = default;

    [[nodiscard]] virtual std::uint64_t id() const noexcept = 0;
    [[nodiscard]] virtual bool wait(const std::stop_token& stop) = 0;
    [[nodiscard]] virtual std::optional<FrameView> frame() = 0;
    [[nodiscard]] virtual std::optional<SoundBlock> sound() = 0;
    [[nodiscard]] virtual std::optional<std::string> ended() const = 0;
};

struct SinkStart {
    std::string device;
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps = 0;
    bool audio = false;
};

class SinkPort
{
public:
    virtual ~SinkPort() = default;

    [[nodiscard]] virtual std::uint64_t id() const noexcept = 0;
    [[nodiscard]] virtual bool frame(const FrameView& frame) = 0;
    [[nodiscard]] virtual bool sound(const SoundView& sound) = 0;
    [[nodiscard]] virtual std::optional<std::string> ended() const = 0;
};

}
