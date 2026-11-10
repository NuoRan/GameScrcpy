#include "gs/media/VideoDecoder.h"

#include "gs/base/Log.h"

#include <d3d11_4.h>
#include <wrl/client.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#include <bit>
#include <chrono>
#include <cstring>
#include <format>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gs::media {

namespace {

using Microsoft::WRL::ComPtr;

constexpr int kSurfacesHeldAfterDecoding = 5;
constexpr std::string_view kLog = "video";

VideoDecodeError ffmpegFailure(std::string_view what, int code)
{
    char reason[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, reason, sizeof(reason));
    return {VideoDecodeErrorKind::Failed, std::format("{}: {}", what, reason)};
}

VideoDecodeError outOfOrder(std::string_view what)
{
    return {VideoDecodeErrorKind::OutOfOrder, std::string(what)};
}

const AVCodec* findDecoder(protocol::VideoCodec codec, bool hardware)
{
    switch (codec) {
    case protocol::VideoCodec::H264:
        return avcodec_find_decoder_by_name("h264");
    case protocol::VideoCodec::H265:
        return avcodec_find_decoder_by_name("hevc");
    case protocol::VideoCodec::Av1:
        return avcodec_find_decoder_by_name(hardware ? "av1" : "libdav1d");
    }
    return nullptr;
}

AVBufferRef* createDeviceContext(ID3D11Device* device)
{
    AVBufferRef* context = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (context == nullptr) {
        return nullptr;
    }
    auto* hardwareContext = reinterpret_cast<AVHWDeviceContext*>(context->data);
    device->AddRef();
    static_cast<AVD3D11VADeviceContext*>(hardwareContext->hwctx)->device = device;
    if (av_hwdevice_ctx_init(context) < 0) {
        av_buffer_unref(&context);
    }
    return context;
}

bool useSampleableSurfaces(AVCodecContext* context)
{
    AVBufferRef* frames = nullptr;
    if (avcodec_get_hw_frames_parameters(context, context->hw_device_ctx, AV_PIX_FMT_D3D11, &frames) < 0) {
        return false;
    }
    auto* framesContext = reinterpret_cast<AVHWFramesContext*>(frames->data);
    static_cast<AVD3D11VAFramesContext*>(framesContext->hwctx)->BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    framesContext->initial_pool_size += kSurfacesHeldAfterDecoding;
    if (av_hwframe_ctx_init(frames) < 0) {
        av_buffer_unref(&frames);
        return false;
    }
    av_buffer_unref(&context->hw_frames_ctx);
    context->hw_frames_ctx = frames;
    return true;
}

AVPixelFormat chooseFormat(AVCodecContext* context, const AVPixelFormat* formats)
{
    for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format) {
        if (*format == AV_PIX_FMT_D3D11 && useSampleableSurfaces(context)) {
            return AV_PIX_FMT_D3D11;
        }
    }
    return avcodec_default_get_format(context, formats);
}

void releaseOwner(void* opaque, std::uint8_t*)
{
    delete static_cast<std::shared_ptr<void>*>(opaque);
}

void applyColor(AVFrame& frame, VideoColor color) noexcept
{
    switch (color.matrix) {
    case VideoMatrix::Bt601:
        frame.colorspace = AVCOL_SPC_SMPTE170M;
        break;
    case VideoMatrix::Bt709:
        frame.colorspace = AVCOL_SPC_BT709;
        break;
    case VideoMatrix::Automatic:
        frame.colorspace = AVCOL_SPC_UNSPECIFIED;
        break;
    }
    frame.color_range = color.fullRange ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
}

}

VideoFrame VideoFrame::adopt(AVFrame* frame, std::uint16_t session, std::int64_t receivedUs) noexcept
{
    return VideoFrame(frame, session, receivedUs);
}

VideoFrame VideoFrame::wrapTexture(ID3D11Texture2D* texture, unsigned slice, int width, int height,
                                   std::shared_ptr<void> owner, std::int64_t ptsUs, std::uint16_t session,
                                   std::int64_t receivedUs, VideoColor color)
{
    if (texture == nullptr || width <= 0 || height <= 0) {
        return {};
    }
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        return {};
    }
    auto* holder = new std::shared_ptr<void>(std::move(owner));
    frame->buf[0] = av_buffer_create(reinterpret_cast<std::uint8_t*>(texture), sizeof(texture), releaseOwner, holder,
                                     AV_BUFFER_FLAG_READONLY);
    if (frame->buf[0] == nullptr) {
        delete holder;
        av_frame_free(&frame);
        return {};
    }
    frame->format = AV_PIX_FMT_D3D11;
    frame->width = width;
    frame->height = height;
    frame->data[0] = reinterpret_cast<std::uint8_t*>(texture);
    frame->data[1] = reinterpret_cast<std::uint8_t*>(static_cast<std::intptr_t>(slice));
    frame->pts = ptsUs;
    applyColor(*frame, color);
    return VideoFrame(frame, session, receivedUs);
}

VideoFrame VideoFrame::copyNv12(const std::uint8_t* luma, std::ptrdiff_t lumaPitch, const std::uint8_t* chroma,
                                std::ptrdiff_t chromaPitch, int width, int height, std::int64_t ptsUs,
                                std::uint16_t session, std::int64_t receivedUs, VideoColor color)
{
    if (luma == nullptr || chroma == nullptr || width <= 0 || height <= 0) {
        return {};
    }
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        return {};
    }
    frame->format = AV_PIX_FMT_NV12;
    frame->width = width;
    frame->height = height;
    if (av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        return {};
    }
    for (int row = 0; row < height; ++row) {
        std::memcpy(frame->data[0] + static_cast<std::ptrdiff_t>(row) * frame->linesize[0],
                    luma + static_cast<std::ptrdiff_t>(row) * lumaPitch, static_cast<std::size_t>(width));
    }
    const int chromaRows = (height + 1) / 2;
    const auto chromaBytes = static_cast<std::size_t>(2 * ((width + 1) / 2));
    for (int row = 0; row < chromaRows; ++row) {
        std::memcpy(frame->data[1] + static_cast<std::ptrdiff_t>(row) * frame->linesize[1],
                    chroma + static_cast<std::ptrdiff_t>(row) * chromaPitch, chromaBytes);
    }
    frame->pts = ptsUs;
    applyColor(*frame, color);
    return VideoFrame(frame, session, receivedUs);
}

struct VideoDecoder::State {
    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    ~State()
    {
        close();
        av_buffer_unref(&hardwareDevice);
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_buffer_pool_uninit(&payloads);
    }

    void close() noexcept
    {
        avcodec_free_context(&context);
        hardware = false;
    }

    Expected<void, VideoDecodeError> open(bool onGpu)
    {
        close();
        const AVCodec* codec = findDecoder(described->codec, onGpu);
        if (codec == nullptr) {
            return makeUnexpected(VideoDecodeError{
                VideoDecodeErrorKind::Unsupported,
                std::format("no decoder for {}", protocol::enumName(described->codec))});
        }
        context = avcodec_alloc_context3(codec);
        if (context == nullptr) {
            return makeUnexpected(ffmpegFailure("allocating the decoder", AVERROR(ENOMEM)));
        }
        context->pkt_timebase = AVRational{1, 1'000'000};
        context->flags |= AV_CODEC_FLAG_LOW_DELAY;
        context->thread_type = FF_THREAD_SLICE;
        context->thread_count = onGpu ? 1 : 0;
        if (!configuration.empty()) {
            context->extradata = static_cast<std::uint8_t*>(
                av_mallocz(configuration.size() + AV_INPUT_BUFFER_PADDING_SIZE));
            if (context->extradata == nullptr) {
                close();
                return makeUnexpected(ffmpegFailure("allocating the codec configuration", AVERROR(ENOMEM)));
            }
            std::memcpy(context->extradata, configuration.data(), configuration.size());
            context->extradata_size = static_cast<int>(configuration.size());
        }
        if (onGpu) {
            context->hw_device_ctx = av_buffer_ref(hardwareDevice);
            context->get_format = chooseFormat;
        }
        if (const int opened = avcodec_open2(context, codec, nullptr); opened < 0) {
            close();
            return makeUnexpected(ffmpegFailure("opening the decoder", opened));
        }
        hardware = onGpu;
        return {};
    }

    Expected<void, VideoDecodeError> decode(const protocol::MediaPacket& input, FrameSink& sink,
                                            std::int64_t receivedUs)
    {
        const std::size_t size = input.payload.size();
        if (size + AV_INPUT_BUFFER_PADDING_SIZE > payloadCapacity) {
            av_buffer_pool_uninit(&payloads);
            payloadCapacity = std::bit_ceil(size + AV_INPUT_BUFFER_PADDING_SIZE);
            payloads = av_buffer_pool_init(payloadCapacity, nullptr);
        }
        AVBufferRef* buffer = av_buffer_pool_get(payloads);
        if (buffer == nullptr) {
            return makeUnexpected(ffmpegFailure("allocating a packet", AVERROR(ENOMEM)));
        }
        std::memcpy(buffer->data, input.payload.data(), size);
        std::memset(buffer->data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        packet->buf = buffer;
        packet->data = buffer->data;
        packet->size = static_cast<int>(size);
        packet->pts = input.header.ptsUs;
        packet->flags = (input.header.keyFrame ? AV_PKT_FLAG_KEY : 0) |
                        (input.header.discardable ? AV_PKT_FLAG_DISPOSABLE : 0);
        const int sent = avcodec_send_packet(context, packet);
        av_packet_unref(packet);
        if (sent < 0) {
            return makeUnexpected(ffmpegFailure("decoding", sent));
        }
        for (;;) {
            const int received = avcodec_receive_frame(context, frame);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                return {};
            }
            if (received < 0) {
                return makeUnexpected(ffmpegFailure("decoding", received));
            }
            hardware = frame->format == AV_PIX_FMT_D3D11;
            if (reported != hardware) {
                reported = hardware;
                log::info(kLog, "video session {}: {} {}x{} decoded {}", session, protocol::enumName(described->codec),
                          frame->width, frame->height,
                          hardware                    ? "on the graphics card"
                          : hardwareDevice != nullptr ? "on the processor; the graphics card did not take it"
                                                      : "on the processor");
            }
            AVFrame* picture = av_frame_alloc();
            if (picture == nullptr) {
                av_frame_unref(frame);
                return makeUnexpected(ffmpegFailure("allocating a picture", AVERROR(ENOMEM)));
            }
            av_frame_move_ref(picture, frame);
            sink.onFrame(VideoFrame(picture, session, receivedUs));
        }
    }

    AVBufferRef* hardwareDevice = nullptr;
    AVCodecContext* context = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVBufferPool* payloads = nullptr;
    std::size_t payloadCapacity = 0;
    std::optional<protocol::VideoSessionInfo> described;
    std::uint16_t session = 0;
    std::vector<std::uint8_t> configuration;
    bool hardware = false;
    std::optional<bool> reported;
};

VideoFrame::VideoFrame(VideoFrame&& other) noexcept
    : m_frame(std::exchange(other.m_frame, nullptr)), m_session(other.m_session), m_receivedUs(other.m_receivedUs)
{
}

VideoFrame& VideoFrame::operator=(VideoFrame&& other) noexcept
{
    if (this != &other) {
        av_frame_free(&m_frame);
        m_frame = std::exchange(other.m_frame, nullptr);
        m_session = other.m_session;
        m_receivedUs = other.m_receivedUs;
    }
    return *this;
}

VideoFrame::~VideoFrame()
{
    av_frame_free(&m_frame);
}

bool VideoFrame::hardware() const noexcept
{
    return m_frame != nullptr && m_frame->format == AV_PIX_FMT_D3D11;
}

int VideoFrame::width() const noexcept
{
    return m_frame != nullptr ? m_frame->width : 0;
}

int VideoFrame::height() const noexcept
{
    return m_frame != nullptr ? m_frame->height : 0;
}

std::int64_t VideoFrame::ptsUs() const noexcept
{
    return m_frame != nullptr ? m_frame->pts : 0;
}

VideoFrame VideoFrame::share() const
{
    if (m_frame == nullptr) {
        return {};
    }
    return VideoFrame(av_frame_clone(m_frame), m_session, m_receivedUs);
}

VideoDecoder::VideoDecoder(ID3D11Device* device) : m_state(std::make_unique<State>())
{
    m_thread.detach();
    if (device == nullptr) {
        return;
    }
    ComPtr<ID3D11DeviceContext> immediate;
    device->GetImmediateContext(&immediate);
    if (ComPtr<ID3D11Multithread> multithread; SUCCEEDED(immediate.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
    }
    m_state->hardwareDevice = createDeviceContext(device);
}

VideoDecoder::~VideoDecoder() = default;

bool VideoDecoder::hardware() const noexcept
{
    return m_state->hardware;
}

Expected<void, VideoDecodeError> VideoDecoder::push(const protocol::MediaPacket& packet, FrameSink& sink)
{
    GS_ASSERT_ON_THREAD(m_thread);
    State& state = *m_state;
    switch (packet.header.kind) {
    case protocol::MediaKind::Session: {
        const auto* info = std::get_if<protocol::VideoSessionInfo>(&packet.session);
        if (info == nullptr) {
            return makeUnexpected(outOfOrder("an audio session description on the video channel"));
        }
        state.close();
        state.described = *info;
        state.session = packet.header.session;
        state.configuration.clear();
        return {};
    }
    case protocol::MediaKind::Config:
        if (!state.described || packet.header.session != state.session) {
            return makeUnexpected(outOfOrder("a codec configuration before its session description"));
        }
        state.configuration.assign(packet.payload.begin(), packet.payload.end());
        return state.open(state.hardwareDevice != nullptr);
    case protocol::MediaKind::Frame: {
        if (state.context == nullptr || packet.header.session != state.session) {
            return makeUnexpected(outOfOrder("a frame before its codec configuration"));
        }
        const std::int64_t receivedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch())
                                            .count();
        auto decoded = state.decode(packet, sink, receivedUs);
        if (!decoded && state.hardware && packet.header.keyFrame) {
            if (auto reopened = state.open(false); !reopened) {
                return reopened;
            }
            return state.decode(packet, sink, receivedUs);
        }
        return decoded;
    }
    }
    return makeUnexpected(outOfOrder("an unknown media packet kind"));
}

}
