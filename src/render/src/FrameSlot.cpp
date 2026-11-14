#include "gs/render/FrameSlot.h"

#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

namespace gs::render {

namespace {

constexpr std::int64_t kLongestFrameGapUs = 100'000;

std::int64_t steadyMicros()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

class Trace
{
public:
    static Trace& instance()
    {
        static Trace trace;
        return trace;
    }

    [[nodiscard]] bool on() const noexcept { return m_out.is_open(); }

    void line(char kind, std::int64_t nowUs, std::int64_t ptsUs, std::int64_t receivedUs, std::size_t queued)
    {
        m_out << kind << ' ' << nowUs << ' ' << ptsUs << ' ' << receivedUs << ' ' << queued << '\n';
    }

    void screenLine(std::int64_t nowUs, std::int64_t ptsUs, std::int64_t receivedUs, std::int64_t takenUs,
                    std::int64_t screenUs)
    {
        m_out << 'd' << ' ' << nowUs << ' ' << ptsUs << ' ' << receivedUs << ' ' << takenUs << ' ' << screenUs
              << '\n';
    }

private:
    Trace()
    {
        const QString path = qEnvironmentVariable("GS_FRAME_TRACE");
        if (!path.isEmpty()) {
            m_out.open(std::filesystem::path(path.toStdWString()), std::ios::out | std::ios::app);
        }
    }

    std::ofstream m_out;
};

}

void FrameSlot::put(media::VideoFrame frame)
{
    std::deque<media::VideoFrame> dropped;
    {
        const std::scoped_lock lock(m_mutex);
        const std::int64_t ptsUs = frame.ptsUs();
        const std::int64_t gapUs = ptsUs - m_lastPtsUs;
        if (m_lastPtsUs != 0 && gapUs > 0 && gapUs < kLongestFrameGapUs) {
            m_frameIntervalUs = m_frameIntervalUs == 0 ? gapUs : m_frameIntervalUs + (gapUs - m_frameIntervalUs) / 8;
        }
        m_lastPtsUs = ptsUs;
        const bool wasEmpty = m_frames.empty();
        if (Trace& trace = Trace::instance(); trace.on()) {
            trace.line('p', steadyMicros(), ptsUs, frame.receivedUs(), m_frames.size());
        }
        m_frames.push_back(std::move(frame));
        const std::size_t limit = pacedLocked() ? kMostQueued : 1;
        while (m_frames.size() > limit) {
            dropped.push_back(std::move(m_frames.front()));
            m_frames.pop_front();
            ++m_replaced;
        }
        if (wasEmpty) {
            if (m_renderWaker) {
                m_renderWaker();
            } else if (m_waker) {
                m_waker();
            }
        }
    }
}

media::VideoFrame FrameSlot::take(std::int64_t* takenUs)
{
    std::shared_ptr<const ShownListener> listener;
    std::int64_t nowUs = 0;
    media::VideoFrame frame;
    {
        const std::scoped_lock lock(m_mutex);
        if (m_frames.empty()) {
            return frame;
        }
        frame = std::move(m_frames.front());
        m_frames.pop_front();
        ++m_taken;
        nowUs = steadyMicros();
        m_waitedUs += nowUs - frame.receivedUs();
        ++m_waitedFrames;
        listener = m_shown;
        if (Trace& trace = Trace::instance(); trace.on()) {
            trace.line('t', nowUs, frame.ptsUs(), frame.receivedUs(), m_frames.size());
        }
    }
    if (takenUs != nullptr) {
        *takenUs = nowUs;
    }
    if (listener) {
        (*listener)(frame.ptsUs(), nowUs);
    }
    return frame;
}

bool FrameSlot::pending() const
{
    const std::scoped_lock lock(m_mutex);
    return !m_frames.empty();
}

void FrameSlot::setShownListener(ShownListener listener)
{
    const std::scoped_lock lock(m_mutex);
    m_shown = listener ? std::make_shared<const ShownListener>(std::move(listener)) : nullptr;
}

void FrameSlot::setDisplayIntervalUs(std::int64_t intervalUs)
{
    const std::scoped_lock lock(m_mutex);
    m_displayIntervalUs = intervalUs;
}

bool FrameSlot::paced() const
{
    const std::scoped_lock lock(m_mutex);
    return pacedLocked();
}

bool FrameSlot::pacedLocked() const noexcept
{
    if (m_displayIntervalUs == 0) {
        return true;
    }
    return m_displayIntervalUs > 0 && m_frameIntervalUs > 0 && m_displayIntervalUs * 10 <= m_frameIntervalUs * 9;
}

std::optional<double> FrameSlot::takeArrivalToRenderMs()
{
    const std::scoped_lock lock(m_mutex);
    if (m_waitedFrames == 0) {
        return std::nullopt;
    }
    const double mean = static_cast<double>(m_waitedUs) / 1000.0 / static_cast<double>(m_waitedFrames);
    m_waitedUs = 0;
    m_waitedFrames = 0;
    return mean;
}

void FrameSlot::setWaker(std::function<void()> waker)
{
    const std::scoped_lock lock(m_mutex);
    m_waker = std::move(waker);
}

void FrameSlot::setRenderWaker(const void* owner, std::function<void()> waker)
{
    const std::scoped_lock lock(m_mutex);
    m_renderOwner = owner;
    m_renderWaker = std::move(waker);
}

void FrameSlot::clearRenderWaker(const void* owner)
{
    const std::scoped_lock lock(m_mutex);
    if (m_renderOwner == owner) {
        m_renderWaker = {};
        m_renderOwner = nullptr;
    }
}

void FrameSlot::wakeGui()
{
    const std::scoped_lock lock(m_mutex);
    if (m_waker) {
        m_waker();
    }
}

void FrameSlot::setScreenListener(ScreenListener listener)
{
    const std::scoped_lock lock(m_mutex);
    m_screen = listener ? std::make_shared<const ScreenListener>(std::move(listener)) : nullptr;
}

void FrameSlot::addScreenLocked(const ScreenSample& sample)
{
    m_screenSamples[m_screenNext] = sample;
    m_screenNext = (m_screenNext + 1) % kMostScreenSamples;
    if (m_screenCount < kMostScreenSamples) {
        ++m_screenCount;
    }
}

void FrameSlot::recordScreen(std::int64_t ptsUs, std::int64_t receivedUs, std::int64_t takenUs,
                             std::int64_t screenUs)
{
    std::shared_ptr<const ScreenListener> listener;
    const std::int64_t nowUs = steadyMicros();
    {
        const std::scoped_lock lock(m_mutex);
        addScreenLocked({ptsUs, receivedUs, takenUs, screenUs});
        listener = m_screen;
        if (Trace& trace = Trace::instance(); trace.on()) {
            trace.screenLine(nowUs, ptsUs, receivedUs, takenUs, screenUs);
        }
    }
    if (listener) {
        (*listener)(ptsUs, receivedUs, takenUs, screenUs);
    }
}

std::optional<ScreenStats> FrameSlot::takeScreenStats()
{
    std::array<double, kMostScreenSamples> arrival{};
    std::array<double, kMostScreenSamples> render{};
    std::size_t count = 0;
    {
        const std::scoped_lock lock(m_mutex);
        count = m_screenCount;
        for (std::size_t i = 0; i < count; ++i) {
            const ScreenSample& sample = m_screenSamples[i];
            arrival[i] = static_cast<double>(sample.screenUs - sample.receivedUs) / 1000.0;
            render[i] = static_cast<double>(sample.screenUs - sample.takenUs) / 1000.0;
        }
        m_screenCount = 0;
        m_screenNext = 0;
    }
    if (count == 0) {
        return std::nullopt;
    }
    const auto mean = [count](const std::array<double, kMostScreenSamples>& values) {
        double total = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            total += values[i];
        }
        return total / static_cast<double>(count);
    };
    const auto percentile95 = [count](std::array<double, kMostScreenSamples>& values) {
        std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
        const std::size_t rank = (count * 95 + 99) / 100;
        return values[rank == 0 ? 0 : rank - 1];
    };
    ScreenStats stats;
    stats.arrivalToScreenMs = mean(arrival);
    stats.renderToScreenMs = mean(render);
    stats.arrivalToScreenP95Ms = percentile95(arrival);
    stats.renderToScreenP95Ms = percentile95(render);
    return stats;
}

void FrameSlot::setGpuTimeListener(GpuTimeListener listener)
{
    const std::scoped_lock lock(m_mutex);
    m_gpuListener = listener ? std::make_shared<const GpuTimeListener>(std::move(listener)) : nullptr;
}

void FrameSlot::setGpuTiming(bool on)
{
    if (m_gpuTiming.exchange(on, std::memory_order_relaxed) == on) {
        return;
    }
    if (!on) {
        const std::scoped_lock lock(m_mutex);
        m_gpuCount = 0;
        m_gpuNext = 0;
    }
}

bool FrameSlot::gpuTiming() const noexcept
{
    return m_gpuTiming.load(std::memory_order_relaxed);
}

void FrameSlot::addGpuLocked(double ms)
{
    m_gpuSamples[m_gpuNext] = ms;
    m_gpuNext = (m_gpuNext + 1) % kMostGpuSamples;
    if (m_gpuCount < kMostGpuSamples) {
        ++m_gpuCount;
    }
}

void FrameSlot::recordGpuTime(double ms)
{
    std::shared_ptr<const GpuTimeListener> listener;
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_gpuTiming.load(std::memory_order_relaxed)) {
            return;
        }
        addGpuLocked(ms);
        listener = m_gpuListener;
    }
    if (listener) {
        (*listener)(ms);
    }
}

std::optional<GpuTimingStats> FrameSlot::takeGpuTimingStats()
{
    std::array<double, kMostGpuSamples> values{};
    std::size_t count = 0;
    {
        const std::scoped_lock lock(m_mutex);
        count = m_gpuCount;
        for (std::size_t i = 0; i < count; ++i) {
            values[i] = m_gpuSamples[i];
        }
        m_gpuCount = 0;
        m_gpuNext = 0;
    }
    if (count == 0) {
        return std::nullopt;
    }
    double total = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        total += values[i];
    }
    std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
    const auto percentile = [count, &values](std::size_t fraction) {
        const std::size_t rank = (count * fraction + 99) / 100;
        return values[rank == 0 ? 0 : rank - 1];
    };
    GpuTimingStats stats;
    stats.meanMs = total / static_cast<double>(count);
    stats.p50Ms = percentile(50);
    stats.p95Ms = percentile(95);
    stats.maxMs = values[count - 1];
    stats.count = count;
    return stats;
}

void FrameSlot::setPresentationMode(PresentationMode mode) noexcept
{
    m_mode.store(mode, std::memory_order_relaxed);
}

PresentationMode FrameSlot::presentationMode() const noexcept
{
    return m_mode.load(std::memory_order_relaxed);
}

std::uint64_t FrameSlot::replaced() const
{
    const std::scoped_lock lock(m_mutex);
    return m_replaced;
}

std::uint64_t FrameSlot::taken() const
{
    const std::scoped_lock lock(m_mutex);
    return m_taken;
}

void FrameSlot::recordDiscarded(std::uint64_t frames)
{
    const std::scoped_lock lock(m_mutex);
    m_discarded += frames;
}

std::uint64_t FrameSlot::discarded() const
{
    const std::scoped_lock lock(m_mutex);
    return m_discarded;
}

}
