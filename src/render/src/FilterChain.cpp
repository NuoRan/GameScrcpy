#include "FilterChain.h"

#include "video_vs_full.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace gs::render {

namespace {

struct FrameConstants {
    float source[4];
    float original[4];
    float output[4];
    std::uint32_t frame;
    std::uint32_t padding[3];
};
static_assert(sizeof(FrameConstants) == 64);

constexpr std::uint32_t kReportEveryFrames = 60;
constexpr double kTimingSmoothing = 0.1;

void describeSize(float (&out)[4], QSize size)
{
    out[0] = static_cast<float>(size.width());
    out[1] = static_cast<float>(size.height());
    out[2] = 1.0F / out[0];
    out[3] = 1.0F / out[1];
}

DXGI_FORMAT formatOf(ext::PassFormat format)
{
    return format == ext::PassFormat::Rgba16f ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
}

QSize scaled(QSize size, double scale)
{
    return {std::max(1, static_cast<int>(std::lround(size.width() * scale))),
            std::max(1, static_cast<int>(std::lround(size.height() * scale)))};
}

std::string describe(const std::vector<ext::Diagnostic>& diagnostics, const std::string& fileName)
{
    std::string text;
    for (const ext::Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == ext::Severity::Error) {
            text += fileName + " " + diagnostic.path + ": " + std::string(ext::codeName(diagnostic.code)) +
                    (diagnostic.detail.empty() ? std::string() : " (" + diagnostic.detail + ")") + "\n";
        }
    }
    return text;
}

bool sameProgram(const VideoFilter& a, const VideoFilter& b)
{
    return a.id == b.id && a.fileName == b.fileName && a.source == b.source && a.textures == b.textures;
}

}

void FilterChain::setFilters(std::vector<VideoFilter> filters)
{
    if (filters == m_wanted) {
        return;
    }
    m_wanted = std::move(filters);
    m_wantedChanged = true;
}

bool FilterChain::prepare(ID3D11Device* device, ID3D11DeviceContext* context, QSize original)
{
    if (m_wantedChanged) {
        m_wantedChanged = false;
        std::vector<Compiled> next;
        next.reserve(m_wanted.size());
        for (const VideoFilter& filter : m_wanted) {
            const auto existing = std::ranges::find_if(
                m_filters, [&filter](const Compiled& compiled) { return sameProgram(compiled.filter, filter); });
            if (existing == m_filters.end()) {
                next.push_back(compile(device, filter));
                continue;
            }
            Compiled reused = std::move(*existing);
            reused.valuesChanged = reused.valuesChanged || reused.filter.values != filter.values;
            reused.filter = filter;
            next.push_back(std::move(reused));
        }
        m_filters = std::move(next);
        resetTimings(device);
        m_statusChanged = true;
    }
    m_active = !original.isEmpty() && std::ranges::any_of(m_filters, &Compiled::ready) && createShared(device) &&
               ensureTarget(device, m_original, original, DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!m_active) {
        return false;
    }
    for (Compiled& compiled : m_filters) {
        if (!compiled.ready) {
            continue;
        }
        for (Pass& pass : compiled.passes) {
            if (!ensureTarget(device, pass.output, scaled(original, pass.scale), pass.format)) {
                m_active = false;
                return false;
            }
        }
        if (compiled.valuesChanged) {
            writeParams(context, compiled);
            compiled.valuesChanged = false;
        }
    }
    return true;
}

ID3D11ShaderResourceView* FilterChain::run(ID3D11DeviceContext* context)
{
    Timing& timing = m_timings[m_timingAt];
    m_timingAt = (m_timingAt + 1) % m_timings.size();
    const bool timed = timing.disjoint && timing.stamps.size() == readyPassCount() + 1;
    if (timed && timing.issued) {
        collectTimings(context, timing);
    }
    if (timed) {
        context->Begin(timing.disjoint.Get());
        context->End(timing.stamps.front().Get());
    }

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->RSSetState(m_rasterizer.Get());
    context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFU);
    context->OMSetDepthStencilState(nullptr, 0);
    ID3D11SamplerState* const samplers[2] = {m_samplers[0].Get(), m_samplers[1].Get()};
    context->PSSetSamplers(0, 2, samplers);

    ID3D11ShaderResourceView* source = m_original.view.Get();
    QSize sourceSize = m_original.size;
    std::size_t stamp = 1;
    for (Compiled& compiled : m_filters) {
        if (!compiled.ready) {
            continue;
        }
        for (Pass& pass : compiled.passes) {
            FrameConstants constants{};
            describeSize(constants.source, sourceSize);
            describeSize(constants.original, m_original.size);
            describeSize(constants.output, pass.output.size);
            constants.frame = m_frame;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(pass.frame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                std::memcpy(mapped.pData, &constants, sizeof(constants));
                context->Unmap(pass.frame.Get(), 0);
            }
            context->OMSetRenderTargets(1, pass.output.target.GetAddressOf(), nullptr);
            const D3D11_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(pass.output.size.width()),
                                          static_cast<float>(pass.output.size.height()), 0.0F, 1.0F};
            context->RSSetViewports(1, &viewport);
            context->PSSetShader(pass.shader.Get(), nullptr, 0);
            ID3D11Buffer* const buffers[2] = {pass.frame.Get(), compiled.params.Get()};
            context->PSSetConstantBuffers(0, 2, buffers);
            std::array<ID3D11ShaderResourceView*, 2 + ext::kMaxFilterTextures> views{};
            views[0] = m_original.view.Get();
            views[1] = source;
            for (std::size_t i = 0; i < compiled.textures.size(); ++i) {
                views[2 + i] = compiled.textures[i].Get();
            }
            const auto count = static_cast<UINT>(2 + compiled.textures.size());
            context->PSSetShaderResources(0, count, views.data());
            context->Draw(3, 0);
            const std::array<ID3D11ShaderResourceView*, 2 + ext::kMaxFilterTextures> unbound{};
            context->PSSetShaderResources(0, count, unbound.data());
            if (timed) {
                context->End(timing.stamps[stamp++].Get());
            }
            source = pass.output.view.Get();
            sourceSize = pass.output.size;
        }
    }
    if (timed) {
        context->End(timing.disjoint.Get());
        timing.issued = true;
    }
    ++m_frame;
    if (++m_framesSinceReport >= kReportEveryFrames) {
        m_framesSinceReport = 0;
        m_statusChanged = true;
    }
    return source;
}

bool FilterChain::takeStatus(std::vector<FilterStatus>& status)
{
    if (!m_statusChanged) {
        return false;
    }
    m_statusChanged = false;
    status.clear();
    for (const Compiled& compiled : m_filters) {
        FilterStatus entry{.id = compiled.filter.id, .ready = compiled.ready, .error = compiled.error};
        for (const Pass& pass : compiled.passes) {
            entry.passes.push_back(pass.name);
        }
        entry.passMs = compiled.passMs;
        status.push_back(std::move(entry));
    }
    return true;
}

void FilterChain::release()
{
    m_filters.clear();
    m_wantedChanged = true;
    m_original = {};
    m_active = false;
    m_vertexShader.Reset();
    m_samplers = {};
    m_rasterizer.Reset();
    m_timings = {};
    m_timingAt = 0;
}

bool FilterChain::createShared(ID3D11Device* device)
{
    if (m_vertexShader) {
        return true;
    }
    D3D11_SAMPLER_DESC linear{};
    linear.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    linear.AddressU = linear.AddressV = linear.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    linear.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_SAMPLER_DESC point = linear;
    point.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    const bool created = SUCCEEDED(device->CreateVertexShader(kVideoVertexShaderFullScreen,
                                                              sizeof(kVideoVertexShaderFullScreen), nullptr,
                                                              &m_vertexShader)) &&
                         SUCCEEDED(device->CreateSamplerState(&linear, &m_samplers[0])) &&
                         SUCCEEDED(device->CreateSamplerState(&point, &m_samplers[1])) &&
                         SUCCEEDED(device->CreateRasterizerState(&rasterizer, &m_rasterizer));
    if (!created) {
        m_vertexShader.Reset();
        m_samplers = {};
        m_rasterizer.Reset();
    }
    return created;
}

FilterChain::Compiled FilterChain::compile(ID3D11Device* device, const VideoFilter& filter) const
{
    Compiled compiled;
    compiled.filter = filter;
    compiled.source = ext::parseFilterSource(filter.source);
    if (!compiled.source.usable()) {
        compiled.error = describe(compiled.source.diagnostics, filter.fileName);
        return compiled;
    }
    const std::string text = ext::filterShaderText(compiled.source, filter.fileName);
    for (const ext::FilterPass& declared : compiled.source.passes) {
        ComPtr<ID3DBlob> code;
        ComPtr<ID3DBlob> errors;
        const std::string entry = ext::passEntryPoint(declared);
        if (FAILED(D3DCompile(text.data(), text.size(), filter.fileName.c_str(), nullptr, nullptr, entry.c_str(),
                              "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(),
                              errors.GetAddressOf()))) {
            compiled.error = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                                  errors->GetBufferSize())
                                    : std::string("the shader did not compile");
            compiled.passes.clear();
            return compiled;
        }
        Pass pass{.name = declared.name, .scale = declared.scale, .format = formatOf(declared.format)};
        D3D11_BUFFER_DESC frame{};
        frame.ByteWidth = sizeof(FrameConstants);
        frame.Usage = D3D11_USAGE_DYNAMIC;
        frame.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        frame.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &pass.shader)) ||
            FAILED(device->CreateBuffer(&frame, nullptr, &pass.frame))) {
            compiled.error = "the graphics device did not accept the shader";
            compiled.passes.clear();
            return compiled;
        }
        compiled.passes.push_back(std::move(pass));
    }
    D3D11_BUFFER_DESC params{};
    params.ByteWidth = static_cast<UINT>(ext::constantBufferSize(compiled.source.params));
    params.Usage = D3D11_USAGE_DYNAMIC;
    params.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    params.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&params, nullptr, &compiled.params))) {
        compiled.error = "the graphics device did not accept the parameters";
        compiled.passes.clear();
        return compiled;
    }
    for (const ext::FilterTexture& declared : compiled.source.textures) {
        const auto image = filter.textures.find(declared.name);
        if (image == filter.textures.end() || image->second.isNull()) {
            compiled.error = "the texture " + declared.file + " is missing";
            compiled.passes.clear();
            return compiled;
        }
        const QImage rgba = image->second.convertToFormat(QImage::Format_RGBA8888);
        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(rgba.width());
        description.Height = static_cast<UINT>(rgba.height());
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA data{rgba.constBits(), static_cast<UINT>(rgba.bytesPerLine()), 0};
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device->CreateTexture2D(&description, &data, &texture)) ||
            FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
            compiled.error = "the graphics device did not accept the texture " + declared.file;
            compiled.passes.clear();
            return compiled;
        }
        compiled.textures.push_back(std::move(view));
    }
    compiled.passMs.assign(compiled.passes.size(), 0.0);
    compiled.ready = true;
    return compiled;
}

bool FilterChain::ensureTarget(ID3D11Device* device, Target& target, QSize size, DXGI_FORMAT format) const
{
    if (target.texture && target.size == size && target.format == format) {
        return true;
    }
    target = {};
    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(size.width());
    description.Height = static_cast<UINT>(size.height());
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&description, nullptr, &target.texture)) ||
        FAILED(device->CreateRenderTargetView(target.texture.Get(), nullptr, &target.target)) ||
        FAILED(device->CreateShaderResourceView(target.texture.Get(), nullptr, &target.view))) {
        target = {};
        return false;
    }
    target.size = size;
    target.format = format;
    return true;
}

void FilterChain::writeParams(ID3D11DeviceContext* context, Compiled& compiled) const
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(compiled.params.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    auto* bytes = static_cast<std::uint8_t*>(mapped.pData);
    std::memset(bytes, 0, ext::constantBufferSize(compiled.source.params));
    for (std::size_t i = 0; i < compiled.source.params.size(); ++i) {
        const ext::FilterParam& param = compiled.source.params[i];
        const auto given = compiled.filter.values.find(param.name);
        double value = given != compiled.filter.values.end() && !std::isnan(given->second) ? given->second
                                                                                           : param.defaultValue;
        if (param.min) {
            value = std::max(value, *param.min);
        }
        if (param.max) {
            value = std::min(value, *param.max);
        }
        std::uint8_t* const at = bytes + i * 4;
        switch (param.type) {
        case ext::ParamType::Float: {
            const auto written = static_cast<float>(value);
            std::memcpy(at, &written, sizeof(written));
            break;
        }
        case ext::ParamType::Int: {
            const auto written = static_cast<std::int32_t>(std::lround(value));
            std::memcpy(at, &written, sizeof(written));
            break;
        }
        case ext::ParamType::Bool: {
            const std::uint32_t written = value != 0.0 ? 1U : 0U;
            std::memcpy(at, &written, sizeof(written));
            break;
        }
        }
    }
    context->Unmap(compiled.params.Get(), 0);
}

void FilterChain::resetTimings(ID3D11Device* device)
{
    const std::size_t stamps = readyPassCount() + 1;
    for (Timing& timing : m_timings) {
        timing = {};
        const D3D11_QUERY_DESC disjoint{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        const D3D11_QUERY_DESC stamp{D3D11_QUERY_TIMESTAMP, 0};
        if (FAILED(device->CreateQuery(&disjoint, &timing.disjoint))) {
            timing = {};
            continue;
        }
        timing.stamps.resize(stamps);
        for (ComPtr<ID3D11Query>& query : timing.stamps) {
            if (FAILED(device->CreateQuery(&stamp, &query))) {
                timing = {};
                break;
            }
        }
    }
    m_timingAt = 0;
}

void FilterChain::collectTimings(ID3D11DeviceContext* context, Timing& timing)
{
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
    if (context->GetData(timing.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
        disjoint.Disjoint || disjoint.Frequency == 0) {
        return;
    }
    std::vector<UINT64> stamps(timing.stamps.size());
    for (std::size_t i = 0; i < stamps.size(); ++i) {
        if (context->GetData(timing.stamps[i].Get(), &stamps[i], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) !=
            S_OK) {
            return;
        }
    }
    std::size_t at = 0;
    for (Compiled& compiled : m_filters) {
        if (!compiled.ready) {
            continue;
        }
        for (double& smoothed : compiled.passMs) {
            const double ms =
                static_cast<double>(stamps[at + 1] - stamps[at]) * 1000.0 / static_cast<double>(disjoint.Frequency);
            smoothed = smoothed == 0.0 ? ms : smoothed + kTimingSmoothing * (ms - smoothed);
            ++at;
        }
    }
}

std::size_t FilterChain::readyPassCount() const
{
    std::size_t count = 0;
    for (const Compiled& compiled : m_filters) {
        if (compiled.ready) {
            count += compiled.passes.size();
        }
    }
    return count;
}

}
