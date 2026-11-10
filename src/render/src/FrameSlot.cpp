#include "gs/render/FrameSlot.h"

#include <QString>
#include <QtGlobal>

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
        if (wasEmpty && m_waker) {
            m_waker();
        }
    }
}

media::VideoFrame FrameSlot::take()
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

}
