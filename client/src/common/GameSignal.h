#pragma once
// Signal.h — Lightweight, type-safe, thread-safe signal/slot system
// Replaces Qt signals/slots/emit/connect without MOC or QObject inheritance.
// Header-only. C++17.

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

// ConnectionId: opaque handle returned by Signal::connect()
using ConnectionId = uint64_t;

// ScopedConnection: RAII guard that auto-disconnects on destruction
class ScopedConnection;

template <typename... Args>
class Signal {
public:
    Signal() = default;
    ~Signal() = default;

    // Non-copyable, non-movable (signals are identity objects)
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&&) = delete;
    Signal& operator=(Signal&&) = delete;

    // ----- connect -----

    // Connect a callable (lambda, std::function, free function, etc.)
    // Returns a ConnectionId that can be used to disconnect later.
    ConnectionId connect(std::function<void(Args...)> slot) {
        std::lock_guard<std::mutex> lock(m_mutex);
        ConnectionId id = ++s_nextId;
        m_slots.push_back({id, std::move(slot), {}});
        return id;
    }

    // Connect with weak_ptr guard: slot is only called if the weak_ptr is still valid.
    // Automatically removed when the weak_ptr expires.
    template <typename T>
    ConnectionId connect(std::weak_ptr<T> guard, std::function<void(Args...)> slot) {
        std::lock_guard<std::mutex> lock(m_mutex);
        ConnectionId id = ++s_nextId;
        // Store the weak_ptr as a type-erased check function
        auto weakCheck = [w = std::move(guard)]() -> bool {
            return !w.expired();
        };
        m_slots.push_back({id, std::move(slot), std::move(weakCheck)});
        return id;
    }

    // ----- disconnect -----

    // Disconnect a specific connection by id
    bool disconnect(ConnectionId id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::find_if(m_slots.begin(), m_slots.end(),
                               [id](const Slot& s) { return s.id == id; });
        if (it != m_slots.end()) {
            m_slots.erase(it);
            return true;
        }
        return false;
    }

    // Disconnect all connections
    void disconnectAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_slots.clear();
    }

    // ----- fire -----

    // Fire the signal — calls all connected slots synchronously.
    // Thread-safe: takes a snapshot of slots under lock, then invokes outside lock
    // to avoid deadlocks if a slot calls connect/disconnect.
    // Named 'fire' instead of 'emit' to avoid conflict with Qt's #define emit macro.
    void fire(Args... args) {
        std::vector<Slot> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // Prune expired weak_ptr connections
            m_slots.erase(
                std::remove_if(m_slots.begin(), m_slots.end(),
                               [](const Slot& s) {
                                   return s.aliveCheck && !s.aliveCheck();
                               }),
                m_slots.end());
            snapshot = m_slots;
        }
        for (auto& s : snapshot) {
            s.fn(args...);
        }
    }

    // operator() as shorthand for fire
    void operator()(Args... args) {
        fire(std::forward<Args>(args)...);
    }

    // ----- blocking fire -----

    // Fire and block until all slots have finished (useful for cross-thread synchronous calls).
    // This is equivalent to Qt::BlockingQueuedConnection when used with ThreadDispatcher.
    void fireBlocking(Args... args) {
        // In direct-call mode, fireBlocking behaves identically to fire
        // because all slots are called synchronously in the caller's thread.
        fire(std::forward<Args>(args)...);
    }

    // ----- query -----

    // Returns true if there are any connected slots
    bool hasConnections() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return !m_slots.empty();
    }

    // Returns the number of connected slots
    size_t connectionCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_slots.size();
    }

private:
    struct Slot {
        ConnectionId id;
        std::function<void(Args...)> fn;
        std::function<bool()> aliveCheck;  // Optional: returns false if connection should be pruned
    };

    mutable std::mutex m_mutex;
    std::vector<Slot> m_slots;

    static std::atomic<uint64_t> s_nextId;
};

// Static member definition (header-only via inline variable, C++17)
template <typename... Args>
inline std::atomic<uint64_t> Signal<Args...>::s_nextId{0};

// ----- ScopedConnection -----
// RAII guard: disconnects a signal connection when destroyed.
// Usage:
//   ScopedConnection sc;
//   sc = signal.connect([](int x) { ... });
//   // auto-disconnects when sc goes out of scope

class ScopedConnection {
public:
    ScopedConnection() = default;

    // Construct from a signal and connection id
    template <typename... Args>
    ScopedConnection(Signal<Args...>& signal, ConnectionId id)
        : m_disconnector([&signal, id]() { signal.disconnect(id); })
        , m_id(id) {}

    ~ScopedConnection() {
        reset();
    }

    // Move-only
    ScopedConnection(ScopedConnection&& other) noexcept
        : m_disconnector(std::move(other.m_disconnector))
        , m_id(other.m_id) {
        other.m_disconnector = nullptr;
        other.m_id = 0;
    }

    ScopedConnection& operator=(ScopedConnection&& other) noexcept {
        if (this != &other) {
            reset();
            m_disconnector = std::move(other.m_disconnector);
            m_id = other.m_id;
            other.m_disconnector = nullptr;
            other.m_id = 0;
        }
        return *this;
    }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    void reset() {
        if (m_disconnector) {
            m_disconnector();
            m_disconnector = nullptr;
            m_id = 0;
        }
    }

    ConnectionId id() const { return m_id; }

private:
    std::function<void()> m_disconnector;
    ConnectionId m_id = 0;
};

// Helper: create a ScopedConnection from a signal connect call
template <typename... Args>
ScopedConnection makeScopedConnection(Signal<Args...>& signal, std::function<void(Args...)> slot) {
    ConnectionId id = signal.connect(std::move(slot));
    return ScopedConnection(signal, id);
}
