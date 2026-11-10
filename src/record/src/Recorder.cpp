#include "gs/record/Recorder.h"

#include "gs/record/MediaFile.h"
#include "gs/record/Recording.h"

#include <gs/platform/AtomicFile.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <optional>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace gs::record {

namespace {

using Steady = std::chrono::steady_clock;

constexpr int kRewriteCostFactor = 20;

std::filesystem::path pathOf(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

struct Pending {
    protocol::MediaChannel channel = protocol::MediaChannel::Video;
    std::vector<std::uint8_t> bytes;
};

struct Track {
    std::optional<std::uint16_t> session;
    bool configured = false;
    bool keyed = false;
    bool written = false;
};

}

struct Recorder::State {
    RecorderOptions options;
    std::shared_ptr<const Clock> clock;
    Microseconds start{0};
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Pending> queue;
    std::size_t queued = 0;
    bool stopping = false;
    std::optional<Microseconds> endedAt;
    std::string problem;
    Track video;
    Track audio;
    std::optional<std::int64_t> videoStartPtsUs;
    std::vector<script::SessionEvent> events;
    std::vector<script::SessionOperation> operations;
    int width = 0;
    int height = 0;
    std::size_t videoSessions = 0;
    std::jthread writer;

    [[nodiscard]] Microseconds now() const { return clock->now() - start; }

    void fail(std::string reason)
    {
        if (problem.empty()) {
            problem = std::move(reason);
        }
        queue.clear();
        queued = 0;
    }

    void enqueue(protocol::MediaChannel channel, const protocol::MediaPacket& packet, Track& track)
    {
        std::vector<std::uint8_t> bytes = packetBytes(packet);
        if (queued + bytes.size() > kMostQueuedBytes) {
            fail("the disk does not keep up with the recording");
            return;
        }
        queued += bytes.size();
        queue.push_back(Pending{.channel = channel, .bytes = std::move(bytes)});
        track.written = true;
        wake.notify_one();
    }

    void header(script::Session& session, Microseconds duration) const
    {
        session.device = options.device;
        session.startedAtMs = options.startedAtMs;
        session.frameRate = options.frameRate;
        session.video = video.written ? std::string(kVideoFile) : std::string();
        session.audio = audio.written ? std::string(kAudioFile) : std::string();
        session.videoStartPtsUs = videoStartPtsUs.value_or(0);
        session.durationMs = duration.count() / 1000;
    }

    void write()
    {
        std::array<std::ofstream, 2> files;
        const std::array<std::string_view, 2> names{kVideoFile, kAudioFile};
        script::Session draft;
        Steady::duration interval = options.rewriteEvery;
        Steady::time_point due = Steady::now() + interval;
        for (;;) {
            std::deque<Pending> batch;
            bool finishing = false;
            {
                std::unique_lock lock(mutex);
                wake.wait_until(lock, due, [this] { return !queue.empty() || stopping; });
                batch.swap(queue);
                queued = 0;
                finishing = stopping;
            }
            bool broken = false;
            for (const Pending& pending : batch) {
                const auto slot = static_cast<std::size_t>(pending.channel == protocol::MediaChannel::Video ? 0 : 1);
                std::ofstream& file = files[slot];
                if (!file.is_open()) {
                    file.open(options.folder / pathOf(names[slot]), std::ios::binary | std::ios::app);
                }
                file.write(reinterpret_cast<const char*>(pending.bytes.data()),
                           static_cast<std::streamsize>(pending.bytes.size()));
                if (!file) {
                    broken = true;
                    break;
                }
            }
            for (std::ofstream& file : files) {
                if (file.is_open()) {
                    file.flush();
                    broken = broken || !file;
                }
            }
            if (broken) {
                const std::scoped_lock lock(mutex);
                fail("the recording could not be written to the disk");
            }
            if (!finishing && Steady::now() < due) {
                continue;
            }
            {
                const std::scoped_lock lock(mutex);
                header(draft, endedAt.value_or(now()));
                draft.events.insert(draft.events.end(), events.begin() + static_cast<std::ptrdiff_t>(draft.events.size()),
                                    events.end());
                draft.operations.insert(draft.operations.end(),
                                        operations.begin() + static_cast<std::ptrdiff_t>(draft.operations.size()),
                                        operations.end());
            }
            const Steady::time_point began = Steady::now();
            const std::error_code error =
                platform::writeFileAtomically(options.folder / pathOf(kSessionFile), script::writeSession(draft));
            if (error) {
                const std::scoped_lock lock(mutex);
                fail("the recording's session could not be written to the disk");
            }
            interval = std::max<Steady::duration>(options.rewriteEvery, (Steady::now() - began) * kRewriteCostFactor);
            due = Steady::now() + interval;
            if (finishing) {
                return;
            }
        }
    }
};

Recorder::Recorder(std::unique_ptr<State> state) : m_state(std::move(state)) {}

Recorder::~Recorder()
{
    static_cast<void>(stop());
}

Expected<std::shared_ptr<Recorder>, std::string> Recorder::start(RecorderOptions options)
{
    std::error_code error;
    if (std::filesystem::exists(options.folder, error)) {
        return makeUnexpected(std::string("the recording folder exists already"));
    }
    std::filesystem::create_directories(options.folder, error);
    if (error) {
        return makeUnexpected(std::string("the recording folder cannot be made: ") + error.message());
    }
    auto state = std::make_unique<State>();
    state->clock = options.clock != nullptr ? options.clock : std::make_shared<SteadyClock>();
    state->options = std::move(options);
    state->start = state->clock->now();
    script::Session first;
    state->header(first, Microseconds{0});
    if (const std::error_code written = platform::writeFileAtomically(state->options.folder / pathOf(kSessionFile),
                                                                       script::writeSession(first))) {
        std::filesystem::remove_all(state->options.folder, error);
        return makeUnexpected(std::string("the recording's session cannot be written: ") + written.message());
    }
    std::shared_ptr<Recorder> recorder(new Recorder(std::move(state)));
    State* const running = recorder->m_state.get();
    running->writer = std::jthread([running] { running->write(); });
    return recorder;
}

void Recorder::video(const protocol::MediaPacket& packet)
{
    State& state = *m_state;
    const std::scoped_lock lock(state.mutex);
    if (state.stopping || !state.problem.empty()) {
        return;
    }
    Track& track = state.video;
    switch (packet.header.kind) {
    case protocol::MediaKind::Session:
        track.session = packet.header.session;
        track.configured = false;
        track.keyed = false;
        if (const auto* info = std::get_if<protocol::VideoSessionInfo>(&packet.session)) {
            ++state.videoSessions;
            if (state.width == 0) {
                state.width = info->frameWidth;
                state.height = info->frameHeight;
            }
        }
        state.enqueue(protocol::MediaChannel::Video, packet, track);
        return;
    case protocol::MediaKind::Config:
        if (!track.session || packet.header.session != *track.session) {
            return;
        }
        track.configured = true;
        state.enqueue(protocol::MediaChannel::Video, packet, track);
        return;
    case protocol::MediaKind::Frame:
        if (!track.configured || packet.header.session != *track.session || (!track.keyed && !packet.header.keyFrame)) {
            return;
        }
        track.keyed = true;
        if (!state.videoStartPtsUs) {
            state.videoStartPtsUs = packet.header.ptsUs - state.now().count();
        }
        state.enqueue(protocol::MediaChannel::Video, packet, track);
        return;
    }
}

void Recorder::audio(const protocol::MediaPacket& packet)
{
    State& state = *m_state;
    const std::scoped_lock lock(state.mutex);
    if (state.stopping || !state.problem.empty()) {
        return;
    }
    Track& track = state.audio;
    switch (packet.header.kind) {
    case protocol::MediaKind::Session:
        track.session = packet.header.session;
        track.configured = false;
        state.enqueue(protocol::MediaChannel::Audio, packet, track);
        return;
    case protocol::MediaKind::Config:
        if (!track.session || packet.header.session != *track.session) {
            return;
        }
        track.configured = true;
        state.enqueue(protocol::MediaChannel::Audio, packet, track);
        return;
    case protocol::MediaKind::Frame:
        if (!track.configured || packet.header.session != *track.session) {
            return;
        }
        state.enqueue(protocol::MediaChannel::Audio, packet, track);
        return;
    }
}

void Recorder::event(script::SessionEventKind kind, std::string id, bool value)
{
    State& state = *m_state;
    const std::scoped_lock lock(state.mutex);
    if (state.stopping || id.empty() || state.events.size() >= script::kMostSessionEvents) {
        return;
    }
    state.events.push_back(script::SessionEvent{.at = state.now(), .kind = kind, .id = std::move(id), .value = value});
}

void Recorder::operation(script::SessionAction action)
{
    State& state = *m_state;
    const std::scoped_lock lock(state.mutex);
    if (state.stopping || state.operations.size() >= script::kMostSessionOperations) {
        return;
    }
    state.operations.push_back(script::SessionOperation{.at = state.now(), .action = std::move(action)});
}

Microseconds Recorder::elapsed() const
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->endedAt.value_or(m_state->now());
}

std::size_t Recorder::operationCount() const
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->operations.size();
}

bool Recorder::hasVideo() const
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->video.keyed;
}

std::string Recorder::problem() const
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->problem;
}

const std::filesystem::path& Recorder::folder() const noexcept
{
    return m_state->options.folder;
}

RecorderSummary Recorder::stop()
{
    State& state = *m_state;
    {
        const std::scoped_lock lock(state.mutex);
        if (!state.stopping) {
            state.stopping = true;
            state.endedAt = state.now();
        }
    }
    state.wake.notify_all();
    if (state.writer.joinable()) {
        state.writer.join();
    }
    RecorderSummary summary;
    {
        const std::scoped_lock lock(state.mutex);
        summary.folder = state.options.folder;
        summary.durationMs = state.endedAt.value_or(Microseconds{0}).count() / 1000;
        summary.operations = state.operations.size();
        summary.events = state.events.size();
        summary.width = state.width;
        summary.height = state.height;
        summary.videoSessions = state.videoSessions;
        summary.video = state.video.keyed;
        summary.audio = state.audio.written;
        summary.problem = state.problem;
    }
    for (const std::string_view name : {kVideoFile, kAudioFile, kSessionFile}) {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(summary.folder / pathOf(name), error);
        summary.bytes += error ? 0 : size;
    }
    return summary;
}

}
