#include "unit/adb/FakeAdbServer.h"

#include "AppSettings.h"
#include "WirelessController.h"

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

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
