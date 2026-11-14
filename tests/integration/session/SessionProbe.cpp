// Measures a whole session on a real phone the way the F4 overlay does: it deploys and starts
// the server, streams for N seconds, decodes the picture and draws it in a window of its own,
// and prints one line a second plus a summary - frames per second received, bit rate, capture
// to encoder output, network, decode per frame, frames the decoder holds, round trip now and
// best, one-way estimate, pauses for a key frame and packets lost - together with the interval
// between frame arrivals on the PC and the interval between the phone's pts values, so that
// jitter added by the link can be told from jitter of the phone. With --touch-test it turns the
// phone's touch dots on, presses at the centre of the screen and prints the press-to-screen
// latency the way the F4 line "Measured with touch dots" does.
//
//   gs_session_probe --serial S [--seconds N] [--transport tunnel|wifi|auto] [--max-fps N]
//                     [--bitrate-kbps N] [--max-size N] [--codec h264|h265|av1] [--software]
//                     [--touch-test N] [--touch-lead N]
//
// GS_SERVER_APK names the server build (default: the Gradle release output). The phone must be
// awake. The window must stay visible for the touch-dot test, which needs frames to be shown.
// Exits with 0 when the session streamed, the summary was printed, the server stopped and any
// touch test measured a latency.

#include "LatencyMeter.h"
#include "SessionMetrics.h"

#include <gs/adb/AdbClient.h>
#include <gs/base/Log.h>
#include <gs/input/Geometry.h>
#include <gs/media/LumaSampler.h>
#include <gs/media/VideoDecoder.h>
#include <gs/protocol/Messages.h>
#include <gs/render/FrameSlot.h>
#include <gs/render/GraphicsDevice.h>
#include <gs/render/VideoItem.h>
#include <gs/session/AdbServerHost.h>
#include <gs/session/Session.h>

#include <QByteArray>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQuickWindow>
#include <QTimer>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace gs;
using namespace gs::session;
using namespace std::chrono_literals;

namespace {

constexpr int kTickMs = 50;
constexpr std::int64_t kSamplePeriodUs = 1'000'000;
constexpr std::int64_t kStartTimeoutUs = 120'000'000;
constexpr std::int64_t kStopTimeoutUs = 10'000'000;
constexpr int kWindowWidth = 480;
constexpr int kWindowHeight = 720;
constexpr int kAssumedDensityDpi = 420;
constexpr std::int64_t kTouchHoldUs = 300'000;
constexpr std::int64_t kTouchGapUs = 500'000;
constexpr std::int64_t kTouchStartUs = 500'000;
constexpr std::int64_t kTouchTailUs = 2'000'000;
constexpr std::int64_t kTouchLimitUs = 60'000'000;
constexpr std::int64_t kNoMorePresses = std::numeric_limits<std::int64_t>::max();
constexpr std::size_t kIntervalBins = 10'000;
constexpr std::int64_t kIntervalBinUs = 100;

std::int64_t steadyMicros()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::atomic<bool> g_interrupted{false};

class StderrSink final : public log::Sink
{
public:
    void write(const log::Record& record) noexcept override
    {
        std::fprintf(stderr, "[%.*s] %.*s\n", static_cast<int>(record.category.size()), record.category.data(),
                     static_cast<int>(record.message.size()), record.message.data());
    }
};

StderrSink g_logSink;
std::atomic<QGuiApplication*> g_app{nullptr};

BOOL WINAPI onConsoleControl(DWORD type)
{
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) {
        return FALSE;
    }
    g_interrupted.store(true);
    if (QGuiApplication* app = g_app.load(); app != nullptr) {
        QMetaObject::invokeMethod(app, [] { QCoreApplication::quit(); }, Qt::QueuedConnection);
    }
    return TRUE;
}

std::string number(double value, int decimals)
{
    switch (decimals) {
    case 0:
        return std::format("{:.0f}", value);
    case 2:
        return std::format("{:.2f}", value);
    default:
        break;
    }
    return std::format("{:.1f}", value);
}

std::string maybe(const std::optional<double>& value, int decimals = 1)
{
    return value ? number(*value, decimals) : std::string("-");
}

std::optional<double> medianOf(std::vector<double> values)
{
    if (values.empty()) {
        return std::nullopt;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) / 2.0;
}

const char* transportName(TransportChoice choice)
{
    switch (choice) {
    case TransportChoice::AdbTunnel:
        return "adb tunnel";
    case TransportChoice::WifiDirect:
        return "WiFi direct";
    case TransportChoice::Auto:
        return "WiFi direct, then the adb tunnel";
    }
    return "unknown";
}

const char* codecName(protocol::VideoCodec codec)
{
    switch (codec) {
    case protocol::VideoCodec::H264:
        return "H.264";
    case protocol::VideoCodec::H265:
        return "H.265";
    case protocol::VideoCodec::Av1:
        return "AV1";
    }
    return "unknown";
}

const char* linkName(TransportKind kind)
{
    switch (kind) {
    case TransportKind::AdbTunnel:
        return "the adb tunnel";
    case TransportKind::WifiDirect:
        return "WiFi direct";
    case TransportKind::Extension:
        return "an extension";
    }
    return "an unknown link";
}

class IntervalSeries final
{
public:
    struct Spread {
        std::size_t count = 0;
        double meanMs = 0;
        double deviationMs = 0;
        double p95Ms = 0;
        double longestMs = 0;
    };

    void add(std::int64_t atUs)
    {
        const std::lock_guard lock(m_mutex);
        if (m_lastUs >= 0) {
            const std::int64_t gapUs = atUs - m_lastUs;
            if (gapUs > 0) {
                ++m_count;
                m_sumUs += static_cast<double>(gapUs);
                m_squaresUs += static_cast<double>(gapUs) * static_cast<double>(gapUs);
                m_longestUs = std::max(m_longestUs, gapUs);
                const std::int64_t lastBin = static_cast<std::int64_t>(kIntervalBins) - 1;
                const auto bin = static_cast<std::size_t>(std::min<std::int64_t>(gapUs / kIntervalBinUs, lastBin));
                ++m_bins[bin];
            }
        }
        m_lastUs = atUs;
    }

    [[nodiscard]] Spread spread() const
    {
        const std::lock_guard lock(m_mutex);
        Spread result;
        result.count = static_cast<std::size_t>(m_count);
        if (m_count == 0) {
            return result;
        }
        const double count = static_cast<double>(m_count);
        const double meanUs = m_sumUs / count;
        const double varianceUs = std::max(0.0, m_squaresUs / count - meanUs * meanUs);
        result.meanMs = meanUs / 1000.0;
        result.deviationMs = std::sqrt(varianceUs) / 1000.0;
        result.longestMs = static_cast<double>(m_longestUs) / 1000.0;
        const std::uint64_t rank = (m_count * 95 + 99) / 100;
        std::uint64_t seen = 0;
        for (std::size_t index = 0; index < m_bins.size(); ++index) {
            seen += m_bins[index];
            if (seen >= rank) {
                result.p95Ms = static_cast<double>(static_cast<std::int64_t>(index + 1) * kIntervalBinUs) / 1000.0;
                break;
            }
        }
        return result;
    }

private:
    mutable std::mutex m_mutex;
    std::int64_t m_lastUs = -1;
    std::uint64_t m_count = 0;
    double m_sumUs = 0;
    double m_squaresUs = 0;
    std::int64_t m_longestUs = 0;
    std::array<std::uint32_t, kIntervalBins> m_bins{};
};

class ProbeListener final : public SessionListener, private media::FrameSink
{
public:
    ProbeListener(app::SessionMetrics& metrics, std::shared_ptr<render::FrameSlot> slot,
                  std::shared_ptr<app::LatencyMeter> meter, ID3D11Device* decoderDevice, ID3D11Device* samplerDevice)
        : m_metrics(metrics), m_slot(std::move(slot)), m_meter(std::move(meter)), m_decoderDevice(decoderDevice),
          m_samplerDevice(samplerDevice)
    {
    }

    void useSession(Session& session) noexcept { m_session = &session; }

    void onStateChanged(const SessionState& state) override
    {
        {
            const std::lock_guard lock(m_mutex);
            m_phase = state.phase;
            if (state.failure) {
                m_failure = state.failure;
            }
        }
        m_changed.notify_all();
    }

    void onWelcome(const protocol::Welcome& welcome) override
    {
        {
            const std::lock_guard lock(m_mutex);
            const protocol::Display primary = welcome.displays.empty() ? protocol::Display{} : welcome.displays.front();
            m_phone = std::format("{} {}, Android {} (SDK {}), display {}x{} at {} Hz, density {} dpi",
                                  welcome.manufacturer, welcome.model, welcome.androidRelease, welcome.sdkInt,
                                  primary.width, primary.height,
                                  static_cast<double>(primary.refreshMilliHz) / 1000.0, primary.densityDpi);
            m_capabilities = welcome.capabilities;
            if (!welcome.displays.empty()) {
                m_densityDpi.store(welcome.displays.front().densityDpi, std::memory_order_relaxed);
            }
        }
        m_changed.notify_all();
    }

    void onMessage(const protocol::ToClientMessage& message) override
    {
        if (const auto* changed = std::get_if<protocol::DisplayChanged>(&message);
            changed != nullptr && changed->display.displayId == 0) {
            m_densityDpi.store(changed->display.densityDpi, std::memory_order_relaxed);
        }
    }

    void onHeartbeat(const Heartbeat& heartbeat) override { m_metrics.heartbeat(heartbeat); }

    void onTransport(TransportKind kind) override { m_link.store(static_cast<int>(kind), std::memory_order_relaxed); }

    void onVideoStall(const VideoStall& stall) override { m_metrics.videoStall(stall); }

    void onVideoOpened() override
    {
        m_decoder = std::make_unique<media::VideoDecoder>(m_decoderDevice);
        m_awaitingKeyFrame = false;
        m_previous = media::VideoFrame();
    }

    void onVideoClosed() override
    {
        m_decoder.reset();
        m_sampler.reset();
        m_previous = media::VideoFrame();
        m_awaitingKeyFrame = false;
    }

    void onVideoPacket(const protocol::MediaPacket& packet) override
    {
        if (const auto* info = std::get_if<protocol::VideoSessionInfo>(&packet.session)) {
            m_videoSession.store(packet.header.session, std::memory_order_relaxed);
            m_frameWidth.store(info->frameWidth, std::memory_order_relaxed);
            m_frameHeight.store(info->frameHeight, std::memory_order_relaxed);
            m_codec.store(static_cast<int>(info->codec), std::memory_order_relaxed);
            const int displayLong = std::max(info->displayWidth, info->displayHeight);
            m_frameScale = displayLong > 0
                               ? static_cast<double>(std::max(info->frameWidth, info->frameHeight)) / displayLong
                               : 1.0;
        }
        if (m_awaitingKeyFrame && packet.header.kind == protocol::MediaKind::Frame && !packet.header.keyFrame) {
            return;
        }
        const std::int64_t receivedUs = steadyMicros();
        const auto pushed = m_decoder->push(packet, *this);
        if (packet.header.kind == protocol::MediaKind::Frame) {
            m_arrivals.add(receivedUs);
            m_pts.add(packet.header.ptsUs);
            m_metrics.videoFrame(packet.header.size, packet.header.ptsUs, packet.header.encodeDelayUs, receivedUs,
                                 steadyMicros() - receivedUs);
            m_metrics.decoderHolds(m_decoder->heldFrames());
        }
        if (pushed) {
            if (m_awaitingKeyFrame && packet.header.keyFrame) {
                m_awaitingKeyFrame = false;
                const VideoStall stall{.waited = std::chrono::microseconds(receivedUs - m_awaitingSinceUs),
                                       .keyFrameRequests = 1};
                m_metrics.videoStall(stall);
            }
        } else if (!m_awaitingKeyFrame) {
            m_awaitingKeyFrame = true;
            m_awaitingSinceUs = receivedUs;
            if (m_session != nullptr) {
                m_session->send(protocol::RequestKeyFrame{});
            }
        }
    }

    [[nodiscard]] SessionPhase phase() const
    {
        const std::lock_guard lock(m_mutex);
        return m_phase;
    }

    [[nodiscard]] bool ended() const
    {
        const std::lock_guard lock(m_mutex);
        return m_phase == SessionPhase::Stopped || m_phase == SessionPhase::Failed;
    }

    [[nodiscard]] std::optional<SessionFailure> failure() const
    {
        const std::lock_guard lock(m_mutex);
        return m_failure;
    }

    [[nodiscard]] std::string phone() const
    {
        const std::lock_guard lock(m_mutex);
        return m_phone;
    }

    [[nodiscard]] std::uint32_t capabilities() const
    {
        const std::lock_guard lock(m_mutex);
        return m_capabilities;
    }

    [[nodiscard]] std::uint16_t videoSession() const noexcept
    {
        return m_videoSession.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint16_t frameWidth() const noexcept { return m_frameWidth.load(std::memory_order_relaxed); }

    [[nodiscard]] std::uint16_t frameHeight() const noexcept { return m_frameHeight.load(std::memory_order_relaxed); }

    [[nodiscard]] protocol::VideoCodec codec() const noexcept
    {
        return static_cast<protocol::VideoCodec>(m_codec.load(std::memory_order_relaxed));
    }

    [[nodiscard]] bool hardware() const noexcept { return m_hardware.load(std::memory_order_relaxed); }

    [[nodiscard]] TransportKind link() const noexcept
    {
        return static_cast<TransportKind>(m_link.load(std::memory_order_relaxed));
    }

    [[nodiscard]] IntervalSeries::Spread arrivalSpread() const { return m_arrivals.spread(); }

    [[nodiscard]] IntervalSeries::Spread ptsSpread() const { return m_pts.spread(); }

    bool waitForEnd(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [this] {
            return m_phase == SessionPhase::Stopped || m_phase == SessionPhase::Failed;
        });
    }

private:
    void onFrame(media::VideoFrame frame) override
    {
        if (m_meter) {
            const std::int64_t nowUs = steadyMicros();
            const int density = m_densityDpi.load(std::memory_order_relaxed);
            const double pixelsPerDp = (density > 0 ? density : kAssumedDensityDpi) / 160.0 * m_frameScale;
            const app::LatencyMeter::Plan plan = m_meter->plan(frame.width(), frame.height(), pixelsPerDp, nowUs);
            if (!plan.empty()) {
                if (!m_sampler) {
                    m_sampler = std::make_unique<media::LumaSampler>(m_samplerDevice);
                }
                std::vector<media::LumaReading> fresh;
                if (!plan.freshSpots.empty() && m_previous) {
                    if (auto read = m_sampler->read(m_previous, plan.freshSpots)) {
                        fresh = std::move(*read);
                    }
                }
                std::vector<media::LumaReading> watched;
                if (!plan.watchedSpots.empty()) {
                    if (auto read = m_sampler->read(frame, plan.watchedSpots)) {
                        watched = std::move(*read);
                    }
                }
                m_meter->observe(plan, fresh, watched, frame.ptsUs(), nowUs);
            }
            m_previous = frame.share();
        }
        m_hardware.store(frame.hardware(), std::memory_order_relaxed);
        m_slot->put(std::move(frame));
    }

    app::SessionMetrics& m_metrics;
    std::shared_ptr<render::FrameSlot> m_slot;
    std::shared_ptr<app::LatencyMeter> m_meter;
    ID3D11Device* m_decoderDevice;
    ID3D11Device* m_samplerDevice;
    Session* m_session = nullptr;

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    SessionPhase m_phase = SessionPhase::Idle;
    std::optional<SessionFailure> m_failure;
    std::string m_phone;
    std::uint32_t m_capabilities = 0;

    std::atomic<std::uint16_t> m_densityDpi{0};
    std::atomic<std::uint16_t> m_videoSession{0};
    std::atomic<std::uint16_t> m_frameWidth{0};
    std::atomic<std::uint16_t> m_frameHeight{0};
    std::atomic<int> m_codec{static_cast<int>(protocol::VideoCodec::H264)};
    std::atomic<int> m_link{static_cast<int>(TransportKind::AdbTunnel)};
    std::atomic<bool> m_hardware{false};

    IntervalSeries m_arrivals;
    IntervalSeries m_pts;

    std::unique_ptr<media::VideoDecoder> m_decoder;
    std::unique_ptr<media::LumaSampler> m_sampler;
    media::VideoFrame m_previous;
    double m_frameScale = 1.0;
    bool m_awaitingKeyFrame = false;
    std::int64_t m_awaitingSinceUs = 0;
};

class TouchDots final
{
public:
    explicit TouchDots(Session& session) noexcept : m_session(session) {}

    ~TouchDots() { restore(); }

    TouchDots(const TouchDots&) = delete;
    TouchDots& operator=(const TouchDots&) = delete;

    bool turnOn()
    {
        protocol::SetShowTouches show;
        show.on = true;
        m_on = m_session.send(show);
        return m_on;
    }

    void restore()
    {
        if (!m_on) {
            return;
        }
        m_on = false;
        protocol::SetShowTouches show;
        show.on = false;
        (void)m_session.send(show);
    }

private:
    Session& m_session;
    bool m_on = false;
};

struct RunOptions {
    int seconds = 20;
    int presses = 0;
};

struct ProbeOptions {
    std::string serial;
    int seconds = 20;
    TransportChoice transport = TransportChoice::AdbTunnel;
    int maxFps = 0;
    int bitrateKbps = 8000;
    int maxSize = 0;
    protocol::VideoCodec codec = protocol::VideoCodec::H264;
    bool software = false;
    int presses = 0;
};

class SessionRun final
{
public:
    SessionRun(QGuiApplication& app, Session& session, ProbeListener& listener, std::shared_ptr<render::FrameSlot> slot,
               app::SessionMetrics& metrics, std::shared_ptr<app::LatencyMeter> meter, TouchDots& dots,
               RunOptions options)
        : m_app(app), m_session(session), m_listener(listener), m_slot(std::move(slot)), m_metrics(metrics),
          m_meter(std::move(meter)), m_dots(dots), m_options(options)
    {
        m_tick.setInterval(kTickMs);
        m_tick.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_tick, &QTimer::timeout, &m_app, [this] { tick(); });
    }

    void start()
    {
        m_clock.start();
        m_tick.start();
    }

    [[nodiscard]] bool succeeded() const noexcept { return m_success; }

    void finish();

private:
    [[nodiscard]] std::int64_t nowUs() const noexcept { return m_clock.nsecsElapsed() / 1000; }

    void tick();
    void beginStreaming();
    void sample();
    void driveTouch();
    void startTouchTest();
    void pressOnce();
    void endTouchTest();
    void fail();
    void requestStop();
    void report() const;

    QGuiApplication& m_app;
    Session& m_session;
    ProbeListener& m_listener;
    std::shared_ptr<render::FrameSlot> m_slot;
    app::SessionMetrics& m_metrics;
    std::shared_ptr<app::LatencyMeter> m_meter;
    TouchDots& m_dots;
    RunOptions m_options;

    QTimer m_tick;
    QElapsedTimer m_clock;
    std::int64_t m_streamingUs = -1;
    std::int64_t m_runEndUs = -1;
    std::int64_t m_nextSampleUs = 0;
    std::int64_t m_lastSampleUs = 0;
    bool m_streaming = false;
    bool m_sampling = false;
    bool m_stopAsked = false;
    std::int64_t m_stopAskedUs = 0;
    bool m_finished = false;
    bool m_reported = false;
    bool m_success = false;
    bool m_failed = false;
    std::size_t m_samples = 0;

    bool m_touchStarted = false;
    bool m_touchDone = false;
    bool m_touchMeasured = false;
    bool m_pressingDone = false;
    bool m_pressDown = false;
    int m_pressesSent = 0;
    std::int64_t m_touchDeadlineUs = 0;
    std::int64_t m_pressAtUs = 0;
    std::int64_t m_touchTailUs = 0;

    std::vector<double> m_framesPerSecond;
    std::vector<double> m_megabits;
    std::vector<double> m_captureMs;
    std::vector<double> m_networkMs;
    std::vector<double> m_decodeMs;
    std::vector<double> m_heldFrames;
    std::vector<double> m_roundTripMs;
    std::vector<double> m_roundTripBestMs;
    std::vector<double> m_oneWayMs;
    std::uint64_t m_stalls = 0;
    double m_longestStallMs = 0;
    std::uint64_t m_lostPackets = 0;
};

void SessionRun::tick()
{
    if (m_finished) {
        return;
    }
    if (m_stopAsked) {
        if (m_listener.ended() || nowUs() - m_stopAskedUs >= kStopTimeoutUs) {
            m_finished = true;
            m_tick.stop();
            QCoreApplication::quit();
        }
        return;
    }
    const SessionPhase phase = m_listener.phase();
    if (!m_streaming) {
        if (phase == SessionPhase::Streaming) {
            beginStreaming();
            return;
        }
        if (phase == SessionPhase::Failed || phase == SessionPhase::Stopped) {
            fail();
            return;
        }
        if (nowUs() >= kStartTimeoutUs) {
            std::fprintf(stderr, "the session did not start streaming within %lld s\n",
                         static_cast<long long>(kStartTimeoutUs / 1'000'000));
            fail();
        }
        return;
    }
    if (phase != SessionPhase::Streaming && phase != SessionPhase::Stopping) {
        std::fprintf(stderr, "the session went to %.*s during the measurement\n",
                     static_cast<int>(phaseName(phase).size()), phaseName(phase).data());
        m_failed = true;
        endTouchTest();
        requestStop();
        return;
    }
    if (m_sampling && nowUs() >= m_nextSampleUs) {
        sample();
    }
    if (m_sampling && nowUs() - m_streamingUs >= static_cast<std::int64_t>(m_options.seconds) * 1'000'000) {
        m_sampling = false;
        m_runEndUs = nowUs();
    }
    driveTouch();
    if (!m_sampling && (m_options.presses <= 0 || m_touchDone)) {
        requestStop();
    }
}

void SessionRun::beginStreaming()
{
    m_streaming = true;
    m_streamingUs = nowUs();
    m_sampling = true;
    m_lastSampleUs = m_streamingUs;
    m_nextSampleUs = m_streamingUs + kSamplePeriodUs;
    const std::string phone = m_listener.phone();
    std::printf("streaming: %s over %s\n", phone.empty() ? "the phone did not say what it is" : phone.c_str(),
                linkName(m_listener.link()));
    std::printf("%d s of measurement, %s\n", m_options.seconds,
                m_meter ? "with a touch-dot test" : "without a touch-dot test");
}

void SessionRun::sample()
{
    const std::int64_t now = nowUs();
    const std::chrono::microseconds elapsed(now - m_lastSampleUs);
    m_lastSampleUs = now;
    m_nextSampleUs += kSamplePeriodUs;
    if (m_nextSampleUs + 2 * kSamplePeriodUs <= now) {
        m_nextSampleUs = now + kSamplePeriodUs;
    }
    const app::SessionMetrics::Sample taken = m_metrics.sample(elapsed);
    ++m_samples;
    m_framesPerSecond.push_back(taken.framesPerSecond);
    m_megabits.push_back(taken.megabitsPerSecond);
    if (taken.encodeMs) {
        m_captureMs.push_back(*taken.encodeMs);
    }
    if (taken.networkMs) {
        m_networkMs.push_back(*taken.networkMs);
    }
    if (taken.decodeMs) {
        m_decodeMs.push_back(*taken.decodeMs);
    }
    m_heldFrames.push_back(static_cast<double>(taken.heldFrames));
    if (taken.roundTripMs) {
        m_roundTripMs.push_back(*taken.roundTripMs);
    }
    if (taken.roundTripBestMs) {
        m_roundTripBestMs.push_back(*taken.roundTripBestMs);
    }
    if (taken.inputMs) {
        m_oneWayMs.push_back(*taken.inputMs);
    }
    m_stalls += taken.stalls;
    m_longestStallMs = std::max(m_longestStallMs, taken.longestStallMs);
    m_lostPackets += taken.lostPackets;
    const std::string line = std::format(
        "[{:6.1f} s] {:6.1f} fps {:5.1f} Mbit/s | capture {} ms network {} ms decode {} ms held {} | "
        "round trip {}/{} ms one way {} ms | stalls {} ({:.0f} ms) lost {}",
        static_cast<double>(now - m_streamingUs) / 1e6, taken.framesPerSecond, taken.megabitsPerSecond,
        maybe(taken.encodeMs), maybe(taken.networkMs), maybe(taken.decodeMs), taken.heldFrames,
        maybe(taken.roundTripMs), maybe(taken.roundTripBestMs), maybe(taken.inputMs), taken.stalls,
        taken.longestStallMs, taken.lostPackets);
    std::puts(line.c_str());
}

void SessionRun::driveTouch()
{
    if (m_options.presses <= 0 || m_touchDone) {
        return;
    }
    if (!m_touchStarted) {
        if (m_listener.videoSession() != 0) {
            startTouchTest();
        } else if (nowUs() - m_streamingUs >= 5'000'000) {
            std::fprintf(stderr, "no video session arrived, so the touch test cannot press\n");
            m_touchDone = true;
        }
        return;
    }
    const app::LatencyProgress progress = m_meter->progress();
    if (!progress.running) {
        endTouchTest();
        return;
    }
    if (nowUs() >= m_touchDeadlineUs) {
        std::fprintf(stderr, "the touch test ran out of time: %d seen, %d missed\n", progress.measured,
                     progress.missed);
        endTouchTest();
        return;
    }
    if (m_pressingDone) {
        if (nowUs() >= m_touchTailUs) {
            endTouchTest();
        }
        return;
    }
    if (nowUs() >= m_pressAtUs) {
        pressOnce();
    }
}

void SessionRun::startTouchTest()
{
    if ((m_listener.capabilities() & protocol::Welcome::kShowTouches) == 0) {
        std::fprintf(stderr, "the phone does not allow touch dots, so the real latency cannot be measured\n");
        m_touchDone = true;
        return;
    }
    m_slot->setShownListener(
        [meter = m_meter](std::int64_t ptsUs, std::int64_t shownUs) { meter->shown(ptsUs, shownUs); });
    if (!m_dots.turnOn()) {
        std::fprintf(stderr, "the touch dots could not be turned on\n");
        m_touchDone = true;
        return;
    }
    m_touchStarted = true;
    m_pressDown = false;
    m_pressesSent = 0;
    m_pressingDone = false;
    m_pressAtUs = nowUs() + kTouchStartUs;
    const std::int64_t wanted =
        static_cast<std::int64_t>(m_options.presses) * (kTouchHoldUs + kTouchGapUs) * 2 + kTouchTailUs;
    m_touchDeadlineUs = nowUs() + std::clamp<std::int64_t>(wanted, 20'000'000, kTouchLimitUs);
    std::printf("measuring the real latency with touch dots: %d presses at the centre of the screen\n",
                m_options.presses);
}

void SessionRun::pressOnce()
{
    const std::uint16_t video = m_listener.videoSession();
    if (video == 0) {
        m_pressAtUs = nowUs() + 100'000;
        return;
    }
    const std::uint16_t at = input::fixedCoordinate(0.5);
    protocol::TouchFrame frame;
    frame.videoSession = video;
    frame.points.push_back(protocol::TouchPoint{
        .action = m_pressDown ? protocol::TouchAction::Up : protocol::TouchAction::Down,
        .pointerId = 0,
        .x = at,
        .y = at,
        .pressure = m_pressDown ? std::uint16_t{0} : std::uint16_t{0xFFFF}});
    const bool sent = m_session.send(frame);
    if (sent) {
        m_meter->touched(frame, steadyMicros());
    }
    if (!m_pressDown) {
        m_pressDown = true;
        ++m_pressesSent;
        m_pressAtUs = nowUs() + kTouchHoldUs;
        return;
    }
    m_pressDown = false;
    if (m_pressesSent >= m_options.presses) {
        m_pressingDone = true;
        m_touchTailUs = nowUs() + kTouchTailUs;
        m_pressAtUs = kNoMorePresses;
        return;
    }
    m_pressAtUs = nowUs() + kTouchGapUs;
}

void SessionRun::endTouchTest()
{
    if (m_touchDone) {
        return;
    }
    m_touchDone = true;
    if (!m_touchStarted) {
        m_slot->setShownListener({});
        m_dots.restore();
        return;
    }
    if (m_meter) {
        m_meter->finish();
    }
    m_slot->setShownListener({});
    m_dots.restore();
    if (!m_meter) {
        return;
    }
    const app::LatencyProgress progress = m_meter->progress();
    if (progress.result) {
        std::printf("measured with touch dots: %.0f ms, the median of %d presses (%.0f to %.0f ms)\n",
                    progress.result->medianMs, progress.result->samples, progress.result->lowMs,
                    progress.result->highMs);
        m_touchMeasured = true;
        return;
    }
    std::fprintf(stderr, "the real latency was not measured: %d seen, %d missed\n", progress.measured,
                 progress.missed);
}

void SessionRun::fail()
{
    if (const std::optional<SessionFailure> failure = m_listener.failure()) {
        std::fprintf(stderr, "the session failed in %.*s: %s\n", static_cast<int>(phaseName(failure->phase).size()),
                     phaseName(failure->phase).data(), failure->detail.c_str());
    } else {
        std::fprintf(stderr, "the session ended before it streamed\n");
    }
    m_failed = true;
    endTouchTest();
    requestStop();
}

void SessionRun::requestStop()
{
    if (m_stopAsked) {
        return;
    }
    m_stopAsked = true;
    m_stopAskedUs = nowUs();
    if (m_sampling) {
        m_sampling = false;
        m_runEndUs = nowUs();
    }
    m_dots.restore();
    m_session.stop();
}

void SessionRun::report() const
{
    const double seconds = m_streamingUs >= 0 && m_runEndUs > m_streamingUs
                               ? static_cast<double>(m_runEndUs - m_streamingUs) / 1e6
                               : 0.0;
    std::printf("\nsummary: %zu samples over %.1f s of streaming\n", m_samples, seconds);
    if (m_listener.frameWidth() > 0) {
        std::puts(std::format("  picture: {}x{} {}, decoded on the {}", m_listener.frameWidth(),
                              m_listener.frameHeight(), codecName(m_listener.codec()),
                              m_listener.hardware() ? "GPU" : "processor")
                      .c_str());
    }
    std::puts(std::format("  received: {} frames per second at {} Mbit/s", maybe(medianOf(m_framesPerSecond)),
                          maybe(medianOf(m_megabits)))
                  .c_str());
    std::puts(std::format("  capture to encoder output: {} ms", maybe(medianOf(m_captureMs))).c_str());
    std::puts(std::format("  network: {} ms", maybe(medianOf(m_networkMs))).c_str());
    std::puts(std::format("  decode: {} ms per frame, the decoder held {} frames", maybe(medianOf(m_decodeMs)),
                          maybe(medianOf(m_heldFrames), 0))
                  .c_str());
    const std::optional<double> bestRoundTrip =
        m_roundTripBestMs.empty() ? std::nullopt : std::optional<double>(*std::min_element(m_roundTripBestMs.begin(),
                                                                                          m_roundTripBestMs.end()));
    std::puts(std::format("  round trip: {} ms now, {} ms best; one way estimate {} ms",
                          maybe(medianOf(m_roundTripMs)), maybe(bestRoundTrip), maybe(medianOf(m_oneWayMs)))
                  .c_str());
    std::puts(std::format("  pauses for a key frame: {}, the longest {:.0f} ms; {} packets lost", m_stalls,
                          m_longestStallMs, m_lostPackets)
                  .c_str());
    const IntervalSeries::Spread arrivals = m_listener.arrivalSpread();
    const IntervalSeries::Spread pts = m_listener.ptsSpread();
    std::puts(std::format("  frame arrivals on the PC: mean {} ms, standard deviation {} ms, 95% {} ms, longest {} ms "
                          "({} intervals)",
                          number(arrivals.meanMs, 2), number(arrivals.deviationMs, 2), number(arrivals.p95Ms, 2),
                          number(arrivals.longestMs, 2), arrivals.count)
                  .c_str());
    std::puts(std::format("  phone pts intervals: mean {} ms, standard deviation {} ms, 95% {} ms, longest {} ms "
                          "({} intervals)",
                          number(pts.meanMs, 2), number(pts.deviationMs, 2), number(pts.p95Ms, 2),
                          number(pts.longestMs, 2), pts.count)
                  .c_str());
    if (m_slot) {
        std::puts(std::format("  on the PC screen: {} frames drawn, {} replaced before drawn, {} dropped by Windows",
                              m_slot->taken(), m_slot->replaced(), m_slot->discarded())
                      .c_str());
    }
    if (m_options.presses > 0 && !m_touchStarted) {
        std::puts("  the touch test did not run");
    }
}

void SessionRun::finish()
{
    if (m_reported) {
        return;
    }
    m_reported = true;
    m_tick.stop();
    endTouchTest();
    requestStop();
    (void)m_listener.waitForEnd(std::chrono::milliseconds(kStopTimeoutUs / 1000));
    const bool stopped = m_listener.phase() == SessionPhase::Stopped;
    if (m_streaming) {
        report();
    } else {
        std::fprintf(stderr, "nothing was measured\n");
    }
    const bool touching = m_options.presses == 0 || m_touchMeasured;
    m_success = m_streaming && m_samples > 0 && !m_failed && !g_interrupted.load() && stopped && touching;
    if (!stopped) {
        std::fprintf(stderr, "the session did not stop cleanly\n");
    }
    if (g_interrupted.load()) {
        std::fprintf(stderr, "interrupted\n");
    }
}

bool readInt(const QString& text, int low, int high, int& value)
{
    bool ok = false;
    const int parsed = text.toInt(&ok);
    if (!ok || parsed < low || parsed > high) {
        return false;
    }
    value = parsed;
    return true;
}

int usage(const char* message)
{
    std::fprintf(stderr, "gs_session_probe: %s\n", message);
    return 2;
}
}

int main(int argc, char* argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("gs_session_probe"));
    g_app.store(&app);
    SetConsoleCtrlHandler(onConsoleControl, TRUE);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Measures a whole session on a real phone the way the F4 overlay does."));
    parser.addHelpOption();
    const QCommandLineOption serialOption(QStringLiteral("serial"),
                                          QStringLiteral("The device's adb serial, as adb devices lists it."),
                                          QStringLiteral("serial"));
    const QCommandLineOption secondsOption(QStringLiteral("seconds"),
                                           QStringLiteral("How long to measure once the stream runs."),
                                           QStringLiteral("seconds"), QStringLiteral("20"));
    const QCommandLineOption transportOption(QStringLiteral("transport"),
                                             QStringLiteral("tunnel, wifi or auto."),
                                             QStringLiteral("transport"), QStringLiteral("tunnel"));
    const QCommandLineOption maxFpsOption(QStringLiteral("max-fps"),
                                          QStringLiteral("Cap the phone's frame rate; 0 keeps the phone's own."),
                                          QStringLiteral("fps"), QStringLiteral("0"));
    const QCommandLineOption bitrateOption(QStringLiteral("bitrate-kbps"),
                                           QStringLiteral("The video bit rate in kbit/s."),
                                           QStringLiteral("kbps"), QStringLiteral("8000"));
    const QCommandLineOption maxSizeOption(QStringLiteral("max-size"),
                                           QStringLiteral("Cap the picture's long side in pixels; 0 keeps the "
                                                          "phone's own."),
                                           QStringLiteral("pixels"), QStringLiteral("0"));
    const QCommandLineOption codecOption(QStringLiteral("codec"), QStringLiteral("h264, h265 or av1."),
                                         QStringLiteral("codec"), QStringLiteral("h264"));
    const QCommandLineOption softwareOption(QStringLiteral("software"), QStringLiteral("Decode on the processor."));
    const QCommandLineOption touchTestOption(
        QStringLiteral("touch-test"),
        QStringLiteral("Press this many times at the centre and measure the latency with touch dots."),
        QStringLiteral("presses"), QStringLiteral("0"));
    const QCommandLineOption touchLeadOption(
        QStringLiteral("touch-lead"),
        QStringLiteral("Put this touch lead, 0 to 8 ms, on the server's command line."), QStringLiteral("ms"));
    const QCommandLineOption logOption(QStringLiteral("log"),
                                       QStringLiteral("Print the session's and the server's log lines."));
    parser.addOptions({serialOption, secondsOption, transportOption, maxFpsOption, bitrateOption, maxSizeOption,
                       codecOption, softwareOption, touchTestOption, touchLeadOption, logOption});
    parser.process(app);
    if (parser.isSet(logOption)) {
        log::setMinimumLevel(log::Level::Debug);
        log::setSink(&g_logSink);
    }

    ProbeOptions options;
    options.serial = parser.value(serialOption).toStdString();
    if (options.serial.empty()) {
        return usage("--serial is required");
    }
    if (!readInt(parser.value(secondsOption), 1, 3600, options.seconds)) {
        return usage("--seconds must be 1 to 3600");
    }
    const QString transport = parser.value(transportOption);
    if (transport == u"tunnel") {
        options.transport = TransportChoice::AdbTunnel;
    } else if (transport == u"wifi") {
        options.transport = TransportChoice::WifiDirect;
    } else if (transport == u"auto") {
        options.transport = TransportChoice::Auto;
    } else {
        return usage("--transport must be tunnel, wifi or auto");
    }
    if (!readInt(parser.value(maxFpsOption), 0, 240, options.maxFps)) {
        return usage("--max-fps must be 0 to 240");
    }
    if (!readInt(parser.value(bitrateOption), 500, 100000, options.bitrateKbps)) {
        return usage("--bitrate-kbps must be 500 to 100000");
    }
    if (!readInt(parser.value(maxSizeOption), 0, 8192, options.maxSize)) {
        return usage("--max-size must be 0 to 8192");
    }
    const QString codec = parser.value(codecOption);
    if (codec == u"h264") {
        options.codec = protocol::VideoCodec::H264;
    } else if (codec == u"h265") {
        options.codec = protocol::VideoCodec::H265;
    } else if (codec == u"av1") {
        options.codec = protocol::VideoCodec::Av1;
    } else {
        return usage("--codec must be h264, h265 or av1");
    }
    options.software = parser.isSet(softwareOption);
    if (!readInt(parser.value(touchTestOption), 0, 1000, options.presses)) {
        return usage("--touch-test must be 0 to 1000");
    }
    if (options.presses == 1 || options.presses == 2) {
        return usage("--touch-test needs 0 or at least 3 presses to give a median");
    }
    if (parser.isSet(touchLeadOption)) {
        int parsedLead = 0;
        if (!readInt(parser.value(touchLeadOption), 0, 8, parsedLead)) {
            return usage("--touch-lead must be 0 to 8");
        }
        qputenv("GS_TOUCH_LEAD_MS", QByteArray::number(parsedLead));
    }

    const auto graphics = render::GraphicsDevice::create();
    if (!graphics) {
        std::fprintf(stderr, "gs_session_probe: no Direct3D 11 device\n");
        return 1;
    }

    const QString apk = qEnvironmentVariable("GS_SERVER_APK", QString::fromUtf8(GS_DEFAULT_SERVER_APK));
    const auto package = ServerPackage::load(std::filesystem::path(apk.toStdWString()));
    if (!package) {
        std::fprintf(stderr, "gs_session_probe: no server build at %s; run gradlew :server:assembleRelease\n",
                     apk.toUtf8().constData());
        return 1;
    }

    auto slot = std::make_shared<render::FrameSlot>();
    QQuickWindow window;
    graphics->share(window);
    window.setColor(Qt::black);
    window.setTitle(QStringLiteral("gs_session_probe"));
    auto* video = new render::VideoItem(window.contentItem());
    video->setSource(slot);
    const auto fill = [video, &window] { video->setSize(window.size()); };
    QObject::connect(&window, &QWindow::widthChanged, &window, fill);
    QObject::connect(&window, &QWindow::heightChanged, &window, fill);
    window.resize(kWindowWidth, kWindowHeight);
    fill();
    window.show();

    std::printf("gs_session_probe: %s, %s, %d s\n", options.serial.c_str(), transportName(options.transport),
                options.seconds);
    std::printf("device: %s%s%s; decoding on the %s\n", graphics->description().c_str(),
                graphics->warp() ? " (WARP)" : "", graphics->debugLayer() ? ", debug layer" : "",
                options.software ? "processor" : "GPU");
    std::printf("%s, %d fps at most, %d kbit/s, long side %d px\n", codecName(options.codec), options.maxFps,
                options.bitrateKbps, options.maxSize);
    const QString touchLead = qEnvironmentVariable("GS_TOUCH_LEAD_MS");
    if (!touchLead.isEmpty()) {
        std::printf("touch lead: %s ms\n", touchLead.toUtf8().constData());
    }

    const adb::AdbClient adb;
    SessionOptions sessionOptions;
    sessionOptions.clientVersion = "session-probe";
    if (parser.isSet(logOption)) {
        sessionOptions.serverLogLevel = "debug";
    }
    protocol::ConfigureVideo videoConfig;
    videoConfig.codec = options.codec;
    videoConfig.maxSize = static_cast<std::uint16_t>(options.maxSize);
    videoConfig.maxFps = static_cast<std::uint16_t>(options.maxFps);
    videoConfig.bitrate = static_cast<std::uint32_t>(options.bitrateKbps) * 1000U;
    sessionOptions.video = videoConfig;
    const DirectRoute route{.devicePort = 0, .endpoint = std::nullopt};

    std::shared_ptr<app::LatencyMeter> meter;
    if (options.presses > 0) {
        app::LatencyMeterOptions meterOptions;
        meterOptions.target = options.presses;
        meter = std::make_shared<app::LatencyMeter>(meterOptions);
    }

    app::SessionMetrics metrics;
    ProbeListener listener(metrics, slot, meter, options.software ? nullptr : graphics->device(), graphics->device());
    Session session(std::make_unique<AdbServerHost>(adb, options.serial, package, options.transport, route),
                    sessionOptions, listener);
    listener.useSession(session);
    TouchDots dots(session);
    SessionRun run(app, session, listener, slot, metrics, meter, dots,
                   RunOptions{.seconds = options.seconds, .presses = options.presses});

    session.start();
    run.start();
    const int code = app.exec();
    run.finish();
    SetConsoleCtrlHandler(onConsoleControl, FALSE);
    return code == 0 && run.succeeded() ? 0 : 1;
}
