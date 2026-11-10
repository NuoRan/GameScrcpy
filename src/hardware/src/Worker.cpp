#include "Worker.h"

#include <utility>

namespace gs::hardware::detail {

Worker::Worker(std::function<void()> enter, std::function<void()> leave)
    : m_enter(std::move(enter))
    , m_leave(std::move(leave))
    , m_thread([this] { run(); })
{
}

Worker::~Worker()
{
    stop();
}

void Worker::post(std::function<void()> task)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (m_stopping) {
            return;
        }
        m_tasks.push_back(std::move(task));
    }
    m_wake.notify_one();
}

void Worker::stop()
{
    {
        const std::scoped_lock lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_one();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void Worker::run()
{
    if (m_enter) {
        m_enter();
    }
    for (;;) {
        std::function<void()> task;
        std::deque<std::function<void()>> dropped;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
            if (m_stopping) {
                dropped.swap(m_tasks);
            } else {
                task = std::move(m_tasks.front());
                m_tasks.pop_front();
            }
        }
        if (!task) {
            break;
        }
        task();
    }
    if (m_leave) {
        m_leave();
    }
}

}
