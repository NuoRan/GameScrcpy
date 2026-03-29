#ifndef D3D11RENDERER_H
#define D3D11RENDERER_H

/**
 * @file D3D11Renderer.h
 * @brief D3D11 YUV/NV12 → RGB 渲染管线 / D3D11 YUV/NV12 → RGB Rendering Pipeline
 *
 * 纯 D3D11 实现，替代 QOpenGLWidget 渲染路径。
 * Pure D3D11 implementation, replacing the QOpenGLWidget rendering path.
 *
 * 支持:
 * - YUV420P 软解帧渲染 (3 个 R8 纹理 + YUV→RGB 像素着色器)
 * - NV12 软解/硬解帧渲染 (R8 Y + R8G8 UV 纹理)
 * - D3D11VA 硬解帧零拷贝渲染 (CopySubresourceRegion + NV12 着色器)
 * - 帧抓取: RGBA (CopyResource → staging) 和灰度 Y (缓存副本)
 *
 * 性能目标: 60fps 1080p 渲染, GPU < 5%
 *
 * 线程模型: 所有方法必须在同一线程调用 (GUI 线程),
 *           D3D11 设备使用 SINGLETHREADED 创建。
 */

#ifdef _WIN32

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace qsc {

class D3D11Renderer
{
public:
    D3D11Renderer();
    ~D3D11Renderer();

    // 禁止拷贝
    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    // =========================================================================
    // 生命周期
    // =========================================================================

    /**
     * @brief 初始化 D3D11 设备、交换链和着色器
     * @param hwnd 目标窗口句柄
     * @param width 初始窗口宽度
     * @param height 初始窗口高度
     * @return 成功返回 true
     */
    bool initialize(HWND hwnd, int width, int height);

    /**
     * @brief 窗口大小改变时调整交换链
     * @param width 新窗口宽度
     * @param height 新窗口高度
     */
    void resize(int width, int height);

    /**
     * @brief 释放所有 D3D11 资源
     */
    void shutdown();

    // =========================================================================
    // 帧渲染
    // =========================================================================

    /**
     * @brief 渲染 YUV420P 帧 (3 个独立平面)
     */
    void renderYUV420P(const uint8_t* dataY, const uint8_t* dataU, const uint8_t* dataV,
                       int linesizeY, int linesizeU, int linesizeV,
                       int frameWidth, int frameHeight);

    /**
     * @brief 渲染 NV12 帧 (Y + 交织 UV)
     */
    void renderNV12(const uint8_t* dataY, const uint8_t* dataUV,
                    int linesizeY, int linesizeUV,
                    int frameWidth, int frameHeight);

    /**
     * @brief 零拷贝渲染 D3D11VA 硬件帧
     *
     * 前提: texture 必须属于本渲染器的 D3D11 设备 (通过 device() 获取后
     *       传给 FFmpeg hw_device_ctx). 否则 CopySubresourceRegion 会失败。
     *
     * @param texture D3D11VA 解码输出的 ID3D11Texture2D* (NV12 格式, 纹理数组)
     * @param index texture array 中的 subresource index
     * @return 成功返回 true
     */
    bool renderHWFrame(ID3D11Texture2D* texture, int index);

    /**
     * @brief 呈现后台缓冲区到屏幕
     * @param vsync 是否等待垂直同步 (true: Present(1,0), false: Present(0,0))
     */
    void present(bool vsync = true);

    /**
     * @brief 从缓存的 GPU 纹理重新渲染并呈现 (用于 WM_PAINT 恢复)
     *
     * 当 Windows/Qt 擦除 HWND 表面后, 需要从 GPU 纹理重新绘制。
     * 不需要 CPU 帧数据 — 直接使用上次 renderYUV420P/renderNV12 上传的纹理。
     */
    void rePresent();

    // =========================================================================
    // 帧抓取
    // =========================================================================

    /**
     * @brief 抓取当前渲染结果为 RGBA 图像
     *
     * 将后台缓冲区拷贝到 staging 纹理后读回 CPU.
     * 输出为 RGBA 格式, 每像素 4 字节.
     *
     * @param outData 输出缓冲区 (调用者分配, 至少 width*height*4 字节)
     * @param outWidth 输出帧宽度
     * @param outHeight 输出帧高度
     * @return 成功返回 true
     */
    bool grabFrame(uint8_t* outData, int* outWidth, int* outHeight);

    /**
     * @brief 抓取当前帧的灰度 Y 分量
     *
     * 使用 renderYUV420P/renderNV12 时缓存的 Y 数据, 不触发 GPU 回读.
     *
     * @param outGray 输出缓冲区 (调用者分配, 至少 width*height 字节)
     * @param outWidth 输出帧宽度
     * @param outHeight 输出帧高度
     * @return 成功返回 true
     */
    bool grabGrayFrame(uint8_t* outGray, int* outWidth, int* outHeight);

    // =========================================================================
    // 状态查询
    // =========================================================================

    bool isInitialized() const { return m_initialized; }
    int frameWidth() const { return m_frameWidth; }
    int frameHeight() const { return m_frameHeight; }
    int windowWidth() const { return m_windowWidth; }
    int windowHeight() const { return m_windowHeight; }

    /**
     * @brief 设置锐化强度
     * @param strength 0.0 = 关闭, 1.0 = 最大锐化 (CAS)
     */
    void setSharpenStrength(float strength);
    float sharpenStrength() const { return m_sharpenStrength; }

    /**
     * @brief 获取 D3D11 设备
     *
     * 用于硬解集成: 将此设备传给 FFmpeg 的 av_hwdevice_ctx,
     * 使解码输出的 ID3D11Texture2D 与渲染器共享同一设备,
     * 从而支持 renderHWFrame 零拷贝。
     */
    ID3D11Device* device() const { return m_device.Get(); }
    ID3D11DeviceContext* deviceContext() const { return m_context.Get(); }

    /**
     * @brief 检查是否支持 NV12 纹理作为 SRV
     *
     * D3D11.1+ (Win8+) 才支持 NV12 SRV. 不支持时 renderHWFrame 和
     * renderNV12 使用 CPU 中转。
     */
    bool supportsNV12SRV() const { return m_nv12SrvSupported; }

private:
    // =========================================================================
    // 初始化辅助
    // =========================================================================
    bool createDevice();
    bool createSwapChain(HWND hwnd, int width, int height);
    bool createRenderTarget();
    bool compileShaders();
    bool createSampler();
    void checkNV12Support();

    // =========================================================================
    // 纹理管理
    // =========================================================================
    void ensureYUV420PTextures(int width, int height);
    void ensureNV12Textures(int width, int height);
    void ensureHWStagingTexture(int width, int height);
    void ensureGrabStagingTexture();

    void updateDynamicTexture(ID3D11Texture2D* texture,
                              const uint8_t* data,
                              int srcStride, int srcRowBytes, int texHeight);

    // =========================================================================
    // 渲染辅助
    // =========================================================================
    void setupViewportAndRenderTarget();
    void drawFullscreenTriangle();
    void releaseRenderTarget();
    void cacheYPlane(const uint8_t* dataY, int linesizeY, int width, int height);

    // 锐化后处理
    void ensureSharpenResources();
    void applySharpenPass();

private:
    bool m_initialized = false;
    HWND m_hwnd = nullptr;
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    bool m_nv12SrvSupported = false;
    float m_sharpenStrength = 0.0f;

    // -------------------------------------------------------
    // D3D11 核心对象
    // -------------------------------------------------------
    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain>      m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_rtv;

    // -------------------------------------------------------
    // 着色器
    // -------------------------------------------------------
    ComPtr<ID3D11VertexShader>  m_vertexShader;
    ComPtr<ID3D11PixelShader>   m_psYUV420P;
    ComPtr<ID3D11PixelShader>   m_psNV12;

    // -------------------------------------------------------
    // 采样器
    // -------------------------------------------------------
    ComPtr<ID3D11SamplerState>  m_sampler;

    // -------------------------------------------------------
    // 锐化后处理资源 (CAS — Contrast Adaptive Sharpening)
    // -------------------------------------------------------
    ComPtr<ID3D11PixelShader>        m_psSharpen;
    ComPtr<ID3D11Texture2D>          m_sharpenTex;
    ComPtr<ID3D11ShaderResourceView> m_sharpenSRV;
    ComPtr<ID3D11RenderTargetView>   m_sharpenRTV;
    ComPtr<ID3D11Buffer>             m_sharpenCB;
    int m_sharpenTexWidth = 0;
    int m_sharpenTexHeight = 0;

    // -------------------------------------------------------
    // YUV420P 纹理 (Y, U, V — 各 R8_UNORM, DYNAMIC)
    // -------------------------------------------------------
    ComPtr<ID3D11Texture2D>             m_texY, m_texU, m_texV;
    ComPtr<ID3D11ShaderResourceView>    m_srvY, m_srvU, m_srvV;
    int m_texYUVWidth = 0;
    int m_texYUVHeight = 0;

    // -------------------------------------------------------
    // NV12 纹理 (Y: R8, UV: R8G8 — DYNAMIC)
    // -------------------------------------------------------
    ComPtr<ID3D11Texture2D>             m_texNV12_Y, m_texNV12_UV;
    ComPtr<ID3D11ShaderResourceView>    m_srvNV12_Y, m_srvNV12_UV;
    int m_texNV12Width = 0;
    int m_texNV12Height = 0;

    // -------------------------------------------------------
    // HW 帧中间纹理 (NV12 DEFAULT — CopySubresourceRegion 目标)
    // -------------------------------------------------------
    ComPtr<ID3D11Texture2D>             m_hwStagingTex;
    ComPtr<ID3D11ShaderResourceView>    m_srvHW_Y, m_srvHW_UV;
    int m_hwStagingWidth = 0;
    int m_hwStagingHeight = 0;

    // -------------------------------------------------------
    // 帧抓取 staging 纹理 (RGBA, STAGING — CPU 回读)
    // -------------------------------------------------------
    ComPtr<ID3D11Texture2D>  m_grabStagingTex;
    int m_grabStagingWidth = 0;
    int m_grabStagingHeight = 0;

    // -------------------------------------------------------
    // Y 分量缓存 (grabGrayFrame 用, 线程安全)
    // -------------------------------------------------------
    std::mutex m_yuvMutex;
    std::vector<uint8_t> m_cachedYData;
    int m_cachedYWidth = 0;
    int m_cachedYHeight = 0;
};

} // namespace qsc

#endif // _WIN32
#endif // D3D11RENDERER_H
