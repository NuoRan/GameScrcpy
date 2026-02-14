#ifndef D3D11GLINTEROP_H
#define D3D11GLINTEROP_H

/**
 * @file D3D11GLInterop.h
 * @brief D3D11VA → OpenGL 零拷贝纹理共享 / D3D11VA → OpenGL Zero-Copy Texture Sharing
 *
 * GPU 直接渲染框架核心组件
 *
 * 使用 WGL_NV_DX_interop2 扩展，将 D3D11VA 解码输出的 ID3D11Texture2D (NV12)
 * 直接映射为 OpenGL 纹理，完全消除 GPU→CPU→GPU 的往返拷贝。
 *
 * 延迟收益: 消除 av_hwframe_transfer_data (~2ms) + glTexSubImage2D (~1ms) = ~3ms
 *
 * 数据流对比:
 *   旧: D3D11VA → [GPU→CPU readback] → CPU NV12 → [memcpy] → FramePool → [CPU→GPU upload] → GL texture
 *   新: D3D11VA → ID3D11Texture2D → [WGL_NV_DX_interop] → GL texture (零拷贝)
 *
 * 驱动要求: NVIDIA/AMD 支持 WGL_NV_DX_interop2 (覆盖 >95% 独立 GPU)
 */

#ifdef _WIN32

#include <QOpenGLFunctions>
#include <cstdint>

// 前置声明，避免在头文件中包含 Windows/D3D11 头
struct ID3D11Device;
struct ID3D11Texture2D;

namespace qsc {

/**
 * @brief D3D11-OpenGL 零拷贝互操作管理器
 *
 * 生命周期:
 *   1. initialize(d3d11Device) — 获取 WGL_NV_DX_interop 函数, 注册 D3D11 设备
 *   2. registerTexture(d3d11Tex) — 将 D3D11 纹理注册为可共享对象
 *   3. lockAndBind(glTextureId) — 帧渲染前: 锁定 + 绑定到 GL 纹理
 *   4. unlock() — 帧渲染后: 解锁归还给 D3D11
 *   5. unregisterTexture() — 销毁时: 取消注册
 *   6. shutdown() — 关闭互操作设备
 *
 * 线程要求: 所有方法必须在拥有 OpenGL 上下文的线程 (GUI 线程) 调用
 */
class D3D11GLInterop
{
public:
    D3D11GLInterop();
    ~D3D11GLInterop();

    // 禁止拷贝
    D3D11GLInterop(const D3D11GLInterop&) = delete;
    D3D11GLInterop& operator=(const D3D11GLInterop&) = delete;

    /**
     * @brief 初始化 WGL_NV_DX_interop 扩展
     * @param d3d11Device FFmpeg hw_device_ctx 中的 ID3D11Device*
     * @return true 如果扩展可用且设备注册成功
     */
    bool initialize(ID3D11Device* d3d11Device);

    /**
     * @brief 是否已成功初始化
     */
    bool isAvailable() const { return m_interopDevice != nullptr; }

    /**
     * @brief 将 D3D11 NV12 纹理注册为可共享对象
     * @param d3d11Texture D3D11VA 解码输出的 ID3D11Texture2D*
     * @param glTextureY 接收 Y 平面的 GL 纹理名称 (必须已创建)
     * @param glTextureUV 接收 UV 平面的 GL 纹理名称 (必须已创建)
     * @return true 如果注册成功
     *
     * 注: D3D11VA 解码输出通常是纹理数组 (texture2D array)，
     *     每帧通过 subresource index 索引。需要先 CopySubresourceRegion
     *     到独立纹理后再注册。
     */
    bool registerTexture(ID3D11Texture2D* d3d11Texture,
                         GLuint glTextureY, GLuint glTextureUV);

    /**
     * @brief 取消纹理注册
     */
    void unregisterTexture();

    /**
     * @brief 锁定 D3D11 纹理供 OpenGL 使用
     * @return true 如果成功锁定
     *
     * 调用后 GL 纹理包含 D3D11 最新渲染内容，可直接 glBindTexture + glDrawArrays
     */
    bool lock();

    /**
     * @brief 解锁纹理，归还给 D3D11 管线
     */
    void unlock();

    /**
     * @brief 关闭 D3D11-GL 互操作
     */
    void shutdown();

    /**
     * @brief 检查当前 GL 上下文是否支持 WGL_NV_DX_interop
     * @return true 如果支持
     */
    static bool checkExtensionSupport();

private:
    // WGL_NV_DX_interop 函数指针类型定义
    using PFNWGLDXOPENDEVICENV          = void* (*)(void*);
    using PFNWGLDXCLOSEDEVICENV         = bool  (*)(void*);
    using PFNWGLDXREGISTEROBJECTNV      = void* (*)(void*, void*, GLuint, unsigned int, unsigned int);
    using PFNWGLDXUNREGISTEROBJECTNV    = bool  (*)(void*, void*);
    using PFNWGLDXLOCKOBJECTSNV         = bool  (*)(void*, int, void**);
    using PFNWGLDXUNLOCKOBJECTSNV       = bool  (*)(void*, int, void**);
    using PFNWGLDXSETRESOURCESHAREMODNV = bool  (*)(void*, void*, unsigned int);

    // 加载 WGL 扩展函数
    bool loadWGLFunctions();

    // WGL 函数指针
    PFNWGLDXOPENDEVICENV          m_wglDXOpenDeviceNV = nullptr;
    PFNWGLDXCLOSEDEVICENV         m_wglDXCloseDeviceNV = nullptr;
    PFNWGLDXREGISTEROBJECTNV      m_wglDXRegisterObjectNV = nullptr;
    PFNWGLDXUNREGISTEROBJECTNV    m_wglDXUnregisterObjectNV = nullptr;
    PFNWGLDXLOCKOBJECTSNV         m_wglDXLockObjectsNV = nullptr;
    PFNWGLDXUNLOCKOBJECTSNV       m_wglDXUnlockObjectsNV = nullptr;

    // 互操作句柄
    void* m_interopDevice = nullptr;    // wglDXOpenDeviceNV 返回
    void* m_interopObjectY = nullptr;   // wglDXRegisterObjectNV 返回 (Y plane)
    void* m_interopObjectUV = nullptr;  // wglDXRegisterObjectNV 返回 (UV plane)
    bool  m_isLocked = false;           // 当前是否已锁定
};

} // namespace qsc

#endif // _WIN32
#endif // D3D11GLINTEROP_H
