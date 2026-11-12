#pragma once

#include <gs/exthost/Module.h>

#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gs::app {

struct ClaimHolder {
    std::string id;
    QString name;
    std::function<bool(const std::string& resource, const QString& by)> wanted;
    std::string extension;
};

class HardwareClaims final : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(exthost::ClaimResult)>;

    static constexpr std::chrono::milliseconds kYieldWait{3'000};
    static constexpr std::size_t kMostWaiters = 8;

    explicit HardwareClaims(QObject* parent = nullptr);
    ~HardwareClaims() override;
    HardwareClaims(const HardwareClaims&) = delete;
    HardwareClaims& operator=(const HardwareClaims&) = delete;

    [[nodiscard]] static HardwareClaims* instance() noexcept { return s_instance; }

    [[nodiscard]] exthost::ClaimResult claim(std::string_view resource, const ClaimHolder& holder, bool wait);
    void claimLater(std::string resource, ClaimHolder holder, Done done);
    void release(std::string_view resource, std::string_view holder);
    void releaseAll(std::string_view holder);
    [[nodiscard]] QString holderName(std::string_view resource) const;
    [[nodiscard]] bool holds(std::string_view resource, std::string_view holder) const;

signals:
    void changed();

private:
    struct Held {
        std::string resource;
        std::string holder;
        QString name;
        std::function<bool(const std::string&, const QString&)> wanted;
    };

    struct Waiter {
        std::jthread thread;
        std::shared_ptr<std::atomic<bool>> finished;
        std::string group;
    };

    static inline HardwareClaims* s_instance = nullptr;

    mutable std::mutex m_mutex;
    std::condition_variable m_freed;
    std::map<std::string, Held> m_held;
    bool m_closing = false;
    std::mutex m_waitersMutex;
    std::vector<Waiter> m_waiters;
};

}
