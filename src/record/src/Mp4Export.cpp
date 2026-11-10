#include "gs/record/Mp4Export.h"

#include "gs/record/MediaFile.h"
#include "gs/record/Recording.h"

#include <gs/platform/AtomicFile.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <algorithm>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace gs::record {

namespace {

constexpr AVRational kMicroseconds{1, 1'000'000};
constexpr AVRational kVideoTimeBase{1, 90'000};
constexpr std::int64_t kFallbackFrameUs = 33'333;
constexpr int kOpusFrameSamples = 960;
constexpr int kAacFrameSamples = 1024;

std::string utf8Of(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string failure(std::string_view what, int code)
{
    char reason[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, reason, sizeof(reason));
    return std::format("{}: {}", what, reason);
}

struct PacketDeleter {
    void operator()(AVPacket* packet) const noexcept { av_packet_free(&packet); }
};
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

struct Segment {
    protocol::VideoSessionInfo info;
    std::uint16_t session = 0;
    std::vector<std::uint8_t> config;
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    std::int64_t firstPts = 0;
    std::int64_t lastPts = 0;
    std::size_t frames = 0;

    [[nodiscard]] std::int64_t gapUs() const noexcept
    {
        return frames > 1 ? std::max<std::int64_t>((lastPts - firstPts) / static_cast<std::int64_t>(frames - 1), 1)
                          : kFallbackFrameUs;
    }
};

std::vector<Segment> segmentsOf(MediaReader& reader)
{
    std::vector<Segment> segments;
    std::optional<Segment> open;
    const auto close = [&](std::uint64_t at) {
        if (open && open->frames > 0 && !open->config.empty()) {
            open->end = at;
            segments.push_back(std::move(*open));
        }
        open.reset();
    };
    for (;;) {
        const std::uint64_t at = reader.position();
        const std::optional<protocol::MediaHeader> header = reader.peek();
        if (!header) {
            break;
        }
        if (header->kind == protocol::MediaKind::Frame) {
            if (open && !open->config.empty() && header->session == open->session) {
                open->firstPts = open->frames == 0 ? header->ptsUs : open->firstPts;
                open->lastPts = open->frames == 0 ? header->ptsUs : std::max(open->lastPts, header->ptsUs);
                ++open->frames;
            }
            reader.skip();
            continue;
        }
        const std::optional<protocol::MediaPacket> packet = reader.next();
        if (!packet) {
            break;
        }
        if (packet->header.kind == protocol::MediaKind::Session) {
            close(at);
            const auto* info = std::get_if<protocol::VideoSessionInfo>(&packet->session);
            if (info != nullptr) {
                open = Segment{.info = *info, .session = packet->header.session, .start = at};
            }
            continue;
        }
        if (!open || packet->header.session != open->session) {
            continue;
        }
        std::vector<std::uint8_t> config(packet->payload.begin(), packet->payload.end());
        if (open->config.empty() || open->frames == 0) {
            open->config = std::move(config);
        } else if (config != open->config) {
            const Segment next{.info = open->info, .session = open->session, .config = std::move(config), .start = at};
            close(at);
            open = next;
        }
    }
    close(reader.position());
    return segments;
}

class AudioCursor
{
public:
    explicit AudioCursor(const std::filesystem::path& file)
    {
        if (!file.empty()) {
            m_reader.emplace(file, protocol::MediaChannel::Audio);
        }
    }

    [[nodiscard]] bool reach(std::int64_t from, std::int64_t until)
    {
        if (!m_reader || !m_reader->opened()) {
            return false;
        }
        for (;;) {
            const std::optional<protocol::MediaHeader> header = m_reader->peek();
            if (!header) {
                return false;
            }
            if (header->kind != protocol::MediaKind::Frame) {
                take();
                continue;
            }
            if (!m_configured || header->session != m_session || header->ptsUs < from) {
                m_reader->skip();
                continue;
            }
            return header->ptsUs < until;
        }
    }

    [[nodiscard]] std::optional<protocol::MediaPacket> frameBefore(std::int64_t until)
    {
        if (!m_reader) {
            return std::nullopt;
        }
        for (;;) {
            const std::optional<protocol::MediaHeader> header = m_reader->peek();
            if (!header || header->kind != protocol::MediaKind::Frame || header->ptsUs >= until) {
                return std::nullopt;
            }
            if (header->session != m_session) {
                m_reader->skip();
                continue;
            }
            return m_reader->next();
        }
    }

    [[nodiscard]] const protocol::AudioSessionInfo& info() const noexcept { return m_info; }
    [[nodiscard]] const std::vector<std::uint8_t>& config() const noexcept { return m_config; }

private:
    void take()
    {
        const std::optional<protocol::MediaPacket> packet = m_reader->next();
        if (!packet) {
            return;
        }
        if (packet->header.kind == protocol::MediaKind::Session) {
            const auto* info = std::get_if<protocol::AudioSessionInfo>(&packet->session);
            m_info = info != nullptr ? *info : protocol::AudioSessionInfo{};
            m_session = packet->header.session;
            m_configured = false;
            m_config.clear();
        } else if (packet->header.session == m_session && m_info.sampleRate > 0 && m_info.channels > 0) {
            m_config.assign(packet->payload.begin(), packet->payload.end());
            m_configured = true;
        }
    }

    std::optional<MediaReader> m_reader;
    protocol::AudioSessionInfo m_info;
    std::uint16_t m_session = 0;
    std::vector<std::uint8_t> m_config;
    bool m_configured = false;
};

AVCodecID codecOf(protocol::VideoCodec codec)
{
    switch (codec) {
    case protocol::VideoCodec::H264:
        return AV_CODEC_ID_H264;
    case protocol::VideoCodec::H265:
        return AV_CODEC_ID_HEVC;
    case protocol::VideoCodec::Av1:
        return AV_CODEC_ID_AV1;
    }
    return AV_CODEC_ID_NONE;
}

bool attach(AVCodecParameters& parameters, const std::vector<std::uint8_t>& extra)
{
    parameters.extradata = static_cast<std::uint8_t*>(av_mallocz(extra.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (parameters.extradata == nullptr) {
        return false;
    }
    std::memcpy(parameters.extradata, extra.data(), extra.size());
    parameters.extradata_size = static_cast<int>(extra.size());
    return true;
}

class Muxer
{
public:
    Muxer() = default;
    Muxer(const Muxer&) = delete;
    Muxer& operator=(const Muxer&) = delete;

    ~Muxer()
    {
        if (m_context != nullptr) {
            if (m_context->pb != nullptr) {
                avio_closep(&m_context->pb);
            }
            avformat_free_context(m_context);
        }
    }

    [[nodiscard]] std::string open(const std::filesystem::path& file, const Segment& segment,
                                   const AudioCursor* audio)
    {
        const std::string name = utf8Of(file);
        if (const int made = avformat_alloc_output_context2(&m_context, nullptr, "mp4", name.c_str()); made < 0) {
            return failure("preparing the MP4", made);
        }
        AVStream* video = avformat_new_stream(m_context, nullptr);
        if (video == nullptr) {
            return failure("adding the picture", AVERROR(ENOMEM));
        }
        video->time_base = kVideoTimeBase;
        video->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        video->codecpar->codec_id = codecOf(segment.info.codec);
        video->codecpar->width = segment.info.frameWidth;
        video->codecpar->height = segment.info.frameHeight;
        if (!attach(*video->codecpar, segment.config)) {
            return failure("adding the picture", AVERROR(ENOMEM));
        }
        m_video = video->index;
        if (audio != nullptr) {
            AVStream* sound = avformat_new_stream(m_context, nullptr);
            if (sound == nullptr) {
                return failure("adding the sound", AVERROR(ENOMEM));
            }
            const bool aac = isRecordedAac(audio->info());
            sound->time_base = AVRational{1, static_cast<int>(audio->info().sampleRate)};
            sound->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            sound->codecpar->codec_id = aac ? AV_CODEC_ID_AAC : AV_CODEC_ID_OPUS;
            sound->codecpar->sample_rate = static_cast<int>(audio->info().sampleRate);
            sound->codecpar->frame_size = aac ? kAacFrameSamples : kOpusFrameSamples;
            av_channel_layout_default(&sound->codecpar->ch_layout, audio->info().channels);
            if (!attach(*sound->codecpar, audio->config())) {
                return failure("adding the sound", AVERROR(ENOMEM));
            }
            m_audio = sound->index;
        }
        if (const int opened = avio_open(&m_context->pb, name.c_str(), AVIO_FLAG_WRITE); opened < 0) {
            return failure("creating the file", opened);
        }
        if (const int written = avformat_write_header(m_context, nullptr); written < 0) {
            return failure("writing the MP4 header", written);
        }
        return {};
    }

    [[nodiscard]] std::string write(bool video, std::int64_t ptsUs, std::int64_t durationUs, bool key,
                                    std::span<const std::uint8_t> data)
    {
        const int index = video ? m_video : m_audio;
        AVStream* stream = m_context->streams[index];
        PacketPtr packet(av_packet_alloc());
        if (packet == nullptr || av_new_packet(packet.get(), static_cast<int>(data.size())) < 0) {
            return failure("copying a packet", AVERROR(ENOMEM));
        }
        std::memcpy(packet->data, data.data(), data.size());
        std::int64_t& last = video ? m_lastVideo : m_lastAudio;
        const std::int64_t at = std::max(av_rescale_q(ptsUs, kMicroseconds, stream->time_base), last + 1);
        last = at;
        packet->pts = at;
        packet->dts = at;
        packet->duration = std::max<std::int64_t>(av_rescale_q(durationUs, kMicroseconds, stream->time_base), 0);
        packet->stream_index = index;
        packet->flags = key ? AV_PKT_FLAG_KEY : 0;
        if (const int written = av_interleaved_write_frame(m_context, packet.get()); written < 0) {
            return failure("writing a packet", written);
        }
        return {};
    }

    [[nodiscard]] std::string finish()
    {
        if (const int written = av_write_trailer(m_context); written < 0) {
            return failure("finishing the MP4", written);
        }
        if (const int closed = avio_closep(&m_context->pb); closed < 0) {
            return failure("closing the file", closed);
        }
        return {};
    }

private:
    AVFormatContext* m_context = nullptr;
    int m_video = -1;
    int m_audio = -1;
    std::int64_t m_lastVideo = -1;
    std::int64_t m_lastAudio = -1;
};

struct Owned {
    std::int64_t ptsUs = 0;
    bool key = false;
    std::vector<std::uint8_t> data;
};

std::optional<Owned> nextFrame(MediaReader& reader, const Segment& segment)
{
    while (reader.position() < segment.end) {
        const std::optional<protocol::MediaPacket> packet = reader.next();
        if (!packet) {
            return std::nullopt;
        }
        if (packet->header.kind == protocol::MediaKind::Frame && packet->header.session == segment.session) {
            return Owned{.ptsUs = packet->header.ptsUs,
                         .key = packet->header.keyFrame,
                         .data = {packet->payload.begin(), packet->payload.end()}};
        }
    }
    return std::nullopt;
}

std::optional<Owned> nextSound(AudioCursor& audio, std::int64_t until)
{
    const std::optional<protocol::MediaPacket> packet = audio.frameBefore(until);
    if (!packet) {
        return std::nullopt;
    }
    return Owned{.ptsUs = packet->header.ptsUs, .data = {packet->payload.begin(), packet->payload.end()}};
}

Expected<ExportedVideo, std::string> exportSegment(const std::filesystem::path& file, const std::filesystem::path& video,
                                                   const Segment& segment, AudioCursor& audio, std::int64_t until)
{
    const bool sound = audio.reach(segment.firstPts, until);
    const std::filesystem::path part = file.parent_path() / (file.filename().wstring() + L".part");
    std::string problem;
    {
        Muxer muxer;
        problem = muxer.open(part, segment, sound ? &audio : nullptr);
        MediaReader frames(video, protocol::MediaChannel::Video);
        frames.seek(segment.start);
        std::optional<Owned> picture = problem.empty() ? nextFrame(frames, segment) : std::nullopt;
        std::optional<Owned> clip = problem.empty() && sound ? nextSound(audio, until) : std::nullopt;
        std::int64_t clipUs = 0;
        while (problem.empty() && (picture || clip)) {
            if (picture && (!clip || picture->ptsUs <= clip->ptsUs)) {
                std::optional<Owned> following = nextFrame(frames, segment);
                const std::int64_t lasts = following ? following->ptsUs - picture->ptsUs : segment.gapUs();
                problem = muxer.write(true, picture->ptsUs - segment.firstPts, lasts, picture->key, picture->data);
                picture = std::move(following);
                continue;
            }
            std::optional<Owned> following = sound ? nextSound(audio, until) : std::nullopt;
            clipUs = following ? following->ptsUs - clip->ptsUs : clipUs;
            problem = muxer.write(false, clip->ptsUs - segment.firstPts, clipUs, true, clip->data);
            clip = std::move(following);
        }
        if (problem.empty()) {
            problem = muxer.finish();
        }
    }
    if (!problem.empty()) {
        std::error_code ignored;
        std::filesystem::remove(part, ignored);
        return makeUnexpected(std::move(problem));
    }
    if (const std::error_code moved = platform::replaceFile(part, file)) {
        std::error_code ignored;
        std::filesystem::remove(part, ignored);
        return makeUnexpected("the video cannot be saved as " + utf8Of(file) + ": " + moved.message());
    }
    return ExportedVideo{.file = file,
                         .durationUs = segment.lastPts - segment.firstPts + segment.gapUs(),
                         .width = segment.info.frameWidth,
                         .height = segment.info.frameHeight,
                         .audio = sound};
}

}

Expected<std::vector<ExportedVideo>, std::string> exportMp4(const std::filesystem::path& recording)
{
    const auto opened = openRecording(recording);
    if (!opened) {
        return makeUnexpected(opened.error());
    }
    if (opened->video.empty()) {
        return makeUnexpected(std::string("the recording has no picture"));
    }
    MediaReader reader(opened->video, protocol::MediaChannel::Video);
    if (!reader.opened()) {
        return makeUnexpected("the picture of the recording cannot be read: " + utf8Of(opened->video));
    }
    const std::vector<Segment> segments = segmentsOf(reader);
    if (segments.empty()) {
        return makeUnexpected(std::string("the recording has no picture that can be shown"));
    }
    AudioCursor audio(opened->audio);
    std::vector<ExportedVideo> exported;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const Segment& segment = segments[index];
        const std::int64_t until =
            index + 1 < segments.size() ? segments[index + 1].firstPts : segment.lastPts + segment.gapUs();
        auto written = exportSegment(exportPath(opened->folder, index), opened->video, segment, audio, until);
        if (!written) {
            return makeUnexpected(written.error());
        }
        exported.push_back(std::move(*written));
    }
    return exported;
}

}
