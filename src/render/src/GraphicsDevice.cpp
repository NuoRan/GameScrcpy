#include "gs/render/GraphicsDevice.h"

#include <QQuickGraphicsDevice>
#include <QQuickWindow>

#include <d3d11_4.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <format>
#include <iterator>
#include <utility>

namespace gs::render {

namespace {

using Microsoft::WRL::ComPtr;

constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};

HRESULT createDevice(D3D_DRIVER_TYPE type, UINT flags, ID3D11Device** device, ID3D11DeviceContext** context)
{
    return D3D11CreateDevice(nullptr, type, nullptr, flags, kFeatureLevels,
                             static_cast<UINT>(std::size(kFeatureLevels)), D3D11_SDK_VERSION, device, nullptr,
                             context);
}

std::string utf8(const wchar_t* text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size > 0 ? static_cast<std::size_t>(size - 1) : 0, '\0');
    if (size > 1) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    }
    return result;
}

struct Adapter {
    std::string name = "unknown adapter";
    bool software = false;
};

Adapter describeAdapter(ID3D11Device* device)
{
    constexpr UINT kMicrosoft = 0x1414;
    constexpr UINT kBasicRenderDriver = 0x8c;
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIAdapter1> adapter1;
    DXGI_ADAPTER_DESC1 description{};
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter.As(&adapter1)) || FAILED(adapter1->GetDesc1(&description))) {
        return {};
    }
    return {utf8(description.Description),
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
                (description.VendorId == kMicrosoft && description.DeviceId == kBasicRenderDriver)};
}

}

std::unique_ptr<GraphicsDevice> GraphicsDevice::create(bool debugLayer, bool forceWarp)
{
    std::unique_ptr<GraphicsDevice> result(new GraphicsDevice);
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    if (debugLayer && SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_NULL, nullptr, D3D11_CREATE_DEVICE_DEBUG,
                                                  nullptr, 0, D3D11_SDK_VERSION, nullptr, nullptr, nullptr))) {
        flags |= D3D11_CREATE_DEVICE_DEBUG;
        result->m_debugLayer = true;
    }

    HRESULT created = forceWarp ? DXGI_ERROR_UNSUPPORTED
                                : createDevice(D3D_DRIVER_TYPE_HARDWARE, flags, &result->m_device, &result->m_context);
    if (FAILED(created) && !forceWarp) {
        created = createDevice(D3D_DRIVER_TYPE_HARDWARE, flags & ~D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                               &result->m_device, &result->m_context);
    }
    if (SUCCEEDED(created)) {
        const Adapter adapter = describeAdapter(result->m_device);
        result->m_description = adapter.name;
        result->m_warp = adapter.software;
    } else {
        result->m_warp = true;
        result->m_description =
            forceWarp ? std::string("GPU skipped on request")
                      : std::format("no hardware Direct3D 11 device (0x{:08X})", static_cast<unsigned>(created));
        if (FAILED(createDevice(D3D_DRIVER_TYPE_WARP, flags & ~D3D11_CREATE_DEVICE_VIDEO_SUPPORT, &result->m_device,
                                &result->m_context))) {
            return nullptr;
        }
    }

    if (ComPtr<ID3D11Multithread> multithread; SUCCEEDED(result->m_context->QueryInterface(IID_PPV_ARGS(&multithread)))) {
        multithread->SetMultithreadProtected(TRUE);
    }
    if (ComPtr<ID3D11InfoQueue> queue;
        result->m_debugLayer && SUCCEEDED(result->m_device->QueryInterface(IID_PPV_ARGS(&queue)))) {
        queue->SetMessageCountLimit(UINT64_MAX);
    }
    return result;
}

GraphicsDevice::~GraphicsDevice()
{
    if (m_context != nullptr) {
        m_context->Release();
    }
    if (m_device != nullptr) {
        m_device->Release();
    }
}

std::vector<std::string> GraphicsDevice::debugErrors(std::size_t limit) const
{
    std::vector<std::string> errors;
    ComPtr<ID3D11InfoQueue> queue;
    if (!m_debugLayer || FAILED(m_device->QueryInterface(IID_PPV_ARGS(&queue)))) {
        return errors;
    }
    const UINT64 stored = queue->GetNumStoredMessages();
    for (UINT64 i = 0; i < stored && errors.size() < limit; ++i) {
        SIZE_T size = 0;
        if (FAILED(queue->GetMessage(i, nullptr, &size)) || size == 0) {
            continue;
        }
        std::vector<std::byte> storage(size);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        if (SUCCEEDED(queue->GetMessage(i, message, &size)) &&
            (message->Severity == D3D11_MESSAGE_SEVERITY_ERROR ||
             message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION)) {
            errors.emplace_back(message->pDescription, message->DescriptionByteLength > 0
                                                           ? message->DescriptionByteLength - 1
                                                           : 0);
        }
    }
    return errors;
}

void GraphicsDevice::share(QQuickWindow& window) const
{
    window.setGraphicsDevice(QQuickGraphicsDevice::fromDeviceAndContext(m_device, m_context));
    struct Frame {
        bool open = false;
        bool held = false;
    };
    const auto frame = std::make_shared<Frame>();
    QObject::connect(
        &window, &QQuickWindow::beforeFrameBegin, &window, [frame] { frame->open = true; }, Qt::DirectConnection);
    QObject::connect(
        &window, &QQuickWindow::beforeRendering, &window,
        [this, frame] {
            if (frame->open && !frame->held) {
                m_rendering.lock();
                frame->held = true;
            }
        },
        Qt::DirectConnection);
    QObject::connect(
        &window, &QQuickWindow::afterFrameEnd, &window,
        [this, frame] {
            frame->open = false;
            if (std::exchange(frame->held, false)) {
                m_rendering.unlock();
            }
        },
        Qt::DirectConnection);
}

}
