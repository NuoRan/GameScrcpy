#include "unit/adb/FakeAdbServer.h"

#include "AppSettings.h"
#include "LanHosts.h"
#include "WirelessController.h"

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using gs::adb::AdbClient;
using gs::adb::test::FakeAdbServer;
using gs::adb::test::FakeConnection;
using gs::app::AppSettings;
using gs::app::WirelessController;

namespace {

class WirelessApplication
{
public:
    WirelessApplication()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_application = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    std::string m_name = "gs_app_tests";
    std::array<char*, 2> m_argv{m_name.data(), nullptr};
    std::unique_ptr<QCoreApplication> m_application;
};

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : m_path(std::filesystem::temp_directory_path() /
                 ("gs-acceptance-wireless-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                  std::to_string(s_next++)))
    {
        std::filesystem::create_directories(m_path);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

private:
    static inline std::atomic<int> s_next = 0;
    std::filesystem::path m_path;
};

template <typename Predicate>
bool waitUntil(Predicate done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::yield();
    }
    return true;
}

class AdbAnswers
{
public:
    void serve(FakeConnection& connection)
    {
        const std::string request = connection.readRequest();
        {
            const std::lock_guard lock(m_mutex);
            m_requests.push_back(request);
        }
        if (request == "host:pair:482913:192.168.1.20:37123") {
            connection.write("OKAY" + FakeConnection::prefixed(
                                          "Successfully paired to 192.168.1.20:37123 [guid=adb-R58M123ABC-AbCdEf]"));
        } else if (request == "host:connect:192.168.1.20:41555") {
            connection.write("OKAY" + FakeConnection::prefixed("connected to 192.168.1.20:41555"));
        } else if (request == "host:connect:192.168.1.21:5555") {
            connection.write("OKAY" + FakeConnection::prefixed(
                                          "failed to connect to '192.168.1.21:5555': Connection refused"));
        } else {
            connection.write("FAIL" + FakeConnection::prefixed("unexpected request"));
        }
    }

    [[nodiscard]] std::vector<std::string> requests() const
    {
        const std::lock_guard lock(m_mutex);
        return m_requests;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_requests;
};

QString addressAt(const QVariantList& recent, qsizetype index)
{
    return recent.at(index).toMap().value(QStringLiteral("address")).toString();
}

}

TEST_CASE("UI-005, ADB-007: a phone paired with its code and connected by address is remembered for a quick connect")
{
    const WirelessApplication application;
    const TemporaryDirectory directory;
    AdbAnswers answers;
    FakeAdbServer adb([&answers](FakeConnection& connection) { answers.serve(connection); });
    {
        AppSettings settings(directory.path());
        WirelessController wireless(AdbClient(adb.port()), settings);

        wireless.pair(QStringLiteral(" 192.168.1.20:37123 "), QStringLiteral("482913"));
        CHECK(wireless.pairingAddress() == QStringLiteral("192.168.1.20:37123"));
        REQUIRE(waitUntil([&] { return wireless.pairingAddress().isEmpty(); }));
        CHECK(wireless.pairedAddress() == QStringLiteral("192.168.1.20:37123"));
        CHECK(wireless.pairProblem().isEmpty());

        wireless.connectTo(QStringLiteral("192.168.1.20:41555"));
        CHECK(wireless.pendingAddress() == QStringLiteral("192.168.1.20:41555"));
        REQUIRE(waitUntil([&] { return wireless.pendingAddress().isEmpty(); }));
        CHECK(wireless.connectedAddress() == QStringLiteral("192.168.1.20:41555"));
        CHECK(wireless.problem().isEmpty());
        const QVariantList recent = settings.recentConnections();
        REQUIRE(recent.size() == 1);
        CHECK(addressAt(recent, 0) == QStringLiteral("192.168.1.20:41555"));

        wireless.connectTo(QStringLiteral("192.168.1.21:5555"));
        REQUIRE(waitUntil([&] { return wireless.pendingAddress().isEmpty(); }));
        CHECK(wireless.connectedAddress().isEmpty());
        CHECK_FALSE(wireless.problem().isEmpty());
        CHECK_FALSE(wireless.action().isEmpty());
        CHECK(wireless.detail().contains(QStringLiteral("Connection refused")));
        CHECK(settings.recentConnections().size() == 1);
    }
    CHECK(answers.requests() == std::vector<std::string>{"host:pair:482913:192.168.1.20:37123",
                                                         "host:connect:192.168.1.20:41555",
                                                         "host:connect:192.168.1.21:5555"});

    AppSettings reopened(directory.path());
    const QVariantList recent = reopened.recentConnections();
    REQUIRE(recent.size() == 1);
    CHECK(addressAt(recent, 0) == QStringLiteral("192.168.1.20:41555"));
}

TEST_CASE("2026-11-14: the phone is looked for on this computer's own networks, at most a /24 each, never on itself")
{
    using gs::app::LanAddress;
    using gs::app::lanHosts;
    constexpr auto ip = [](std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
        return (a << 24) | (b << 16) | (c << 8) | d;
    };

    const std::array home{LanAddress{.ip = ip(192, 168, 31, 68), .prefix = 24}};
    const std::vector<std::uint32_t> hosts = lanHosts(home, 1024);
    REQUIRE(hosts.size() == 253);
    CHECK(hosts.front() == ip(192, 168, 31, 1));
    CHECK(hosts.back() == ip(192, 168, 31, 254));
    CHECK(std::ranges::find(hosts, ip(192, 168, 31, 68)) == hosts.end());

    const std::array wide{LanAddress{.ip = ip(10, 1, 2, 3), .prefix = 16}};
    const std::vector<std::uint32_t> narrowed = lanHosts(wide, 1024);
    REQUIRE(narrowed.size() == 253);
    CHECK(narrowed.front() == ip(10, 1, 2, 1));

    const std::array small{LanAddress{.ip = ip(192, 168, 7, 9), .prefix = 30}};
    CHECK(lanHosts(small, 1024) == std::vector<std::uint32_t>{ip(192, 168, 7, 10)});

    const std::array skipped{LanAddress{.ip = ip(127, 0, 0, 1), .prefix = 8},
                             LanAddress{.ip = ip(169, 254, 3, 4), .prefix = 16},
                             LanAddress{.ip = ip(192, 168, 1, 5), .prefix = 32}};
    CHECK(lanHosts(skipped, 1024).empty());

    const std::array twice{LanAddress{.ip = ip(192, 168, 31, 68), .prefix = 24},
                           LanAddress{.ip = ip(192, 168, 31, 70), .prefix = 24}};
    const std::vector<std::uint32_t> shared = lanHosts(twice, 1024);
    CHECK(shared.size() == 252);
    CHECK(std::ranges::find(shared, ip(192, 168, 31, 70)) == shared.end());

    CHECK(lanHosts(home, 10).size() == 10);
}
