#pragma once

#include <cstdint>
#include <mutex>

namespace gs::session::detail {

struct SendDelay {
    std::int64_t meanUs = 0;
    std::int64_t maxUs = 0;
};

class SendDelayStats
{
public:
    void add(std::int64_t delayUs)
    {
        const std::lock_guard lock(m_mutex);
        m_sumUs += delayUs;
        m_maxUs = delayUs > m_maxUs ? delayUs : m_maxUs;
        ++m_count;
    }

    [[nodiscard]] SendDelay take()
    {
        const std::lock_guard lock(m_mutex);
        SendDelay delay;
        if (m_count > 0) {
            delay.meanUs = m_sumUs / static_cast<std::int64_t>(m_count);
            delay.maxUs = m_maxUs;
        }
        m_sumUs = 0;
        m_maxUs = 0;
        m_count = 0;
        return delay;
    }

private:
    std::mutex m_mutex;
    std::int64_t m_sumUs = 0;
    std::int64_t m_maxUs = 0;
    std::uint64_t m_count = 0;
};

}
