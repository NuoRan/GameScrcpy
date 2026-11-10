#pragma once

#include "gs/render/VideoFilter.h"

#include <gs/ext/FilterSource.h>

#include <QSize>

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gs::render {

class FilterChain
{
public:
    void setFilters(std::vector<VideoFilter> filters);
    bool prepare(ID3D11Device* device, ID3D11DeviceContext* context, QSize original);
    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] ID3D11RenderTargetView* originalTarget() const noexcept { return m_original.target.Get(); }
    [[nodiscard]] QSize originalSize() const noexcept { return m_original.size; }
    ID3D11ShaderResourceView* run(ID3D11DeviceContext* context);
    bool takeStatus(std::vector<FilterStatus>& status);
    void release();

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Target {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11ShaderResourceView> view;
        QSize size;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    };

    struct Pass {
        std::string name;
        double scale = 1.0;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ComPtr<ID3D11PixelShader> shader;
        ComPtr<ID3D11Buffer> frame;
        Target output;
    };

    struct Compiled {
        VideoFilter filter;
        ext::FilterSource source;
        std::vector<Pass> passes;
        ComPtr<ID3D11Buffer> params;
        std::vector<ComPtr<ID3D11ShaderResourceView>> textures;
        std::vector<double> passMs;
        std::string error;
        bool ready = false;
        bool valuesChanged = true;
    };

    struct Timing {
        ComPtr<ID3D11Query> disjoint;
        std::vector<ComPtr<ID3D11Query>> stamps;
        bool issued = false;
    };

    bool createShared(ID3D11Device* device);
    [[nodiscard]] Compiled compile(ID3D11Device* device, const VideoFilter& filter) const;
    bool ensureTarget(ID3D11Device* device, Target& target, QSize size, DXGI_FORMAT format) const;
    void writeParams(ID3D11DeviceContext* context, Compiled& compiled) const;
    void resetTimings(ID3D11Device* device);
    void collectTimings(ID3D11DeviceContext* context, Timing& timing);
    [[nodiscard]] std::size_t readyPassCount() const;

    std::vector<VideoFilter> m_wanted;
    bool m_wantedChanged = false;
    std::vector<Compiled> m_filters;
    Target m_original;
    bool m_active = false;
    ComPtr<ID3D11VertexShader> m_vertexShader;
    std::array<ComPtr<ID3D11SamplerState>, 2> m_samplers;
    ComPtr<ID3D11RasterizerState> m_rasterizer;
    std::array<Timing, 4> m_timings;
    std::size_t m_timingAt = 0;
    std::uint32_t m_frame = 0;
    std::uint32_t m_framesSinceReport = 0;
    bool m_statusChanged = false;
};

}
