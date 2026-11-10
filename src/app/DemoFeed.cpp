#include "DemoFeed.h"

#include <gs/media/VideoDecoder.h>
#include <gs/protocol/Constants.h>
#include <gs/record/MediaFile.h>
#include <gs/record/Recorder.h>
#include <gs/render/GraphicsDevice.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <utility>

namespace gs::app {

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kRateWindow = std::chrono::seconds(1);
constexpr auto kLongestCatchUp = std::chrono::milliseconds(100);
constexpr std::int64_t kFallbackFrameUs = 33'333;
constexpr std::int64_t kLongestGapUs = 5'000'000;
constexpr std::int64_t kUnknownGap = -1;
constexpr std::chrono::milliseconds kStatusPeriod{2000};
constexpr std::uint64_t kStatusCycle = 40;
constexpr std::uint64_t kStatusStepsPerPercent = 5;
constexpr int kStatusBattery = 82;
constexpr int kStatusTemperatureDeciC = 352;
constexpr std::uint64_t kStatusTemperatureSteps = 8;
constexpr int kStatusChargingWarmth = 12;
constexpr std::array<std::uint16_t, 8> kStatusCpuPermille{230, 310, 270, 420, 380, 190, 260, 350};

class PublishingSink final : public media::FrameSink
{
public:
    PublishingSink(render::FrameSlot& slot, media::FrameSnapshots& snapshots, std::atomic<double>& rate,
                   render::FrameSlot& bench, const std::atomic<bool>& benchFed)
        : m_slot(slot), m_snapshots(snapshots), m_rate(rate), m_bench(bench), m_benchFed(benchFed)
    {
    }

    void onFrame(media::VideoFrame frame) override
    {
        m_snapshots.offer(frame);
        m_snapshots.poll();
        if (m_benchFed.load(std::memory_order_relaxed)) {
            m_bench.put(frame.share());
        }
        m_slot.put(std::move(frame));
        ++m_published;
        measure(Clock::now());
    }

    void measure(Clock::time_point now)
    {
        const Clock::duration elapsed = now - m_windowStart;
        if (elapsed < kRateWindow) {
            return;
        }
        m_rate.store(static_cast<double>(m_published) / std::chrono::duration<double>(elapsed).count(),
                     std::memory_order_relaxed);
        m_published = 0;
        m_windowStart = now;
    }

    [[nodiscard]] Clock::time_point nextMeasure() const noexcept { return m_windowStart + kRateWindow; }

private:
    render::FrameSlot& m_slot;
    media::FrameSnapshots& m_snapshots;
    std::atomic<double>& m_rate;
    render::FrameSlot& m_bench;
    const std::atomic<bool>& m_benchFed;
    Clock::time_point m_windowStart = Clock::now();
    std::uint64_t m_published = 0;
};

class Pacer
{
public:
    [[nodiscard]] bool waitUntil(const std::stop_token& token, Clock::time_point deadline, PublishingSink& sink)
    {
        std::unique_lock lock(m_mutex);
        for (;;) {
            m_wake.wait_until(lock, token, std::min(deadline, sink.nextMeasure()), [] { return false; });
            if (token.stop_requested()) {
                return false;
            }
            const Clock::time_point now = Clock::now();
            sink.measure(now);
            if (now >= deadline) {
                return true;
            }
        }
    }

private:
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
};

}

QString demoSerialOf(int number)
{
    return number > 1 ? QStringLiteral("gs-demo-%1").arg(number) : QStringLiteral("gs-demo");
}

std::vector<DemoDevice> demoDevicesOf(const QStringList& recordings, const QStringList& games)
{
    std::vector<DemoDevice> devices;
    devices.reserve(static_cast<std::size_t>(recordings.size()));
    for (qsizetype i = 0; i < recordings.size(); ++i) {
        const QString game = i < games.size() ? games[i] : (games.isEmpty() ? QString() : games.constLast());
        devices.push_back({.recording = recordings[i], .game = game});
    }
    return devices;
}

DemoFeed::DemoFeed(const render::GraphicsDevice& graphics, std::filesystem::path recording)
    : m_graphics(graphics), m_recording(std::move(recording)), m_slot(std::make_shared<render::FrameSlot>()),
      m_snapshots(std::make_shared<media::FrameSnapshots>(graphics.device()))
{
}

DemoFeed::~DemoFeed()
{
    stop();
}

bool DemoFeed::start()
{
    stop();
    m_statusStart = Clock::now();
    setProblem(load());
    if (!problem().isEmpty()) {
        return false;
    }
    std::promise<QString> promise;
    std::future<QString> opened = promise.get_future();
    m_player = std::jthread(
        [this, ready = std::move(promise)](const std::stop_token& token) mutable { run(token, ready); });
    if (QString reason = opened.get(); !reason.isEmpty()) {
        stop();
        setProblem(std::move(reason));
        return false;
    }
    return true;
}

void DemoFeed::stop()
{
    if (m_player.joinable()) {
        m_player.request_stop();
        m_player.join();
    }
    m_snapshots->forget();
    m_framesPerSecond.store(0.0, std::memory_order_relaxed);
}

QString DemoFeed::problem() const
{
    const std::scoped_lock lock(m_mutex);
    return m_problem;
}

std::shared_ptr<render::FrameSlot> DemoFeed::frames() const noexcept
{
    return m_slot;
}

double DemoFeed::framesPerSecond() const noexcept
{
    return m_framesPerSecond.load(std::memory_order_relaxed);
}

protocol::DeviceStatus DemoFeed::status() const
{
    return fakeStatus(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_statusStart));
}

protocol::DeviceStatus DemoFeed::fakeStatus(std::chrono::milliseconds elapsed)
{
    const auto step = static_cast<std::uint64_t>(std::max(elapsed, std::chrono::milliseconds::zero()) / kStatusPeriod);
    const std::uint64_t phase = step % kStatusCycle;
    const std::uint64_t half = kStatusCycle / 2;
    const bool charging = phase >= half;
    const int moved = static_cast<int>((charging ? phase - half : phase) / kStatusStepsPerPercent);
    const int drained = static_cast<int>(half / kStatusStepsPerPercent);
    protocol::DeviceStatus status;
    status.batteryPercent =
        static_cast<std::uint8_t>(charging ? kStatusBattery - drained + moved : kStatusBattery - moved);
    status.charging = charging;
    status.temperatureDeciC = static_cast<std::int16_t>(kStatusTemperatureDeciC +
                                                        static_cast<int>(step % kStatusTemperatureSteps) +
                                                        (charging ? kStatusChargingWarmth : 0));
    status.cpuPermille = kStatusCpuPermille[step % kStatusCpuPermille.size()];
    return status;
}

QString DemoFeed::load()
{
    const QString name = QString::fromStdWString(m_recording.wstring());
    m_packets.clear();
    m_delaysUs.clear();
    m_bytes.clear();
    m_videoSession = 0;
    m_frameWidth = 0;
    m_frameHeight = 0;
    std::ifstream file(m_recording, std::ios::binary);
    if (!file) {
        return QStringLiteral("Cannot read the recording %1.").arg(name);
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    m_bytes = std::move(bytes);
    std::optional<std::uint16_t> session;
    bool configured = false;
    std::optional<std::int64_t> previousPts;
    std::optional<std::int64_t> firstPts;
    std::int64_t lastPts = 0;
    std::int64_t gapSumUs = 0;
    std::int64_t gapCount = 0;
    const auto refuse = [this](QString reason) {
        m_packets.clear();
        m_delaysUs.clear();
        return reason;
    };
    for (std::size_t offset = 0; offset < m_bytes.size();) {
        auto packet = protocol::decodeMediaPacket(protocol::MediaChannel::Video, std::span(m_bytes).subspan(offset));
        if (!packet) {
            return refuse(QStringLiteral("%1 is not a video recording: no video packet starts at byte %2.")
                              .arg(name)
                              .arg(static_cast<qulonglong>(offset)));
        }
        const protocol::MediaHeader header = packet->header;
        const bool frame = header.kind == protocol::MediaKind::Frame;
        std::int64_t delayUs = 0;
        if (header.kind == protocol::MediaKind::Session) {
            session = header.session;
            const auto* info = std::get_if<protocol::VideoSessionInfo>(&packet->session);
            if (info != nullptr && m_frameWidth == 0) {
                m_videoSession = header.session;
                m_frameWidth = info->frameWidth;
                m_frameHeight = info->frameHeight;
            }
            configured = false;
        } else if (!session || header.session != *session || (frame && !configured)) {
            return refuse(
                QStringLiteral("%1 has a packet before its session description or codec configuration.").arg(name));
        } else if (!frame) {
            configured = true;
        } else if (header.ptsUs < 0) {
            return refuse(QStringLiteral("%1 has a frame with a negative timestamp.").arg(name));
        } else {
            delayUs = previousPts && header.ptsUs >= *previousPts
                          ? std::min(header.ptsUs - *previousPts, kLongestGapUs)
                          : kUnknownGap;
            if (delayUs > 0) {
                gapSumUs += delayUs;
                ++gapCount;
            }
            previousPts = header.ptsUs;
            firstPts = firstPts.value_or(header.ptsUs);
            lastPts = std::max(lastPts, header.ptsUs);
        }
        offset += protocol::kMediaHeaderSize + header.size;
        m_packets.push_back(std::move(*packet));
        m_delaysUs.push_back(delayUs);
    }
    if (!previousPts) {
        return refuse(QStringLiteral("%1 holds no video frames.").arg(name));
    }
    const std::int64_t typicalUs = gapCount > 0 ? gapSumUs / gapCount : kFallbackFrameUs;
    std::replace(m_delaysUs.begin(), m_delaysUs.end(), kUnknownGap, typicalUs);
    m_loopUs = std::max(lastPts - *firstPts, std::int64_t{0}) + typicalUs;
    return {};
}

void DemoFeed::setRecorder(std::shared_ptr<record::Recorder> recorder)
{
    const std::scoped_lock lock(m_recorderMutex);
    m_recorder = std::move(recorder);
}

std::shared_ptr<record::Recorder> DemoFeed::recorder() const
{
    const std::scoped_lock lock(m_recorderMutex);
    return m_recorder;
}

void DemoFeed::run(const std::stop_token& token, std::promise<QString>& opened)
{
    media::VideoDecoder decoder(m_graphics.device());
    PublishingSink sink(*m_slot, *m_snapshots, m_framesPerSecond, *m_bench, m_benchFed);
    record::MediaHead head;
    std::weak_ptr<record::Recorder> primed;
    std::int64_t shift = 0;
    const auto deliver = [&](const protocol::MediaPacket& original) {
        protocol::MediaPacket packet = original;
        if (packet.header.kind == protocol::MediaKind::Frame) {
            packet.header.ptsUs += shift;
        }
        if (const std::shared_ptr<record::Recorder> recording = recorder()) {
            if (primed.owner_before(recording) || recording.owner_before(primed)) {
                primed = recording;
                if (packet.header.kind != protocol::MediaKind::Session) {
                    for (const protocol::MediaPacket& earlier : head.packets(protocol::MediaChannel::Video)) {
                        recording->video(earlier);
                    }
                }
            }
            recording->video(packet);
        }
        head.see(packet);
        return decoder.push(packet, sink);
    };
    std::size_t next = 0;
    for (; next < m_packets.size() && m_packets[next].header.kind != protocol::MediaKind::Frame; ++next) {
        if (const auto pushed = deliver(m_packets[next]); !pushed) {
            opened.set_value(QStringLiteral("The recording cannot be decoded: %1.")
                                 .arg(QString::fromStdString(pushed.error().message)));
            return;
        }
    }
    opened.set_value(QString());
    Pacer pacer;
    Clock::time_point due = Clock::now() - std::chrono::microseconds(m_delaysUs[next]);
    for (;;) {
        for (; next < m_packets.size(); ++next) {
            const protocol::MediaPacket& packet = m_packets[next];
            if (packet.header.kind == protocol::MediaKind::Frame) {
                due = std::max(due + std::chrono::microseconds(m_delaysUs[next]), Clock::now() - kLongestCatchUp);
                if (!pacer.waitUntil(token, due, sink)) {
                    return;
                }
            }
            if (const auto pushed = deliver(packet); !pushed) {
                setProblem(QStringLiteral("The recording cannot be decoded: %1.")
                               .arg(QString::fromStdString(pushed.error().message)));
                return;
            }
        }
        next = 0;
        shift += m_loopUs;
    }
}

void DemoFeed::setProblem(QString reason)
{
    {
        const std::scoped_lock lock(m_mutex);
        m_problem = std::move(reason);
    }
    m_framesPerSecond.store(0.0, std::memory_order_relaxed);
}

}
