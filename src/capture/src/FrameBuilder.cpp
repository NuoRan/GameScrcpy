#include "gs/capture/FrameBuilder.h"

#include "MediaFoundation.h"

#include <gs/media/FrameUploader.h>

#include <d3d11.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace gs::capture {

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t kMostQueuedPackets = 60;

class MjpegDecoder
{
public:
    using Use = std::function<void(SampleKind kind, const LockedPlanes& planes)>;

    bool decode(std::span<const std::uint8_t> jpeg, int width, int height, const Use& use, std::string& problem)
    {
        if (!m_foundation.ready()) {
            problem = "Media Foundation is not available";
            return false;
        }
        if (!m_transform || width != m_width || height != m_height) {
            if (!open(width, height, problem)) {
                return false;
            }
        }
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(jpeg.size()), &buffer))) {
            problem = "the picture cannot be handed to the MJPEG decoder";
            return false;
        }
        BYTE* data = nullptr;
        if (FAILED(buffer->Lock(&data, nullptr, nullptr))) {
            return false;
        }
        std::memcpy(data, jpeg.data(), jpeg.size());
        buffer->Unlock();
        buffer->SetCurrentLength(static_cast<DWORD>(jpeg.size()));
        ComPtr<IMFSample> input;
        if (FAILED(MFCreateSample(&input)) || FAILED(input->AddBuffer(buffer.Get()))) {
            return false;
        }
        HRESULT accepted = m_transform->ProcessInput(0, input.Get(), 0);
        if (accepted == MF_E_NOTACCEPTING) {
            drain(width, height, use);
            accepted = m_transform->ProcessInput(0, input.Get(), 0);
        }
        if (FAILED(accepted)) {
            problem = failureText("the MJPEG decoder refused a picture", accepted);
            return false;
        }
        return drain(width, height, use) > 0;
    }

private:
    bool open(int width, int height, std::string& problem)
    {
        m_transform.Reset();
        MFT_REGISTER_TYPE_INFO wanted{MFMediaType_Video, MFVideoFormat_MJPG};
        IMFActivate** found = nullptr;
        UINT32 count = 0;
        if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                             MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER, &wanted,
                             nullptr, &found, &count)) ||
            found == nullptr) {
            problem = "no MJPEG decoder is installed";
            return false;
        }
        for (UINT32 index = 0; index < count; ++index) {
            if (!m_transform && found[index] != nullptr) {
                static_cast<void>(found[index]->ActivateObject(IID_PPV_ARGS(&m_transform)));
            }
            if (found[index] != nullptr) {
                found[index]->Release();
            }
        }
        CoTaskMemFree(found);
        if (!m_transform) {
            problem = "the MJPEG decoder cannot be started";
            return false;
        }
        ComPtr<IMFMediaType> input;
        if (FAILED(MFCreateMediaType(&input)) || FAILED(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MJPG)) ||
            FAILED(MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(width),
                                      static_cast<UINT32>(height))) ||
            FAILED(m_transform->SetInputType(0, input.Get(), 0))) {
            problem = "the MJPEG decoder does not take this picture size";
            m_transform.Reset();
            return false;
        }
        if (!chooseOutput()) {
            problem = "the MJPEG decoder gives neither NV12 nor YUY2";
            m_transform.Reset();
            return false;
        }
        m_width = width;
        m_height = height;
        static_cast<void>(m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));
        static_cast<void>(m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
        return true;
    }

    bool chooseOutput()
    {
        ComPtr<IMFMediaType> fallback;
        ComPtr<IMFMediaType> chosen;
        for (DWORD index = 0;; ++index) {
            ComPtr<IMFMediaType> candidate;
            if (FAILED(m_transform->GetOutputAvailableType(0, index, &candidate))) {
                break;
            }
            GUID subtype{};
            if (FAILED(candidate->GetGUID(MF_MT_SUBTYPE, &subtype))) {
                continue;
            }
            if (subtype == MFVideoFormat_NV12) {
                chosen = candidate;
                break;
            }
            if (subtype == MFVideoFormat_YUY2 && !fallback) {
                fallback = candidate;
            }
        }
        if (!chosen) {
            chosen = fallback;
        }
        if (!chosen || FAILED(m_transform->SetOutputType(0, chosen.Get(), 0))) {
            return false;
        }
        GUID subtype{};
        chosen->GetGUID(MF_MT_SUBTYPE, &subtype);
        m_kind = subtype == MFVideoFormat_NV12 ? SampleKind::Nv12 : SampleKind::Yuy2;
        MFT_OUTPUT_STREAM_INFO info{};
        if (FAILED(m_transform->GetOutputStreamInfo(0, &info))) {
            return false;
        }
        m_provides = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
        m_outputSize = info.cbSize;
        return true;
    }

    int drain(int width, int height, const Use& use)
    {
        int produced = 0;
        for (;;) {
            ComPtr<IMFSample> owned;
            if (!m_provides) {
                const DWORD size =
                    m_outputSize > 0 ? m_outputSize : static_cast<DWORD>(width) * static_cast<DWORD>(height) * 2U;
                ComPtr<IMFMediaBuffer> buffer;
                if (FAILED(MFCreateMemoryBuffer(size, &buffer)) || FAILED(MFCreateSample(&owned)) ||
                    FAILED(owned->AddBuffer(buffer.Get()))) {
                    return produced;
                }
            }
            MFT_OUTPUT_DATA_BUFFER output{};
            output.dwStreamID = 0;
            output.pSample = owned.Get();
            DWORD status = 0;
            const HRESULT result = m_transform->ProcessOutput(0, 1, &output, &status);
            if (output.pEvents != nullptr) {
                output.pEvents->Release();
            }
            ComPtr<IMFSample> sample;
            if (m_provides) {
                sample.Attach(output.pSample);
            } else {
                sample = owned;
            }
            if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
                if (!chooseOutput()) {
                    return produced;
                }
                continue;
            }
            if (FAILED(result) || !sample) {
                return produced;
            }
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
                return produced;
            }
            const BufferLock lock(buffer.Get(), m_kind == SampleKind::Nv12 ? Layout::Nv12 : Layout::Yuy2, width,
                                  height);
            if (lock.locked()) {
                use(m_kind, lock.planes());
                ++produced;
            }
        }
    }

    MediaFoundation m_foundation;
    ComPtr<IMFTransform> m_transform;
    int m_width = 0;
    int m_height = 0;
    SampleKind m_kind = SampleKind::Nv12;
    bool m_provides = false;
    DWORD m_outputSize = 0;
};

class Relay final : public media::FrameSink
{
public:
    explicit Relay(const FrameBuilder::Deliver& deliver) : m_deliver(deliver) {}

    void onFrame(media::VideoFrame frame) override
    {
        if (frame && m_deliver) {
            m_deliver(std::move(frame));
        }
    }

private:
    const FrameBuilder::Deliver& m_deliver;
};

class H264Worker
{
public:
    H264Worker(ID3D11Device* device, FrameBuilder::Deliver deliver)
        : m_device(device), m_deliver(std::move(deliver)),
          m_thread([this](const std::stop_token& stop) { run(stop); })
    {
    }

    ~H264Worker()
    {
        m_thread.request_stop();
        m_wake.notify_all();
    }

    H264Worker(const H264Worker&) = delete;
    H264Worker& operator=(const H264Worker&) = delete;

    void push(const CaptureSample& sample, std::uint16_t session)
    {
        Packet packet{.bytes = std::vector<std::uint8_t>(sample.bytes.begin(), sample.bytes.end()),
                      .config = std::vector<std::uint8_t>(sample.config.begin(), sample.config.end()),
                      .width = sample.width,
                      .height = sample.height,
                      .ptsUs = sample.ptsUs,
                      .key = sample.keyFrame,
                      .session = session};
        {
            const std::scoped_lock lock(m_mutex);
            if (m_queue.size() >= kMostQueuedPackets) {
                m_queue.clear();
                m_awaitKey = true;
            }
            if (m_awaitKey && !packet.key) {
                return;
            }
            m_awaitKey = false;
            m_queue.push_back(std::move(packet));
        }
        m_wake.notify_one();
    }

private:
    struct Packet {
        std::vector<std::uint8_t> bytes;
        std::vector<std::uint8_t> config;
        int width = 0;
        int height = 0;
        std::int64_t ptsUs = 0;
        bool key = false;
        std::uint16_t session = 0;
    };

    void run(const std::stop_token& stop)
    {
        media::VideoDecoder decoder(m_device);
        Relay relay(m_deliver);
        std::optional<std::uint16_t> session;
        std::vector<std::uint8_t> config;
        bool broken = false;
        for (;;) {
            Packet packet;
            {
                std::unique_lock lock(m_mutex);
                if (!m_wake.wait(lock, stop, [this] { return !m_queue.empty(); })) {
                    return;
                }
                packet = std::move(m_queue.front());
                m_queue.pop_front();
            }
            if (!session || *session != packet.session || packet.config != config || broken) {
                if (!packet.key) {
                    continue;
                }
                session = packet.session;
                config = packet.config;
                protocol::VideoSessionInfo info;
                info.codec = protocol::VideoCodec::H264;
                info.frameWidth = static_cast<std::uint16_t>(packet.width);
                info.frameHeight = static_cast<std::uint16_t>(packet.height);
                protocol::MediaPacket described{.header = {.kind = protocol::MediaKind::Session, .session = *session},
                                                .session = info,
                                                .payload = {}};
                protocol::MediaPacket configured{
                    .header = {.kind = protocol::MediaKind::Config,
                               .session = *session,
                               .size = static_cast<std::uint32_t>(config.size())},
                    .session = std::monostate{},
                    .payload = config};
                broken = !decoder.push(described, relay) || !decoder.push(configured, relay);
                if (broken) {
                    continue;
                }
            }
            protocol::MediaPacket frame{.header = {.kind = protocol::MediaKind::Frame,
                                                   .keyFrame = packet.key,
                                                   .session = *session,
                                                   .ptsUs = packet.ptsUs,
                                                   .size = static_cast<std::uint32_t>(packet.bytes.size())},
                                        .session = std::monostate{},
                                        .payload = packet.bytes};
            broken = !decoder.push(frame, relay);
        }
    }

    ID3D11Device* m_device;
    FrameBuilder::Deliver m_deliver;
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<Packet> m_queue;
    bool m_awaitKey = true;
    std::jthread m_thread;
};

}

struct FrameBuilder::State {
    State(ID3D11Device* graphics, Deliver handOver) : device(graphics), uploader(graphics), deliver(std::move(handOver))
    {
    }

    void hand(media::VideoFrame frame)
    {
        if (frame && deliver) {
            deliver(std::move(frame));
        }
    }

    void fail(std::string reason)
    {
        const std::scoped_lock lock(mutex);
        problem = std::move(reason);
    }

    ID3D11Device* device;
    media::FrameUploader uploader;
    Deliver deliver;
    std::uint16_t session = 0;
    int width = 0;
    int height = 0;
    std::unique_ptr<MjpegDecoder> mjpeg;
    std::unique_ptr<H264Worker> h264;
    mutable std::mutex mutex;
    std::string problem;
    std::vector<std::uint8_t> scratch;
};

void bgraToNv12(const std::uint8_t* pixels, std::ptrdiff_t pitch, int width, int height, std::uint8_t* luma,
                std::ptrdiff_t lumaPitch, std::uint8_t* chroma, std::ptrdiff_t chromaPitch) noexcept
{
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = pixels + y * pitch;
        std::uint8_t* out = luma + y * lumaPitch;
        for (int x = 0; x < width; ++x) {
            const int blue = row[4 * x];
            const int green = row[4 * x + 1];
            const int red = row[4 * x + 2];
            out[x] = static_cast<std::uint8_t>(((47 * red + 157 * green + 16 * blue + 128) >> 8) + 16);
        }
    }
    for (int y = 0; y < (height + 1) / 2; ++y) {
        const std::uint8_t* top = pixels + (2 * y) * pitch;
        const std::uint8_t* bottom = pixels + std::min(2 * y + 1, height - 1) * pitch;
        std::uint8_t* out = chroma + y * chromaPitch;
        for (int x = 0; x < (width + 1) / 2; ++x) {
            int blue = 0;
            int green = 0;
            int red = 0;
            for (const std::uint8_t* row : {top, bottom}) {
                for (const int column : {2 * x, std::min(2 * x + 1, width - 1)}) {
                    blue += row[4 * column];
                    green += row[4 * column + 1];
                    red += row[4 * column + 2];
                }
            }
            blue = (blue + 2) / 4;
            green = (green + 2) / 4;
            red = (red + 2) / 4;
            out[2 * x] = static_cast<std::uint8_t>(((-26 * red - 86 * green + 112 * blue + 128) >> 8) + 128);
            out[2 * x + 1] = static_cast<std::uint8_t>(((112 * red - 102 * green - 10 * blue + 128) >> 8) + 128);
        }
    }
}

FrameBuilder::FrameBuilder(ID3D11Device* device, Deliver deliver)
    : m_state(std::make_unique<State>(device, std::move(deliver)))
{
}

FrameBuilder::~FrameBuilder()
{
    m_state->h264.reset();
}

void FrameBuilder::build(const CaptureSample& sample)
{
    State& state = *m_state;
    if (sample.width <= 0 || sample.height <= 0) {
        return;
    }
    if (sample.width != state.width || sample.height != state.height) {
        state.width = sample.width;
        state.height = sample.height;
        state.session = static_cast<std::uint16_t>(state.session % 0xFFFFU + 1U);
    }
    const media::FrameStamp stamp{.ptsUs = sample.ptsUs,
                                  .session = state.session,
                                  .receivedUs = sample.receivedUs,
                                  .color = sample.color};
    switch (sample.kind) {
    case SampleKind::Texture: {
        if (sample.texture == nullptr) {
            return;
        }
        D3D11_TEXTURE2D_DESC description{};
        sample.texture->GetDesc(&description);
        const UINT levels = std::max(description.MipLevels, 1U);
        const unsigned slice = sample.subresource / levels;
        if (description.Format == DXGI_FORMAT_NV12 && (description.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 &&
            sample.subresource % levels == 0) {
            state.hand(media::VideoFrame::wrapTexture(sample.texture, slice, sample.width, sample.height, sample.owner,
                                                      sample.ptsUs, state.session, sample.receivedUs, sample.color));
        } else {
            state.hand(state.uploader.copy(sample.texture, sample.subresource, sample.width, sample.height, stamp));
        }
        return;
    }
    case SampleKind::Nv12:
        state.hand(state.uploader.nv12({sample.bytes.data(), sample.pitch, sample.bytes.data() + sample.chromaOffset,
                                        sample.pitch},
                                       sample.width, sample.height, stamp));
        return;
    case SampleKind::Yuy2:
        state.hand(state.uploader.yuy2(sample.bytes.data(), sample.pitch, sample.width, sample.height, stamp));
        return;
    case SampleKind::Mjpg: {
        if (state.mjpeg == nullptr) {
            state.mjpeg = std::make_unique<MjpegDecoder>();
        }
        std::string problem;
        const bool decoded = state.mjpeg->decode(
            sample.bytes, sample.width, sample.height,
            [&](SampleKind kind, const LockedPlanes& planes) {
                if (kind == SampleKind::Nv12) {
                    state.hand(state.uploader.nv12(
                        {planes.data, planes.pitch, planes.data + planes.chromaOffset, planes.pitch}, sample.width,
                        sample.height, stamp));
                } else {
                    state.hand(state.uploader.yuy2(planes.data, planes.pitch, sample.width, sample.height, stamp));
                }
            },
            problem);
        if (!decoded && !problem.empty()) {
            state.fail(std::move(problem));
        }
        return;
    }
    case SampleKind::H264:
        if (state.h264 == nullptr) {
            state.h264 = std::make_unique<H264Worker>(state.device, state.deliver);
        }
        state.h264->push(sample, state.session);
        return;
    case SampleKind::Bgra: {
        const auto rowBytes = static_cast<std::size_t>(sample.width) * 4;
        if (sample.pitch < static_cast<std::ptrdiff_t>(rowBytes) ||
            sample.bytes.size() <
                static_cast<std::size_t>(sample.pitch) * static_cast<std::size_t>(sample.height - 1) + rowBytes) {
            return;
        }
        const int lumaPitch = (sample.width + 1) / 2 * 2;
        const int rows = (sample.height + 1) / 2 * 2;
        state.scratch.resize(static_cast<std::size_t>(lumaPitch) * static_cast<std::size_t>(rows) * 3 / 2);
        std::uint8_t* luma = state.scratch.data();
        std::uint8_t* chroma = luma + static_cast<std::ptrdiff_t>(lumaPitch) * rows;
        bgraToNv12(sample.bytes.data(), sample.pitch, sample.width, sample.height, luma, lumaPitch, chroma, lumaPitch);
        state.hand(state.uploader.nv12({luma, lumaPitch, chroma, lumaPitch}, sample.width, sample.height, stamp));
        return;
    }
    }
}

void FrameBuilder::reset()
{
    m_state->h264.reset();
    m_state->mjpeg.reset();
    m_state->width = 0;
    m_state->height = 0;
}

std::string FrameBuilder::problem() const
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->problem;
}

std::uint64_t FrameBuilder::dropped() const noexcept
{
    return m_state->uploader.dropped();
}

std::uint16_t FrameBuilder::session() const noexcept
{
    return m_state->session;
}

}
