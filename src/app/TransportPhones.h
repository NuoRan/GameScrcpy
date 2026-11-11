#pragma once

#include <gs/exthost/Module.h>
#include <gs/session/AdbServerHost.h>
#include <gs/session/StreamHost.h>

#include <QString>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>

namespace gs::app {

struct TransportPhone {
    std::string extension;
    QString extensionName;
    std::string transport;
    QString transportTitle;
    std::string id;
    QString name;
    QString model;
    QString detail;
    QString icon;
    QString iconPath;

    friend bool operator==(const TransportPhone&, const TransportPhone&) = default;
};

class TransportChanges final
{
public:
    void bump(std::string_view key);
    [[nodiscard]] std::uint64_t generation(std::string_view key) const;
    [[nodiscard]] bool waitPast(std::string_view key, std::uint64_t seen, std::chrono::steady_clock::time_point deadline,
                                const std::stop_token& stop);

private:
    mutable std::mutex m_mutex;
    std::condition_variable_any m_changed;
    std::map<std::string, std::uint64_t, std::less<>> m_generations;
};

struct PhoneRoute {
    std::weak_ptr<exthost::Module> module;
    std::size_t transport = 0;
    std::string key;
    std::string phone;
    std::string through;
};

[[nodiscard]] std::string transportKey(std::string_view extension, std::string_view transport);
[[nodiscard]] std::string streamSession(const session::LaunchParams& params, const session::ServerPackage* package);
[[nodiscard]] std::shared_ptr<session::PhoneStreams> makePhoneStreams(PhoneRoute route,
                                                                    std::shared_ptr<TransportChanges> changes,
                                                                    std::shared_ptr<const session::ServerPackage> package);

}
