#include "gs/media/FrameUploader.h"

#include <d3d11.h>
#include <wrl/client.h>

#include "convert_scale_chroma.h"
#include "convert_scale_luma.h"
#include "convert_vs.h"
#include "convert_yuy2_chroma.h"
#include "convert_yuy2_luma.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace gs::media {

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint8_t kBlack = 16;
constexpr std::uint8_t kNeutral = 128;
constexpr float kBlackLuma = 16.0F / 255.0F;
constexpr float kNeutralChroma = 128.0F / 255.0F;
constexpr std::size_t kMostSourceViews = 32;

struct Slot {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> lumaTarget;
    ComPtr<ID3D11RenderTargetView> chromaTarget;
    ComPtr<ID3D11Texture2D> staging;
    std::atomic<bool> busy{false};
};

struct SourceViews {
    ComPtr<ID3D11ShaderResourceView> luma;
    ComPtr<ID3D11ShaderResourceView> chroma;
    UINT width = 0;
    UINT height = 0;
};

int evenUp(int value) noexcept
{
    return (value + 1) & ~1;
}

D3D11_VIEWPORT viewportOf(int left, int top, int width, int height) noexcept
{
    return {static_cast<float>(left), static_cast<float>(top), static_cast<float>(width), static_cast<float>(height),
            0.0F, 1.0F};
}

}

struct FrameUploader::State {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<ID3D11DeviceContext> deferred;
    int depth = kDefaultDepth;
    std::vector<std::shared_ptr<Slot>> slots;
    int width = 0;
    int height = 0;
    bool targets = false;
    bool pipelineTried = false;
    bool pipeline = false;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> yuy2Luma;
    ComPtr<ID3D11PixelShader> yuy2Chroma;
    ComPtr<ID3D11PixelShader> scaleLuma;
    ComPtr<ID3D11PixelShader> scaleChroma;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11Buffer> scale;
    ComPtr<ID3D11Texture2D> packed;
    ComPtr<ID3D11ShaderResourceView> packedView;
    int packedWidth = 0;
    int packedHeight = 0;
    std::map<std::pair<ID3D11Texture2D*, UINT>, SourceViews> sources;
    std::vector<std::uint8_t> scratch;
    std::atomic<std::uint64_t> dropped{0};

    bool canDraw()
    {
        if (pipelineTried) {
            return pipeline;
        }
        pipelineTried = true;
        UINT support = 0;
        if (FAILED(device->CheckFormatSupport(DXGI_FORMAT_NV12, &support)) ||
            (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0) {
            return false;
        }
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = 16;
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_SAMPLER_DESC filter{};
        filter.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        filter.AddressU = filter.AddressV = filter.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        filter.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        pipeline = SUCCEEDED(device->CreateDeferredContext(0, &deferred)) &&
                   SUCCEEDED(device->CreateVertexShader(kConvertVertexShader, sizeof(kConvertVertexShader), nullptr,
                                                        &vertex)) &&
                   SUCCEEDED(device->CreatePixelShader(kYuy2LumaShader, sizeof(kYuy2LumaShader), nullptr, &yuy2Luma)) &&
                   SUCCEEDED(device->CreatePixelShader(kYuy2ChromaShader, sizeof(kYuy2ChromaShader), nullptr,
                                                       &yuy2Chroma)) &&
                   SUCCEEDED(device->CreatePixelShader(kScaleLumaShader, sizeof(kScaleLumaShader), nullptr,
                                                       &scaleLuma)) &&
                   SUCCEEDED(device->CreatePixelShader(kScaleChromaShader, sizeof(kScaleChromaShader), nullptr,
                                                       &scaleChroma)) &&
                   SUCCEEDED(device->CreateBuffer(&buffer, nullptr, &scale)) &&
                   SUCCEEDED(device->CreateSamplerState(&filter, &sampler)) &&
                   SUCCEEDED(device->CreateRasterizerState(&raster, &rasterizer));
        if (!pipeline) {
            deferred.Reset();
            vertex.Reset();
            yuy2Luma.Reset();
            yuy2Chroma.Reset();
            scaleLuma.Reset();
            scaleChroma.Reset();
            scale.Reset();
            sampler.Reset();
            rasterizer.Reset();
        }
        return pipeline;
    }

    bool makeSlot(Slot& slot, bool withTargets) const
    {
        D3D11_TEXTURE2D_DESC layout{};
        layout.Width = static_cast<UINT>(width);
        layout.Height = static_cast<UINT>(height);
        layout.MipLevels = 1;
        layout.ArraySize = 1;
        layout.Format = DXGI_FORMAT_NV12;
        layout.SampleDesc.Count = 1;
        layout.Usage = D3D11_USAGE_DEFAULT;
        layout.BindFlags = D3D11_BIND_SHADER_RESOURCE | (withTargets ? D3D11_BIND_RENDER_TARGET : 0U);
        const auto lumaBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        std::vector<std::uint8_t> black(lumaBytes + lumaBytes / 2, kNeutral);
        std::fill_n(black.begin(), lumaBytes, kBlack);
        const D3D11_SUBRESOURCE_DATA initial{black.data(), static_cast<UINT>(width), 0};
        if (FAILED(device->CreateTexture2D(&layout, &initial, &slot.texture)) &&
            FAILED(device->CreateTexture2D(&layout, nullptr, &slot.texture))) {
            return false;
        }
        if (!withTargets) {
            return true;
        }
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        view.Format = DXGI_FORMAT_R8_UNORM;
        if (FAILED(device->CreateRenderTargetView(slot.texture.Get(), &view, &slot.lumaTarget))) {
            slot.texture.Reset();
            return false;
        }
        view.Format = DXGI_FORMAT_R8G8_UNORM;
        if (FAILED(device->CreateRenderTargetView(slot.texture.Get(), &view, &slot.chromaTarget))) {
            slot.lumaTarget.Reset();
            slot.texture.Reset();
            return false;
        }
        return true;
    }

    std::shared_ptr<Slot> lease(int frameWidth, int frameHeight, bool wantTargets)
    {
        const int textureWidth = evenUp(frameWidth);
        const int textureHeight = evenUp(frameHeight);
        if (slots.empty() || textureWidth != width || textureHeight != height || (wantTargets && !targets)) {
            targets = wantTargets || (targets && textureWidth == width && textureHeight == height);
            width = textureWidth;
            height = textureHeight;
            slots.clear();
            for (int index = 0; index < depth; ++index) {
                slots.push_back(std::make_shared<Slot>());
            }
        }
        for (const std::shared_ptr<Slot>& slot : slots) {
            bool expected = false;
            if (!slot->busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                continue;
            }
            if (!slot->texture && !makeSlot(*slot, targets)) {
                slot->busy.store(false, std::memory_order_release);
                dropped.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
            }
            return slot;
        }
        dropped.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    VideoFrame wrap(const std::shared_ptr<Slot>& slot, int frameWidth, int frameHeight, const FrameStamp& stamp) const
    {
        std::shared_ptr<void> owner(slot.get(), [slot](void*) { slot->busy.store(false, std::memory_order_release); });
        return VideoFrame::wrapTexture(slot->texture.Get(), 0, frameWidth, frameHeight, std::move(owner), stamp.ptsUs,
                                       stamp.session, stamp.receivedUs, stamp.color);
    }

    bool upload(Slot& slot, const Nv12Planes& planes, int frameWidth, int frameHeight)
    {
        if (!slot.staging) {
            D3D11_TEXTURE2D_DESC layout{};
            layout.Width = static_cast<UINT>(width);
            layout.Height = static_cast<UINT>(height);
            layout.MipLevels = 1;
            layout.ArraySize = 1;
            layout.Format = DXGI_FORMAT_NV12;
            layout.SampleDesc.Count = 1;
            layout.Usage = D3D11_USAGE_STAGING;
            layout.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateTexture2D(&layout, nullptr, &slot.staging))) {
                return false;
            }
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(immediate->Map(slot.staging.Get(), 0, D3D11_MAP_WRITE, 0, &mapped))) {
            return false;
        }
        auto* out = static_cast<std::uint8_t*>(mapped.pData);
        const auto pitch = static_cast<std::ptrdiff_t>(mapped.RowPitch);
        for (int row = 0; row < frameHeight; ++row) {
            std::memcpy(out + row * pitch, planes.luma + row * planes.lumaPitch, static_cast<std::size_t>(frameWidth));
        }
        std::uint8_t* chroma = out + pitch * height;
        const auto chromaBytes = static_cast<std::size_t>(2 * ((frameWidth + 1) / 2));
        for (int row = 0; row < (frameHeight + 1) / 2; ++row) {
            std::memcpy(chroma + row * pitch, planes.chroma + row * planes.chromaPitch, chromaBytes);
        }
        immediate->Unmap(slot.staging.Get(), 0);
        immediate->CopyResource(slot.texture.Get(), slot.staging.Get());
        return true;
    }

    void draw(ID3D11RenderTargetView* target, const D3D11_VIEWPORT& viewport, ID3D11PixelShader* shader,
              ID3D11ShaderResourceView* const* views, UINT count)
    {
        ID3D11DeviceContext* context = deferred.Get();
        context->OMSetRenderTargets(1, &target, nullptr);
        context->RSSetViewports(1, &viewport);
        context->RSSetState(rasterizer.Get());
        context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFU);
        context->OMSetDepthStencilState(nullptr, 0);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(vertex.Get(), nullptr, 0);
        context->PSSetShader(shader, nullptr, 0);
        context->PSSetShaderResources(0, count, views);
        context->PSSetSamplers(0, 1, sampler.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, scale.GetAddressOf());
        context->Draw(4, 0);
        ID3D11ShaderResourceView* const unbound[2] = {};
        context->PSSetShaderResources(0, 2, unbound);
    }

    bool submit()
    {
        ComPtr<ID3D11CommandList> list;
        if (FAILED(deferred->FinishCommandList(FALSE, &list))) {
            return false;
        }
        immediate->ExecuteCommandList(list.Get(), TRUE);
        return true;
    }

    bool convertYuy2(Slot& slot, const std::uint8_t* pixels, std::ptrdiff_t pitch, int frameWidth, int frameHeight)
    {
        const int texels = (frameWidth + 1) / 2;
        if (!packed || packedWidth != texels || packedHeight != frameHeight) {
            packed.Reset();
            packedView.Reset();
            packedWidth = 0;
            packedHeight = 0;
            D3D11_TEXTURE2D_DESC layout{};
            layout.Width = static_cast<UINT>(texels);
            layout.Height = static_cast<UINT>(frameHeight);
            layout.MipLevels = 1;
            layout.ArraySize = 1;
            layout.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            layout.SampleDesc.Count = 1;
            layout.Usage = D3D11_USAGE_DEFAULT;
            layout.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->CreateTexture2D(&layout, nullptr, &packed)) ||
                FAILED(device->CreateShaderResourceView(packed.Get(), nullptr, &packedView))) {
                packed.Reset();
                packedView.Reset();
                return false;
            }
            packedWidth = texels;
            packedHeight = frameHeight;
        }
        immediate->UpdateSubresource(packed.Get(), 0, nullptr, pixels, static_cast<UINT>(pitch), 0);
        draw(slot.lumaTarget.Get(), viewportOf(0, 0, frameWidth, frameHeight), yuy2Luma.Get(),
             packedView.GetAddressOf(), 1);
        draw(slot.chromaTarget.Get(), viewportOf(0, 0, texels, (frameHeight + 1) / 2), yuy2Chroma.Get(),
             packedView.GetAddressOf(), 1);
        return submit();
    }

    const SourceViews* viewsOf(ID3D11Texture2D* texture, UINT slice)
    {
        const auto key = std::make_pair(texture, slice);
        if (const auto found = sources.find(key); found != sources.end()) {
            return &found->second;
        }
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        if (description.Format != DXGI_FORMAT_NV12 || (description.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0 ||
            slice >= description.ArraySize) {
            return nullptr;
        }
        if (sources.size() >= kMostSourceViews) {
            sources.clear();
        }
        SourceViews views;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.MipLevels = 1;
        view.Texture2DArray.FirstArraySlice = slice;
        view.Texture2DArray.ArraySize = 1;
        view.Format = DXGI_FORMAT_R8_UNORM;
        if (FAILED(device->CreateShaderResourceView(texture, &view, &views.luma))) {
            return nullptr;
        }
        view.Format = DXGI_FORMAT_R8G8_UNORM;
        if (FAILED(device->CreateShaderResourceView(texture, &view, &views.chroma))) {
            return nullptr;
        }
        views.width = description.Width;
        views.height = description.Height;
        return &sources.emplace(key, std::move(views)).first->second;
    }
};

FrameUploader::FrameUploader(ID3D11Device* device, int depth) : m_state(std::make_unique<State>())
{
    m_state->depth = std::clamp(depth, 2, 16);
    if (device != nullptr) {
        m_state->device = device;
        device->GetImmediateContext(&m_state->immediate);
    }
}

FrameUploader::~FrameUploader() = default;

VideoFrame FrameUploader::nv12(const Nv12Planes& planes, int width, int height, const FrameStamp& stamp)
{
    State& state = *m_state;
    if (planes.luma == nullptr || planes.chroma == nullptr || width <= 0 || height <= 0) {
        return {};
    }
    if (!state.device) {
        return VideoFrame::copyNv12(planes.luma, planes.lumaPitch, planes.chroma, planes.chromaPitch, width, height,
                                    stamp.ptsUs, stamp.session, stamp.receivedUs, stamp.color);
    }
    const std::shared_ptr<Slot> slot = state.lease(width, height, false);
    if (slot == nullptr) {
        return {};
    }
    if (!state.upload(*slot, planes, width, height)) {
        slot->busy.store(false, std::memory_order_release);
        state.dropped.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    return state.wrap(slot, width, height, stamp);
}

VideoFrame FrameUploader::yuy2(const std::uint8_t* pixels, std::ptrdiff_t pitch, int width, int height,
                               const FrameStamp& stamp)
{
    State& state = *m_state;
    if (pixels == nullptr || width < 2 || height < 1 || pitch < 2 * static_cast<std::ptrdiff_t>(width)) {
        return {};
    }
    if (state.device && state.canDraw()) {
        const std::shared_ptr<Slot> slot = state.lease(width, height, true);
        if (slot == nullptr) {
            return {};
        }
        if (state.convertYuy2(*slot, pixels, pitch, width, height)) {
            return state.wrap(slot, width, height, stamp);
        }
        slot->busy.store(false, std::memory_order_release);
    }
    const int lumaPitch = evenUp(width);
    const int rows = evenUp(height);
    state.scratch.resize(static_cast<std::size_t>(lumaPitch) * static_cast<std::size_t>(rows) * 3 / 2);
    std::uint8_t* luma = state.scratch.data();
    std::uint8_t* chroma = luma + static_cast<std::ptrdiff_t>(lumaPitch) * rows;
    yuy2ToNv12(pixels, pitch, width, height, luma, lumaPitch, chroma, lumaPitch);
    return nv12({luma, lumaPitch, chroma, lumaPitch}, width, height, stamp);
}

VideoFrame FrameUploader::copy(ID3D11Texture2D* source, unsigned subresource, int width, int height,
                               const FrameStamp& stamp)
{
    State& state = *m_state;
    if (source == nullptr || !state.device || width <= 0 || height <= 0) {
        return {};
    }
    D3D11_TEXTURE2D_DESC description{};
    source->GetDesc(&description);
    if (description.Format != DXGI_FORMAT_NV12) {
        return {};
    }
    const std::shared_ptr<Slot> slot = state.lease(width, height, false);
    if (slot == nullptr) {
        return {};
    }
    const UINT right = std::min(static_cast<UINT>(evenUp(width)), description.Width & ~1U);
    const UINT bottom = std::min(static_cast<UINT>(evenUp(height)), description.Height & ~1U);
    const D3D11_BOX box{0, 0, 0, right, bottom, 1};
    state.immediate->CopySubresourceRegion(slot->texture.Get(), 0, 0, 0, 0, source, subresource, &box);
    return state.wrap(slot, width, height, stamp);
}

VideoFrame FrameUploader::fitted(ID3D11Texture2D* source, unsigned subresource, int width, int height, int outerWidth,
                                 int outerHeight, const FrameStamp& stamp)
{
    State& state = *m_state;
    if (source == nullptr || !state.device || width <= 0 || height <= 0 || outerWidth < 2 || outerHeight < 2) {
        return {};
    }
    const SourceViews* views = state.canDraw() ? state.viewsOf(source, subresource) : nullptr;
    if (views == nullptr) {
        return copy(source, subresource, width, height, stamp);
    }
    const std::shared_ptr<Slot> slot = state.lease(outerWidth, outerHeight, true);
    if (slot == nullptr) {
        return {};
    }
    const int evenWidth = outerWidth & ~1;
    const int evenHeight = outerHeight & ~1;
    const double factor = std::min(static_cast<double>(outerWidth) / width, static_cast<double>(outerHeight) / height);
    const int shownWidth = std::clamp(static_cast<int>(std::lround(width * factor)) & ~1, 2, evenWidth);
    const int shownHeight = std::clamp(static_cast<int>(std::lround(height * factor)) & ~1, 2, evenHeight);
    const int left = ((evenWidth - shownWidth) / 2) & ~1;
    const int top = ((evenHeight - shownHeight) / 2) & ~1;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(state.deferred->Map(state.scale.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        slot->busy.store(false, std::memory_order_release);
        return {};
    }
    const std::array<float, 4> visible{static_cast<float>(width) / static_cast<float>(views->width),
                                       static_cast<float>(height) / static_cast<float>(views->height), 0.0F, 0.0F};
    std::memcpy(mapped.pData, visible.data(), sizeof(visible));
    state.deferred->Unmap(state.scale.Get(), 0);
    constexpr float kLumaBlack[4] = {kBlackLuma, 0.0F, 0.0F, 0.0F};
    constexpr float kChromaNeutral[4] = {kNeutralChroma, kNeutralChroma, 0.0F, 0.0F};
    state.deferred->ClearRenderTargetView(slot->lumaTarget.Get(), kLumaBlack);
    state.deferred->ClearRenderTargetView(slot->chromaTarget.Get(), kChromaNeutral);
    ID3D11ShaderResourceView* const both[2] = {views->luma.Get(), views->chroma.Get()};
    state.draw(slot->lumaTarget.Get(), viewportOf(left, top, shownWidth, shownHeight), state.scaleLuma.Get(), both, 2);
    state.draw(slot->chromaTarget.Get(), viewportOf(left / 2, top / 2, shownWidth / 2, shownHeight / 2),
               state.scaleChroma.Get(), both, 2);
    if (!state.submit()) {
        slot->busy.store(false, std::memory_order_release);
        return {};
    }
    return state.wrap(slot, outerWidth, outerHeight, stamp);
}

bool FrameUploader::drawsOnGpu()
{
    return m_state->device && m_state->canDraw();
}

std::uint64_t FrameUploader::dropped() const noexcept
{
    return m_state->dropped.load(std::memory_order_relaxed);
}

void yuy2ToNv12(const std::uint8_t* pixels, std::ptrdiff_t pitch, int width, int height, std::uint8_t* luma,
                std::ptrdiff_t lumaPitch, std::uint8_t* chroma, std::ptrdiff_t chromaPitch) noexcept
{
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = pixels + y * pitch;
        std::uint8_t* out = luma + y * lumaPitch;
        for (int x = 0; x < width; ++x) {
            out[x] = row[2 * x];
        }
    }
    for (int y = 0; y < (height + 1) / 2; ++y) {
        const std::uint8_t* upper = pixels + static_cast<std::ptrdiff_t>(2 * y) * pitch;
        const std::uint8_t* lower = pixels + static_cast<std::ptrdiff_t>(std::min(2 * y + 1, height - 1)) * pitch;
        std::uint8_t* out = chroma + y * chromaPitch;
        for (int x = 0; x < (width + 1) / 2; ++x) {
            out[2 * x] = static_cast<std::uint8_t>((upper[4 * x + 1] + lower[4 * x + 1] + 1) / 2);
            out[2 * x + 1] = static_cast<std::uint8_t>((upper[4 * x + 3] + lower[4 * x + 3] + 1) / 2);
        }
    }
}

}
