#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

struct AVFrame;
struct ID3D11Device;

namespace gs::media {

class VideoFrame;

struct Thumbnail {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    friend bool operator==(const Thumbnail&, const Thumbnail&) = default;
};

class Thumbnailer
{
public:
    explicit Thumbnailer(ID3D11Device* device = nullptr);
    ~Thumbnailer();
    Thumbnailer(const Thumbnailer&) = delete;
    Thumbnailer& operator=(const Thumbnailer&) = delete;

    [[nodiscard]] std::optional<Thumbnail> make(const VideoFrame& frame, int longSide);
    [[nodiscard]] std::optional<Thumbnail> make(const AVFrame& frame, int longSide);

    void request(const VideoFrame& frame, int longSide);
    [[nodiscard]] std::optional<Thumbnail> collect();

private:
    struct State;
    std::unique_ptr<State> m_state;
};

}
