#include <gs/capture/CaptureSource.h>
#include <gs/capture/FrameBuilder.h>

#include <gs/media/FrameSnapshot.h>
#include <gs/media/FrameUploader.h>
#include <gs/media/VideoDecoder.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <windows.h>

#include <d3d11.h>
#include <objbase.h>
#include <wrl/client.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using gs::capture::CaptureFormat;
using gs::capture::CaptureSample;
using gs::capture::CaptureSource;
using gs::capture::FrameBuilder;
using gs::capture::SampleKind;
using gs::capture::SampleSink;
using gs::capture::Subtype;
using gs::media::VideoFrame;
using Microsoft::WRL::ComPtr;

namespace {

class Apartment
{
public:
    Apartment() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~Apartment()
    {
        if (SUCCEEDED(m_result)) {
            CoUninitialize();
        }
    }
    Apartment(const Apartment&) = delete;
    Apartment& operator=(const Apartment&) = delete;

private:
    HRESULT m_result;
};

ComPtr<ID3D11Device> graphicsDevice()
{
    ComPtr<ID3D11Device> device;
    for (const D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        if (SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr,
                                        nullptr))) {
            return device;
        }
    }
    return nullptr;
}

std::vector<ID3D11Device*> devicesToTry(const ComPtr<ID3D11Device>& gpu)
{
    std::vector<ID3D11Device*> devices{nullptr};
    if (gpu) {
        devices.push_back(gpu.Get());
    } else {
        MESSAGE("no Direct3D 11 device: only the CPU path is checked");
    }
    return devices;
}

class FakeSource final : public CaptureSource
{
public:
    explicit FakeSource(std::vector<CaptureFormat> formats) : m_formats(std::move(formats)) {}

    [[nodiscard]] bool start(SampleSink& sink, const std::string& format) override
    {
        m_sink = &sink;
        sink.offered(m_formats, gs::capture::chooseFormat(m_formats, format));
        return true;
    }

    void stop() override { m_sink = nullptr; }
    [[nodiscard]] std::string problem() const override { return {}; }

    void deliver(const CaptureSample& sample) const
    {
        if (m_sink != nullptr) {
            m_sink->sample(sample);
        }
    }

private:
    std::vector<CaptureFormat> m_formats;
    SampleSink* m_sink = nullptr;
};

class Feed final : public SampleSink
{
public:
    explicit Feed(ID3D11Device* device)
        : m_builder(device, [this](VideoFrame frame) { frames.push_back(std::move(frame)); })
    {
    }

    void offered(const std::vector<CaptureFormat>& formats, const std::optional<CaptureFormat>& picked) override
    {
        offeredCount = formats.size();
        chosen = picked;
    }

    void sample(const CaptureSample& sample) override { m_builder.build(sample); }
    void frame(VideoFrame frame) override { frames.push_back(std::move(frame)); }
    void ended(const std::string& problem) override { endedWith = problem; }

    [[nodiscard]] std::string problem() const { return m_builder.problem(); }
    [[nodiscard]] std::uint16_t session() const noexcept { return m_builder.session(); }

    std::vector<VideoFrame> frames;
    std::size_t offeredCount = 0;
    std::optional<CaptureFormat> chosen;
    std::string endedWith;

private:
    FrameBuilder m_builder;
};

struct Planes {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> luma;
    std::vector<std::uint8_t> chroma;

    [[nodiscard]] int y(int x, int row) const { return luma[static_cast<std::size_t>(row * width + x)]; }
    [[nodiscard]] int u(int x, int row) const { return chroma[static_cast<std::size_t>(row * 2 * ((width + 1) / 2) + 2 * x)]; }
    [[nodiscard]] int v(int x, int row) const
    {
        return chroma[static_cast<std::size_t>(row * 2 * ((width + 1) / 2) + 2 * x + 1)];
    }
};

std::optional<Planes> planesOf(const VideoFrame& frame, ID3D11Device* device)
{
    gs::media::FrameSnapshots snapshots(device);
    snapshots.request(true);
    snapshots.offer(frame);
    snapshots.finish();
    const auto shot = snapshots.latest();
    if (shot == nullptr || !shot->colored()) {
        return std::nullopt;
    }
    return Planes{.width = shot->width, .height = shot->height, .luma = shot->luma, .chroma = shot->chroma};
}

constexpr int kWidth = 64;
constexpr int kHeight = 36;

struct Nv12Picture {
    std::ptrdiff_t pitch = 0;
    std::vector<std::uint8_t> bytes;
};

Nv12Picture nv12Halves(int width, int height, std::ptrdiff_t pitch)
{
    Nv12Picture picture{.pitch = pitch,
                        .bytes = std::vector<std::uint8_t>(static_cast<std::size_t>(pitch * height * 3 / 2), 0)};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            picture.bytes[static_cast<std::size_t>(y * pitch + x)] = x < width / 2 ? 200 : 50;
        }
    }
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            const bool left = x < width / 4;
            const auto at = static_cast<std::size_t>(pitch * height + y * pitch + 2 * x);
            picture.bytes[at] = left ? 90 : 128;
            picture.bytes[at + 1] = left ? 160 : 128;
        }
    }
    return picture;
}

std::vector<std::uint8_t> yuy2Pattern(int width, int height, std::ptrdiff_t pitch)
{
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(pitch * height), 0);
    for (int y = 0; y < height; ++y) {
        for (int pair = 0; pair < width / 2; ++pair) {
            const auto at = static_cast<std::size_t>(y * pitch + 4 * pair);
            bytes[at] = static_cast<std::uint8_t>(16 + (2 * pair * 7 + y * 3) % 220);
            bytes[at + 1] = static_cast<std::uint8_t>(64 + (pair * 5 + y * 11) % 128);
            bytes[at + 2] = static_cast<std::uint8_t>(16 + ((2 * pair + 1) * 7 + y * 3) % 220);
            bytes[at + 3] = static_cast<std::uint8_t>(32 + (pair * 13 + y * 7) % 160);
        }
    }
    return bytes;
}

std::vector<std::uint8_t> testJpeg()
{
    std::ifstream file(std::filesystem::path(GS_CAPTURE_TEST_DATA_DIR) / "halves-32x16.jpg.hex");
    REQUIRE(file);
    std::vector<std::uint8_t> bytes;
    std::string token;
    while (file >> token) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(token, nullptr, 16)));
    }
    return bytes;
}

std::vector<CaptureFormat> offeredFormats()
{
    return {{.width = kWidth, .height = kHeight, .rateNumerator = 60, .subtype = Subtype::Nv12},
            {.width = kWidth, .height = kHeight, .rateNumerator = 60, .subtype = Subtype::Yuy2},
            {.width = 32, .height = 16, .rateNumerator = 30, .subtype = Subtype::Mjpg}};
}

}

TEST_CASE("NV12 pictures from the card become frames with their size, times and colours, on the GPU and without (MED-014)")
{
    const ComPtr<ID3D11Device> gpu = graphicsDevice();
    for (ID3D11Device* device : devicesToTry(gpu)) {
        CAPTURE(device != nullptr);
        FakeSource source(offeredFormats());
        Feed feed(device);
        REQUIRE(source.start(feed, {}));
        CHECK(feed.offeredCount == 3);
        REQUIRE(feed.chosen);
        CHECK(feed.chosen->subtype == Subtype::Nv12);

        const Nv12Picture picture = nv12Halves(kWidth, kHeight, 80);
        source.deliver({.kind = SampleKind::Nv12,
                        .width = kWidth,
                        .height = kHeight,
                        .bytes = picture.bytes,
                        .pitch = picture.pitch,
                        .chromaOffset = picture.pitch * kHeight,
                        .ptsUs = 1'000,
                        .receivedUs = 2'000});
        REQUIRE(feed.frames.size() == 1);
        const VideoFrame& frame = feed.frames.front();
        CHECK(frame.width() == kWidth);
        CHECK(frame.height() == kHeight);
        CHECK(frame.ptsUs() == 1'000);
        CHECK(frame.receivedUs() == 2'000);
        CHECK(frame.session() == 1);
        CHECK(frame.hardware() == (device != nullptr));
        const std::optional<Planes> planes = planesOf(frame, device);
        REQUIRE(planes);
        CHECK(planes->width == kWidth);
        CHECK(planes->y(10, 5) == 200);
        CHECK(planes->y(50, 30) == 50);
        CHECK(planes->u(5, 3) == 90);
        CHECK(planes->v(5, 3) == 160);
        CHECK(planes->u(28, 10) == 128);
        CHECK(planes->v(28, 10) == 128);

        const Nv12Picture smaller = nv12Halves(32, 18, 32);
        source.deliver({.kind = SampleKind::Nv12,
                        .width = 32,
                        .height = 18,
                        .bytes = smaller.bytes,
                        .pitch = smaller.pitch,
                        .chromaOffset = smaller.pitch * 18,
                        .ptsUs = 2'000});
        REQUIRE(feed.frames.size() == 2);
        CHECK(feed.frames.back().width() == 32);
        CHECK(feed.frames.back().session() == 2);
        CHECK(feed.session() == 2);
        CHECK(feed.problem().empty());
        source.stop();
        source.deliver({.kind = SampleKind::Nv12, .width = 32, .height = 18, .bytes = smaller.bytes, .pitch = 32,
                        .chromaOffset = 32 * 18});
        CHECK(feed.frames.size() == 2);
    }
}

TEST_CASE("YUY2 pictures from the card become the NV12 the CPU reference makes, on the GPU and without (MED-014)")
{
    const ComPtr<ID3D11Device> gpu = graphicsDevice();
    const std::ptrdiff_t pitch = 2 * kWidth + 16;
    const std::vector<std::uint8_t> packed = yuy2Pattern(kWidth, kHeight, pitch);
    std::vector<std::uint8_t> luma(static_cast<std::size_t>(kWidth * kHeight));
    std::vector<std::uint8_t> chroma(static_cast<std::size_t>(kWidth * kHeight / 2));
    gs::media::yuy2ToNv12(packed.data(), pitch, kWidth, kHeight, luma.data(), kWidth, chroma.data(), kWidth);
    for (ID3D11Device* device : devicesToTry(gpu)) {
        CAPTURE(device != nullptr);
        FakeSource source(offeredFormats());
        Feed feed(device);
        REQUIRE(source.start(feed, "64x36@60/YUY2"));
        REQUIRE(feed.chosen);
        CHECK(feed.chosen->subtype == Subtype::Yuy2);
        source.deliver({.kind = SampleKind::Yuy2, .width = kWidth, .height = kHeight, .bytes = packed, .pitch = pitch,
                        .ptsUs = 7'000});
        REQUIRE(feed.frames.size() == 1);
        CHECK(feed.frames.front().ptsUs() == 7'000);
        const std::optional<Planes> planes = planesOf(feed.frames.front(), device);
        REQUIRE(planes);
        CHECK(planes->luma == luma);
        REQUIRE(planes->chroma.size() == chroma.size());
        int worst = 0;
        for (std::size_t index = 0; index < chroma.size(); ++index) {
            worst = std::max(worst, std::abs(planes->chroma[index] - chroma[index]));
        }
        CHECK(worst <= 1);
    }
}

TEST_CASE("MJPEG pictures from a USB 2 card are decoded by Windows and become frames (MED-014)")
{
    const Apartment apartment;
    const std::vector<std::uint8_t> jpeg = testJpeg();
    REQUIRE(jpeg.size() > 100);
    const ComPtr<ID3D11Device> gpu = graphicsDevice();
    for (ID3D11Device* device : devicesToTry(gpu)) {
        CAPTURE(device != nullptr);
        FakeSource source(offeredFormats());
        Feed feed(device);
        REQUIRE(source.start(feed, "32x16@30/MJPG"));
        REQUIRE(feed.chosen);
        CHECK(feed.chosen->subtype == Subtype::Mjpg);
        source.deliver({.kind = SampleKind::Mjpg, .width = 32, .height = 16, .bytes = jpeg, .ptsUs = 3'000});
        if (feed.frames.empty()) {
            MESSAGE("no MJPEG decoder on this system (" << feed.problem() << "): skipped");
            CHECK_FALSE(feed.problem().empty());
            return;
        }
        REQUIRE(feed.frames.size() == 1);
        CHECK(feed.frames.front().width() == 32);
        CHECK(feed.frames.front().height() == 16);
        CHECK(feed.frames.front().ptsUs() == 3'000);
        const std::optional<Planes> planes = planesOf(feed.frames.front(), device);
        REQUIRE(planes);
        struct Spot {
            int x = 0;
            int y = 0;
            int value = 0;
        };
        for (const Spot& spot : {Spot{2, 2, 192}, Spot{12, 13, 192}, Spot{20, 3, 64}, Spot{29, 14, 64}}) {
            CAPTURE(spot.x);
            CAPTURE(spot.y);
            CHECK(std::abs(planes->y(spot.x, spot.y) - spot.value) <= 3);
        }
        CHECK(std::abs(planes->u(4, 4) - 128) <= 3);
        CHECK(std::abs(planes->v(12, 4) - 128) <= 3);

        source.deliver({.kind = SampleKind::Mjpg, .width = 32, .height = 16, .bytes = jpeg, .ptsUs = 4'000});
        CHECK(feed.frames.size() == 2);
        CHECK(feed.problem().empty());
    }
}

TEST_CASE("a card picture that is already a GPU texture is shown without a copy when it can be drawn, else copied")
{
    const ComPtr<ID3D11Device> gpu = graphicsDevice();
    if (!gpu) {
        MESSAGE("no Direct3D 11 device: skipped");
        return;
    }
    const Nv12Picture picture = nv12Halves(kWidth, kHeight, kWidth);
    for (const UINT binding : {UINT{D3D11_BIND_SHADER_RESOURCE}, UINT{0}}) {
        CAPTURE(binding);
        D3D11_TEXTURE2D_DESC layout{};
        layout.Width = kWidth;
        layout.Height = kHeight;
        layout.MipLevels = 1;
        layout.ArraySize = 1;
        layout.Format = DXGI_FORMAT_NV12;
        layout.SampleDesc.Count = 1;
        layout.Usage = D3D11_USAGE_DEFAULT;
        layout.BindFlags = binding;
        const D3D11_SUBRESOURCE_DATA initial{picture.bytes.data(), static_cast<UINT>(kWidth), 0};
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(gpu->CreateTexture2D(&layout, &initial, &texture))) {
            MESSAGE("this device cannot make NV12 textures: skipped");
            return;
        }
        auto owner = std::make_shared<int>(7);
        const std::weak_ptr<int> watched = owner;
        Feed feed(gpu.Get());
        {
            FakeSource source(offeredFormats());
            REQUIRE(source.start(feed, {}));
            source.deliver({.kind = SampleKind::Texture,
                            .width = kWidth,
                            .height = kHeight,
                            .texture = texture.Get(),
                            .subresource = 0,
                            .owner = std::move(owner),
                            .ptsUs = 9'000});
        }
        REQUIRE(feed.frames.size() == 1);
        const VideoFrame& frame = feed.frames.front();
        CHECK(frame.hardware());
        CHECK(frame.ptsUs() == 9'000);
        const bool zeroCopy = frame.avFrame()->data[0] == reinterpret_cast<std::uint8_t*>(texture.Get());
        CHECK(zeroCopy == (binding != 0));
        CHECK(watched.expired() == (binding == 0));
        const std::optional<Planes> planes = planesOf(frame, gpu.Get());
        REQUIRE(planes);
        CHECK(planes->y(10, 5) == 200);
        CHECK(planes->y(50, 30) == 50);
        CHECK(planes->u(5, 3) == 90);
        feed.frames.clear();
        CHECK(watched.expired());
    }
}
