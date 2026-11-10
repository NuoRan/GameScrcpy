#pragma once

#include "gs/media/ColorMatrix.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

struct AVFrame;
struct ID3D11Device;

namespace gs::media {

class VideoFrame;

struct LumaSnapshot {
    std::uint64_t sequence = 0;
    std::uint64_t frame = 0;
    std::int64_t ptsUs = 0;
    std::uint16_t session = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> luma;
    std::vector<std::uint8_t> chroma;
    ColorMatrix matrix;

    [[nodiscard]] bool colored() const noexcept { return !chroma.empty(); }
    [[nodiscard]] int chromaWidth() const noexcept { return (width + 1) / 2; }
    [[nodiscard]] int chromaHeight() const noexcept { return (height + 1) / 2; }
};

struct SnapshotCounters {
    std::uint64_t made = 0;
    std::uint64_t copied = 0;
    std::uint64_t stillDrawing = 0;
    std::uint64_t ringFull = 0;
    std::uint64_t failed = 0;
    std::uint64_t captured = 0;
};

class FrameSnapshots
{
public:
    using Listener = std::function<void(const std::shared_ptr<const LumaSnapshot>&)>;

    explicit FrameSnapshots(ID3D11Device* device = nullptr, int depth = 3);
    ~FrameSnapshots();
    FrameSnapshots(const FrameSnapshots&) = delete;
    FrameSnapshots& operator=(const FrameSnapshots&) = delete;

    void setListener(Listener listener);
    void setCrop(double x, double y, double width, double height);
    void clearCrop();
    void request(bool color = false) noexcept;
    void offer(const VideoFrame& frame);
    void offer(const AVFrame& frame, std::int64_t ptsUs = 0, std::uint16_t session = 0);
    void poll();
    void finish();
    void forget();

    [[nodiscard]] std::shared_ptr<const LumaSnapshot> capture(bool color = false);
    [[nodiscard]] std::uint64_t offered() const noexcept;
    [[nodiscard]] std::shared_ptr<const LumaSnapshot> latest() const;
    [[nodiscard]] SnapshotCounters counters() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

[[nodiscard]] std::vector<std::uint8_t> rgbOf(const LumaSnapshot& shot, int left, int top, int width, int height);

}
