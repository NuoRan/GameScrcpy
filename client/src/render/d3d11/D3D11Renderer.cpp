/**
 * @file D3D11Renderer.cpp
 * @brief D3D11 YUV/NV12 → RGB 渲染管线实现
 *
 * 核心流程:
 *   1. D3D11CreateDevice — 创建硬件设备 (Feature Level 11.0+)
 *   2. CreateSwapChain — DXGI 交换链 (FLIP_DISCARD 优先, 回退 DISCARD)
 *   3. D3DCompile — 运行时编译 HLSL 着色器 (全屏三角 VS + YUV/NV12 PS)
 *   4. 动态纹理 — Map/Unmap 上传 YUV 数据到 GPU
 *   5. Draw(3) — 无顶点缓冲, SV_VertexID 生成全屏三角形
 *   6. Present — VSync 显示
 */

#ifdef _WIN32

#include "D3D11Renderer.h"
#include "Logger.h"

#include <d3dcompiler.h>
#include <cassert>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace qsc {

// =============================================================================
// 内嵌 HLSL 着色器
// =============================================================================

/**
 * 全屏三角形顶点着色器 (无顶点缓冲)
 *
 * 利用 SV_VertexID 生成覆盖整个屏幕的超大三角形:
 *   id=0 → (-1, +1) → UV (0, 0)  左上
 *   id=1 → (+3, +1) → UV (2, 0)  右上×2
 *   id=2 → (-1, -3) → UV (0, 2)  左下×2
 *
 * 这比传统的两三角形全屏四边形更高效, 无需顶点缓冲和输入布局。
 */
static const char* k_fullscreenVS = R"(
struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VS_OUTPUT main(uint id : SV_VertexID) {
    VS_OUTPUT o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
)";

/**
 * YUV420P → RGB 像素着色器
 *
 * 输入: 3 个 R8_UNORM 纹理 (Y, U, V)
 * 输出: sRGB RGBA
 *
 * 使用 BT.709 有限范围矩阵:
 *   Y' = 1.16438 * (Y - 16/255)
 *   R = Y' + 1.79274 * Cr
 *   G = Y' - 0.21325 * Cb - 0.53291 * Cr
 *   B = Y' + 2.11240 * Cb
 */
static const char* k_yuv420pPS = R"(
Texture2D    texY : register(t0);
Texture2D    texU : register(t1);
Texture2D    texV : register(t2);
SamplerState sam  : register(s0);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float y = texY.Sample(sam, uv).r;
    float u = texU.Sample(sam, uv).r;
    float v = texV.Sample(sam, uv).r;

    // BT.709 limited range
    y = 1.16438 * (y - 0.0625);
    u -= 0.5;
    v -= 0.5;

    float3 rgb;
    rgb.r = y + 1.79274 * v;
    rgb.g = y - 0.21325 * u - 0.53291 * v;
    rgb.b = y + 2.11240 * u;

    return float4(saturate(rgb), 1.0);
}
)";

/**
 * NV12 → RGB 像素着色器
 *
 * 输入: R8_UNORM (Y) + R8G8_UNORM (UV 交织)
 * 输出: sRGB RGBA
 *
 * 适用于 NV12 软解帧和 D3D11VA 硬解帧。
 */
static const char* k_nv12PS = R"(
Texture2D    texY  : register(t0);
Texture2D    texUV : register(t1);
SamplerState sam   : register(s0);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float  y  = texY.Sample(sam, uv).r;
    float2 cb_cr = texUV.Sample(sam, uv).rg;

    // BT.709 limited range
    y = 1.16438 * (y - 0.0625);
    float u = cb_cr.r - 0.5;
    float v = cb_cr.g - 0.5;

    float3 rgb;
    rgb.r = y + 1.79274 * v;
    rgb.g = y - 0.21325 * u - 0.53291 * v;
    rgb.b = y + 2.11240 * u;

    return float4(saturate(rgb), 1.0);
}
)";

// =============================================================================
// 着色器编译辅助
// =============================================================================

static ComPtr<ID3DBlob> compileShaderFromSource(
    const char* source, const char* entryPoint, const char* target)
{
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errBlob;
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3
               | D3DCOMPILE_WARNINGS_ARE_ERRORS;

    HRESULT hr = D3DCompile(
        source, strlen(source),
        nullptr,    // sourceName
        nullptr,    // defines
        nullptr,    // include handler
        entryPoint, target,
        flags, 0,
        &blob, &errBlob
    );

    if (FAILED(hr)) {
        if (errBlob) {
            LOG_E("[D3D11] Shader compile error: %s",
                  (const char*)errBlob->GetBufferPointer());
        }
        return nullptr;
    }
    return blob;
}

// =============================================================================
// 构造 / 析构
// =============================================================================

D3D11Renderer::D3D11Renderer() = default;

D3D11Renderer::~D3D11Renderer()
{
    shutdown();
}

// =============================================================================
// 初始化
// =============================================================================

bool D3D11Renderer::initialize(HWND hwnd, int width, int height)
{
    if (m_initialized) {
        shutdown();
    }

    m_hwnd = hwnd;
    m_windowWidth  = (std::max)(width, 1);
    m_windowHeight = (std::max)(height, 1);

    if (!createDevice()) {
        LOG_E("[D3D11] Failed to create D3D11 device");
        shutdown();
        return false;
    }

    if (!createSwapChain(hwnd, m_windowWidth, m_windowHeight)) {
        LOG_E("[D3D11] Failed to create swap chain");
        shutdown();
        return false;
    }

    if (!createRenderTarget()) {
        LOG_E("[D3D11] Failed to create render target view");
        shutdown();
        return false;
    }

    if (!compileShaders()) {
        LOG_E("[D3D11] Failed to compile shaders");
        shutdown();
        return false;
    }

    if (!createSampler()) {
        LOG_E("[D3D11] Failed to create sampler state");
        shutdown();
        return false;
    }

    checkNV12Support();

    m_initialized = true;
    LOG_I("[D3D11] Renderer initialized (%dx%d, NV12 SRV: %s)",
          m_windowWidth, m_windowHeight,
          m_nv12SrvSupported ? "yes" : "no");
    return true;
}

bool D3D11Renderer::createDevice()
{
    UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL achievedLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,                        // default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,                        // no software rasterizer
        flags,
        featureLevels, _countof(featureLevels),
        D3D11_SDK_VERSION,
        &m_device, &achievedLevel, &m_context
    );

    if (FAILED(hr)) {
        // Retry without debug layer (may not be installed)
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            flags,
            featureLevels, _countof(featureLevels),
            D3D11_SDK_VERSION,
            &m_device, &achievedLevel, &m_context
        );
    }

    if (FAILED(hr)) {
        LOG_E("[D3D11] D3D11CreateDevice failed: 0x%08lx", hr);
        return false;
    }

    LOG_I("[D3D11] Device created, feature level: 0x%x", (unsigned)achievedLevel);
    return true;
}

bool D3D11Renderer::createSwapChain(HWND hwnd, int width, int height)
{
    // 获取 DXGI 工厂
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return false;

    // v13: 检测子窗口 — FLIP 交换链在 WS_CHILD 窗口上不可靠
    // 子窗口直接使用传统 DISCARD 模型，避免 DWM 合成/Present 异常 abort
    LONG style = ::GetWindowLongW(hwnd, GWL_STYLE);
    bool isChildWindow = (style & WS_CHILD) != 0;

    if (!isChildWindow) {
        // 仅顶级窗口尝试 DXGI 1.2+ FLIP 模型
        ComPtr<IDXGIFactory2> factory2;
        hr = adapter->GetParent(IID_PPV_ARGS(&factory2));
        if (SUCCEEDED(hr)) {
            DXGI_SWAP_CHAIN_DESC1 desc1 = {};
            desc1.Width  = (UINT)width;
            desc1.Height = (UINT)height;
            desc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc1.SampleDesc.Count = 1;
            desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc1.BufferCount = 2;
            desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // Win10+
            desc1.Flags = 0;

            ComPtr<IDXGISwapChain1> sc1;
            hr = factory2->CreateSwapChainForHwnd(
                m_device.Get(), hwnd, &desc1, nullptr, nullptr, &sc1);

            if (FAILED(hr)) {
                // 回退到 FLIP_SEQUENTIAL (Win8.1+)
                desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                hr = factory2->CreateSwapChainForHwnd(
                    m_device.Get(), hwnd, &desc1, nullptr, nullptr, &sc1);
            }

            if (SUCCEEDED(hr)) {
                sc1.As(&m_swapChain);
                LOG_I("[D3D11] Swap chain created (DXGI 1.2, flip model)");

                // 禁用 Alt+Enter 全屏切换
                factory2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
                return true;
            }
        }
    } else {
        LOG_I("[D3D11] Child window detected (style=0x%08lX), skipping FLIP model", style);
    }

    // 传统 DXGI 1.0: CreateSwapChain + DISCARD (兼容所有窗口类型)
    ComPtr<IDXGIFactory> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 1;
    desc.BufferDesc.Width  = (UINT)width;
    desc.BufferDesc.Height = (UINT)height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = factory->CreateSwapChain(m_device.Get(), &desc, &m_swapChain);
    if (FAILED(hr)) {
        LOG_E("[D3D11] CreateSwapChain failed: 0x%08lx", hr);
        return false;
    }

    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    LOG_I("[D3D11] Swap chain created (DXGI 1.0, discard model)");
    return true;
}

bool D3D11Renderer::createRenderTarget()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv);
    return SUCCEEDED(hr);
}

void D3D11Renderer::releaseRenderTarget()
{
    if (m_context) {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
    }
    m_rtv.Reset();
}

bool D3D11Renderer::compileShaders()
{
    // 顶点着色器 — 全屏三角形
    auto vsBlob = compileShaderFromSource(k_fullscreenVS, "main", "vs_5_0");
    if (!vsBlob) return false;

    HRESULT hr = m_device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
        nullptr, &m_vertexShader);
    if (FAILED(hr)) return false;

    // YUV420P 像素着色器
    auto ps420Blob = compileShaderFromSource(k_yuv420pPS, "main", "ps_5_0");
    if (!ps420Blob) return false;

    hr = m_device->CreatePixelShader(
        ps420Blob->GetBufferPointer(), ps420Blob->GetBufferSize(),
        nullptr, &m_psYUV420P);
    if (FAILED(hr)) return false;

    // NV12 像素着色器
    auto psNV12Blob = compileShaderFromSource(k_nv12PS, "main", "ps_5_0");
    if (!psNV12Blob) return false;

    hr = m_device->CreatePixelShader(
        psNV12Blob->GetBufferPointer(), psNV12Blob->GetBufferSize(),
        nullptr, &m_psNV12);
    if (FAILED(hr)) return false;

    return true;
}

bool D3D11Renderer::createSampler()
{
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter   = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD   = D3D11_FLOAT32_MAX;

    HRESULT hr = m_device->CreateSamplerState(&sd, &m_sampler);
    return SUCCEEDED(hr);
}

void D3D11Renderer::checkNV12Support()
{
    UINT formatSupport = 0;
    HRESULT hr = m_device->CheckFormatSupport(DXGI_FORMAT_NV12, &formatSupport);
    m_nv12SrvSupported = SUCCEEDED(hr)
        && (formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D)
        && (formatSupport & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE);
}

// =============================================================================
// 窗口大小改变
// =============================================================================

void D3D11Renderer::resize(int width, int height)
{
    if (!m_initialized) return;

    width  = (std::max)(width, 1);
    height = (std::max)(height, 1);

    if (width == m_windowWidth && height == m_windowHeight) return;

    LOG_I("[D3D11] resize: %dx%d -> %dx%d", m_windowWidth, m_windowHeight, width, height);

    m_windowWidth  = width;
    m_windowHeight = height;

    // 必须先释放 RTV, 否则 ResizeBuffers 会失败
    releaseRenderTarget();
    m_grabStagingTex.Reset();
    m_grabStagingWidth  = 0;
    m_grabStagingHeight = 0;

    HRESULT hr = m_swapChain->ResizeBuffers(
        0,                                  // 保持缓冲区数量
        (UINT)width, (UINT)height,
        DXGI_FORMAT_UNKNOWN,                // 保持格式
        0
    );
    if (FAILED(hr)) {
        LOG_E("[D3D11] ResizeBuffers failed: 0x%08lx", hr);
        return;
    }

    createRenderTarget();
}

// =============================================================================
// 关闭
// =============================================================================

void D3D11Renderer::shutdown()
{
    if (m_context) {
        m_context->ClearState();
        m_context->Flush();
    }

    // 释放所有 COM 对象
    m_grabStagingTex.Reset();
    m_hwStagingTex.Reset();
    m_srvHW_Y.Reset();
    m_srvHW_UV.Reset();
    m_texNV12_Y.Reset();
    m_texNV12_UV.Reset();
    m_srvNV12_Y.Reset();
    m_srvNV12_UV.Reset();
    m_texY.Reset();
    m_texU.Reset();
    m_texV.Reset();
    m_srvY.Reset();
    m_srvU.Reset();
    m_srvV.Reset();
    m_sampler.Reset();
    m_psNV12.Reset();
    m_psYUV420P.Reset();
    m_vertexShader.Reset();
    m_rtv.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();

    m_initialized = false;
    m_hwnd = nullptr;
    m_windowWidth = 0;
    m_windowHeight = 0;
    m_frameWidth = 0;
    m_frameHeight = 0;

    m_texYUVWidth = 0;
    m_texYUVHeight = 0;
    m_texNV12Width = 0;
    m_texNV12Height = 0;
    m_hwStagingWidth = 0;
    m_hwStagingHeight = 0;
    m_grabStagingWidth = 0;
    m_grabStagingHeight = 0;

    {
        std::lock_guard<std::mutex> lock(m_yuvMutex);
        m_cachedYData.clear();
        m_cachedYWidth = 0;
        m_cachedYHeight = 0;
    }
}

// =============================================================================
// 纹理管理
// =============================================================================

void D3D11Renderer::ensureYUV420PTextures(int width, int height)
{
    if (width == m_texYUVWidth && height == m_texYUVHeight) return;

    // 释放旧纹理
    m_texY.Reset(); m_texU.Reset(); m_texV.Reset();
    m_srvY.Reset(); m_srvU.Reset(); m_srvV.Reset();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format    = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage     = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    // Y 纹理: full resolution
    desc.Width  = (UINT)width;
    desc.Height = (UINT)height;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_texY);
    if (FAILED(hr)) { LOG_E("CreateTexture2D(Y) failed: 0x%08lx", hr); return; }
    hr = m_device->CreateShaderResourceView(m_texY.Get(), nullptr, &m_srvY);
    if (FAILED(hr)) { LOG_E("CreateSRV(Y) failed: 0x%08lx", hr); return; }

    // U 纹理: half resolution
    desc.Width  = (UINT)(width / 2);
    desc.Height = (UINT)(height / 2);
    hr = m_device->CreateTexture2D(&desc, nullptr, &m_texU);
    if (FAILED(hr)) { LOG_E("CreateTexture2D(U) failed: 0x%08lx", hr); return; }
    hr = m_device->CreateShaderResourceView(m_texU.Get(), nullptr, &m_srvU);
    if (FAILED(hr)) { LOG_E("CreateSRV(U) failed: 0x%08lx", hr); return; }

    // V 纹理: half resolution
    hr = m_device->CreateTexture2D(&desc, nullptr, &m_texV);
    if (FAILED(hr)) { LOG_E("CreateTexture2D(V) failed: 0x%08lx", hr); return; }
    hr = m_device->CreateShaderResourceView(m_texV.Get(), nullptr, &m_srvV);
    if (FAILED(hr)) { LOG_E("CreateSRV(V) failed: 0x%08lx", hr); return; }

    m_texYUVWidth  = width;
    m_texYUVHeight = height;
}

void D3D11Renderer::ensureNV12Textures(int width, int height)
{
    if (width == m_texNV12Width && height == m_texNV12Height) return;

    m_texNV12_Y.Reset();  m_texNV12_UV.Reset();
    m_srvNV12_Y.Reset();  m_srvNV12_UV.Reset();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Usage     = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    // Y 纹理: R8_UNORM, full resolution
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.Width  = (UINT)width;
    desc.Height = (UINT)height;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_texNV12_Y);
    if (FAILED(hr)) { LOG_E("CreateTexture2D(NV12_Y) failed: 0x%08lx", hr); return; }
    hr = m_device->CreateShaderResourceView(m_texNV12_Y.Get(), nullptr, &m_srvNV12_Y);
    if (FAILED(hr)) { LOG_E("CreateSRV(NV12_Y) failed: 0x%08lx", hr); return; }

    // UV 纹理: R8G8_UNORM, half resolution
    // NV12 UV 平面: 每行 width 字节 (width/2 个 UV 对, 每对 2 字节)
    desc.Format = DXGI_FORMAT_R8G8_UNORM;
    desc.Width  = (UINT)(width / 2);
    desc.Height = (UINT)(height / 2);
    hr = m_device->CreateTexture2D(&desc, nullptr, &m_texNV12_UV);
    if (FAILED(hr)) { LOG_E("CreateTexture2D(NV12_UV) failed: 0x%08lx", hr); return; }
    hr = m_device->CreateShaderResourceView(m_texNV12_UV.Get(), nullptr, &m_srvNV12_UV);
    if (FAILED(hr)) { LOG_E("CreateSRV(NV12_UV) failed: 0x%08lx", hr); return; }

    m_texNV12Width  = width;
    m_texNV12Height = height;
}

void D3D11Renderer::ensureHWStagingTexture(int width, int height)
{
    if (width == m_hwStagingWidth && height == m_hwStagingHeight) return;

    m_hwStagingTex.Reset();
    m_srvHW_Y.Reset();
    m_srvHW_UV.Reset();

    if (!m_nv12SrvSupported) return;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width  = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format    = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage     = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_hwStagingTex);
    if (FAILED(hr)) {
        LOG_W("[D3D11] Failed to create NV12 staging texture: 0x%08lx", hr);
        m_nv12SrvSupported = false;
        return;
    }

    // Y 平面 SRV: R8_UNORM
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
    hr = m_device->CreateShaderResourceView(m_hwStagingTex.Get(), &srvDesc, &m_srvHW_Y);
    if (FAILED(hr)) {
        m_hwStagingTex.Reset();
        m_nv12SrvSupported = false;
        return;
    }

    // UV 平面 SRV: R8G8_UNORM
    srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    hr = m_device->CreateShaderResourceView(m_hwStagingTex.Get(), &srvDesc, &m_srvHW_UV);
    if (FAILED(hr)) {
        m_hwStagingTex.Reset();
        m_srvHW_Y.Reset();
        m_nv12SrvSupported = false;
        return;
    }

    m_hwStagingWidth  = width;
    m_hwStagingHeight = height;
}

void D3D11Renderer::ensureGrabStagingTexture()
{
    if (m_grabStagingWidth == m_windowWidth && m_grabStagingHeight == m_windowHeight)
        return;

    m_grabStagingTex.Reset();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width  = (UINT)m_windowWidth;
    desc.Height = (UINT)m_windowHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage     = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_grabStagingTex);
    if (SUCCEEDED(hr)) {
        m_grabStagingWidth  = m_windowWidth;
        m_grabStagingHeight = m_windowHeight;
    }
}

// =============================================================================
// 动态纹理上传
// =============================================================================

void D3D11Renderer::updateDynamicTexture(
    ID3D11Texture2D* texture,
    const uint8_t* data,
    int srcStride,
    int srcRowBytes,
    int texHeight)
{
    if (!texture || !data) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    if (srcStride == (int)mapped.RowPitch && srcStride == srcRowBytes) {
        // 快速路径: 无 padding, 直接整块拷贝
        memcpy(mapped.pData, data, (size_t)srcStride * texHeight);
    } else {
        // 逐行拷贝 (处理 stride 不匹配)
        const int rowBytes = (std::min)(srcRowBytes, (int)mapped.RowPitch);
        for (int row = 0; row < texHeight; ++row) {
            memcpy(
                static_cast<uint8_t*>(mapped.pData) + (size_t)row * mapped.RowPitch,
                data + (size_t)row * srcStride,
                rowBytes
            );
        }
    }

    m_context->Unmap(texture, 0);
}

// =============================================================================
// 渲染辅助
// =============================================================================

void D3D11Renderer::setupViewportAndRenderTarget()
{
    D3D11_VIEWPORT vp = {};
    vp.Width    = (float)m_windowWidth;
    vp.Height   = (float)m_windowHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
}

void D3D11Renderer::drawFullscreenTriangle()
{
    // 无顶点缓冲, 无输入布局 — 顶点着色器从 SV_VertexID 生成位置和 UV
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->IASetInputLayout(nullptr);
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    m_context->Draw(3, 0);
}

void D3D11Renderer::cacheYPlane(
    const uint8_t* dataY, int linesizeY, int width, int height)
{
    std::lock_guard<std::mutex> lock(m_yuvMutex);
    const size_t totalBytes = (size_t)width * height;
    m_cachedYData.resize(totalBytes);
    m_cachedYWidth  = width;
    m_cachedYHeight = height;

    if (linesizeY == width) {
        // 无 padding, 直接拷贝
        memcpy(m_cachedYData.data(), dataY, totalBytes);
    } else {
        // 逐行拷贝去除 padding
        for (int row = 0; row < height; ++row) {
            memcpy(
                m_cachedYData.data() + (size_t)row * width,
                dataY + (size_t)row * linesizeY,
                width
            );
        }
    }
}

// =============================================================================
// 帧渲染: YUV420P
// =============================================================================

void D3D11Renderer::renderYUV420P(
    const uint8_t* dataY, const uint8_t* dataU, const uint8_t* dataV,
    int linesizeY, int linesizeU, int linesizeV,
    int frameWidth, int frameHeight)
{
    if (!m_initialized || !dataY || !dataU || !dataV) return;
    if (frameWidth <= 0 || frameHeight <= 0) return;

    m_frameWidth  = frameWidth;
    m_frameHeight = frameHeight;

    // 确保纹理尺寸正确
    ensureYUV420PTextures(frameWidth, frameHeight);

    static int yuv420pCount = 0;
    if (++yuv420pCount <= 1) {
        LOG_I("[D3D11] renderYUV420P #%d: frame=%dx%d, texY=%p, srvY=%p, viewport=%dx%d",
              yuv420pCount, frameWidth, frameHeight,
              m_texY.Get(), m_srvY.Get(), m_windowWidth, m_windowHeight);
    }

    // 上传 YUV 数据到 GPU
    updateDynamicTexture(m_texY.Get(), dataY, linesizeY, frameWidth,     frameHeight);
    updateDynamicTexture(m_texU.Get(), dataU, linesizeU, frameWidth / 2, frameHeight / 2);
    updateDynamicTexture(m_texV.Get(), dataV, linesizeV, frameWidth / 2, frameHeight / 2);

    // 缓存 Y 分量用于 grabGrayFrame
    cacheYPlane(dataY, linesizeY, frameWidth, frameHeight);

    // 设置渲染管线
    setupViewportAndRenderTarget();
    m_context->PSSetShader(m_psYUV420P.Get(), nullptr, 0);

    ID3D11ShaderResourceView* srvs[3] = {
        m_srvY.Get(), m_srvU.Get(), m_srvV.Get()
    };
    m_context->PSSetShaderResources(0, 3, srvs);

    // 绘制
    drawFullscreenTriangle();

    // 解绑 SRV (防止 D3D11 WARNING)
    ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
    m_context->PSSetShaderResources(0, 3, nullSRVs);
}

// =============================================================================
// 帧渲染: NV12
// =============================================================================

void D3D11Renderer::renderNV12(
    const uint8_t* dataY, const uint8_t* dataUV,
    int linesizeY, int linesizeUV,
    int frameWidth, int frameHeight)
{
    if (!m_initialized || !dataY || !dataUV) return;
    if (frameWidth <= 0 || frameHeight <= 0) return;

    m_frameWidth  = frameWidth;
    m_frameHeight = frameHeight;

    // 确保纹理尺寸正确
    ensureNV12Textures(frameWidth, frameHeight);

    // 上传 Y 数据: R8_UNORM, srcRowBytes = frameWidth
    updateDynamicTexture(m_texNV12_Y.Get(), dataY, linesizeY,
                         frameWidth, frameHeight);

    // 上传 UV 数据: R8G8_UNORM, srcRowBytes = frameWidth (width/2 pairs × 2 bytes)
    updateDynamicTexture(m_texNV12_UV.Get(), dataUV, linesizeUV,
                         frameWidth, frameHeight / 2);

    // 缓存 Y 分量
    cacheYPlane(dataY, linesizeY, frameWidth, frameHeight);

    // 设置渲染管线
    setupViewportAndRenderTarget();
    m_context->PSSetShader(m_psNV12.Get(), nullptr, 0);

    ID3D11ShaderResourceView* srvs[2] = {
        m_srvNV12_Y.Get(), m_srvNV12_UV.Get()
    };
    m_context->PSSetShaderResources(0, 2, srvs);

    // 绘制
    drawFullscreenTriangle();

    // 解绑 SRV
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    m_context->PSSetShaderResources(0, 2, nullSRVs);
}

// =============================================================================
// 帧渲染: 硬件帧 (D3D11VA 零拷贝)
// =============================================================================

bool D3D11Renderer::renderHWFrame(ID3D11Texture2D* texture, int index)
{
    if (!m_initialized || !texture) return false;

    if (!m_nv12SrvSupported) {
        LOG_W_ONCE("[D3D11] NV12 SRV not supported, cannot render HW frame zero-copy");
        return false;
    }

    // 获取源纹理尺寸
    D3D11_TEXTURE2D_DESC srcDesc;
    texture->GetDesc(&srcDesc);

    int w = (int)srcDesc.Width;
    int h = (int)srcDesc.Height;
    m_frameWidth  = w;
    m_frameHeight = h;

    // 确保中间 NV12 纹理存在
    ensureHWStagingTexture(w, h);
    if (!m_hwStagingTex) return false;

    // 从 D3D11VA 纹理数组拷贝指定 subresource 到独立纹理
    // D3D11VA 解码输出通常是 texture2D array, 通过 index 索引各帧
    m_context->CopySubresourceRegion(
        m_hwStagingTex.Get(), 0,        // dst: subresource 0
        0, 0, 0,                        // dst offset
        texture,                        // src: D3D11VA 纹理
        (UINT)index,                    // src subresource index
        nullptr                         // 整个 subresource
    );

    // 设置渲染管线
    setupViewportAndRenderTarget();
    m_context->PSSetShader(m_psNV12.Get(), nullptr, 0);

    ID3D11ShaderResourceView* srvs[2] = {
        m_srvHW_Y.Get(), m_srvHW_UV.Get()
    };
    m_context->PSSetShaderResources(0, 2, srvs);

    drawFullscreenTriangle();

    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    m_context->PSSetShaderResources(0, 2, nullSRVs);

    return true;
}

// =============================================================================
// 呈现
// =============================================================================

void D3D11Renderer::present(bool vsync)
{
    if (!m_swapChain) return;
    HRESULT hr = m_swapChain->Present(vsync ? 1 : 0, 0);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            LOG_E("[D3D11] Present: device lost (0x%08lx), renderer needs reset", hr);
            m_initialized = false;
        } else {
            LOG_W("[D3D11] Present failed: 0x%08lx", hr);
        }
    }
    static int presentCount = 0;
    if (++presentCount <= 1) {
        LOG_I("[D3D11] Present #%d: hr=0x%08lx, viewport=%dx%d, frame=%dx%d",
              presentCount, hr, m_windowWidth, m_windowHeight, m_frameWidth, m_frameHeight);
    }
}

// =============================================================================
// 重新呈现: 从缓存的 GPU 纹理重新渲染 (WM_PAINT 恢复)
// =============================================================================

void D3D11Renderer::rePresent()
{
    if (!m_initialized || !m_swapChain) return;

    // 从缓存的 GPU 纹理重新渲染
    if (m_srvY.Get() && m_srvU.Get() && m_srvV.Get()) {
        // YUV420P 模式
        setupViewportAndRenderTarget();
        m_context->PSSetShader(m_psYUV420P.Get(), nullptr, 0);
        ID3D11ShaderResourceView* srvs[3] = { m_srvY.Get(), m_srvU.Get(), m_srvV.Get() };
        m_context->PSSetShaderResources(0, 3, srvs);
        drawFullscreenTriangle();
        ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
        m_context->PSSetShaderResources(0, 3, nullSRVs);
    } else if (m_srvNV12_Y.Get() && m_srvNV12_UV.Get()) {
        // NV12 模式
        setupViewportAndRenderTarget();
        m_context->PSSetShader(m_psNV12.Get(), nullptr, 0);
        ID3D11ShaderResourceView* srvs[2] = { m_srvNV12_Y.Get(), m_srvNV12_UV.Get() };
        m_context->PSSetShaderResources(0, 2, srvs);
        drawFullscreenTriangle();
        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        m_context->PSSetShaderResources(0, 2, nullSRVs);
    } else if (m_srvHW_Y.Get() && m_srvHW_UV.Get()) {
        // HW 零拷贝模式
        setupViewportAndRenderTarget();
        m_context->PSSetShader(m_psNV12.Get(), nullptr, 0);
        ID3D11ShaderResourceView* srvs[2] = { m_srvHW_Y.Get(), m_srvHW_UV.Get() };
        m_context->PSSetShaderResources(0, 2, srvs);
        drawFullscreenTriangle();
        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        m_context->PSSetShaderResources(0, 2, nullSRVs);
    } else {
        return;  // 没有纹理可用
    }

    // 呈现
    HRESULT hr = m_swapChain->Present(0, 0);
    if (FAILED(hr) && (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)) {
        LOG_E("[D3D11] rePresent: device lost (0x%08lx)", hr);
        m_initialized = false;
    }
}

bool D3D11Renderer::grabFrame(uint8_t* outData, int* outWidth, int* outHeight)
{
    if (!m_initialized || !outData || !outWidth || !outHeight) return false;

    // 获取后台缓冲区
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    // 确保 staging 纹理存在
    ensureGrabStagingTexture();
    if (!m_grabStagingTex) return false;

    // 拷贝后台缓冲区到 staging 纹理
    m_context->CopyResource(m_grabStagingTex.Get(), backBuffer.Get());

    // Map 读取
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = m_context->Map(m_grabStagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    *outWidth  = m_windowWidth;
    *outHeight = m_windowHeight;

    const int rowBytes = m_windowWidth * 4;  // RGBA, 4 bytes per pixel
    for (int row = 0; row < m_windowHeight; ++row) {
        memcpy(
            outData + (size_t)row * rowBytes,
            static_cast<const uint8_t*>(mapped.pData) + (size_t)row * mapped.RowPitch,
            rowBytes
        );
    }

    m_context->Unmap(m_grabStagingTex.Get(), 0);
    return true;
}

// =============================================================================
// 帧抓取: 灰度 Y 分量
// =============================================================================

bool D3D11Renderer::grabGrayFrame(uint8_t* outGray, int* outWidth, int* outHeight)
{
    if (!outGray || !outWidth || !outHeight) return false;

    std::lock_guard<std::mutex> lock(m_yuvMutex);
    if (m_cachedYData.empty()) return false;

    *outWidth  = m_cachedYWidth;
    *outHeight = m_cachedYHeight;
    memcpy(outGray, m_cachedYData.data(), m_cachedYData.size());
    return true;
}

} // namespace qsc

#endif // _WIN32
