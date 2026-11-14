#include "PresentClock.h"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <rhi/qrhi.h>

#include <QtGui/private/qrhid3d11_p.h>

#include <QtCore/qt_windows.h>

#include <dxgi1_3.h>

#include <optional>
#include <utility>

namespace gs::render {

namespace {

std::int64_t perfFrequency()
{
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        return SUCCEEDED(QueryPerformanceFrequency(&value)) ? value.QuadPart : 0;
    }();
    return frequency;
}

std::int64_t qpcToMicros(std::int64_t ticks)
{
    const std::int64_t frequency = perfFrequency();
    if (frequency <= 0) {
        return 0;
    }
    const std::int64_t seconds = ticks / frequency;
    const std::int64_t remainder = ticks % frequency;
    return seconds * 1'000'000 + remainder * 1'000'000 / frequency;
}

PresentationMode modeOf(DXGI_FRAME_PRESENTATION_MODE mode)
{
    switch (mode) {
    case DXGI_FRAME_PRESENTATION_MODE_COMPOSED:
        return PresentationMode::Composed;
    case DXGI_FRAME_PRESENTATION_MODE_OVERLAY:
        return PresentationMode::HardwareOverlay;
    case DXGI_FRAME_PRESENTATION_MODE_NONE:
    case DXGI_FRAME_PRESENTATION_MODE_COMPOSITION_FAILURE:
        return PresentationMode::Unknown;
    }
    return PresentationMode::Unknown;
}

}

PresentClock::PresentClock(QObject* parent)
    : QObject(parent), m_newestFrame(qEnvironmentVariable("GS_PRESENT_NEWEST") == u"1")
{
}

PresentClock::~PresentClock()
{
    detach();
}

void PresentClock::setSink(Sink sink)
{
    m_sink = std::move(sink);
}

void PresentClock::setModeSink(ModeSink sink)
{
    m_modeSink = std::move(sink);
}

void PresentClock::setDiscardSink(DiscardSink sink)
{
    m_discardSink = std::move(sink);
}

void PresentClock::detach()
{
    if (m_connection) {
        disconnect(m_connection);
        m_connection = {};
    }
    m_window = nullptr;
    m_swapChain = nullptr;
    if (m_media != nullptr) {
        m_media->Release();
        m_media = nullptr;
    }
    m_pendingCount = 0;
    m_presented = false;
    m_drew = false;
    m_shownPresentCount = 0;
    m_shownPresentRefresh = 0;
}

bool PresentClock::attach(QQuickWindow* window)
{
    if (window == nullptr) {
        return false;
    }
    if (window != m_window) {
        detach();
        m_window = window;
        m_connection = connect(
            window, &QQuickWindow::afterFrameEnd, this, [this] { afterFrame(); }, Qt::DirectConnection);
    }
    return refreshSwapChain();
}

bool PresentClock::refreshSwapChain()
{
    if (m_window == nullptr) {
        return false;
    }
    QSGRendererInterface* renderer = m_window->rendererInterface();
    if (renderer == nullptr || renderer->graphicsApi() != QSGRendererInterface::Direct3D11) {
        return false;
    }
    auto* swapChain = static_cast<QRhiSwapChain*>(
        renderer->getResource(m_window, QSGRendererInterface::RhiSwapchainResource));
    auto* chain = swapChain != nullptr ? static_cast<QD3D11SwapChain*>(swapChain) : nullptr;
    if (chain != nullptr && m_newestFrame && chain->swapInterval != 0
        && (chain->swapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) == 0) {
        chain->swapInterval = 0;
    }
    IDXGISwapChain* native = chain != nullptr ? chain->swapChain : nullptr;
    if (native == m_swapChain) {
        return native != nullptr;
    }
    m_swapChain = native;
    m_pendingCount = 0;
    m_presented = false;
    m_shownPresentCount = 0;
    m_shownPresentRefresh = 0;
    if (m_media != nullptr) {
        m_media->Release();
        m_media = nullptr;
    }
    if (m_swapChain != nullptr) {
        if (IDXGISwapChainMedia* media = nullptr; SUCCEEDED(m_swapChain->QueryInterface(IID_PPV_ARGS(&media)))) {
            m_media = media;
        }
    }
    return m_swapChain != nullptr;
}

void PresentClock::drew(std::int64_t ptsUs, std::int64_t receivedUs, std::int64_t takenUs)
{
    m_drew = true;
    m_drawn = {ptsUs, receivedUs, takenUs, 0};
}

void PresentClock::afterFrame()
{
    if (!refreshSwapChain()) {
        m_drew = false;
        return;
    }
    UINT count = 0;
    if (FAILED(m_swapChain->GetLastPresentCount(&count))) {
        m_drew = false;
        return;
    }
    const bool fresh = m_drew;
    if (m_drew && (!m_presented || count != m_lastPresentCount)) {
        if (m_pendingCount == kMostPending) {
            for (std::size_t i = 1; i < m_pendingCount; ++i) {
                m_pending[i - 1] = m_pending[i];
            }
            --m_pendingCount;
        }
        m_pending[m_pendingCount] = {count, m_drawn};
        ++m_pendingCount;
    }
    m_drew = false;
    m_lastPresentCount = count;
    m_presented = true;

    if (fresh && m_media != nullptr) {
        DXGI_FRAME_STATISTICS_MEDIA media{};
        if (SUCCEEDED(m_media->GetFrameStatisticsMedia(&media))) {
            const PresentationMode mode = modeOf(media.CompositionMode);
            if (mode != m_mode) {
                m_mode = mode;
                if (m_modeSink) {
                    m_modeSink(mode);
                }
            }
        }
    }
    if (m_pendingCount == 0) {
        return;
    }

    DXGI_FRAME_STATISTICS stats{};
    if (FAILED(m_swapChain->GetFrameStatistics(&stats)) || stats.SyncQPCTime.QuadPart <= 0) {
        return;
    }
    if (stats.PresentCount != m_shownPresentCount) {
        if (m_shownPresentCount != 0 && stats.PresentCount > m_shownPresentCount) {
            const std::uint32_t presents = stats.PresentCount - m_shownPresentCount;
            const std::uint32_t refreshes = stats.PresentRefreshCount - m_shownPresentRefresh;
            if (refreshes < presents && m_discardSink) {
                m_discardSink(presents - refreshes);
            }
        }
        m_shownPresentCount = stats.PresentCount;
        m_shownPresentRefresh = stats.PresentRefreshCount;
    }
    const std::int64_t syncQpc = stats.SyncQPCTime.QuadPart;
    if (m_lastSyncQpc > 0 && stats.SyncRefreshCount > m_lastSyncRefresh && syncQpc > m_lastSyncQpc) {
        const std::uint32_t refreshes = stats.SyncRefreshCount - m_lastSyncRefresh;
        if (refreshes <= kMostRefreshesBetweenSyncs) {
            m_refreshQpc = (syncQpc - m_lastSyncQpc) / refreshes;
        }
    }
    m_lastSyncRefresh = stats.SyncRefreshCount;
    m_lastSyncQpc = syncQpc;
    std::int64_t shownQpc = syncQpc;
    bool reliable = true;
    if (stats.SyncRefreshCount > stats.PresentRefreshCount) {
        const std::uint32_t behind = stats.SyncRefreshCount - stats.PresentRefreshCount;
        reliable = m_refreshQpc > 0 && behind <= kMostRefreshesBetweenSyncs;
        shownQpc -= static_cast<std::int64_t>(behind) * m_refreshQpc;
    }
    const std::int64_t screenUs = qpcToMicros(shownQpc);
    std::optional<Sample> shown;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < m_pendingCount; ++i) {
        const Pending& entry = m_pending[i];
        if (entry.presentCount == stats.PresentCount && !shown && reliable) {
            shown = entry.sample;
            shown->screenUs = screenUs;
        } else if (entry.presentCount > stats.PresentCount) {
            m_pending[kept] = entry;
            ++kept;
        }
    }
    m_pendingCount = kept;
    if (shown && m_sink) {
        m_sink(*shown, m_mode);
    }
}

}
