#include "gs/media/Thumbnailer.h"

#include "PlaneReader.h"
#include "gs/media/ColorMatrix.h"
#include "gs/media/VideoDecoder.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace gs::media {

namespace {

Thumbnail scaleDown(const Planes& planes, int width, int height, const ColorMatrix& matrix, int longSide)
{
    const double factor = static_cast<double>(longSide) / std::max(width, height);
    const int outWidth = std::max(1, static_cast<int>(std::lround(width * factor)));
    const int outHeight = std::max(1, static_cast<int>(std::lround(height * factor)));
    Thumbnail thumbnail{outWidth, outHeight,
                        std::vector<std::uint32_t>(static_cast<std::size_t>(outWidth) * outHeight)};
    constexpr int kSamples = 2;
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            std::array<float, 3> rgb{};
            for (int sy = 0; sy < kSamples; ++sy) {
                for (int sx = 0; sx < kSamples; ++sx) {
                    const int px = std::min(width - 1, static_cast<int>((x + (sx + 0.5) / kSamples) * width / outWidth));
                    const int py =
                        std::min(height - 1, static_cast<int>((y + (sy + 0.5) / kSamples) * height / outHeight));
                    const float luma = planes.luma[py * planes.lumaPitch + px] / 255.0f;
                    const std::ptrdiff_t chroma = (py / 2) * planes.chromaPitch + (px / 2) * planes.chromaStep;
                    const float cb = planes.cb[chroma] / 255.0f;
                    const float cr = planes.cr[chroma] / 255.0f;
                    for (std::size_t c = 0; c < rgb.size(); ++c) {
                        const auto& row = matrix.rows[c];
                        rgb[c] += row[0] * luma + row[1] * cb + row[2] * cr + row[3];
                    }
                }
            }
            std::uint32_t pixel = 0xff000000U;
            for (std::size_t c = 0; c < rgb.size(); ++c) {
                const float mean = rgb[c] / (kSamples * kSamples);
                const auto value = static_cast<std::uint32_t>(std::clamp(std::lround(mean * 255.0f), 0L, 255L));
                pixel |= value << (16 - 8 * c);
            }
            thumbnail.pixels[static_cast<std::size_t>(y) * outWidth + x] = pixel;
        }
    }
    return thumbnail;
}

}

struct Thumbnailer::State {
    explicit State(ID3D11Device* device) : reader(device)
    {
        if (device != nullptr) {
            this->device = device;
            device->GetImmediateContext(&context);
        }
    }

    PlaneReader reader;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    UINT stagingWidth = 0;
    UINT stagingHeight = 0;
    bool pending = false;
    int width = 0;
    int height = 0;
    int longSide = 0;
    ColorMatrix matrix;
    std::optional<Thumbnail> ready;
};

Thumbnailer::Thumbnailer(ID3D11Device* device) : m_state(std::make_unique<State>(device)) {}

Thumbnailer::~Thumbnailer() = default;

void Thumbnailer::request(const VideoFrame& frame, int longSide)
{
    if (!frame) {
        return;
    }
    const AVFrame& picture = *frame.avFrame();
    State& state = *m_state;
    if (picture.format != AV_PIX_FMT_D3D11) {
        state.ready = make(picture, longSide);
        return;
    }
    if (state.pending || !state.context || picture.width <= 0 || picture.height <= 0 || longSide <= 0) {
        return;
    }
    auto* texture = reinterpret_cast<ID3D11Texture2D*>(picture.data[0]);
    const auto slice = static_cast<UINT>(reinterpret_cast<std::intptr_t>(picture.data[1]));
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    if (description.Format != DXGI_FORMAT_NV12) {
        return;
    }
    if (!state.staging || state.stagingWidth != description.Width || state.stagingHeight != description.Height) {
        D3D11_TEXTURE2D_DESC wanted{};
        wanted.Width = description.Width;
        wanted.Height = description.Height;
        wanted.MipLevels = 1;
        wanted.ArraySize = 1;
        wanted.Format = DXGI_FORMAT_NV12;
        wanted.SampleDesc.Count = 1;
        wanted.Usage = D3D11_USAGE_STAGING;
        wanted.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        state.staging.Reset();
        state.stagingWidth = 0;
        state.stagingHeight = 0;
        if (FAILED(state.device->CreateTexture2D(&wanted, nullptr, &state.staging))) {
            return;
        }
        state.stagingWidth = wanted.Width;
        state.stagingHeight = wanted.Height;
    }
    state.context->CopySubresourceRegion(state.staging.Get(), 0, 0, 0, 0, texture,
                                         D3D11CalcSubresource(0, slice, description.MipLevels), nullptr);
    state.pending = true;
    state.width = picture.width;
    state.height = picture.height;
    state.longSide = longSide;
    state.matrix = yuvToRgb(picture);
}

std::optional<Thumbnail> Thumbnailer::collect()
{
    State& state = *m_state;
    if (state.pending) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result =
            state.context->Map(state.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (result != DXGI_ERROR_WAS_STILL_DRAWING) {
            state.pending = false;
            if (SUCCEEDED(result)) {
                const auto* luma = static_cast<const std::uint8_t*>(mapped.pData);
                const auto pitch = static_cast<std::ptrdiff_t>(mapped.RowPitch);
                const std::uint8_t* chroma = luma + pitch * static_cast<std::ptrdiff_t>(state.stagingHeight);
                state.ready = scaleDown({luma, pitch, chroma, chroma + 1, pitch, 2}, state.width, state.height,
                                        state.matrix, state.longSide);
                state.context->Unmap(state.staging.Get(), 0);
            }
        }
    }
    return std::exchange(state.ready, std::nullopt);
}

std::optional<Thumbnail> Thumbnailer::make(const VideoFrame& frame, int longSide)
{
    if (!frame) {
        return std::nullopt;
    }
    return make(*frame.avFrame(), longSide);
}

std::optional<Thumbnail> Thumbnailer::make(const AVFrame& frame, int longSide)
{
    if (frame.width <= 0 || frame.height <= 0 || longSide <= 0) {
        return std::nullopt;
    }
    const ColorMatrix matrix = yuvToRgb(frame);
    std::optional<Thumbnail> thumbnail;
    m_state->reader.read(frame, [&](const Planes& planes) {
        thumbnail = scaleDown(planes, frame.width, frame.height, matrix, longSide);
    });
    return thumbnail;
}

}
