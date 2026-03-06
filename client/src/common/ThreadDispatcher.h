#pragma once
// ThreadDispatcher.h — Cross-thread task dispatch system
// Replaces QMetaObject::invokeMethod(obj, lambda, Qt::QueuedConnection)
// During the transition period, uses QCoreApplication::postEvent for main thread dispatch.
// C++17.

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace dispatch {

// Post a task to the main (UI) thread for asynchronous execution.
// The task will be executed during the next Qt event loop iteration (transition period)
// or during the next processMainQueue() call.
void postToMain(std::function<void()> fn);

// Synchronously invoke a task on the main thread and block until completion.
// WARNING: Do not call from the main thread — will deadlock!
void invokeOnMain(std::function<void()> fn);

// Process all pending tasks in the main thread queue.
// Call this from the main thread's event loop or timer.
// During Qt transition period, this is called automatically via QEvent.
void processMainQueue();

// Initialize the dispatcher system (call once from main thread at startup)
void initialize();

// Shutdown the dispatcher system
void shutdown();

} // namespace dispatch
