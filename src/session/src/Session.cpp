#include "gs/session/Session.h"

#include "Handshake.h"
#include "KeyFrameGate.h"
#include "ReceiverStats.h"

#include "gs/base/Log.h"
#include "gs/base/ThreadChecker.h"
#include "gs/platform/Crypto.h"
#include "gs/platform/Thread.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace gs::session {

namespace {

constexpr std::string_view kLog = "session";
constexpr std::int64_t kKeyFrameRequestSpacingUs = 500'000;
constexpr std::int64_t kProbeSpacingUs = 100'000;

using SteadyClock = std::chrono::steady_clock;

std::int64_t nowMicros() noexcept
{
    return std::chrono::duration_cast<std::chrono::microseconds>(SteadyClock::now().time_since_epoch()).count();
}

SessionFailure failure(FailureKind kind, std::string detail)
{
    SessionFailure result;
    result.kind = kind;
    result.detail = std::move(detail);
    return result;
}

bool isFinal(const SessionFailure& failure) noexcept
{
    return failure.kind == FailureKind::HandshakeRejected || failure.kind == FailureKind::ProtocolError ||
           failure.kind == FailureKind::ServerEnded;
}

template <typename T>
T take(std::optional<T>& slot)
{
    T value = std::move(*slot);
    slot.reset();
    return value;
}

class ClockFilter
{
public:
    Heartbeat add(std::int64_t roundTripUs, std::int64_t offsetUs, std::int64_t receivedUs) noexcept
    {
        m_samples[m_next] = {roundTripUs, offsetUs, receivedUs};
        m_next = (m_next + 1) % m_samples.size();
        m_count = std::min(m_count + 1, m_samples.size());
        const Sample* best = nullptr;
        for (std::size_t i = 0; i < m_count; ++i) {
            const Sample& sample = m_samples[i];
            if (receivedUs - sample.receivedUs > kWindowUs) {
                continue;
            }
            if (best == nullptr || sample.roundTripUs < best->roundTripUs) {
                best = &sample;
            }
        }
        Heartbeat heartbeat;
        heartbeat.roundTrip = std::chrono::microseconds(roundTripUs);
        heartbeat.clockOffset = std::chrono::microseconds(best != nullptr ? best->offsetUs : offsetUs);
        return heartbeat;
    }

private:
    static constexpr std::int64_t kWindowUs = 16'000'000;

    struct Sample {
        std::int64_t roundTripUs = 0;
        std::int64_t offsetUs = 0;
        std::int64_t receivedUs = 0;
    };

    std::array<Sample, 64> m_samples{};
    std::size_t m_next = 0;
    std::size_t m_count = 0;
};

}

class Session::Impl final : private ServerHost::Events
{
public:
    Impl(std::unique_ptr<ServerHost> host, SessionOptions options, SessionListener& listener)
        : m_host(std::move(host)), m_options(std::move(options)), m_listener(listener)
    {
        platform::randomBytes(m_params.token);
        std::array<std::uint8_t, 8> name{};
        platform::randomBytes(name);
        m_id = platform::toHex(name);
        m_params.socketName = "gamescrcpy_" + m_id;
        m_params.linger = m_options.linger;
        m_params.logLevel = m_options.serverLogLevel;
        m_videoConfig = m_options.video;
        m_onSessionThread.detach();
    }

    ~Impl()
    {
        m_stop.request_stop();
        if (m_sessionThread.joinable()) {
            m_sessionThread.join();
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void start()
    {
        if (!m_sessionThread.joinable()) {
            m_sessionThread = std::jthread([this] { run(m_stop.get_token()); });
        }
    }

    void stop() noexcept { m_stop.request_stop(); }

    [[nodiscard]] SessionPhase phase() const noexcept { return m_phase.load(std::memory_order_acquire); }

    bool enqueue(std::span<const std::uint8_t> frame, bool input)
    {
        {
            const std::lock_guard lock(m_sendMutex);
            if (!m_sendable) {
                return false;
            }
            const std::int64_t now = nowMicros();
            if (m_outgoing.empty()) {
                m_outgoingSinceUs = now;
            }
            m_outgoing.insert(m_outgoing.end(), frame.begin(), frame.end());
            if (input && now - m_lastProbeUs >= kProbeSpacingUs) {
                protocol::Ping ping;
                ping.nonce = m_pingNonce.fetch_add(1, std::memory_order_relaxed) + 1;
                ping.clientTimeUs = static_cast<std::uint64_t>(now);
                std::array<std::uint8_t, 32> buffer;
                if (const auto written = protocol::encodeFrameInto(ping, buffer)) {
                    m_outgoing.insert(m_outgoing.end(), buffer.begin(),
                                      buffer.begin() + static_cast<std::ptrdiff_t>(*written));
                    m_lastProbeUs = now;
                }
            }
        }
        m_sendReady.notify_one();
        return true;
    }

    void rememberVideo(const protocol::ConfigureVideo& message)
    {
        const std::lock_guard lock(m_videoMutex);
        m_videoConfig = message;
    }

    void rememberVideo(const protocol::SetVideoBitrate& message)
    {
        const std::lock_guard lock(m_videoMutex);
        if (m_videoConfig) {
            m_videoConfig->bitrate = message.bitrate;
        }
    }

    void rememberVideo(const protocol::SetMaxFps& message)
    {
        const std::lock_guard lock(m_videoMutex);
        if (m_videoConfig) {
            m_videoConfig->maxFps = message.maxFps;
        }
    }

    void rememberVideo(const protocol::SetVideoPaused& message)
    {
        const std::lock_guard lock(m_videoMutex);
        m_videoPaused = message.paused;
    }

private:
    struct Interruption {
        SessionFailure failure;
        bool fatal = false;
    };

    void hostPhase(SessionPhase phase) override { setPhase(phase); }
    void serverEnded(SessionFailure ended) override { reportFatal(std::move(ended)); }

    void run(std::stop_token stop)
    {
        platform::setCurrentThreadName("gs-session");
        GS_ASSERT_ON_THREAD(m_onSessionThread);
        std::optional<SessionFailure> ended = establish(stop);
        while (!ended && !stop.stop_requested()) {
            std::optional<Interruption> interruption = stream(stop);
            if (!interruption) {
                break;
            }
            if (interruption->fatal) {
                ended = std::move(interruption->failure);
            } else {
                ended = resume(stop, std::move(interruption->failure));
            }
        }
        shutDown(stop, std::move(ended));
    }

    std::optional<SessionFailure> establish(const std::stop_token& stop)
    {
        GS_ASSERT_ON_THREAD(m_onSessionThread);
        if (stop.stop_requested()) {
            return std::nullopt;
        }
        if (auto started = m_host->start(m_params, *this, stop); !started) {
            return std::move(started.error());
        }
        if (stop.stop_requested()) {
            return std::nullopt;
        }
        m_transport = m_host->transport(m_options.handshakeTimeout);
        setPhase(SessionPhase::Handshaking);
        if (auto opened = openChannels(false, stop); !opened) {
            return std::move(opened.error());
        }
        setPhase(SessionPhase::Streaming);
        return std::nullopt;
    }

    std::optional<Interruption> stream(const std::stop_token& stop)
    {
        GS_ASSERT_ON_THREAD(m_onSessionThread);
        auto nextPing = SteadyClock::now();
        auto nextReport = nextPing + m_options.reportInterval;
        while (true) {
            {
                std::unique_lock lock(m_eventMutex);
                m_wake.wait_until(lock, stop, std::min(nextPing, nextReport),
                                  [this] { return m_fatal.has_value() || m_lost.has_value(); });
                if (stop.stop_requested()) {
                    return std::nullopt;
                }
                if (m_fatal) {
                    return Interruption{take(m_fatal), true};
                }
                if (m_lost) {
                    return Interruption{take(m_lost), false};
                }
            }
            const auto now = SteadyClock::now();
            if (now >= nextReport) {
                if (m_video) {
                    if (const auto report = m_receiverStats.take(nowMicros())) {
                        send(*report);
                    }
                }
                nextReport = now + m_options.reportInterval;
            }
            if (now < nextPing) {
                continue;
            }
            const auto silence = std::chrono::microseconds(nowMicros() - m_lastPongUs.load(std::memory_order_acquire));
            if (silence > m_options.pongTimeout) {
                return Interruption{
                    failure(FailureKind::ConnectionLost,
                            std::format("no Pong for {} ms",
                                        std::chrono::duration_cast<std::chrono::milliseconds>(silence).count())),
                    false};
            }
            protocol::Ping ping;
            ping.nonce = m_pingNonce.fetch_add(1, std::memory_order_relaxed) + 1;
            ping.clientTimeUs = static_cast<std::uint64_t>(nowMicros());
            send(ping);
            nextPing = now + m_options.pingInterval;
        }
    }

    std::optional<SessionFailure> resume(const std::stop_token& stop, SessionFailure lost)
    {
        GS_ASSERT_ON_THREAD(m_onSessionThread);
        closeChannels();
        setPhase(SessionPhase::Reconnecting, lost);
        const auto deadline = SteadyClock::now() + m_options.linger;
        SessionFailure last = std::move(lost);
        bool afterFailure = false;
        while (!stop.stop_requested()) {
            if (std::optional<SessionFailure> ended = takeFatal()) {
                return ended;
            }
            if (SteadyClock::now() >= deadline) {
                break;
            }
            Expected<void, SessionFailure> attempt = m_host->restore(afterFailure, deadline, stop);
            if (attempt) {
                attempt = openChannels(true, stop);
            }
            if (attempt) {
                setPhase(SessionPhase::Streaming);
                return std::nullopt;
            }
            last = std::move(attempt.error());
            if (isFinal(last)) {
                return last;
            }
            afterFailure = true;
        }
        if (stop.stop_requested()) {
            return std::nullopt;
        }
        return failure(FailureKind::ConnectionLost,
                       std::format("not resumed within {} ms; last attempt: {}", m_options.linger.count(), last.detail));
    }

    void shutDown(const std::stop_token& stop, std::optional<SessionFailure> ended)
    {
        GS_ASSERT_ON_THREAD(m_onSessionThread);
        const bool requested = stop.stop_requested();
        if (requested) {
            setPhase(SessionPhase::Stopping);
        }
        protocol::Goodbye goodbye;
        if (!requested) {
            goodbye.reason = ended && ended->kind == FailureKind::ProtocolError ? protocol::ErrorCode::MalformedFrame
                                                                                 : protocol::ErrorCode::InternalError;
        }
        if (send(goodbye)) {
            const auto deadline = SteadyClock::now() + m_options.stopGrace;
            flush(deadline);
            finishControl(deadline);
        }
        closeChannels();
        m_host->stop(m_options.stopGrace);
        if (requested || !ended) {
            setPhase(SessionPhase::Stopped);
        } else {
            setPhase(SessionPhase::Failed, std::move(ended));
        }
    }

    Expected<void, SessionFailure> openChannels(bool resuming, const std::stop_token& stop)
    {
        GS_ASSERT_ON_THREAD(m_onSessionThread);
        std::optional<protocol::ConfigureVideo> videoConfig;
        std::optional<bool> videoPaused;
        {
            const std::lock_guard lock(m_videoMutex);
            videoConfig = m_videoConfig;
            videoPaused = m_videoPaused;
        }
        auto link = m_transport->establish(m_params.token, m_options.clientVersion, stop);
        if (!link) {
            return makeUnexpected(std::move(link.error()));
        }
        const protocol::Welcome& welcome = (*link)->welcome();

        std::unique_ptr<MediaChannel> video;
        if (videoConfig) {
            auto opened = (*link)->openMedia(protocol::MediaChannel::Video, stop);
            if (!opened) {
                return makeUnexpected(std::move(opened.error()));
            }
            video = std::move(*opened);
        }
        std::unique_ptr<MediaChannel> audio;
        if (m_options.audio && welcome.audioStatus == protocol::AudioStatus::Available) {
            audio = openAudio(**link, stop);
        }
        std::unique_ptr<FrameChannel> bulk;
        if (m_options.bulk) {
            bulk = openBulk(**link, stop);
        }
        if (resuming && !welcome.resumed) {
            log::warning(kLog, "[{}] the server started over instead of resuming", m_id);
        }

        (*link)->control().setSendTimeout(m_options.pongTimeout);
        m_listener.onTransport((*link)->kind());
        m_listener.onWelcome(welcome);

        m_link = std::move(*link);
        m_video = std::move(video);
        m_audio = std::move(audio);
        m_bulk = std::move(bulk);
        m_lastPongUs.store(nowMicros(), std::memory_order_release);
        {
            const std::lock_guard lock(m_sendMutex);
            m_outgoing.clear();
            m_sendable = true;
            m_writerRunning = true;
        }
        {
            const std::lock_guard lock(m_eventMutex);
            m_controlReaderDone = false;
        }
        m_controlReader = std::jthread([this, &channel = m_link->control()] { readControl(channel); });
        m_controlWriter = std::jthread(
            [this, &channel = m_link->control()](std::stop_token token) { writeControl(token, channel); });
        if (m_video) {
            m_receiverStats.reset();
            m_videoReader = std::jthread(
                [this, &channel = *m_video] { readMedia(channel, protocol::MediaChannel::Video); });
        }
        if (m_audio) {
            m_audioReader = std::jthread(
                [this, &channel = *m_audio] { readMedia(channel, protocol::MediaChannel::Audio); });
        }
        if (m_bulk) {
            m_bulkReader = std::jthread([this, &channel = *m_bulk] { readBulk(channel); });
        }
        if (videoConfig) {
            send(*videoConfig);
        }
        if (m_audio) {
            send(*m_options.audio);
        }
        if (videoPaused) {
            protocol::SetVideoPaused pause;
            pause.paused = *videoPaused;
            send(pause);
        }
        return {};
    }

    std::unique_ptr<MediaChannel> openAudio(Link& link, const std::stop_token& stop)
    {
        auto opened = link.openMedia(protocol::MediaChannel::Audio, stop);
        if (!opened) {
            log::warning(kLog, "[{}] no audio: {}", m_id, opened.error().detail);
            return nullptr;
        }
        return std::move(*opened);
    }

    std::unique_ptr<FrameChannel> openBulk(Link& link, const std::stop_token& stop)
    {
        auto opened = link.openBulk(stop);
        if (!opened) {
            log::warning(kLog, "[{}] no bulk channel: {}", m_id, opened.error().detail);
            return nullptr;
        }
        return std::move(*opened);
    }

    void closeChannels()
    {
        GS_ASSERT_ON_THREAD(m_onSessionThread);
        m_closing.store(true, std::memory_order_release);
        {
            const std::lock_guard lock(m_sendMutex);
            m_sendable = false;
            m_outgoing.clear();
        }
        if (m_link) {
            m_link->control().interrupt();
        }
        if (m_video) {
            m_video->interrupt();
        }
        if (m_audio) {
            m_audio->interrupt();
        }
        if (m_bulk) {
            m_bulk->interrupt();
        }
        m_controlWriter = {};
        m_controlReader = {};
        m_videoReader = {};
        m_audioReader = {};
        m_bulkReader = {};
        m_video.reset();
        m_audio.reset();
        m_bulk.reset();
        m_link.reset();
        {
            const std::lock_guard lock(m_eventMutex);
            m_lost.reset();
        }
        m_closing.store(false, std::memory_order_release);
    }

    void flush(SteadyClock::time_point deadline)
    {
        std::unique_lock lock(m_sendMutex);
        m_sendDrained.wait_until(lock, deadline,
                                 [this] { return !m_writerRunning || (m_outgoing.empty() && !m_writing); });
    }

    void finishControl(SteadyClock::time_point deadline)
    {
        GS_ASSERT_ON_THREAD(m_onSessionThread);
        if (!m_link) {
            return;
        }
        m_closing.store(true, std::memory_order_release);
        std::unique_lock lock(m_eventMutex);
        if (!m_controlReaderDone) {
            m_wake.wait_until(lock, deadline, [this] { return m_controlReaderDone; });
            return;
        }
        lock.unlock();
        m_link->control().drain(deadline);
    }

    template <typename M>
    bool send(const M& message)
    {
        std::array<std::uint8_t, 64> buffer;
        if (const auto written = protocol::encodeFrameInto(message, buffer)) {
            return enqueue(std::span<const std::uint8_t>(buffer).first(*written), false);
        }
        const auto frame = protocol::encodeFrame(message);
        return frame && enqueue(*frame, false);
    }

    void setPhase(SessionPhase phase, std::optional<SessionFailure> reason = std::nullopt)
    {
        GS_ASSERT_ON_THREAD(m_onSessionThread);
        const SessionPhase previous = m_phase.exchange(phase, std::memory_order_acq_rel);
        if (reason) {
            if (reason->phase == SessionPhase::Idle) {
                reason->phase = previous;
            }
            log::warning(kLog, "[{}] {}: {} while {} ({})", m_id, phaseName(phase), failureName(reason->kind),
                         phaseName(reason->phase), reason->detail);
        } else {
            log::info(kLog, "[{}] {}", m_id, phaseName(phase));
        }
        SessionState state;
        state.phase = phase;
        state.failure = std::move(reason);
        m_listener.onStateChanged(state);
    }

    std::optional<SessionFailure> takeFatal()
    {
        const std::lock_guard lock(m_eventMutex);
        if (m_fatal) {
            return take(m_fatal);
        }
        return std::nullopt;
    }

    void readControl(FrameChannel& channel)
    {
        platform::setCurrentThreadName("gs-control-in");
        readControlMessages(channel);
        {
            const std::lock_guard lock(m_eventMutex);
            m_controlReaderDone = true;
        }
        m_wake.notify_all();
    }

    void readControlMessages(FrameChannel& channel)
    {
        while (true) {
            const auto frame = channel.receive();
            if (!frame) {
                readerFailed(frame.error(), "control");
                return;
            }
            auto message = protocol::decodeToClientFrame(protocol::FrameContext::Control, *frame);
            if (!message) {
                readerFailed({ChannelError::Malformed, message.error()}, "control");
                return;
            }
            if (const auto* pong = std::get_if<protocol::Pong>(&*message)) {
                onPong(*pong);
            } else if (const auto* goodbye = std::get_if<protocol::ServerGoodbye>(&*message)) {
                SessionFailure ended = failure(FailureKind::ServerEnded,
                                               std::format("the server said goodbye ({}): {}",
                                                           protocol::enumName(goodbye->reason), goodbye->message));
                ended.code = goodbye->reason;
                reportFatal(std::move(ended));
                return;
            } else {
                m_listener.onMessage(*message);
            }
        }
    }

    void readMedia(MediaChannel& source, protocol::MediaChannel channel)
    {
        const bool video = channel == protocol::MediaChannel::Video;
        const char* const name = video ? "video" : "audio";
        platform::setCurrentThreadName(video ? "gs-video-in" : "gs-audio-in");
        platform::setCurrentThreadRole(platform::ThreadRole::Media);
        video ? m_listener.onVideoOpened() : m_listener.onAudioOpened();
        struct Closed {
            SessionListener& listener;
            bool video;
            ~Closed() { video ? listener.onVideoClosed() : listener.onAudioClosed(); }
        } closed{m_listener, video};
        detail::KeyFrameGate gate(kKeyFrameRequestSpacingUs);
        while (true) {
            const auto received = source.receive();
            if (!received) {
                readerFailed(received.error(), name);
                return;
            }
            const std::int64_t arrivalUs = nowMicros();
            const auto packet = protocol::decodeMediaPacket(channel, received->bytes);
            if (!packet) {
                readerFailed({ChannelError::Malformed, packet.error()}, name);
                return;
            }
            if (!video) {
                m_listener.onAudioPacket(*packet);
                continue;
            }
            if (const auto loss = source.takeLossCounts(); loss.lost != 0 || loss.rebuilt != 0) {
                m_receiverStats.addLoss(loss.lost, loss.rebuilt);
            }
            if (const auto report = m_receiverStats.add(packet->header, received->bytes.size(), arrivalUs)) {
                send(*report);
            }
            const auto decision = gate.admit(packet->header, received->lostBefore, arrivalUs);
            if (decision.askKeyFrame) {
                log::debug(kLog, "[{}] {} video packets lost; asking for a key frame", m_id, received->lostBefore);
                send(protocol::RequestKeyFrame{});
            }
            if (decision.deliver) {
                m_listener.onVideoPacket(*packet);
            }
        }
    }

    void readBulk(FrameChannel& channel)
    {
        platform::setCurrentThreadName("gs-bulk-in");
        m_listener.onBulkOpened();
        struct Closed {
            SessionListener& listener;
            ~Closed() { listener.onBulkClosed(); }
        } closed{m_listener};
        while (true) {
            const auto frame = channel.receive();
            if (!frame) {
                if (frame.error().error == ChannelError::Malformed) {
                    log::warning(kLog, "[{}] malformed data on the bulk channel: {}", m_id,
                                 protocol::errorName(frame.error().decode));
                }
                return;
            }
            const auto message = protocol::decodeToClientFrame(protocol::FrameContext::Bulk, *frame);
            if (!message) {
                log::warning(kLog, "[{}] malformed data on the bulk channel: {}", m_id,
                             protocol::errorName(message.error()));
                return;
            }
            m_listener.onMessage(*message);
        }
    }

    void writeControl(const std::stop_token& stop, FrameChannel& channel)
    {
        platform::setCurrentThreadName("gs-control-out");
        platform::setCurrentThreadRole(platform::ThreadRole::Input);
        std::vector<std::uint8_t> batch;
        while (true) {
            std::int64_t sinceUs = 0;
            {
                std::unique_lock lock(m_sendMutex);
                m_writing = false;
                m_sendDrained.notify_all();
                if (!m_sendReady.wait(lock, stop, [this] { return !m_outgoing.empty(); })) {
                    break;
                }
                batch.swap(m_outgoing);
                sinceUs = m_outgoingSinceUs;
                m_writing = true;
            }
            const bool written = channel.send(batch);
            batch.clear();
            if (!written) {
                if (!m_closing.load(std::memory_order_acquire)) {
                    reportLost(failure(FailureKind::ConnectionLost, "the control connection broke while sending"));
                }
                break;
            }
            recordSendDelay(nowMicros() - sinceUs);
        }
        const std::lock_guard lock(m_sendMutex);
        m_writing = false;
        m_writerRunning = false;
        m_sendable = false;
        m_sendDrained.notify_all();
    }

    void onPong(const protocol::Pong& pong)
    {
        const std::int64_t now = nowMicros();
        m_lastPongUs.store(now, std::memory_order_release);
        const auto sent = static_cast<std::int64_t>(pong.clientTimeUs);
        if (sent <= 0 || sent > now) {
            return;
        }
        const std::int64_t roundTrip = now - sent;
        m_receiverStats.setRoundTrip(roundTrip);
        const std::int64_t offset = static_cast<std::int64_t>(pong.serverTimeUs) - (sent + roundTrip / 2);
        Heartbeat heartbeat = m_clock.add(roundTrip, offset, now);
        heartbeat.sendDelay = std::chrono::microseconds(m_sendDelayMaxUs.exchange(0, std::memory_order_relaxed));
        heartbeat.sentUs = sent;
        heartbeat.serverTimeUs = static_cast<std::int64_t>(pong.serverTimeUs);
        m_listener.onHeartbeat(heartbeat);
    }

    void recordSendDelay(std::int64_t delayUs) noexcept
    {
        std::int64_t longest = m_sendDelayMaxUs.load(std::memory_order_relaxed);
        while (delayUs > longest &&
               !m_sendDelayMaxUs.compare_exchange_weak(longest, delayUs, std::memory_order_relaxed)) {
        }
    }

    void readerFailed(const ChannelFailure& failed, std::string_view channel)
    {
        if (m_closing.load(std::memory_order_acquire)) {
            return;
        }
        if (failed.error == ChannelError::Malformed) {
            reportFatal(failure(FailureKind::ProtocolError, std::format("malformed data on the {} channel: {}", channel,
                                                                        protocol::errorName(failed.decode))));
        } else {
            reportLost(failure(FailureKind::ConnectionLost, std::format("the {} connection closed", channel)));
        }
    }

    void reportLost(SessionFailure lost)
    {
        {
            const std::lock_guard lock(m_eventMutex);
            if (!m_lost) {
                m_lost = std::move(lost);
            }
        }
        m_wake.notify_all();
    }

    void reportFatal(SessionFailure fatal)
    {
        {
            const std::lock_guard lock(m_eventMutex);
            if (!m_fatal) {
                m_fatal = std::move(fatal);
            }
        }
        m_wake.notify_all();
    }

    std::unique_ptr<ServerHost> m_host;
    const SessionOptions m_options;
    SessionListener& m_listener;
    LaunchParams m_params;
    std::string m_id;
    std::atomic<SessionPhase> m_phase{SessionPhase::Idle};
    std::stop_source m_stop;
    ThreadChecker m_onSessionThread;

    std::unique_ptr<Transport> m_transport;
    std::unique_ptr<Link> m_link;
    std::unique_ptr<MediaChannel> m_video;
    std::unique_ptr<MediaChannel> m_audio;
    std::unique_ptr<FrameChannel> m_bulk;
    std::jthread m_controlReader;
    std::jthread m_controlWriter;
    std::jthread m_videoReader;
    std::jthread m_audioReader;
    std::jthread m_bulkReader;
    std::atomic<std::uint32_t> m_pingNonce{0};
    std::atomic<bool> m_closing{false};

    std::mutex m_sendMutex;
    std::condition_variable_any m_sendReady;
    std::condition_variable_any m_sendDrained;
    std::vector<std::uint8_t> m_outgoing;
    std::int64_t m_outgoingSinceUs = 0;
    std::int64_t m_lastProbeUs = 0;
    bool m_sendable = false;
    bool m_writing = false;
    bool m_writerRunning = false;
    std::atomic<std::int64_t> m_sendDelayMaxUs{0};

    ClockFilter m_clock;
    std::atomic<std::int64_t> m_lastPongUs{0};

    detail::ReceiverStats m_receiverStats;

    std::mutex m_videoMutex;
    std::optional<protocol::ConfigureVideo> m_videoConfig;
    std::optional<bool> m_videoPaused;

    std::mutex m_eventMutex;
    std::condition_variable_any m_wake;
    std::optional<SessionFailure> m_lost;
    std::optional<SessionFailure> m_fatal;
    bool m_controlReaderDone = true;

    std::jthread m_sessionThread;
};

Session::Session(std::unique_ptr<ServerHost> host, SessionOptions options, SessionListener& listener)
    : m_impl(std::make_unique<Impl>(std::move(host), std::move(options), listener))
{
}

Session::~Session() = default;

void Session::start()
{
    m_impl->start();
}

void Session::stop() noexcept
{
    m_impl->stop();
}

SessionPhase Session::phase() const noexcept
{
    return m_impl->phase();
}

bool Session::sendFrame(std::span<const std::uint8_t> frame, bool input)
{
    return m_impl->enqueue(frame, input);
}

void Session::rememberVideo(const protocol::ConfigureVideo& message)
{
    m_impl->rememberVideo(message);
}

void Session::rememberVideo(const protocol::SetVideoBitrate& message)
{
    m_impl->rememberVideo(message);
}

void Session::rememberVideo(const protocol::SetMaxFps& message)
{
    m_impl->rememberVideo(message);
}

void Session::rememberVideo(const protocol::SetVideoPaused& message)
{
    m_impl->rememberVideo(message);
}

}
