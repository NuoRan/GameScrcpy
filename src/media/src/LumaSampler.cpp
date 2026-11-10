#include "gs/media/LumaSampler.h"

#include "PlaneReader.h"
#include "gs/media/VideoDecoder.h"

extern "C" {
#include <libavutil/frame.h>
}

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace gs::media {

namespace {

struct Sum {
    std::int64_t total = 0;
    std::int64_t count = 0;

    [[nodiscard]] double mean() const noexcept
    {
        return count > 0 ? static_cast<double>(total) / static_cast<double>(count) : 0.0;
    }
};

LumaReading readSpot(const Planes& planes, int width, int height, const LumaSpot& spot)
{
    Sum core;
    Sum ring;
    const int top = std::max(0, spot.y - spot.ringTo);
    const int bottom = std::min(height - 1, spot.y + spot.ringTo);
    const int left = std::max(0, spot.x - spot.ringTo);
    const int right = std::min(width - 1, spot.x + spot.ringTo);
    for (int y = top; y <= bottom; ++y) {
        const std::uint8_t* row = planes.luma + static_cast<std::ptrdiff_t>(y) * planes.lumaPitch;
        const int dy = std::abs(y - spot.y);
        for (int x = left; x <= right; ++x) {
            const int distance = std::max(dy, std::abs(x - spot.x));
            if (distance <= spot.core) {
                core.total += row[x];
                ++core.count;
            } else if (distance >= spot.ringFrom) {
                ring.total += row[x];
                ++ring.count;
            }
        }
    }
    return {core.mean(), ring.mean()};
}

}

struct LumaSampler::State {
    explicit State(ID3D11Device* device) : reader(device) {}

    PlaneReader reader;
};

LumaSampler::LumaSampler(ID3D11Device* device) : m_state(std::make_unique<State>(device)) {}

LumaSampler::~LumaSampler() = default;

std::optional<std::vector<LumaReading>> LumaSampler::read(const VideoFrame& frame, std::span<const LumaSpot> spots)
{
    if (!frame) {
        return std::nullopt;
    }
    return read(*frame.avFrame(), spots);
}

std::optional<std::vector<LumaReading>> LumaSampler::read(const AVFrame& frame, std::span<const LumaSpot> spots)
{
    if (spots.empty()) {
        return std::vector<LumaReading>{};
    }
    std::optional<std::vector<LumaReading>> readings;
    m_state->reader.read(frame, [&](const Planes& planes) {
        std::vector<LumaReading> values;
        values.reserve(spots.size());
        for (const LumaSpot& spot : spots) {
            values.push_back(readSpot(planes, frame.width, frame.height, spot));
        }
        readings = std::move(values);
    });
    return readings;
}

}
