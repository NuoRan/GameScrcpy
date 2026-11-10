#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

struct AVFrame;
struct ID3D11Device;

namespace gs::media {

class VideoFrame;

struct LumaSpot {
    int x = 0;
    int y = 0;
    int core = 0;
    int ringFrom = 0;
    int ringTo = 0;

    friend bool operator==(const LumaSpot&, const LumaSpot&) = default;
};

struct LumaReading {
    double core = 0;
    double ring = 0;

    friend bool operator==(const LumaReading&, const LumaReading&) = default;
};

class LumaSampler
{
public:
    explicit LumaSampler(ID3D11Device* device = nullptr);
    ~LumaSampler();
    LumaSampler(const LumaSampler&) = delete;
    LumaSampler& operator=(const LumaSampler&) = delete;

    [[nodiscard]] std::optional<std::vector<LumaReading>> read(const VideoFrame& frame, std::span<const LumaSpot> spots);
    [[nodiscard]] std::optional<std::vector<LumaReading>> read(const AVFrame& frame, std::span<const LumaSpot> spots);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

}
