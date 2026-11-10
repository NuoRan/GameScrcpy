#include "gs/capture/MediaFoundationSource.h"

#include "MediaFoundation.h"

#include <d3d11.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace gs::capture {

namespace {

using Microsoft::WRL::ComPtr;

constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr DWORD kFlushWaitMs = 3000;
constexpr UINT32 kExtraSamples = 8;
constexpr std::int64_t kTimestampSlackUs = 1'000'000;
constexpr GUID kBindFlags{0xeacf97ad, 0x065c, 0x4408, {0xbe, 0xe3, 0xfd, 0xcb, 0xfd, 0x12, 0x8b, 0xe2}};
constexpr GUID kMinimumOutputSamples{0x851745d5, 0xc3d6, 0x476d, {0x95, 0x27, 0x49, 0x8e, 0xf2, 0xd1, 0x0d, 0x18}};
constexpr GUID kDeviceTimestamp{0x8f3e35e7, 0x2dcd, 0x4887, {0x86, 0x22, 0x2a, 0x58, 0xba, 0xa6, 0x52, 0xb0}};

std::optional<Subtype> subtypeOf(const GUID& subtype) noexcept
{
    if (subtype == MFVideoFormat_NV12) {
        return Subtype::Nv12;
    }
    if (subtype == MFVideoFormat_YUY2) {
        return Subtype::Yuy2;
    }
    if (subtype == MFVideoFormat_MJPG) {
        return Subtype::Mjpg;
    }
    if (subtype == MFVideoFormat_H264) {
        return Subtype::H264;
    }
    return std::nullopt;
}

SampleKind kindOf(Subtype subtype) noexcept
{
    switch (subtype) {
    case Subtype::Yuy2:
        return SampleKind::Yuy2;
    case Subtype::Mjpg:
        return SampleKind::Mjpg;
    case Subtype::H264:
        return SampleKind::H264;
    case Subtype::Nv12:
    case Subtype::Other:
        break;
    }
    return SampleKind::Nv12;
}

std::vector<CaptureFormat> nativeFormats(IMFSourceReader& reader)
{
    std::vector<CaptureFormat> formats;
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader.GetNativeMediaType(kVideoStream, index, &type))) {
            break;
        }
        GUID subtype{};
        UINT32 width = 0;
        UINT32 height = 0;
        UINT32 numerator = 0;
        UINT32 denominator = 0;
        if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
            FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height)) ||
            FAILED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator))) {
            continue;
        }
        formats.push_back(CaptureFormat{.width = static_cast<int>(width),
                                        .height = static_cast<int>(height),
                                        .rateNumerator = numerator,
                                        .rateDenominator = denominator == 0 ? 1 : denominator,
                                        .subtype = subtypeOf(subtype).value_or(Subtype::Other),
                                        .index = index});
    }
    return formats;
}

ComPtr<IMFMediaType> outputOf(IMFMediaType& native, const GUID& subtype)
{
    ComPtr<IMFMediaType> output;
    UINT32 width = 0;
    UINT32 height = 0;
    UINT32 numerator = 0;
    UINT32 denominator = 0;
    if (FAILED(MFCreateMediaType(&output)) || FAILED(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(output->SetGUID(MF_MT_SUBTYPE, subtype))) {
        return nullptr;
    }
    if (SUCCEEDED(MFGetAttributeSize(&native, MF_MT_FRAME_SIZE, &width, &height))) {
        static_cast<void>(MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, width, height));
    }
    if (SUCCEEDED(MFGetAttributeRatio(&native, MF_MT_FRAME_RATE, &numerator, &denominator))) {
        static_cast<void>(MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, numerator, denominator));
    }
    return output;
}

void bindForSampling(IMFSourceReader& reader)
{
    ComPtr<IMFSourceReaderEx> extended;
    if (FAILED(reader.QueryInterface(IID_PPV_ARGS(&extended)))) {
        return;
    }
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFTransform> transform;
        GUID category{};
        if (FAILED(extended->GetTransformForStream(kVideoStream, index, &category, &transform))) {
            return;
        }
        ComPtr<IMFAttributes> attributes;
        if (SUCCEEDED(transform->GetOutputStreamAttributes(0, &attributes))) {
            static_cast<void>(attributes->SetUINT32(kBindFlags, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DECODER));
            static_cast<void>(attributes->SetUINT32(kMinimumOutputSamples, kExtraSamples));
        }
    }
}

}

struct MediaFoundationSource::Shared {
    SampleSink* sink = nullptr;
    std::atomic<bool> stopping{false};
    std::atomic<bool> over{false};
    mutable std::mutex mutex;
    ComPtr<IMFSourceReader> reader;
    SampleKind kind = SampleKind::Nv12;
    int width = 0;
    int height = 0;
    media::VideoColor color;
    std::vector<std::uint8_t> sequence;
    bool nativeMjpeg = false;
    std::string problem;
    Event flushed;

    [[nodiscard]] ComPtr<IMFSourceReader> readerNow() const
    {
        const std::scoped_lock lock(mutex);
        return reader;
    }

    void end(std::string reason)
    {
        if (over.exchange(true) || stopping.load()) {
            return;
        }
        {
            const std::scoped_lock lock(mutex);
            problem = reason;
        }
        if (sink != nullptr) {
            sink->ended(reason);
        }
    }

    void describe(IMFSourceReader& source)
    {
        ComPtr<IMFMediaType> type;
        if (FAILED(source.GetCurrentMediaType(kVideoStream, &type))) {
            return;
        }
        GUID subtype{};
        UINT32 frameWidth = 0;
        UINT32 frameHeight = 0;
        UINT32 range = 0;
        UINT32 matrix = 0;
        static_cast<void>(type->GetGUID(MF_MT_SUBTYPE, &subtype));
        static_cast<void>(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &frameWidth, &frameHeight));
        static_cast<void>(type->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, &range));
        static_cast<void>(type->GetUINT32(MF_MT_YUV_MATRIX, &matrix));
        std::vector<std::uint8_t> header;
        UINT32 size = 0;
        if (SUCCEEDED(type->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &size)) && size > 0) {
            header.resize(size);
            if (FAILED(type->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, header.data(), size, nullptr))) {
                header.clear();
            }
        }
        const std::scoped_lock lock(mutex);
        kind = kindOf(subtypeOf(subtype).value_or(Subtype::Other));
        width = static_cast<int>(frameWidth);
        height = static_cast<int>(frameHeight);
        const bool jpeg = nativeMjpeg && range == 0;
        const bool hd = matrix == static_cast<UINT32>(MFVideoTransferMatrix_BT709);
        const bool sd = matrix == static_cast<UINT32>(MFVideoTransferMatrix_BT601) || jpeg;
        color.matrix = hd ? media::VideoMatrix::Bt709 : sd ? media::VideoMatrix::Bt601 : media::VideoMatrix::Automatic;
        color.fullRange = range == static_cast<UINT32>(MFNominalRange_0_255) || jpeg;
        sequence = std::move(header);
    }

    void deliver(IMFSample& sample, LONGLONG timestamp)
    {
        CaptureSample out;
        std::vector<std::uint8_t> header;
        {
            const std::scoped_lock lock(mutex);
            out.kind = kind;
            out.width = width;
            out.height = height;
            out.color = color;
            if (kind == SampleKind::H264) {
                header = sequence;
            }
        }
        if (out.width <= 0 || out.height <= 0 || sink == nullptr) {
            return;
        }
        out.ptsUs = timestamp / 10;
        const std::int64_t now = steadyMicros();
        UINT64 captured = 0;
        out.receivedUs = now;
        if (SUCCEEDED(sample.GetUINT64(kDeviceTimestamp, &captured))) {
            const auto at = static_cast<std::int64_t>(captured / 10);
            if (at <= now && now - at < kTimestampSlackUs) {
                out.receivedUs = at;
            }
        }
        UINT32 clean = 0;
        out.keyFrame = FAILED(sample.GetUINT32(MFSampleExtension_CleanPoint, &clean)) || clean != 0;
        out.config = header;
        ComPtr<IMFMediaBuffer> buffer;
        DWORD buffers = 0;
        if (FAILED(sample.GetBufferCount(&buffers)) || buffers == 0) {
            return;
        }
        if (FAILED(buffers == 1 ? sample.GetBufferByIndex(0, &buffer) : sample.ConvertToContiguousBuffer(&buffer))) {
            return;
        }
        ComPtr<IMFDXGIBuffer> surface;
        if (SUCCEEDED(buffer.As(&surface))) {
            ComPtr<ID3D11Texture2D> texture;
            UINT subresource = 0;
            if (SUCCEEDED(surface->GetResource(IID_PPV_ARGS(&texture))) &&
                SUCCEEDED(surface->GetSubresourceIndex(&subresource))) {
                sample.AddRef();
                out.kind = SampleKind::Texture;
                out.texture = texture.Get();
                out.subresource = subresource;
                out.owner = std::shared_ptr<void>(&sample, [](void* held) { static_cast<IMFSample*>(held)->Release(); });
                sink->sample(out);
                return;
            }
        }
        const Layout layout = out.kind == SampleKind::Nv12   ? Layout::Nv12
                              : out.kind == SampleKind::Yuy2 ? Layout::Yuy2
                                                             : Layout::Bytes;
        const BufferLock lock(buffer.Get(), layout, out.width, out.height);
        if (!lock.locked()) {
            return;
        }
        out.bytes = std::span<const std::uint8_t>(lock.planes().data, lock.planes().length);
        out.pitch = lock.planes().pitch;
        out.chromaOffset = lock.planes().chromaOffset;
        sink->sample(out);
    }
};

namespace {

class ReaderCallback final : public IMFSourceReaderCallback
{
public:
    explicit ReaderCallback(std::shared_ptr<MediaFoundationSource::Shared> shared) : m_shared(std::move(shared)) {}

    STDMETHODIMP QueryInterface(REFIID id, void** object) override
    {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (id == __uuidof(IUnknown) || id == __uuidof(IMFSourceReaderCallback)) {
            *object = static_cast<IMFSourceReaderCallback*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return m_references.fetch_add(1) + 1; }

    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG left = m_references.fetch_sub(1) - 1;
        if (left == 0) {
            delete this;
        }
        return left;
    }

    STDMETHODIMP OnReadSample(HRESULT status, DWORD, DWORD flags, LONGLONG timestamp, IMFSample* sample) override
    {
        MediaFoundationSource::Shared& shared = *m_shared;
        if (shared.stopping.load()) {
            return S_OK;
        }
        if (FAILED(status)) {
            shared.end(failureText("the capture device stopped", status));
            return S_OK;
        }
        if ((flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) != 0) {
            shared.end("the capture device ended its picture");
            return S_OK;
        }
        const ComPtr<IMFSourceReader> reader = shared.readerNow();
        if (!reader) {
            return S_OK;
        }
        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0) {
            shared.describe(*reader.Get());
        }
        if (const HRESULT next = reader->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
            FAILED(next)) {
            shared.end(failureText("the capture device stopped giving pictures", next));
        }
        if (sample != nullptr) {
            shared.deliver(*sample, timestamp);
        }
        return S_OK;
    }

    STDMETHODIMP OnFlush(DWORD) override
    {
        SetEvent(m_shared->flushed.get());
        return S_OK;
    }

    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }

private:
    ~ReaderCallback() = default;

    std::atomic<ULONG> m_references{1};
    std::shared_ptr<MediaFoundationSource::Shared> m_shared;
};

bool configure(IMFSourceReader& reader, const CaptureFormat& chosen, MediaFoundationSource::Shared& shared)
{
    ComPtr<IMFMediaType> native;
    if (FAILED(reader.GetNativeMediaType(kVideoStream, chosen.index, &native))) {
        return false;
    }
    ComPtr<IMFSourceReaderEx> extended;
    if (SUCCEEDED(reader.QueryInterface(IID_PPV_ARGS(&extended)))) {
        DWORD flags = 0;
        static_cast<void>(extended->SetNativeMediaType(kVideoStream, native.Get(), &flags));
    }
    shared.nativeMjpeg = chosen.subtype == Subtype::Mjpg;
    if (chosen.subtype == Subtype::Mjpg) {
        for (const GUID& decoded : {MFVideoFormat_NV12, MFVideoFormat_YUY2}) {
            if (const ComPtr<IMFMediaType> output = outputOf(*native.Get(), decoded);
                output && SUCCEEDED(reader.SetCurrentMediaType(kVideoStream, nullptr, output.Get()))) {
                bindForSampling(reader);
                return true;
            }
        }
    }
    return SUCCEEDED(reader.SetCurrentMediaType(kVideoStream, nullptr, native.Get()));
}

}

MediaFoundationSource::MediaFoundationSource(std::wstring link, std::wstring container, ID3D11Device* device)
    : m_link(std::move(link)), m_container(std::move(container)), m_device(device)
{
}

MediaFoundationSource::~MediaFoundationSource()
{
    stop();
}

bool MediaFoundationSource::start(SampleSink& sink, const std::string& format)
{
    stop();
    m_shared = std::make_shared<Shared>();
    m_shared->sink = &sink;
    m_thread = std::jthread([this, format](const std::stop_token& stop) { run(stop, format); });
    return true;
}

void MediaFoundationSource::stop()
{
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

std::string MediaFoundationSource::problem() const
{
    if (m_shared == nullptr) {
        return {};
    }
    const std::scoped_lock lock(m_shared->mutex);
    return m_shared->problem;
}

void MediaFoundationSource::run(const std::stop_token& stop, std::string format)
{
    const std::shared_ptr<Shared> shared = m_shared;
    const ComScope com;
    const MediaFoundation foundation;
    if (!com.ready() || !foundation.ready()) {
        shared->end("Media Foundation is not available on this computer");
        return;
    }
    ComPtr<IMFAttributes> wanted;
    ComPtr<IMFMediaSource> source;
    if (FAILED(MFCreateAttributes(&wanted, 2)) ||
        FAILED(wanted->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)) ||
        FAILED(wanted->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, m_link.c_str()))) {
        shared->end("the capture device cannot be described");
        return;
    }
    if (const HRESULT created = MFCreateDeviceSource(wanted.Get(), &source); FAILED(created)) {
        shared->end(failureText("the capture device cannot be opened; another program may be using it", created));
        return;
    }
    ComPtr<IMFDXGIDeviceManager> manager;
    UINT token = 0;
    if (m_device != nullptr && SUCCEEDED(MFCreateDXGIDeviceManager(&token, &manager)) &&
        FAILED(manager->ResetDevice(m_device, token))) {
        manager.Reset();
    }
    ComPtr<ReaderCallback> callback;
    callback.Attach(new ReaderCallback(shared));
    ComPtr<IMFAttributes> settings;
    ComPtr<IMFSourceReader> reader;
    HRESULT made = MFCreateAttributes(&settings, 6);
    if (SUCCEEDED(made)) {
        static_cast<void>(settings->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.Get()));
        static_cast<void>(settings->SetUINT32(MF_LOW_LATENCY, TRUE));
        static_cast<void>(settings->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
        static_cast<void>(settings->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, FALSE));
        if (manager) {
            static_cast<void>(settings->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, manager.Get()));
        }
        made = MFCreateSourceReaderFromMediaSource(source.Get(), settings.Get(), &reader);
    }
    if (FAILED(made)) {
        source->Shutdown();
        shared->end(failureText("the picture of the capture device cannot be read", made));
        return;
    }
    const std::vector<CaptureFormat> formats = nativeFormats(*reader.Get());
    const std::optional<CaptureFormat> chosen = chooseFormat(formats, format);
    if (shared->sink != nullptr) {
        shared->sink->offered(formats, chosen);
    }
    if (!chosen || !configure(*reader.Get(), *chosen, *shared)) {
        reader.Reset();
        source->Shutdown();
        shared->end(chosen ? "the capture device refused its picture format"
                           : "the capture device offers no picture format GameScrcpy can show");
        return;
    }
    shared->describe(*reader.Get());
    {
        const std::scoped_lock lock(shared->mutex);
        shared->reader = reader;
    }
    if (const HRESULT first = reader->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr); FAILED(first)) {
        shared->end(failureText("the capture device gives no picture", first));
    }
    std::mutex idle;
    std::condition_variable_any parked;
    std::unique_lock lock(idle);
    parked.wait(lock, stop, [] { return false; });
    shared->stopping.store(true);
    if (SUCCEEDED(reader->Flush(kAllStreams))) {
        WaitForSingleObject(shared->flushed.get(), kFlushWaitMs);
    }
    {
        const std::scoped_lock held(shared->mutex);
        shared->reader.Reset();
    }
    reader.Reset();
    source->Shutdown();
    source.Reset();
    manager.Reset();
}

}
