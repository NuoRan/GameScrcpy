#define LOG_TAG "FpsCounter"
#include "Logger.h"

#include "fpscounter.h"

void FpsCounter::start()
{
    m_rendered.store(0, std::memory_order_relaxed);
    m_skipped.store(0, std::memory_order_relaxed);
    m_lastRendered.store(0, std::memory_order_relaxed);
    m_running.store(true, std::memory_order_release);
}

void FpsCounter::stop()
{
    m_running.store(false, std::memory_order_release);
    m_rendered.store(0, std::memory_order_relaxed);
    m_skipped.store(0, std::memory_order_relaxed);
}

bool FpsCounter::isStarted() const
{
    return m_running.load(std::memory_order_acquire);
}

void FpsCounter::addRenderedFrame()
{
    m_rendered.fetch_add(1, std::memory_order_relaxed);
}

void FpsCounter::addSkippedFrame()
{
    m_skipped.fetch_add(1, std::memory_order_relaxed);
}

uint32_t FpsCounter::pollFps()
{
    uint32_t rendered = m_rendered.exchange(0, std::memory_order_acq_rel);
    m_skipped.exchange(0, std::memory_order_relaxed);
    m_lastRendered.store(rendered, std::memory_order_relaxed);
    return rendered;
}
