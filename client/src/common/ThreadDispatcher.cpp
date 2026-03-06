// ThreadDispatcher.cpp — Cross-thread task dispatch implementation
// Uses QCoreApplication::postEvent during Qt transition period for main-thread dispatch.

#include "ThreadDispatcher.h"

#include <QCoreApplication>
#include <QEvent>
#include <QObject>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace {

// Custom QEvent type for dispatching tasks to the main thread via Qt event loop
class DispatchEvent : public QEvent {
public:
    static const QEvent::Type EventType;

    explicit DispatchEvent(std::function<void()> fn)
        : QEvent(EventType)
        , m_fn(std::move(fn)) {}

    void execute() {
        if (m_fn) {
            m_fn();
        }
    }

private:
    std::function<void()> m_fn;
};

const QEvent::Type DispatchEvent::EventType =
    static_cast<QEvent::Type>(QEvent::registerEventType());

// Event filter installed on QCoreApplication to handle DispatchEvents
class DispatchReceiver : public QObject {
public:
    static DispatchReceiver& instance() {
        static DispatchReceiver s_instance;
        return s_instance;
    }

    bool event(QEvent* e) override {
        if (e->type() == DispatchEvent::EventType) {
            static_cast<DispatchEvent*>(e)->execute();
            return true;
        }
        return QObject::event(e);
    }

private:
    DispatchReceiver() = default;
};

// Fallback queue for when Qt event loop is not available
std::mutex g_queueMutex;
std::queue<std::function<void()>> g_mainQueue;
std::thread::id g_mainThreadId;
std::atomic<bool> g_initialized{false};

} // anonymous namespace

namespace dispatch {

void initialize() {
    g_mainThreadId = std::this_thread::get_id();
    g_initialized.store(true);
    // Ensure the receiver exists
    (void)DispatchReceiver::instance();
}

void shutdown() {
    g_initialized.store(false);
    // Drain remaining tasks
    processMainQueue();
}

void postToMain(std::function<void()> fn) {
    if (!fn) return;

    // ALWAYS post via the Qt event system — never execute inline.
    //
    // Previous code had a "same-thread shortcut" that executed fn() directly
    // when already on the main thread. This caused deep re-entrant call stacks
    // (NativeTimer callback → dispatch → onWaitKcpTimer → serverStarted.fire
    //  → dispatch → onServerStart → adbProcess.emitResult → dispatch → …)
    // which crashed Qt 6.10.1's DirectWrite font fallback (NULL QFontEngine
    // at Qt6Gui+0x35B093). By always deferring to the next event-loop
    // iteration we guarantee:
    //   1. No re-entrant nesting of dispatched callbacks
    //   2. Qt internal state (font engine, paint device) is clean
    //   3. QBasicTimer / QPropertyAnimation work correctly

    // Use Qt event system to post to main thread
    auto* app = QCoreApplication::instance();
    if (app) {
        QCoreApplication::postEvent(
            &DispatchReceiver::instance(),
            new DispatchEvent(std::move(fn)));
    } else {
        // Fallback: queue for manual processing
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_mainQueue.push(std::move(fn));
    }
}

void invokeOnMain(std::function<void()> fn) {
    if (!fn) return;

    // If already on main thread, execute directly
    if (std::this_thread::get_id() == g_mainThreadId) {
        fn();
        return;
    }

    // Block until the task completes on the main thread
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    postToMain([&]() {
        fn();
        {
            std::lock_guard<std::mutex> lock(mtx);
            done = true;
        }
        cv.notify_one();
    });

    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&] { return done; });
}

void processMainQueue() {
    std::queue<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        std::swap(tasks, g_mainQueue);
    }
    while (!tasks.empty()) {
        auto& fn = tasks.front();
        if (fn) fn();
        tasks.pop();
    }
}

} // namespace dispatch
