#include "VideoRenderNode.h"

#include <gs/media/ColorMatrix.h>

#include "video_ps_copy.h"
#include "video_ps_nv12.h"
#include "video_ps_planar.h"
#include "video_vs.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QVector4D>
#include <rhi/qrhi.h>

#include <cstring>

namespace gs::render {

namespace {

struct Parameters {
    float corners[4][4];
    float toRgb[3][4];
    float extra[4];
    float sharpen[4];
    float crop[4];
};
static_assert(sizeof(Parameters) % 16 == 0);

void setCropOf(Parameters& parameters, const QRectF& crop)
{
    parameters.crop[0] = static_cast<float>(crop.x());
    parameters.crop[1] = static_cast<float>(crop.y());
    parameters.crop[2] = static_cast<float>(crop.width());
    parameters.crop[3] = static_cast<float>(crop.height());
}

constexpr std::size_t kMaxCachedSurfaces = 64;
constexpr float kFullScreen[4][4] = {{-1.0F, 1.0F, 0.0F, 1.0F},
                                     {1.0F, 1.0F, 0.0F, 1.0F},
                                     {-1.0F, -1.0F, 0.0F, 1.0F},
                                     {1.0F, -1.0F, 0.0F, 1.0F}};

bool upload(ID3D11DeviceContext* context, ID3D11Buffer* buffer, const Parameters& parameters)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    std::memcpy(mapped.pData, &parameters, sizeof(parameters));
    context->Unmap(buffer, 0);
    return true;
}

}

VideoRenderNode::VideoRenderNode(QQuickWindow* window) : m_window(window) {}

VideoRenderNode::~VideoRenderNode()
{
    releaseResources();
}

void VideoRenderNode::setFrame(media::VideoFrame frame)
{
    m_frame = std::move(frame);
    m_frameChanged = true;
}

void VideoRenderNode::setRect(const QRectF& rect)
{
    m_rect = rect;
}

void VideoRenderNode::setCrop(const QRectF& crop)
{
    m_crop = crop;
}

void VideoRenderNode::setSharpness(float sharpness)
{
    m_sharpness = sharpness;
}

void VideoRenderNode::setFilters(std::vector<VideoFilter> filters)
{
    m_filters.setFilters(std::move(filters));
}

bool VideoRenderNode::takeFilterStatus(std::vector<FilterStatus>& status)
{
    return m_filters.takeStatus(status);
}

QSize VideoRenderNode::frameSize() const
{
    return m_frame ? QSize(m_frame.width(), m_frame.height()) : QSize();
}

void VideoRenderNode::prepare()
{
    QSGRendererInterface* renderer = m_window->rendererInterface();
    auto* device = static_cast<ID3D11Device*>(renderer->getResource(m_window, QSGRendererInterface::DeviceResource));
    auto* context = static_cast<ID3D11DeviceContext*>(
        renderer->getResource(m_window, QSGRendererInterface::DeviceContextResource));
    if (device == nullptr || context == nullptr || !m_frame || (!m_vertexShader && !createPipeline(device))) {
        m_ready = false;
        return;
    }
    if (m_frameChanged) {
        m_frameChanged = false;
        m_ready = m_frame.hardware() ? bindSurface(device) : uploadPlanes(device, context);
    }
    if (!m_ready) {
        return;
    }

    Parameters parameters{};
    const QMatrix4x4 transform = *projectionMatrix() * *matrix();
    const QPointF corners[4] = {m_rect.topLeft(), m_rect.topRight(), m_rect.bottomLeft(), m_rect.bottomRight()};
    for (std::size_t i = 0; i < 4; ++i) {
        const QVector4D clip = transform.map(QVector4D(static_cast<float>(corners[i].x()),
                                                       static_cast<float>(corners[i].y()), 0.0F, 1.0F));
        parameters.corners[i][0] = clip.x();
        parameters.corners[i][1] = clip.y();
        parameters.corners[i][2] = clip.z();
        parameters.corners[i][3] = clip.w();
    }
    const media::ColorMatrix color = media::yuvToRgb(*m_frame.avFrame());
    for (std::size_t row = 0; row < 3; ++row) {
        std::memcpy(parameters.toRgb[row], color.rows[row].data(), sizeof(parameters.toRgb[row]));
    }
    parameters.extra[0] = static_cast<float>(inheritedOpacity());
    parameters.extra[1] = m_visibleWidth;
    parameters.extra[2] = m_visibleHeight;
    parameters.sharpen[0] = m_sharpness;
    parameters.sharpen[1] = m_texelWidth;
    parameters.sharpen[2] = m_texelHeight;
    setCropOf(parameters, m_crop);
    if (!upload(context, m_parameters.Get(), parameters)) {
        m_ready = false;
        return;
    }
    const AVFrame* frame = m_frame.avFrame();
    if (!m_filters.prepare(device, context, QSize(frame->width, frame->height))) {
        return;
    }
    Parameters offscreen = parameters;
    std::memcpy(offscreen.corners, kFullScreen, sizeof(kFullScreen));
    setCropOf(offscreen, QRectF(0.0, 0.0, 1.0, 1.0));
    offscreen.extra[0] = 1.0F;
    Parameters shown = parameters;
    shown.extra[1] = 1.0F;
    shown.extra[2] = 1.0F;
    if (!upload(context, m_offscreenParameters.Get(), offscreen) ||
        !upload(context, m_finalParameters.Get(), shown)) {
        m_ready = false;
    }
}

void VideoRenderNode::render(const RenderState* state)
{
    if (!m_ready) {
        return;
    }
    auto* context = static_cast<ID3D11DeviceContext*>(
        m_window->rendererInterface()->getResource(m_window, QSGRendererInterface::DeviceContextResource));
    const QSize target = renderTarget()->pixelSize();
    ID3D11PixelShader* const picture = m_nv12 ? m_nv12Shader.Get() : m_planarShader.Get();
    if (!m_filters.active()) {
        bindWindow(context, state, target);
        draw(context, m_parameters.Get(), picture, m_views.data(), m_viewCount);
        return;
    }
    ComPtr<ID3D11RenderTargetView> window;
    ComPtr<ID3D11DepthStencilView> depth;
    context->OMGetRenderTargets(1, window.GetAddressOf(), depth.GetAddressOf());
    ID3D11RenderTargetView* const original = m_filters.originalTarget();
    context->OMSetRenderTargets(1, &original, nullptr);
    const QSize size = m_filters.originalSize();
    const D3D11_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(size.width()), static_cast<float>(size.height()),
                                  0.0F, 1.0F};
    context->RSSetViewports(1, &viewport);
    context->RSSetState(m_rasterizers[0].Get());
    constexpr float kBlendFactor[4] = {};
    context->OMSetBlendState(m_blend.Get(), kBlendFactor, 0xFFFFFFFFU);
    context->OMSetDepthStencilState(m_depthStencil.Get(), 0);
    draw(context, m_offscreenParameters.Get(), picture, m_views.data(), m_viewCount);
    ID3D11ShaderResourceView* const filtered = m_filters.run(context);
    context->OMSetRenderTargets(1, window.GetAddressOf(), depth.Get());
    bindWindow(context, state, target);
    draw(context, m_finalParameters.Get(), m_copyShader.Get(), &filtered, 1);
}

void VideoRenderNode::bindWindow(ID3D11DeviceContext* context, const RenderState* state, QSize target)
{
    const D3D11_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(target.width()), static_cast<float>(target.height()),
                                  0.0F, 1.0F};
    context->RSSetViewports(1, &viewport);
    const bool scissor = state->scissorEnabled();
    if (scissor) {
        const QRect clip = state->scissorRect();
        const D3D11_RECT rect{clip.x(), target.height() - (clip.y() + clip.height()), clip.x() + clip.width(),
                              target.height() - clip.y()};
        context->RSSetScissorRects(1, &rect);
    }
    context->RSSetState(m_rasterizers[scissor ? 1 : 0].Get());
    constexpr float kBlendFactor[4] = {};
    context->OMSetBlendState(m_blend.Get(), kBlendFactor, 0xFFFFFFFFU);
    context->OMSetDepthStencilState(m_depthStencil.Get(), 0);
}

void VideoRenderNode::draw(ID3D11DeviceContext* context, ID3D11Buffer* parameters, ID3D11PixelShader* shader,
                           ID3D11ShaderResourceView* const* views, UINT count)
{
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &parameters);
    context->PSSetShader(shader, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &parameters);
    context->PSSetShaderResources(0, count, views);
    context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    context->Draw(4, 0);
    ID3D11ShaderResourceView* const unbound[3] = {};
    context->PSSetShaderResources(0, 3, unbound);
}

void VideoRenderNode::releaseResources()
{
    m_ready = false;
    m_frameChanged = static_cast<bool>(m_frame);
    m_views = {};
    m_viewCount = 0;
    m_surfaces.clear();
    m_planes = {};
    m_vertexShader.Reset();
    m_nv12Shader.Reset();
    m_planarShader.Reset();
    m_copyShader.Reset();
    m_parameters.Reset();
    m_offscreenParameters.Reset();
    m_finalParameters.Reset();
    m_sampler.Reset();
    m_rasterizers = {};
    m_blend.Reset();
    m_depthStencil.Reset();
    m_filters.release();
}

QSGRenderNode::StateFlags VideoRenderNode::changedStates() const
{
    return ViewportState | ScissorState | RenderTargetState;
}

QSGRenderNode::RenderingFlags VideoRenderNode::flags() const
{
    return BoundedRectRendering;
}

QRectF VideoRenderNode::rect() const
{
    return m_rect;
}

bool VideoRenderNode::createPipeline(ID3D11Device* device)
{
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(Parameters);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;

    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    const D3D11_DEPTH_STENCIL_DESC depthStencil{};

    bool created = SUCCEEDED(device->CreateVertexShader(kVideoVertexShader, sizeof(kVideoVertexShader), nullptr,
                                                        &m_vertexShader)) &&
                   SUCCEEDED(device->CreatePixelShader(kVideoPixelShaderNv12, sizeof(kVideoPixelShaderNv12), nullptr,
                                                       &m_nv12Shader)) &&
                   SUCCEEDED(device->CreatePixelShader(kVideoPixelShaderPlanar, sizeof(kVideoPixelShaderPlanar),
                                                       nullptr, &m_planarShader)) &&
                   SUCCEEDED(device->CreatePixelShader(kVideoPixelShaderCopy, sizeof(kVideoPixelShaderCopy), nullptr,
                                                       &m_copyShader)) &&
                   SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &m_parameters)) &&
                   SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &m_offscreenParameters)) &&
                   SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &m_finalParameters)) &&
                   SUCCEEDED(device->CreateSamplerState(&sampler, &m_sampler)) &&
                   SUCCEEDED(device->CreateRasterizerState(&rasterizer, &m_rasterizers[0])) &&
                   SUCCEEDED(device->CreateBlendState(&blend, &m_blend)) &&
                   SUCCEEDED(device->CreateDepthStencilState(&depthStencil, &m_depthStencil));
    rasterizer.ScissorEnable = TRUE;
    created = created && SUCCEEDED(device->CreateRasterizerState(&rasterizer, &m_rasterizers[1]));
    if (!created) {
        releaseResources();
    }
    return created;
}

bool VideoRenderNode::bindSurface(ID3D11Device* device)
{
    const AVFrame* frame = m_frame.avFrame();
    auto* texture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
    const auto slice = static_cast<UINT>(reinterpret_cast<std::uintptr_t>(frame->data[1]));
    if (m_frame.session() != m_surfacesSession || m_surfaces.size() >= kMaxCachedSurfaces) {
        m_surfaces.clear();
        m_surfacesSession = m_frame.session();
    }
    SurfaceViews& views = m_surfaces[{texture, slice}];
    if (!views.luma) {
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        if (description.Format != DXGI_FORMAT_NV12 || (description.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0) {
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.MipLevels = 1;
        view.Texture2DArray.FirstArraySlice = slice;
        view.Texture2DArray.ArraySize = 1;
        view.Format = DXGI_FORMAT_R8_UNORM;
        if (FAILED(device->CreateShaderResourceView(texture, &view, &views.luma))) {
            return false;
        }
        view.Format = DXGI_FORMAT_R8G8_UNORM;
        if (FAILED(device->CreateShaderResourceView(texture, &view, &views.chroma))) {
            views.luma.Reset();
            return false;
        }
        views.texture = texture;
        views.width = description.Width;
        views.height = description.Height;
    }
    m_views = {views.luma.Get(), views.chroma.Get(), nullptr};
    m_viewCount = 2;
    m_nv12 = true;
    m_visibleWidth = static_cast<float>(frame->width) / static_cast<float>(views.width);
    m_visibleHeight = static_cast<float>(frame->height) / static_cast<float>(views.height);
    m_texelWidth = 1.0F / static_cast<float>(views.width);
    m_texelHeight = 1.0F / static_cast<float>(views.height);
    return true;
}

bool VideoRenderNode::uploadPlanes(ID3D11Device* device, ID3D11DeviceContext* context)
{
    const AVFrame* frame = m_frame.avFrame();
    const bool interleaved = frame->format == AV_PIX_FMT_NV12;
    if (!interleaved && frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) {
        return false;
    }
    const std::size_t count = interleaved ? 2 : m_planes.size();
    for (std::size_t i = 0; i < count; ++i) {
        const int width = i == 0 ? frame->width : (frame->width + 1) / 2;
        const int height = i == 0 ? frame->height : (frame->height + 1) / 2;
        const DXGI_FORMAT format = interleaved && i == 1 ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8_UNORM;
        const std::size_t rowBytes = static_cast<std::size_t>(width) * (format == DXGI_FORMAT_R8G8_UNORM ? 2U : 1U);
        Plane& plane = m_planes[i];
        if (plane.width != width || plane.height != height || plane.format != format) {
            plane = {};
            D3D11_TEXTURE2D_DESC description{};
            description.Width = static_cast<UINT>(width);
            description.Height = static_cast<UINT>(height);
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = format;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = format;
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.MipLevels = 1;
            view.Texture2DArray.ArraySize = 1;
            if (FAILED(device->CreateTexture2D(&description, nullptr, &plane.texture)) ||
                FAILED(device->CreateShaderResourceView(plane.texture.Get(), &view, &plane.view))) {
                plane = {};
                return false;
            }
            plane.width = width;
            plane.height = height;
            plane.format = format;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(plane.texture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return false;
        }
        for (int row = 0; row < height; ++row) {
            std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch,
                        frame->data[i] + static_cast<std::ptrdiff_t>(row) * frame->linesize[i], rowBytes);
        }
        context->Unmap(plane.texture.Get(), 0);
        m_views[i] = plane.view.Get();
    }
    if (interleaved) {
        m_views[2] = nullptr;
    }
    m_viewCount = static_cast<UINT>(count);
    m_nv12 = interleaved;
    m_visibleWidth = 1.0F;
    m_visibleHeight = 1.0F;
    m_texelWidth = 1.0F / static_cast<float>(m_planes[0].width);
    m_texelHeight = 1.0F / static_cast<float>(m_planes[0].height);
    return true;
}

}
