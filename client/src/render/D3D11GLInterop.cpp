/**
 * @file D3D11GLInterop.cpp
 * @brief D3D11VA → OpenGL 零拷贝互操作实现 / D3D11-GL Zero-Copy Interop Implementation
 *
 * GPU 直接渲染核心实现
 *
 * 使用 WGL_NV_DX_interop2 扩展实现:
 *   1. wglDXOpenDeviceNV   — 注册 D3D11 设备到 OpenGL
 *   2. wglDXRegisterObjectNV — 将 ID3D11Texture2D 映射为 GL 纹理
 *   3. wglDXLockObjectsNV  — 帧渲染前锁定纹理
 *   4. wglDXUnlockObjectsNV — 帧渲染后解锁纹理
 *
 * 延迟收益: 消除 av_hwframe_transfer_data + memcpy + glTexSubImage2D ≈ 3-5ms
 */

#ifdef _WIN32

#include "D3D11GLInterop.h"
#include <QDebug>
#include <QOpenGLContext>

#include <windows.h>
#include <d3d11.h>

// WGL_NV_DX_interop 常量定义
#ifndef WGL_ACCESS_READ_ONLY_NV
#define WGL_ACCESS_READ_ONLY_NV           0x0000
#endif
#ifndef WGL_ACCESS_READ_WRITE_NV
#define WGL_ACCESS_READ_WRITE_NV          0x0001
#endif
#ifndef WGL_ACCESS_WRITE_DISCARD_NV
#define WGL_ACCESS_WRITE_DISCARD_NV       0x0002
#endif

namespace qsc {

D3D11GLInterop::D3D11GLInterop() = default;

D3D11GLInterop::~D3D11GLInterop()
{
    shutdown();
}

bool D3D11GLInterop::checkExtensionSupport()
{
    // 检查 WGL_NV_DX_interop2 扩展字符串
    using PFNWGLGETEXTENSIONSSTRINGARBPROC = const char* (*)(HDC);

    auto wglGetExtStr = reinterpret_cast<PFNWGLGETEXTENSIONSSTRINGARBPROC>(
        wglGetProcAddress("wglGetExtensionsStringARB"));

    if (!wglGetExtStr) {
        qWarning() << "[D3D11GLInterop] wglGetExtensionsStringARB not available";
        return false;
    }

    HDC hdc = wglGetCurrentDC();
    if (!hdc) {
        qWarning() << "[D3D11GLInterop] No current DC";
        return false;
    }

    const char* extensions = wglGetExtStr(hdc);
    if (!extensions) {
        return false;
    }

    bool hasInterop = strstr(extensions, "WGL_NV_DX_interop") != nullptr;
    qInfo() << "[D3D11GLInterop] WGL_NV_DX_interop supported:" << hasInterop;
    return hasInterop;
}

bool D3D11GLInterop::loadWGLFunctions()
{
    m_wglDXOpenDeviceNV = reinterpret_cast<PFNWGLDXOPENDEVICENV>(
        wglGetProcAddress("wglDXOpenDeviceNV"));
    m_wglDXCloseDeviceNV = reinterpret_cast<PFNWGLDXCLOSEDEVICENV>(
        wglGetProcAddress("wglDXCloseDeviceNV"));
    m_wglDXRegisterObjectNV = reinterpret_cast<PFNWGLDXREGISTEROBJECTNV>(
        wglGetProcAddress("wglDXRegisterObjectNV"));
    m_wglDXUnregisterObjectNV = reinterpret_cast<PFNWGLDXUNREGISTEROBJECTNV>(
        wglGetProcAddress("wglDXUnregisterObjectNV"));
    m_wglDXLockObjectsNV = reinterpret_cast<PFNWGLDXLOCKOBJECTSNV>(
        wglGetProcAddress("wglDXLockObjectsNV"));
    m_wglDXUnlockObjectsNV = reinterpret_cast<PFNWGLDXUNLOCKOBJECTSNV>(
        wglGetProcAddress("wglDXUnlockObjectsNV"));

    if (!m_wglDXOpenDeviceNV || !m_wglDXCloseDeviceNV ||
        !m_wglDXRegisterObjectNV || !m_wglDXUnregisterObjectNV ||
        !m_wglDXLockObjectsNV || !m_wglDXUnlockObjectsNV) {
        qWarning() << "[D3D11GLInterop] Failed to load WGL_NV_DX_interop functions";
        return false;
    }

    qInfo() << "[D3D11GLInterop] WGL_NV_DX_interop functions loaded successfully";
    return true;
}

bool D3D11GLInterop::initialize(ID3D11Device* d3d11Device)
{
    if (m_interopDevice) {
        qWarning() << "[D3D11GLInterop] Already initialized";
        return true;
    }

    if (!d3d11Device) {
        qWarning() << "[D3D11GLInterop] Null D3D11 device";
        return false;
    }

    // Step 1: 加载 WGL 扩展函数
    if (!loadWGLFunctions()) {
        return false;
    }

    // Step 2: 注册 D3D11 设备到 OpenGL 上下文
    m_interopDevice = m_wglDXOpenDeviceNV(d3d11Device);
    if (!m_interopDevice) {
        DWORD err = GetLastError();
        qWarning() << "[D3D11GLInterop] wglDXOpenDeviceNV failed, error:" << err;
        return false;
    }

    qInfo() << "[D3D11GLInterop] D3D11-GL interop initialized successfully";
    return true;
}

bool D3D11GLInterop::registerTexture(ID3D11Texture2D* d3d11Texture,
                                      GLuint glTextureY, GLuint glTextureUV)
{
    if (!m_interopDevice) {
        qWarning() << "[D3D11GLInterop] Not initialized";
        return false;
    }

    // 先取消之前注册的纹理
    unregisterTexture();

    // 注册 Y 平面 (GL_TEXTURE_2D, 只读)
    // 注: D3D11 NV12 纹理的 Y 平面可通过 SRV 对应 GL_LUMINANCE 纹理
    m_interopObjectY = m_wglDXRegisterObjectNV(
        m_interopDevice,
        d3d11Texture,
        glTextureY,
        GL_TEXTURE_2D,
        WGL_ACCESS_READ_ONLY_NV
    );

    if (!m_interopObjectY) {
        DWORD err = GetLastError();
        qWarning() << "[D3D11GLInterop] Failed to register Y texture, error:" << err;
        return false;
    }

    // 注册 UV 平面
    // 注: WGL_NV_DX_interop 通常将整个纹理映射，NV12 的 UV 需要通过
    //     单独的 SRV 或 staging texture 来分离
    //     简化方案: 注册整个 NV12 纹理，在 shader 中通过纹理坐标分离 Y/UV
    m_interopObjectUV = m_wglDXRegisterObjectNV(
        m_interopDevice,
        d3d11Texture,
        glTextureUV,
        GL_TEXTURE_2D,
        WGL_ACCESS_READ_ONLY_NV
    );

    if (!m_interopObjectUV) {
        DWORD err = GetLastError();
        qWarning() << "[D3D11GLInterop] Failed to register UV texture, error:" << err;
        // 清理 Y 对象
        m_wglDXUnregisterObjectNV(m_interopDevice, m_interopObjectY);
        m_interopObjectY = nullptr;
        return false;
    }

    qInfo() << "[D3D11GLInterop] Textures registered: Y=" << glTextureY << " UV=" << glTextureUV;
    return true;
}

void D3D11GLInterop::unregisterTexture()
{
    if (!m_interopDevice) return;

    // 确保已解锁
    if (m_isLocked) {
        unlock();
    }

    if (m_interopObjectUV) {
        m_wglDXUnregisterObjectNV(m_interopDevice, m_interopObjectUV);
        m_interopObjectUV = nullptr;
    }

    if (m_interopObjectY) {
        m_wglDXUnregisterObjectNV(m_interopDevice, m_interopObjectY);
        m_interopObjectY = nullptr;
    }
}

bool D3D11GLInterop::lock()
{
    if (!m_interopDevice || !m_interopObjectY) {
        return false;
    }

    if (m_isLocked) {
        qWarning() << "[D3D11GLInterop] Already locked";
        return true;
    }

    // 锁定所有注册的对象
    void* objects[2] = { m_interopObjectY, m_interopObjectUV };
    int count = m_interopObjectUV ? 2 : 1;

    if (!m_wglDXLockObjectsNV(m_interopDevice, count, objects)) {
        DWORD err = GetLastError();
        qWarning() << "[D3D11GLInterop] wglDXLockObjectsNV failed, error:" << err;
        return false;
    }

    m_isLocked = true;
    return true;
}

void D3D11GLInterop::unlock()
{
    if (!m_interopDevice || !m_isLocked) {
        return;
    }

    void* objects[2] = { m_interopObjectY, m_interopObjectUV };
    int count = m_interopObjectUV ? 2 : 1;

    if (!m_wglDXUnlockObjectsNV(m_interopDevice, count, objects)) {
        DWORD err = GetLastError();
        qWarning() << "[D3D11GLInterop] wglDXUnlockObjectsNV failed, error:" << err;
    }

    m_isLocked = false;
}

void D3D11GLInterop::shutdown()
{
    unregisterTexture();

    if (m_interopDevice) {
        m_wglDXCloseDeviceNV(m_interopDevice);
        m_interopDevice = nullptr;
        qInfo() << "[D3D11GLInterop] Shutdown complete";
    }

    // 清零函数指针
    m_wglDXOpenDeviceNV = nullptr;
    m_wglDXCloseDeviceNV = nullptr;
    m_wglDXRegisterObjectNV = nullptr;
    m_wglDXUnregisterObjectNV = nullptr;
    m_wglDXLockObjectsNV = nullptr;
    m_wglDXUnlockObjectsNV = nullptr;
}

} // namespace qsc

#endif // _WIN32
