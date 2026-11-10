#include "gs/record/RecordedAudio.h"

#include "gs/record/MediaFile.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace gs::record {

namespace {

struct ContextDeleter {
    void operator()(AVCodecContext* context) const noexcept { avcodec_free_context(&context); }
};
using ContextPtr = std::unique_ptr<AVCodecContext, ContextDeleter>;

struct PacketDeleter {
    void operator()(AVPacket* packet) const noexcept { av_packet_free(&packet); }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};

std::string failure(std::string_view what, int code)
{
    char reason[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, reason, sizeof(reason));
    return std::format("{}: {}", what, reason);
}

ContextPtr openDecoder(bool aac, std::span<const std::uint8_t> configuration, std::string& problem)
{
    const AVCodec* codec = avcodec_find_decoder_by_name(aac ? "aac" : "opus");
    if (codec == nullptr) {
        problem = aac ? "this build has no AAC decoder" : "this build has no Opus decoder";
        return nullptr;
    }
    ContextPtr context(avcodec_alloc_context3(codec));
    if (context == nullptr) {
        problem = "the decoder cannot be made";
        return nullptr;
    }
    context->pkt_timebase = AVRational{1, 1'000'000};
    context->extradata = static_cast<std::uint8_t*>(av_mallocz(configuration.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (context->extradata == nullptr) {
        problem = "the codec configuration cannot be kept";
        return nullptr;
    }
    std::memcpy(context->extradata, configuration.data(), configuration.size());
    context->extradata_size = static_cast<int>(configuration.size());
    if (const int opened = avcodec_open2(context.get(), codec, nullptr); opened < 0) {
        problem = failure("opening the decoder", opened);
        return nullptr;
    }
    return context;
}

void collect(const AVFrame& frame, RecordedSound& sound, std::size_t mostFrames)
{
    const int channels = frame.ch_layout.nb_channels;
    if (channels <= 0 || frame.nb_samples <= 0) {
        return;
    }
    sound.channels = channels;
    sound.sampleRate = frame.sample_rate;
    const std::size_t room = mostFrames * static_cast<std::size_t>(channels);
    for (int index = 0; index < frame.nb_samples && sound.samples.size() < room; ++index) {
        for (int channel = 0; channel < channels; ++channel) {
            float value = 0.0F;
            if (frame.format == AV_SAMPLE_FMT_FLTP) {
                value = reinterpret_cast<const float*>(frame.extended_data[channel])[index];
            } else if (frame.format == AV_SAMPLE_FMT_FLT) {
                value = reinterpret_cast<const float*>(frame.extended_data[0])[index * channels + channel];
            } else if (frame.format == AV_SAMPLE_FMT_S16P) {
                value = reinterpret_cast<const std::int16_t*>(frame.extended_data[channel])[index] / 32768.0F;
            } else if (frame.format == AV_SAMPLE_FMT_S16) {
                value = reinterpret_cast<const std::int16_t*>(frame.extended_data[0])[index * channels + channel] /
                        32768.0F;
            }
            sound.samples.push_back(value);
        }
    }
}

}

Expected<RecordedSound, std::string> decodeRecordedSound(const std::filesystem::path& file, std::size_t mostFrames)
{
    MediaReader reader(file, protocol::MediaChannel::Audio);
    if (!reader.opened()) {
        return makeUnexpected(std::string("the sound of the recording cannot be read"));
    }
    RecordedSound sound;
    std::optional<protocol::AudioSessionInfo> described;
    std::optional<std::uint16_t> session;
    ContextPtr decoder;
    const std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    const std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    if (packet == nullptr || frame == nullptr) {
        return makeUnexpected(std::string("out of memory"));
    }
    bool first = true;
    while (const std::optional<protocol::MediaPacket> next = reader.next()) {
        switch (next->header.kind) {
        case protocol::MediaKind::Session:
            if (const auto* info = std::get_if<protocol::AudioSessionInfo>(&next->session)) {
                described = *info;
                session = next->header.session;
                decoder.reset();
            }
            break;
        case protocol::MediaKind::Config: {
            if (!described || next->header.session != session) {
                break;
            }
            std::string problem;
            sound.aac = isRecordedAac(*described);
            decoder = openDecoder(sound.aac, next->payload, problem);
            if (decoder == nullptr) {
                return makeUnexpected(std::move(problem));
            }
            break;
        }
        case protocol::MediaKind::Frame: {
            if (decoder == nullptr || next->header.session != session) {
                break;
            }
            std::vector<std::uint8_t> payload(next->payload.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
            std::memcpy(payload.data(), next->payload.data(), next->payload.size());
            packet->data = payload.data();
            packet->size = static_cast<int>(next->payload.size());
            packet->pts = next->header.ptsUs;
            if (first) {
                sound.firstPtsUs = next->header.ptsUs;
                first = false;
            }
            const int sent = avcodec_send_packet(decoder.get(), packet.get());
            packet->data = nullptr;
            packet->size = 0;
            if (sent < 0) {
                return makeUnexpected(failure("decoding", sent));
            }
            ++sound.packets;
            for (;;) {
                const int received = avcodec_receive_frame(decoder.get(), frame.get());
                if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                    break;
                }
                if (received < 0) {
                    return makeUnexpected(failure("decoding", received));
                }
                collect(*frame, sound, mostFrames);
                av_frame_unref(frame.get());
            }
            break;
        }
        }
    }
    if (sound.packets == 0) {
        return makeUnexpected(std::string("the recording has no sound that can be decoded"));
    }
    return sound;
}

}
