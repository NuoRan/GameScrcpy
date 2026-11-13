#pragma once

#include "FilterChain.h"
#include "PresentClock.h"

#include <gs/media/VideoDecoder.h>
#include <gs/render/FrameSlot.h>

#include <QRectF>
#include <QSGRenderNode>
#include <QSize>

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

class QQuickWindow;

namespace gs::render {

struct WakeFlag {
    std::atomic<bool> posted{false};
};

class VideoRenderNode final : public QSGRenderNode
{
public:
    explicit VideoRenderNode(QQuickWindow* window);
    ~VideoRenderNode() override;

    void setSource(std::shared_ptr<FrameSlot> slot, bool frozen);
    void setBounds(const QRectF& bounds);
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

    struct GpuQuerySet {
        ComPtr<ID3D11Query> disjoint;
        ComPtr<ID3D11Query> begin;
        ComPtr<ID3D11Query> end;
    };

    static constexpr std::size_t kGpuQuerySets = 6;

    void takeFrame();
    void installWake();
    void postWake();
    bool createPipeline(ID3D11Device* device);
    void createGpuQueries(ID3D11Device* device);
    void startGpuTiming(ID3D11DeviceContext* context);
    void markGpuBegin(ID3D11DeviceContext* context);
    void finishGpuTiming(ID3D11DeviceContext* context);
    void readGpuSamples(ID3D11DeviceContext* context);
    bool bindSurface(ID3D11Device* device);
    bool uploadPlanes(ID3D11Device* device, ID3D11DeviceContext* context);
    void bindWindow(ID3D11DeviceContext* context, const RenderState* state, QSize target);
    void draw(ID3D11DeviceContext* context, ID3D11Buffer* parameters, ID3D11PixelShader* shader,
              ID3D11ShaderResourceView* const* views, UINT count);

    QQuickWindow* m_window;
    std::shared_ptr<FrameSlot> m_slot;
    std::shared_ptr<WakeFlag> m_wake;
    PresentClock m_present;
    bool m_guiWake = false;
    bool m_frozen = false;
    media::VideoFrame m_frame;
    bool m_frameChanged = false;
    bool m_ready = false;
    QSize m_reportedSize;
    QRectF m_bounds;
    QRectF m_drawnRect;
    QRectF m_crop{0.0, 0.0, 1.0, 1.0};
    float m_sharpness = 0.0F;
    std::int64_t m_takenPtsUs = 0;
    std::int64_t m_takenReceivedUs = 0;
    std::int64_t m_takenUs = 0;
    bool m_drewFrame = false;

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
    std::array<GpuQuerySet, kGpuQuerySets> m_gpuQueries;
    std::uint64_t m_gpuIssued = 0;
    std::uint64_t m_gpuRead = 0;
    bool m_gpuQueriesReady = false;
    bool m_gpuMeasuring = false;
    bool m_gpuActive = false;
};

}
