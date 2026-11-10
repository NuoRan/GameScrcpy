#pragma once

#include "FilterChain.h"

#include <gs/media/VideoDecoder.h>

#include <QRectF>
#include <QSGRenderNode>
#include <QSize>

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

class QQuickWindow;

namespace gs::render {

class VideoRenderNode final : public QSGRenderNode
{
public:
    explicit VideoRenderNode(QQuickWindow* window);
    ~VideoRenderNode() override;

    void setFrame(media::VideoFrame frame);
    void setRect(const QRectF& rect);
    void setCrop(const QRectF& crop);
    void setSharpness(float sharpness);
    void setFilters(std::vector<VideoFilter> filters);
    bool takeFilterStatus(std::vector<FilterStatus>& status);
    [[nodiscard]] QSize frameSize() const;

    void prepare() override;
    void render(const RenderState* state) override;
    void releaseResources() override;
    [[nodiscard]] StateFlags changedStates() const override;
    [[nodiscard]] RenderingFlags flags() const override;
    [[nodiscard]] QRectF rect() const override;

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct SurfaceViews {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> luma;
        ComPtr<ID3D11ShaderResourceView> chroma;
        UINT width = 0;
        UINT height = 0;
    };

    struct Plane {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        int width = 0;
        int height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    };

    bool createPipeline(ID3D11Device* device);
    bool bindSurface(ID3D11Device* device);
    bool uploadPlanes(ID3D11Device* device, ID3D11DeviceContext* context);
    void bindWindow(ID3D11DeviceContext* context, const RenderState* state, QSize target);
    void draw(ID3D11DeviceContext* context, ID3D11Buffer* parameters, ID3D11PixelShader* shader,
              ID3D11ShaderResourceView* const* views, UINT count);

    QQuickWindow* m_window;
    media::VideoFrame m_frame;
    bool m_frameChanged = false;
    bool m_ready = false;
    QRectF m_rect;
    QRectF m_crop{0.0, 0.0, 1.0, 1.0};
    float m_sharpness = 0.0F;

    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_nv12Shader;
    ComPtr<ID3D11PixelShader> m_planarShader;
    ComPtr<ID3D11PixelShader> m_copyShader;
    ComPtr<ID3D11Buffer> m_parameters;
    ComPtr<ID3D11Buffer> m_offscreenParameters;
    ComPtr<ID3D11Buffer> m_finalParameters;
    ComPtr<ID3D11SamplerState> m_sampler;
    std::array<ComPtr<ID3D11RasterizerState>, 2> m_rasterizers;
    ComPtr<ID3D11BlendState> m_blend;
    ComPtr<ID3D11DepthStencilState> m_depthStencil;

    std::map<std::pair<ID3D11Texture2D*, UINT>, SurfaceViews> m_surfaces;
    std::uint16_t m_surfacesSession = 0;
    std::array<Plane, 3> m_planes;

    std::array<ID3D11ShaderResourceView*, 3> m_views{};
    UINT m_viewCount = 0;
    bool m_nv12 = false;
    float m_visibleWidth = 1.0F;
    float m_visibleHeight = 1.0F;
    float m_texelWidth = 0.0F;
    float m_texelHeight = 0.0F;
    FilterChain m_filters;
};

}
