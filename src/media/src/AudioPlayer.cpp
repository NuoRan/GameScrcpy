#include "gs/media/AudioPlayer.h"

#include "AudioOutput.h"

#include "gs/base/Log.h"
#include "gs/media/AudioRegulator.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace gs::media {

namespace {

constexpr std::string_view kLog = "audio";
constexpr std::int64_t kResamplerSlack = 256;

std::string ffmpegFailure(std::string_view what, int code)
{
    char reason[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, reason, sizeof(reason));
    return std::format("{}: {}", what, reason);
}

}

struct AudioPlayer::State {
    explicit State(std::chrono::milliseconds bufferTarget) : target(bufferTarget) {}

    ~State()
    {
        stopPlayback();
        closeDecoder();
        closePcm();
        av_packet_free(&packet);
        av_frame_free(&frame);
    }

    void closePcm() noexcept
    {
        swr_free(&pcmResampler);
        pcmChannels = 0;
        pcmRate = 0;
    }

    Expected<void, std::string> openPcm(int channels, int sampleRate)
    {
        closePcm();
        closeDecoder();
        const int shown = std::min(channels, 2);
        AVChannelLayout inputLayout{};
        AVChannelLayout outputLayout{};
        av_channel_layout_default(&inputLayout, channels);
        av_channel_layout_default(&outputLayout, shown);
        const int allocated = swr_alloc_set_opts2(&pcmResampler, &outputLayout, AV_SAMPLE_FMT_FLT, sampleRate,
                                                  &inputLayout, AV_SAMPLE_FMT_FLT, sampleRate, 0, nullptr);
        av_channel_layout_uninit(&inputLayout);
        av_channel_layout_uninit(&outputLayout);
        if (allocated < 0) {
            closePcm();
            return makeUnexpected(ffmpegFailure("setting up the resampler", allocated));
        }
        if (const int initialized = swr_init(pcmResampler); initialized < 0) {
            closePcm();
            return makeUnexpected(ffmpegFailure("setting up the resampler", initialized));
        }
        if (regulator == nullptr || shown != playbackChannels || sampleRate != playbackRate) {
            stopPlayback();
            const auto targetFrames = static_cast<std::uint32_t>(sampleRate * target.count() / 1000);
            const std::lock_guard guard(lock);
            regulator = std::make_unique<AudioRegulator>(shown, sampleRate, targetFrames);
            output = std::make_unique<AudioOutput>(*regulator, shown, sampleRate, gain);
            playbackChannels = shown;
            playbackRate = sampleRate;
        }
        pcmChannels = channels;
        pcmRate = sampleRate;
        log::info(kLog, "sound from a capture device: {} Hz, {} channels", sampleRate, channels);
        return {};
    }

    Expected<void, std::string> playPcm(std::span<const float> interleaved, std::int64_t ptsUs)
    {
        const int frames = static_cast<int>(interleaved.size() / static_cast<std::size_t>(pcmChannels));
        if (frames <= 0 || regulator == nullptr) {
            return {};
        }
        const int channels = playbackChannels;
        const auto capacity =
            static_cast<int>(swr_get_delay(pcmResampler, playbackRate) + frames + kResamplerSlack);
        resampled.resize(static_cast<std::size_t>(capacity) * static_cast<std::size_t>(channels));
        auto* out = reinterpret_cast<std::uint8_t*>(resampled.data());
        const auto* in = reinterpret_cast<const std::uint8_t*>(interleaved.data());
        const int converted = swr_convert(pcmResampler, &out, capacity, &in, frames);
        if (converted < 0) {
            return makeUnexpected(ffmpegFailure("resampling", converted));
        }
        const std::span<const float> samples(resampled.data(),
                                             static_cast<std::size_t>(converted) * static_cast<std::size_t>(channels));
        tell(samples, channels, ptsUs);
        if (const auto compensation = regulator->push(samples, static_cast<std::uint32_t>(frames), ptsUs)) {
            if (const int set = swr_set_compensation(pcmResampler, compensation->delta, compensation->distance);
                set < 0) {
                log::warning(kLog, "{}", ffmpegFailure("compensating the clock drift", set));
            }
        }
        return {};
    }

    Expected<void, std::string> open(std::span<const std::uint8_t> configuration)
    {
        closeDecoder();
        const AVCodec* codec = avcodec_find_decoder_by_name("opus");
        if (codec == nullptr) {
            return makeUnexpected(std::string("this build has no Opus decoder"));
        }
        decoder = avcodec_alloc_context3(codec);
        if (decoder == nullptr) {
            return makeUnexpected(ffmpegFailure("allocating the decoder", AVERROR(ENOMEM)));
        }
        decoder->pkt_timebase = AVRational{1, 1'000'000};
        decoder->extradata =
            static_cast<std::uint8_t*>(av_mallocz(configuration.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (decoder->extradata == nullptr) {
            closeDecoder();
            return makeUnexpected(ffmpegFailure("allocating the codec configuration", AVERROR(ENOMEM)));
        }
        std::memcpy(decoder->extradata, configuration.data(), configuration.size());
        decoder->extradata_size = static_cast<int>(configuration.size());
        if (const int opened = avcodec_open2(decoder, codec, nullptr); opened < 0) {
            closeDecoder();
            return makeUnexpected(ffmpegFailure("opening the decoder", opened));
        }
        if (const int allocated =
                swr_alloc_set_opts2(&resampler, &decoder->ch_layout, AV_SAMPLE_FMT_FLT, decoder->sample_rate,
                                    &decoder->ch_layout, decoder->sample_fmt, decoder->sample_rate, 0, nullptr);
            allocated < 0) {
            closeDecoder();
            return makeUnexpected(ffmpegFailure("setting up the resampler", allocated));
        }
        if (const int initialized = swr_init(resampler); initialized < 0) {
            closeDecoder();
            return makeUnexpected(ffmpegFailure("setting up the resampler", initialized));
        }
        const int channels = decoder->ch_layout.nb_channels;
        const int sampleRate = decoder->sample_rate;
        if (regulator == nullptr || channels != playbackChannels || sampleRate != playbackRate) {
            stopPlayback();
            const auto targetFrames = static_cast<std::uint32_t>(sampleRate * target.count() / 1000);
            const std::lock_guard guard(lock);
            regulator = std::make_unique<AudioRegulator>(channels, sampleRate, targetFrames);
            output = std::make_unique<AudioOutput>(*regulator, channels, sampleRate, gain);
            playbackChannels = channels;
            playbackRate = sampleRate;
        }
        log::info(kLog, "audio session {}: Opus, {} Hz, {} channels", session, sampleRate, channels);
        return {};
    }

    Expected<void, std::string> decode(const protocol::MediaPacket& input)
    {
        payload.assign(input.payload.begin(), input.payload.end());
        payload.resize(input.payload.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
        packet->data = payload.data();
        packet->size = static_cast<int>(input.payload.size());
        packet->pts = input.header.ptsUs;
        const int sent = avcodec_send_packet(decoder, packet);
        av_packet_unref(packet);
        if (sent < 0) {
            return makeUnexpected(ffmpegFailure("decoding", sent));
        }
        for (;;) {
            const int received = avcodec_receive_frame(decoder, frame);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                return {};
            }
            if (received < 0) {
                return makeUnexpected(ffmpegFailure("decoding", received));
            }
            const int channels = playbackChannels;
            const auto capacity = static_cast<int>(swr_get_delay(resampler, playbackRate) + frame->nb_samples +
                                                   kResamplerSlack);
            resampled.resize(static_cast<std::size_t>(capacity) * static_cast<std::size_t>(channels));
            auto* out = reinterpret_cast<std::uint8_t*>(resampled.data());
            const int converted = swr_convert(resampler, &out, capacity,
                                              const_cast<const std::uint8_t**>(frame->extended_data), frame->nb_samples);
            const std::int64_t pts = frame->pts != AV_NOPTS_VALUE ? frame->pts : input.header.ptsUs;
            const auto decoded = static_cast<std::uint32_t>(frame->nb_samples);
            av_frame_unref(frame);
            if (converted < 0) {
                return makeUnexpected(ffmpegFailure("resampling", converted));
            }
            const std::span<const float> samples(resampled.data(), static_cast<std::size_t>(converted) *
                                                                       static_cast<std::size_t>(channels));
            tell(samples, channels, pts);
            if (const auto compensation = regulator->push(samples, decoded, pts)) {
                if (const int set = swr_set_compensation(resampler, compensation->delta, compensation->distance);
                    set < 0) {
                    log::warning(kLog, "{}", ffmpegFailure("compensating the clock drift", set));
                }
            }
        }
    }

    void closeDecoder() noexcept
    {
        avcodec_free_context(&decoder);
        swr_free(&resampler);
    }

    void stopPlayback() noexcept
    {
        const std::lock_guard guard(lock);
        output.reset();
        regulator.reset();
        playbackChannels = 0;
        playbackRate = 0;
    }

    void updateGain() noexcept { gain.store(muted ? 0.0f : volume, std::memory_order_relaxed); }

    void tell(std::span<const float> samples, int channels, std::int64_t ptsUs)
    {
        const std::lock_guard guard(tapLock);
        if (tap && !samples.empty()) {
            tap(samples, channels, playbackRate, ptsUs);
        }
    }

    const std::chrono::milliseconds target;

    std::optional<protocol::AudioSessionInfo> described;
    std::uint16_t session = 0;
    AVCodecContext* decoder = nullptr;
    SwrContext* resampler = nullptr;
    SwrContext* pcmResampler = nullptr;
    int pcmChannels = 0;
    int pcmRate = 0;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    std::vector<std::uint8_t> payload;
    std::vector<float> resampled;

    mutable std::mutex lock;
    std::unique_ptr<AudioRegulator> regulator;
    std::unique_ptr<AudioOutput> output;
    int playbackChannels = 0;
    int playbackRate = 0;

    std::mutex volumeLock;
    float volume = 1.0f;
    bool muted = false;
    std::atomic<float> gain{1.0f};

    std::mutex tapLock;
    AudioTap tap;
};

AudioPlayer::AudioPlayer(std::chrono::milliseconds target) : m_state(std::make_unique<State>(target)) {}

AudioPlayer::~AudioPlayer() = default;

Expected<void, std::string> AudioPlayer::push(const protocol::MediaPacket& packet)
{
    State& state = *m_state;
    switch (packet.header.kind) {
    case protocol::MediaKind::Session: {
        const auto* info = std::get_if<protocol::AudioSessionInfo>(&packet.session);
        if (info == nullptr) {
            return makeUnexpected(std::string("a session description without audio parameters"));
        }
        if (info->codec != protocol::AudioCodec::Opus) {
            return makeUnexpected(std::format("unsupported audio codec {}", protocol::enumName(info->codec)));
        }
        state.closeDecoder();
        state.described = *info;
        state.session = packet.header.session;
        return {};
    }
    case protocol::MediaKind::Config:
        if (!state.described || packet.header.session != state.session) {
            return makeUnexpected(std::string("a codec configuration without its session description"));
        }
        return state.open(packet.payload);
    case protocol::MediaKind::Frame:
        if (state.decoder == nullptr || packet.header.session != state.session) {
            return makeUnexpected(std::string("audio before its codec configuration"));
        }
        return state.decode(packet);
    }
    return makeUnexpected(std::string("unknown audio packet"));
}

Expected<void, std::string> AudioPlayer::pushPcm(std::span<const float> interleaved, int channels, int sampleRate,
                                                 std::int64_t ptsUs)
{
    State& state = *m_state;
    if (channels <= 0 || sampleRate <= 0 || interleaved.size() < static_cast<std::size_t>(channels)) {
        return {};
    }
    if (state.pcmResampler == nullptr || channels != state.pcmChannels || sampleRate != state.pcmRate) {
        if (auto opened = state.openPcm(channels, sampleRate); !opened) {
            return opened;
        }
    }
    return state.playPcm(interleaved, ptsUs);
}

void AudioPlayer::close()
{
    if (const AudioStats played = stats(); played.sampleRate > 0) {
        log::info(kLog, "audio stopped: {:.1f} ms of silence for want of sound, {:.1f} ms dropped as excess",
                  static_cast<double>(played.underflowFrames) * 1000 / played.sampleRate,
                  static_cast<double>(played.droppedFrames) * 1000 / played.sampleRate);
    }
    m_state->stopPlayback();
    m_state->closeDecoder();
    m_state->closePcm();
    m_state->described.reset();
}

void AudioPlayer::setVolume(float volume) noexcept
{
    const std::lock_guard guard(m_state->volumeLock);
    m_state->volume = std::clamp(volume, 0.0f, 1.0f);
    m_state->updateGain();
}

void AudioPlayer::setMuted(bool muted) noexcept
{
    const std::lock_guard guard(m_state->volumeLock);
    m_state->muted = muted;
    m_state->updateGain();
}

void AudioPlayer::setTap(AudioTap tap)
{
    const std::lock_guard guard(m_state->tapLock);
    m_state->tap = std::move(tap);
}

AudioStats AudioPlayer::stats() const
{
    AudioStats stats;
    const std::lock_guard guard(m_state->lock);
    if (m_state->regulator != nullptr) {
        stats.bufferedFrames = m_state->regulator->buffered();
        stats.targetFrames = m_state->regulator->targetFrames();
        stats.underflowFrames = m_state->regulator->underflowFrames();
        stats.droppedFrames = m_state->regulator->droppedFrames();
        stats.sampleRate = m_state->playbackRate;
    }
    if (m_state->output != nullptr) {
        stats.periodFrames = m_state->output->periodFrames();
        stats.lowLatency = m_state->output->lowLatency();
    }
    return stats;
}

}
