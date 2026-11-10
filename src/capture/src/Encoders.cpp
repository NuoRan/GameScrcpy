#include "gs/capture/Encoders.h"

#include "MediaFoundation.h"

#include <gs/platform/Thread.h>

#include <d3d11.h>
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <strmif.h>
#include <wrl/client.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace gs::capture {

namespace {

using Microsoft::WRL::ComPtr;

constexpr LONGLONG kHundredNanosecondsPerSecond = 10'000'000;
constexpr UINT32 kNominalRate = 60;
constexpr double kBitsPerPixel = 0.1;
constexpr UINT32 kLeastBitrate = 2'000'000;
constexpr UINT32 kMostBitrate = 20'000'000;
constexpr std::size_t kMostJobs = 2;
constexpr int kInputTextures = 3;
constexpr int kSoundRate = 48'000;
constexpr UINT32 kSoundBytesPerSecond = 16'000;
constexpr int kResamplerSlack = 256;

struct Transform {
    ComPtr<IMFTransform> mft;
    ComPtr<IMFMediaEventGenerator> events;
    bool async = false;
    bool hardware = false;
    bool provides = false;
    DWORD outputSize = 0;
    int credits = 0;
};

ComPtr<IMFTransform> activateFirst(const GUID& category, UINT32 flags, const MFT_REGISTER_TYPE_INFO& input,
                                   const MFT_REGISTER_TYPE_INFO& output)
{
    IMFActivate** found = nullptr;
    UINT32 count = 0;
    ComPtr<IMFTransform> transform;
    if (FAILED(MFTEnumEx(category, flags, &input, &output, &found, &count)) || found == nullptr) {
        return transform;
    }
    for (UINT32 index = 0; index < count; ++index) {
        if (!transform && found[index] != nullptr) {
            static_cast<void>(found[index]->ActivateObject(IID_PPV_ARGS(&transform)));
        }
        if (found[index] != nullptr) {
            found[index]->Release();
        }
    }
    CoTaskMemFree(found);
    return transform;
}

bool refreshStreamInfo(Transform& transform)
{
    MFT_OUTPUT_STREAM_INFO info{};
    if (FAILED(transform.mft->GetOutputStreamInfo(0, &info))) {
        return false;
    }
    transform.provides =
        (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
    transform.outputSize = info.cbSize;
    return true;
}

bool renegotiate(Transform& transform)
{
    ComPtr<IMFMediaType> type;
    return SUCCEEDED(transform.mft->GetOutputAvailableType(0, 0, &type)) &&
           SUCCEEDED(transform.mft->SetOutputType(0, type.Get(), 0)) && refreshStreamInfo(transform);
}

template <typename Use>
HRESULT pullOutput(Transform& transform, DWORD fallbackSize, const Use& use)
{
    ComPtr<IMFSample> owned;
    if (!transform.provides) {
        ComPtr<IMFMediaBuffer> buffer;
        const DWORD size = std::max(transform.outputSize, fallbackSize);
        if (FAILED(MFCreateMemoryBuffer(size, &buffer)) || FAILED(MFCreateSample(&owned)) ||
            FAILED(owned->AddBuffer(buffer.Get()))) {
            return E_OUTOFMEMORY;
        }
    }
    MFT_OUTPUT_DATA_BUFFER output{};
    output.dwStreamID = 0;
    output.pSample = owned.Get();
    DWORD status = 0;
    const HRESULT result = transform.mft->ProcessOutput(0, 1, &output, &status);
    if (output.pEvents != nullptr) {
        output.pEvents->Release();
    }
    ComPtr<IMFSample> produced;
    if (transform.provides) {
        produced.Attach(output.pSample);
    } else {
        produced = owned;
    }
    if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
        return renegotiate(transform) ? S_FALSE : result;
    }
    if (FAILED(result) || !produced) {
        return FAILED(result) ? result : E_UNEXPECTED;
    }
    use(*produced.Get());
    return S_OK;
}

template <typename Use>
void drainSync(Transform& transform, DWORD fallbackSize, const Use& use)
{
    for (;;) {
        const HRESULT pulled = pullOutput(transform, fallbackSize, use);
        if (FAILED(pulled)) {
            return;
        }
    }
}

template <typename Use>
bool pump(Transform& transform, DWORD fallbackSize, const Use& use)
{
    bool drained = false;
    for (;;) {
        ComPtr<IMFMediaEvent> event;
        if (FAILED(transform.events->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event))) {
            return drained;
        }
        MediaEventType type = MEUnknown;
        if (FAILED(event->GetType(&type))) {
            continue;
        }
        if (type == METransformNeedInput) {
            ++transform.credits;
        } else if (type == METransformHaveOutput) {
            static_cast<void>(pullOutput(transform, fallbackSize, use));
        } else if (type == METransformDrainComplete) {
            drained = true;
        }
    }
}

bool containsIdr(std::span<const std::uint8_t> bytes) noexcept
{
    for (std::size_t at = 0; at + 3 < bytes.size(); ++at) {
        if (bytes[at] == 0 && bytes[at + 1] == 0 && bytes[at + 2] == 1 && (bytes[at + 3] & 0x1FU) == 5U) {
            return true;
        }
    }
    return false;
}

bool setVideoTypes(Transform& transform, int width, int height)
{
    const auto frameWidth = static_cast<UINT32>(width);
    const auto frameHeight = static_cast<UINT32>(height);
    const double bits = static_cast<double>(width) * static_cast<double>(height) * kNominalRate * kBitsPerPixel;
    const auto bitrate = static_cast<UINT32>(std::clamp(bits, static_cast<double>(kLeastBitrate),
                                                        static_cast<double>(kMostBitrate)));
    ComPtr<IMFMediaType> output;
    if (FAILED(MFCreateMediaType(&output)) || FAILED(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) ||
        FAILED(output->SetUINT32(MF_MT_AVG_BITRATE, bitrate)) ||
        FAILED(MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, frameWidth, frameHeight)) ||
        FAILED(MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, kNominalRate, 1)) ||
        FAILED(MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
        FAILED(output->SetUINT32(MF_MT_INTERLACE_MODE, static_cast<UINT32>(MFVideoInterlace_Progressive))) ||
        FAILED(output->SetUINT32(MF_MT_MPEG2_PROFILE, static_cast<UINT32>(eAVEncH264VProfile_Main))) ||
        FAILED(transform.mft->SetOutputType(0, output.Get(), 0))) {
        return false;
    }
    ComPtr<IMFMediaType> input;
    if (FAILED(MFCreateMediaType(&input)) || FAILED(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)) ||
        FAILED(MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, frameWidth, frameHeight)) ||
        FAILED(MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, kNominalRate, 1)) ||
        FAILED(MFSetAttributeRatio(input.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) ||
        FAILED(input->SetUINT32(MF_MT_INTERLACE_MODE, static_cast<UINT32>(MFVideoInterlace_Progressive)))) {
        return false;
    }
    if (SUCCEEDED(transform.mft->SetInputType(0, input.Get(), 0))) {
        return true;
    }
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> offered;
        if (FAILED(transform.mft->GetInputAvailableType(0, index, &offered))) {
            return false;
        }
        GUID subtype{};
        if (FAILED(offered->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_NV12) {
            continue;
        }
        static_cast<void>(MFSetAttributeSize(offered.Get(), MF_MT_FRAME_SIZE, frameWidth, frameHeight));
        static_cast<void>(MFSetAttributeRatio(offered.Get(), MF_MT_FRAME_RATE, kNominalRate, 1));
        if (SUCCEEDED(transform.mft->SetInputType(0, offered.Get(), 0))) {
            return true;
        }
    }
}

void tuneForLiveRecording(IMFTransform& transform)
{
    ComPtr<ICodecAPI> codec;
    if (FAILED(transform.QueryInterface(IID_PPV_ARGS(&codec)))) {
        return;
    }
    VARIANT lowLatency{};
    lowLatency.vt = VT_BOOL;
    lowLatency.boolVal = VARIANT_TRUE;
    static_cast<void>(codec->SetValue(&CODECAPI_AVLowLatencyMode, &lowLatency));
    VARIANT pictures{};
    pictures.vt = VT_UI4;
    pictures.ulVal = kNominalRate * 2;
    static_cast<void>(codec->SetValue(&CODECAPI_AVEncMPVGOPSize, &pictures));
}

bool openVideo(Transform& transform, IMFDXGIDeviceManager* manager, int width, int height, bool hardware)
{
    transform = {};
    const MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_NV12};
    const MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, MFVideoFormat_H264};
    const UINT32 flags = MFT_ENUM_FLAG_SORTANDFILTER |
                         (hardware ? MFT_ENUM_FLAG_HARDWARE : (MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT));
    transform.mft = activateFirst(MFT_CATEGORY_VIDEO_ENCODER, flags, input, output);
    if (!transform.mft) {
        return false;
    }
    transform.hardware = hardware;
    if (hardware) {
        ComPtr<IMFAttributes> attributes;
        UINT32 async = 0;
        if (SUCCEEDED(transform.mft->GetAttributes(&attributes)) &&
            SUCCEEDED(attributes->GetUINT32(MF_TRANSFORM_ASYNC, &async)) && async != 0) {
            static_cast<void>(attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE));
            transform.async = SUCCEEDED(transform.mft.As(&transform.events));
            if (!transform.async) {
                transform = {};
                return false;
            }
        }
        if (manager == nullptr ||
            FAILED(transform.mft->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(manager)))) {
            transform = {};
            return false;
        }
    }
    if (!setVideoTypes(transform, width, height) || !refreshStreamInfo(transform)) {
        transform = {};
        return false;
    }
    tuneForLiveRecording(*transform.mft.Get());
    static_cast<void>(transform.mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
    static_cast<void>(transform.mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
    return true;
}

template <typename Make>
void emitEncoded(const VideoEncoder::Output& output, const Make& make)
{
    const auto encoded = make();
    if (!encoded || !output) {
        return;
    }
    if (const auto packet = protocol::decodeMediaPacket(protocol::MediaChannel::Video, *encoded)) {
        output(*packet);
    }
}

}

std::vector<std::uint8_t> h264ParameterSets(std::span<const std::uint8_t> accessUnit)
{
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    const auto startOf = [&accessUnit](std::size_t from) {
        for (std::size_t at = from; at + 3 <= accessUnit.size(); ++at) {
            if (accessUnit[at] != 0 || accessUnit[at + 1] != 0) {
                continue;
            }
            if (accessUnit[at + 2] == 1) {
                return std::pair{at, std::size_t{3}};
            }
            if (at + 4 <= accessUnit.size() && accessUnit[at + 2] == 0 && accessUnit[at + 3] == 1) {
                return std::pair{at, std::size_t{4}};
            }
        }
        return std::pair{kNone, std::size_t{0}};
    };
    std::vector<std::uint8_t> sets;
    auto [start, length] = startOf(0);
    while (start != kNone) {
        const std::size_t unit = start + length;
        const auto [next, nextLength] = startOf(unit);
        const std::size_t end = next == kNone ? accessUnit.size() : next;
        if (unit < end) {
            const unsigned type = accessUnit[unit] & 0x1FU;
            if (type == 7U || type == 8U) {
                constexpr std::array<std::uint8_t, 4> kStart{0, 0, 0, 1};
                sets.insert(sets.end(), kStart.begin(), kStart.end());
                sets.insert(sets.end(), accessUnit.begin() + static_cast<std::ptrdiff_t>(unit),
                            accessUnit.begin() + static_cast<std::ptrdiff_t>(end));
            }
        }
        start = next;
        length = nextLength;
    }
    return sets;
}

std::vector<std::uint8_t> aacConfig(int sampleRate, int channels)
{
    constexpr std::array<int, 13> kRates{96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                         22050, 16000, 12000, 11025, 8000,  7350};
    const auto found = std::ranges::find(kRates, sampleRate);
    const auto index = static_cast<unsigned>(found == kRates.end() ? 3 : found - kRates.begin());
    const unsigned value = (2U << 11U) | (index << 7U) | ((static_cast<unsigned>(channels) & 0xFU) << 3U);
    return {static_cast<std::uint8_t>(value >> 8U), static_cast<std::uint8_t>(value & 0xFFU)};
}

struct VideoEncoder::State {
    struct Job {
        media::VideoFrame frame;
        PixelBox region;
        std::int64_t atUs = 0;
    };

    struct Session {
        Transform transform;
        int width = 0;
        int height = 0;
        std::uint16_t id = 0;
        bool configured = false;
        std::int64_t firstUs = 0;
        std::array<ComPtr<ID3D11Texture2D>, kInputTextures> inputs;
        int nextInput = 0;
        ComPtr<ID3D11Texture2D> staging;
    };

    ID3D11Device* device = nullptr;
    Output output;
    mutable std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<Job> jobs;
    std::string problem;
    bool hardwareUsed = false;
    std::uint16_t lastSession = 0;
    std::jthread worker;

    void fail(std::string reason)
    {
        const std::scoped_lock lock(mutex);
        if (problem.empty()) {
            problem = std::move(reason);
        }
    }

    void emit(Session& session, IMFSample& sample)
    {
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample.ConvertToContiguousBuffer(&buffer))) {
            return;
        }
        const BufferLock lock(buffer.Get(), Layout::Bytes, 0, 0);
        if (!lock.locked()) {
            return;
        }
        const std::span<const std::uint8_t> bytes(lock.planes().data, lock.planes().length);
        LONGLONG time = 0;
        static_cast<void>(sample.GetSampleTime(&time));
        UINT32 clean = 0;
        const bool key = (SUCCEEDED(sample.GetUINT32(MFSampleExtension_CleanPoint, &clean)) && clean != 0) ||
                         containsIdr(bytes);
        if (!session.configured) {
            if (!key) {
                return;
            }
            const std::vector<std::uint8_t> config = h264ParameterSets(bytes);
            const std::uint16_t id = session.id;
            emitEncoded(output, [&] { return protocol::encodeMediaConfig(protocol::MediaChannel::Video, id, config); });
            session.configured = true;
        }
        const std::uint16_t id = session.id;
        const std::int64_t ptsUs = session.firstUs + time / 10;
        emitEncoded(output, [&] {
            return protocol::encodeMediaFrame(protocol::MediaChannel::Video, id, protocol::MediaFrameFlags{.keyFrame = key},
                                              ptsUs, 0, bytes);
        });
    }

    void feed(Session& session, IMFSample& sample)
    {
        Transform& transform = session.transform;
        const auto use = [this, &session](IMFSample& produced) { emit(session, produced); };
        const auto fallback = static_cast<DWORD>(session.width * session.height * 3 / 2);
        if (transform.async) {
            static_cast<void>(pump(transform, fallback, use));
            if (transform.credits <= 0) {
                return;
            }
            --transform.credits;
            if (FAILED(transform.mft->ProcessInput(0, &sample, 0))) {
                ++transform.credits;
                return;
            }
            static_cast<void>(pump(transform, fallback, use));
            return;
        }
        HRESULT accepted = transform.mft->ProcessInput(0, &sample, 0);
        if (accepted == MF_E_NOTACCEPTING) {
            drainSync(transform, fallback, use);
            accepted = transform.mft->ProcessInput(0, &sample, 0);
        }
        if (SUCCEEDED(accepted)) {
            drainSync(transform, fallback, use);
        }
    }

    void finish(Session& session)
    {
        Transform& transform = session.transform;
        if (transform.mft) {
            const auto use = [this, &session](IMFSample& produced) { emit(session, produced); };
            const auto fallback = static_cast<DWORD>(session.width * session.height * 3 / 2);
            static_cast<void>(transform.mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0));
            static_cast<void>(transform.mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0));
            if (transform.async) {
                static_cast<void>(pump(transform, fallback, use));
            } else {
                drainSync(transform, fallback, use);
            }
            static_cast<void>(transform.mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0));
        }
        session = Session{};
    }

    bool open(Session& session, IMFDXGIDeviceManager* manager, int width, int height)
    {
        finish(session);
        bool opened = manager != nullptr && openVideo(session.transform, manager, width, height, true);
        if (!opened) {
            opened = openVideo(session.transform, nullptr, width, height, false);
        }
        if (!opened) {
            return false;
        }
        {
            const std::scoped_lock lock(mutex);
            hardwareUsed = session.transform.hardware;
        }
        session.width = width;
        session.height = height;
        lastSession = static_cast<std::uint16_t>(lastSession % 0xFFFFU + 1U);
        session.id = lastSession;
        protocol::VideoSessionInfo info;
        info.codec = protocol::VideoCodec::H264;
        info.frameWidth = static_cast<std::uint16_t>(width);
        info.frameHeight = static_cast<std::uint16_t>(height);
        info.displayWidth = info.frameWidth;
        info.displayHeight = info.frameHeight;
        const std::uint16_t id = session.id;
        emitEncoded(output, [&] { return protocol::encodeVideoSession(id, info); });
        return true;
    }

    ComPtr<IMFSample> gpuInput(Session& session, ID3D11DeviceContext& context, ID3D11Texture2D& source,
                               UINT subresource, const PixelBox& region)
    {
        ComPtr<ID3D11Texture2D>& texture = session.inputs[static_cast<std::size_t>(session.nextInput)];
        session.nextInput = (session.nextInput + 1) % kInputTextures;
        if (!texture) {
            D3D11_TEXTURE2D_DESC layout{};
            layout.Width = static_cast<UINT>(region.width);
            layout.Height = static_cast<UINT>(region.height);
            layout.MipLevels = 1;
            layout.ArraySize = 1;
            layout.Format = DXGI_FORMAT_NV12;
            layout.SampleDesc.Count = 1;
            layout.Usage = D3D11_USAGE_DEFAULT;
            layout.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->CreateTexture2D(&layout, nullptr, &texture))) {
                return nullptr;
            }
        }
        const D3D11_BOX box{static_cast<UINT>(region.x), static_cast<UINT>(region.y), 0,
                            static_cast<UINT>(region.x + region.width), static_cast<UINT>(region.y + region.height), 1};
        context.CopySubresourceRegion(texture.Get(), 0, 0, 0, 0, &source, subresource, &box);
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFSample> sample;
        if (FAILED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), texture.Get(), 0, FALSE, &buffer)) ||
            FAILED(MFCreateSample(&sample)) || FAILED(sample->AddBuffer(buffer.Get()))) {
            return nullptr;
        }
        ComPtr<IMF2DBuffer> surface;
        DWORD length = 0;
        if (SUCCEEDED(buffer.As(&surface)) && SUCCEEDED(surface->GetContiguousLength(&length))) {
            static_cast<void>(buffer->SetCurrentLength(length));
        }
        return sample;
    }

    ComPtr<IMFSample> memoryInput(Session& session, ID3D11DeviceContext* context, const AVFrame& frame,
                                  const PixelBox& region)
    {
        const auto lumaBytes = static_cast<DWORD>(region.width * region.height);
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFSample> sample;
        if (FAILED(MFCreateMemoryBuffer(lumaBytes + lumaBytes / 2, &buffer)) || FAILED(MFCreateSample(&sample)) ||
            FAILED(sample->AddBuffer(buffer.Get()))) {
            return nullptr;
        }
        BYTE* out = nullptr;
        if (FAILED(buffer->Lock(&out, nullptr, nullptr))) {
            return nullptr;
        }
        bool copied = false;
        const auto rows = [&](const std::uint8_t* luma, std::ptrdiff_t lumaPitch, const std::uint8_t* chroma,
                              std::ptrdiff_t chromaPitch, int left, int top) {
            for (int row = 0; row < region.height; ++row) {
                std::memcpy(out + static_cast<std::ptrdiff_t>(row) * region.width, luma + (top + row) * lumaPitch + left,
                            static_cast<std::size_t>(region.width));
            }
            BYTE* chromaOut = out + lumaBytes;
            for (int row = 0; row < region.height / 2; ++row) {
                std::memcpy(chromaOut + static_cast<std::ptrdiff_t>(row) * region.width,
                            chroma + (top / 2 + row) * chromaPitch + left, static_cast<std::size_t>(region.width));
            }
            copied = true;
        };
        if (frame.format == AV_PIX_FMT_NV12) {
            rows(frame.data[0], frame.linesize[0], frame.data[1], frame.linesize[1], region.x, region.y);
        } else if (frame.format == AV_PIX_FMT_D3D11 && context != nullptr) {
            auto* texture = reinterpret_cast<ID3D11Texture2D*>(frame.data[0]);
            const auto slice = static_cast<UINT>(reinterpret_cast<std::intptr_t>(frame.data[1]));
            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            if (!session.staging) {
                D3D11_TEXTURE2D_DESC layout{};
                layout.Width = static_cast<UINT>(region.width);
                layout.Height = static_cast<UINT>(region.height);
                layout.MipLevels = 1;
                layout.ArraySize = 1;
                layout.Format = DXGI_FORMAT_NV12;
                layout.SampleDesc.Count = 1;
                layout.Usage = D3D11_USAGE_STAGING;
                layout.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                static_cast<void>(device->CreateTexture2D(&layout, nullptr, &session.staging));
            }
            if (session.staging) {
                const D3D11_BOX box{static_cast<UINT>(region.x), static_cast<UINT>(region.y), 0,
                                    static_cast<UINT>(region.x + region.width),
                                    static_cast<UINT>(region.y + region.height), 1};
                context->CopySubresourceRegion(session.staging.Get(), 0, 0, 0, 0, texture,
                                               D3D11CalcSubresource(0, slice, description.MipLevels), &box);
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (SUCCEEDED(context->Map(session.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
                    const auto pitch = static_cast<std::ptrdiff_t>(mapped.RowPitch);
                    rows(pixels, pitch, pixels + pitch * region.height, pitch, 0, 0);
                    context->Unmap(session.staging.Get(), 0);
                }
            }
        }
        buffer->Unlock();
        if (!copied) {
            return nullptr;
        }
        static_cast<void>(buffer->SetCurrentLength(lumaBytes + lumaBytes / 2));
        return sample;
    }

    void run(const std::stop_token& stop)
    {
        platform::setCurrentThreadName("gs-capture-encoder");
        const ComScope com;
        const MediaFoundation foundation;
        if (!foundation.ready()) {
            fail("Media Foundation is not available");
            return;
        }
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IMFDXGIDeviceManager> manager;
        UINT token = 0;
        if (device != nullptr) {
            device->GetImmediateContext(&context);
            if (SUCCEEDED(MFCreateDXGIDeviceManager(&token, &manager)) && FAILED(manager->ResetDevice(device, token))) {
                manager.Reset();
            }
        }
        Session session;
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                if (!wake.wait(lock, stop, [this] { return !jobs.empty(); })) {
                    break;
                }
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            const AVFrame* frame = job.frame.avFrame();
            if (frame == nullptr || job.region.width < 2 || job.region.height < 2 ||
                job.region.x + job.region.width > frame->width || job.region.y + job.region.height > frame->height) {
                continue;
            }
            if (!session.transform.mft || job.region.width != session.width || job.region.height != session.height) {
                if (!open(session, manager.Get(), job.region.width, job.region.height)) {
                    fail("no H.264 encoder could be started for the recording");
                    continue;
                }
                session.firstUs = job.atUs;
            }
            ComPtr<IMFSample> sample;
            if (session.transform.hardware && frame->format == AV_PIX_FMT_D3D11 && context) {
                auto* texture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
                const auto slice = static_cast<UINT>(reinterpret_cast<std::intptr_t>(frame->data[1]));
                D3D11_TEXTURE2D_DESC description{};
                texture->GetDesc(&description);
                sample = gpuInput(session, *context.Get(), *texture,
                                  D3D11CalcSubresource(0, slice, description.MipLevels), job.region);
            } else {
                sample = memoryInput(session, context.Get(), *frame, job.region);
            }
            if (!sample) {
                continue;
            }
            static_cast<void>(sample->SetSampleTime((job.atUs - session.firstUs) * 10));
            static_cast<void>(sample->SetSampleDuration(kHundredNanosecondsPerSecond / kNominalRate));
            feed(session, *sample.Get());
        }
        finish(session);
    }
};

VideoEncoder::VideoEncoder(ID3D11Device* device, Output output) : m_state(std::make_unique<State>())
{
    m_state->device = device;
    m_state->output = std::move(output);
    State* const state = m_state.get();
    m_state->worker = std::jthread([state](const std::stop_token& stop) { state->run(stop); });
}

VideoEncoder::~VideoEncoder()
{
    m_state->worker.request_stop();
    if (m_state->worker.joinable()) {
        m_state->worker.join();
    }
}

void VideoEncoder::encode(const media::VideoFrame& frame, const PixelBox& region, std::int64_t atUs)
{
    if (!frame) {
        return;
    }
    {
        const std::scoped_lock lock(m_state->mutex);
        while (m_state->jobs.size() >= kMostJobs) {
            m_state->jobs.pop_front();
        }
        m_state->jobs.push_back(State::Job{.frame = frame.share(), .region = region, .atUs = atUs});
    }
    m_state->wake.notify_one();
}

std::string VideoEncoder::problem() const
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->problem;
}

bool VideoEncoder::hardware() const
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->hardwareUsed;
}

struct AudioEncoder::State {
    Output output;
    MediaFoundation foundation;
    Transform transform;
    SwrContext* resampler = nullptr;
    int inputChannels = 0;
    int inputRate = 0;
    int channels = 0;
    std::int64_t baseUs = 0;
    std::int64_t written = 0;
    std::vector<std::int16_t> pcm;
    mutable std::mutex mutex;
    std::string problem;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() { swr_free(&resampler); }

    void fail(std::string reason)
    {
        const std::scoped_lock lock(mutex);
        if (problem.empty()) {
            problem = std::move(reason);
        }
    }

    bool open(int sourceChannels, int sourceRate, std::int64_t ptsUs)
    {
        transform = {};
        swr_free(&resampler);
        inputChannels = sourceChannels;
        inputRate = sourceRate;
        channels = std::min(sourceChannels, 2);
        AVChannelLayout from{};
        AVChannelLayout to{};
        av_channel_layout_default(&from, sourceChannels);
        av_channel_layout_default(&to, channels);
        const int allocated = swr_alloc_set_opts2(&resampler, &to, AV_SAMPLE_FMT_S16, kSoundRate, &from,
                                                  AV_SAMPLE_FMT_FLT, sourceRate, 0, nullptr);
        av_channel_layout_uninit(&from);
        av_channel_layout_uninit(&to);
        if (allocated < 0 || swr_init(resampler) < 0) {
            swr_free(&resampler);
            return false;
        }
        const MFT_REGISTER_TYPE_INFO input{MFMediaType_Audio, MFAudioFormat_PCM};
        const MFT_REGISTER_TYPE_INFO wanted{MFMediaType_Audio, MFAudioFormat_AAC};
        transform.mft = activateFirst(MFT_CATEGORY_AUDIO_ENCODER,
                                      MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                                      input, wanted);
        if (!transform.mft) {
            return false;
        }
        const auto rate = static_cast<UINT32>(kSoundRate);
        const auto count = static_cast<UINT32>(channels);
        ComPtr<IMFMediaType> encoded;
        ComPtr<IMFMediaType> raw;
        const bool typed =
            SUCCEEDED(MFCreateMediaType(&encoded)) && SUCCEEDED(encoded->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) &&
            SUCCEEDED(encoded->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC)) &&
            SUCCEEDED(encoded->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) &&
            SUCCEEDED(encoded->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate)) &&
            SUCCEEDED(encoded->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, count)) &&
            SUCCEEDED(encoded->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kSoundBytesPerSecond)) &&
            SUCCEEDED(encoded->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0)) &&
            SUCCEEDED(transform.mft->SetOutputType(0, encoded.Get(), 0)) && SUCCEEDED(MFCreateMediaType(&raw)) &&
            SUCCEEDED(raw->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) &&
            SUCCEEDED(raw->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) &&
            SUCCEEDED(raw->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) &&
            SUCCEEDED(raw->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate)) &&
            SUCCEEDED(raw->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, count)) &&
            SUCCEEDED(raw->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, count * 2)) &&
            SUCCEEDED(raw->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * count * 2)) &&
            SUCCEEDED(transform.mft->SetInputType(0, raw.Get(), 0)) && refreshStreamInfo(transform);
        if (!typed) {
            transform = {};
            return false;
        }
        static_cast<void>(transform.mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
        static_cast<void>(transform.mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
        baseUs = ptsUs;
        written = 0;
        const std::vector<std::uint8_t> config = aacConfig(kSoundRate, channels);
        if (output) {
            output(EncodedSound{.config = true,
                                .sampleRate = static_cast<std::uint32_t>(kSoundRate),
                                .channels = static_cast<std::uint8_t>(channels),
                                .ptsUs = ptsUs,
                                .bytes = config});
        }
        return true;
    }

    void emit(IMFSample& sample)
    {
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample.ConvertToContiguousBuffer(&buffer))) {
            return;
        }
        const BufferLock lock(buffer.Get(), Layout::Bytes, 0, 0);
        if (!lock.locked() || !output) {
            return;
        }
        LONGLONG time = 0;
        static_cast<void>(sample.GetSampleTime(&time));
        output(EncodedSound{.config = false,
                            .sampleRate = static_cast<std::uint32_t>(kSoundRate),
                            .channels = static_cast<std::uint8_t>(channels),
                            .ptsUs = baseUs + time / 10,
                            .bytes = std::span<const std::uint8_t>(lock.planes().data, lock.planes().length)});
    }
};

AudioEncoder::AudioEncoder(Output output) : m_state(std::make_unique<State>())
{
    m_state->output = std::move(output);
}

AudioEncoder::~AudioEncoder() = default;

void AudioEncoder::encode(std::span<const float> interleaved, int channels, int sampleRate, std::int64_t ptsUs)
{
    State& state = *m_state;
    if (channels <= 0 || sampleRate <= 0 || interleaved.size() < static_cast<std::size_t>(channels)) {
        return;
    }
    if (channels != state.inputChannels || sampleRate != state.inputRate) {
        if (!state.foundation.ready() || !state.open(channels, sampleRate, ptsUs)) {
            state.fail("no AAC encoder could be started for the recording");
            state.transform = {};
            state.inputChannels = channels;
            state.inputRate = sampleRate;
            return;
        }
    }
    if (!state.transform.mft) {
        return;
    }
    const int frames = static_cast<int>(interleaved.size() / static_cast<std::size_t>(channels));
    const auto capacity =
        static_cast<int>(swr_get_delay(state.resampler, kSoundRate) + std::int64_t{frames} * kSoundRate / sampleRate +
                         kResamplerSlack);
    state.pcm.resize(static_cast<std::size_t>(capacity) * static_cast<std::size_t>(state.channels));
    auto* out = reinterpret_cast<std::uint8_t*>(state.pcm.data());
    const auto* in = reinterpret_cast<const std::uint8_t*>(interleaved.data());
    const int converted = swr_convert(state.resampler, &out, capacity, &in, frames);
    if (converted <= 0) {
        return;
    }
    const auto bytes = static_cast<DWORD>(converted * state.channels * static_cast<int>(sizeof(std::int16_t)));
    ComPtr<IMFMediaBuffer> buffer;
    ComPtr<IMFSample> sample;
    BYTE* data = nullptr;
    if (FAILED(MFCreateMemoryBuffer(bytes, &buffer)) || FAILED(MFCreateSample(&sample)) ||
        FAILED(sample->AddBuffer(buffer.Get())) || FAILED(buffer->Lock(&data, nullptr, nullptr))) {
        return;
    }
    std::memcpy(data, state.pcm.data(), bytes);
    buffer->Unlock();
    static_cast<void>(buffer->SetCurrentLength(bytes));
    static_cast<void>(sample->SetSampleTime(state.written * kHundredNanosecondsPerSecond / kSoundRate));
    static_cast<void>(sample->SetSampleDuration(std::int64_t{converted} * kHundredNanosecondsPerSecond / kSoundRate));
    state.written += converted;
    const auto use = [&state](IMFSample& produced) { state.emit(produced); };
    HRESULT accepted = state.transform.mft->ProcessInput(0, sample.Get(), 0);
    if (accepted == MF_E_NOTACCEPTING) {
        drainSync(state.transform, 4096, use);
        accepted = state.transform.mft->ProcessInput(0, sample.Get(), 0);
    }
    if (SUCCEEDED(accepted)) {
        drainSync(state.transform, 4096, use);
    }
}

std::string AudioEncoder::problem() const
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->problem;
}

}
