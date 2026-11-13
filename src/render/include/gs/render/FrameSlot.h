#pragma once

#include <gs/media/VideoDecoder.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace gs::render {

enum class PresentationMode : std::uint8_t {
    Unknown,
    Composed,
    HardwareOverlay,
};

struct ScreenStats {
    double arrivalToScreenMs = 0.0;
    double arrivalToScreenP95Ms = 0.0;
    double renderToScreenMs = 0.0;
    double renderToScreenP95Ms = 0.0;
};

class FrameSlot
{
public:
    using ShownListener = std::function<void(std::int64_t ptsUs, std::int64_t shownUs)>;
    using ScreenListener = std::function<void(std::int64_t ptsUs, std::int64_t receivedUs, std::int64_t takenUs,
                                              std::int64_t screenUs)>;

    static constexpr std::size_t kMostQueued = 2;
    static constexpr std::size_t kMostScreenSamples = 1024;

    void put(media::VideoFrame frame);
    [[nodiscard]] media::VideoFrame take(std::int64_t* takenUs = nullptr);
    [[nodiscard]] bool pending() const;

    void setWaker(std::function<void()> waker);
    void setRenderWaker(const void* owner, std::function<void()> waker);
    void clearRenderWaker(const void* owner);
    void wakeGui();
    void setShownListener(ShownListener listener);
    void setScreenListener(ScreenListener listener);
    void setDisplayIntervalUs(std::int64_t intervalUs);

    void recordScreen(std::int64_t ptsUs, std::int64_t receivedUs, std::int64_t takenUs, std::int64_t screenUs);
    [[nodiscard]] std::optional<ScreenStats> takeScreenStats();
    void setPresentationMode(PresentationMode mode) noexcept;
    [[nodiscard]] PresentationMode presentationMode() const noexcept;

    [[nodiscard]] std::uint64_t replaced() const;
    [[nodiscard]] std::uint64_t taken() const;
    [[nodiscard]] std::optional<double> takeArrivalToRenderMs();
    [[nodiscard]] bool paced() const;

private:
    struct ScreenSample {
        std::int64_t ptsUs = 0;
        std::int64_t receivedUs = 0;
        std::int64_t takenUs = 0;
        std::int64_t screenUs = 0;
    };

    [[nodiscard]] bool pacedLocked() const noexcept;
    void addScreenLocked(const ScreenSample& sample);

    mutable std::mutex m_mutex;
    std::deque<media::VideoFrame> m_frames;
    std::function<void()> m_waker;
    const void* m_renderOwner = nullptr;
    std::function<void()> m_renderWaker;
    std::shared_ptr<const ShownListener> m_shown;
    std::shared_ptr<const ScreenListener> m_screen;
    std::uint64_t m_replaced = 0;
    std::uint64_t m_taken = 0;
    std::int64_t m_waitedUs = 0;
    std::uint64_t m_waitedFrames = 0;
    std::int64_t m_displayIntervalUs = -1;
    std::int64_t m_lastPtsUs = 0;
    std::int64_t m_frameIntervalUs = 0;
    std::array<ScreenSample, kMostScreenSamples> m_screenSamples{};
    std::size_t m_screenCount = 0;
    std::size_t m_screenNext = 0;
    std::atomic<PresentationMode> m_mode{PresentationMode::Unknown};
};

}
