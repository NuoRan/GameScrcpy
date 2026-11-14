#pragma once

#include <gs/render/FrameSlot.h>

#include <QObject>

#include <array>
#include <cstdint>
#include <functional>

struct IDXGISwapChain;
struct IDXGISwapChainMedia;

class QQuickWindow;

namespace gs::render {

class PresentClock final : public QObject
{
public:
    struct Sample {
        std::int64_t ptsUs = 0;
        std::int64_t receivedUs = 0;
        std::int64_t takenUs = 0;
        std::int64_t screenUs = 0;
    };

    using Sink = std::function<void(const Sample&, PresentationMode)>;
    using ModeSink = std::function<void(PresentationMode)>;
    using DiscardSink = std::function<void(std::uint32_t frames)>;

    static constexpr std::size_t kMostPending = 16;
    static constexpr std::uint32_t kMostRefreshesBetweenSyncs = 16;

    explicit PresentClock(QObject* parent = nullptr);
    ~PresentClock() override;
    PresentClock(const PresentClock&) = delete;
    PresentClock& operator=(const PresentClock&) = delete;

    void setSink(Sink sink);
    void setModeSink(ModeSink sink);
    void setDiscardSink(DiscardSink sink);
    bool attach(QQuickWindow* window);
    void detach();
    void drew(std::int64_t ptsUs, std::int64_t receivedUs, std::int64_t takenUs);
    void afterFrame();

private:
    struct Pending {
        std::uint32_t presentCount = 0;
        Sample sample;
    };

    bool refreshSwapChain();

    Sink m_sink;
    ModeSink m_modeSink;
    DiscardSink m_discardSink;
    bool m_newestFrame = false;
    QMetaObject::Connection m_connection;
    QQuickWindow* m_window = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    IDXGISwapChainMedia* m_media = nullptr;
    std::array<Pending, kMostPending> m_pending{};
    std::size_t m_pendingCount = 0;
    bool m_drew = false;
    Sample m_drawn;
    std::uint32_t m_lastPresentCount = 0;
    bool m_presented = false;
    PresentationMode m_mode = PresentationMode::Unknown;
    std::uint32_t m_lastSyncRefresh = 0;
    std::int64_t m_lastSyncQpc = 0;
    std::int64_t m_refreshQpc = 0;
    std::uint32_t m_shownPresentCount = 0;
    std::uint32_t m_shownPresentRefresh = 0;
};

}
