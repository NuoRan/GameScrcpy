#pragma once

#include <gs/media/VideoDecoder.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace gs::render {

class FrameSlot
{
public:
    using ShownListener = std::function<void(std::int64_t ptsUs, std::int64_t shownUs)>;

    static constexpr std::size_t kMostQueued = 2;

    void put(media::VideoFrame frame);
    [[nodiscard]] media::VideoFrame take();
    [[nodiscard]] bool pending() const;

    void setWaker(std::function<void()> waker);
    void setShownListener(ShownListener listener);
    void setDisplayIntervalUs(std::int64_t intervalUs);

    [[nodiscard]] std::uint64_t replaced() const;
    [[nodiscard]] std::uint64_t taken() const;
    [[nodiscard]] std::optional<double> takeArrivalToRenderMs();
    [[nodiscard]] bool paced() const;

private:
    [[nodiscard]] bool pacedLocked() const noexcept;

    mutable std::mutex m_mutex;
    std::deque<media::VideoFrame> m_frames;
    std::function<void()> m_waker;
    std::shared_ptr<const ShownListener> m_shown;
    std::uint64_t m_replaced = 0;
    std::uint64_t m_taken = 0;
    std::int64_t m_waitedUs = 0;
    std::uint64_t m_waitedFrames = 0;
    std::int64_t m_displayIntervalUs = -1;
    std::int64_t m_lastPtsUs = 0;
    std::int64_t m_frameIntervalUs = 0;
};

}
