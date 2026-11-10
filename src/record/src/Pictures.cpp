#include "Pictures.h"

#include <gs/vision/Matcher.h>

#include <windows.h>

#include <wincodec.h>
#include <wrl/client.h>

#include <system_error>

namespace gs::record {

namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kBytesPerPixel = 4;

bool transparent(IWICImagingFactory& factory, const WICPixelFormatGUID& format)
{
    ComPtr<IWICComponentInfo> component;
    if (FAILED(factory.CreateComponentInfo(format, &component))) {
        return true;
    }
    ComPtr<IWICPixelFormatInfo2> info;
    if (FAILED(component.As(&info))) {
        return true;
    }
    BOOL supports = FALSE;
    return SUCCEEDED(info->SupportsTransparency(&supports)) && supports != FALSE;
}

bool copyRgba(IWICImagingFactory& factory, IWICBitmapSource& source, UINT width, UINT height,
              std::vector<std::uint8_t>& rgba)
{
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory.CreateFormatConverter(&converter))
        || FAILED(converter->Initialize(&source, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom))) {
        return false;
    }
    rgba.resize(static_cast<std::size_t>(width) * height * kBytesPerPixel);
    return SUCCEEDED(
        converter->CopyPixels(nullptr, width * kBytesPerPixel, static_cast<UINT>(rgba.size()), rgba.data()));
}

}

struct PictureReader::State {
    ComPtr<IWICImagingFactory> factory;
    bool joined = false;
};

PictureReader::PictureReader() : m_state(std::make_unique<State>())
{
    m_state->joined = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&m_state->factory)))) {
        m_state->factory.Reset();
    }
}

PictureReader::~PictureReader()
{
    m_state->factory.Reset();
    if (m_state->joined) {
        CoUninitialize();
    }
}

PictureProblem PictureReader::read(const std::filesystem::path& file, Picture& picture)
{
    picture = {};
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) {
        return PictureProblem::Missing;
    }
    IWICImagingFactory* factory = m_state->factory.Get();
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    UINT width = 0;
    UINT height = 0;
    if (factory == nullptr
        || FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnDemand, &decoder))
        || FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height)) || width == 0
        || height == 0) {
        return PictureProblem::Unreadable;
    }
    if (width > static_cast<UINT>(vision::kMaxTemplateSide) || height > static_cast<UINT>(vision::kMaxTemplateSide)
        || static_cast<std::int64_t>(width) * height > vision::kMaxTemplatePixels) {
        return PictureProblem::TooLarge;
    }
    WICPixelFormatGUID format{};
    picture.alpha = SUCCEEDED(frame->GetPixelFormat(&format)) && transparent(*factory, format);
    if (!copyRgba(*factory, *frame.Get(), width, height, picture.rgba)) {
        picture = {};
        return PictureProblem::Unreadable;
    }
    picture.width = static_cast<int>(width);
    picture.height = static_cast<int>(height);
    return PictureProblem::None;
}

Picture PictureReader::scaled(const Picture& picture, int width, int height)
{
    IWICImagingFactory* factory = m_state->factory.Get();
    if (picture.empty() || width <= 0 || height <= 0 || (width == picture.width && height == picture.height)
        || factory == nullptr) {
        return picture;
    }
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapFromMemory(static_cast<UINT>(picture.width), static_cast<UINT>(picture.height),
                                               GUID_WICPixelFormat32bppRGBA,
                                               static_cast<UINT>(picture.width) * kBytesPerPixel,
                                               static_cast<UINT>(picture.rgba.size()),
                                               const_cast<BYTE*>(picture.rgba.data()), &bitmap))
        || FAILED(factory->CreateBitmapScaler(&scaler))
        || FAILED(scaler->Initialize(bitmap.Get(), static_cast<UINT>(width), static_cast<UINT>(height),
                                     WICBitmapInterpolationModeFant))) {
        return picture;
    }
    Picture result{.width = width, .height = height, .alpha = picture.alpha};
    if (!copyRgba(*factory, *scaler.Get(), static_cast<UINT>(width), static_cast<UINT>(height), result.rgba)) {
        return picture;
    }
    return result;
}

}
