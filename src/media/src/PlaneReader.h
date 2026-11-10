#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <functional>

struct AVFrame;

namespace gs::media {

struct Planes {
    const std::uint8_t* luma = nullptr;
    std::ptrdiff_t lumaPitch = 0;
    const std::uint8_t* cb = nullptr;
    const std::uint8_t* cr = nullptr;
    std::ptrdiff_t chromaPitch = 0;
    std::ptrdiff_t chromaStep = 1;
};

class PlaneReader
{
public:
    explicit PlaneReader(ID3D11Device* device);

    bool read(const AVFrame& frame, const std::function<void(const Planes&)>& use);

private:
    bool readSurface(const AVFrame& frame, const std::function<void(const Planes&)>& use);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_staging;
    UINT m_stagingWidth = 0;
    UINT m_stagingHeight = 0;
};

}
