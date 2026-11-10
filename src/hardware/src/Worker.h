#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace gs::hardware::detail {

class Worker
{
public:
    explicit Worker(std::function<void()> enter = {}, std::function<void()> leave = {});
    ~Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void post(std::function<void()> task);
    void stop();

private:
    void run();

    std::function<void()> m_enter;
    std::function<void()> m_leave;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<std::function<void()>> m_tasks;
    bool m_stopping = false;
    std::thread m_thread;
};

}
