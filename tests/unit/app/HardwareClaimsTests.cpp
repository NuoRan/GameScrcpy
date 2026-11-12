#include "HardwareClaims.h"

#include <doctest/doctest.h>

#include <QObject>
#include <QString>

#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace gs;
using namespace std::chrono_literals;

namespace {

app::ClaimHolder holder(std::string id, QString name,
                        std::function<bool(const std::string&, const QString&)> wanted = {},
                        std::string extension = {})
{
    return app::ClaimHolder{std::move(id), std::move(name), std::move(wanted), std::move(extension)};
}

exthost::ClaimResult claimElsewhere(app::HardwareClaims& claims, const std::string& resource,
                                    const app::ClaimHolder& by, bool wait)
{
    exthost::ClaimResult result = exthost::ClaimResult::Unsupported;
    std::thread([&] { result = claims.claim(resource, by, wait); }).join();
    return result;
}

}

TEST_CASE("ADR-0112: hardware has one holder at a time, and its name does not depend on case")
{
    app::HardwareClaims claims;
    int changes = 0;
    QObject::connect(&claims, &app::HardwareClaims::changed, [&changes] { ++changes; });
    const app::ClaimHolder board = holder("native:board#1", QStringLiteral("Board"));
    const app::ClaimHolder panel = holder("logic:7", QStringLiteral("Panel"));

    CHECK(app::HardwareClaims::instance() == &claims);
    CHECK(claims.claim("serial:COM9", board, false) == exthost::ClaimResult::Claimed);
    CHECK(changes == 1);
    CHECK(claims.claim("SERIAL:com9", board, false) == exthost::ClaimResult::Claimed);
    CHECK(changes == 1);
    CHECK(claims.claim("serial:com9", panel, false) == exthost::ClaimResult::Busy);
    CHECK(claims.holderName("Serial:Com9") == "Board");
    CHECK(claims.holds("serial:COM9", "native:board#1"));
    CHECK_FALSE(claims.holds("serial:COM9", "logic:7"));
    claims.release("serial:COM9", "logic:7");
    CHECK(claims.holds("serial:COM9", "native:board#1"));
    claims.release("serial:com9", "native:board#1");
    CHECK(claims.holderName("serial:COM9").isEmpty());
    CHECK(changes == 2);
    CHECK(claims.claim("serial:COM9", panel, false) == exthost::ClaimResult::Claimed);

    CHECK(claims.claim("hid:\\\\?\\HID#VID_046D&PID_C52B#7&1", board, false) == exthost::ClaimResult::Claimed);
    CHECK(claims.claim("hid:hid#vid_046d&pid_c52b#7&1", panel, false) == exthost::ClaimResult::Busy);
    CHECK(claims.claim("usb:2341:0043:A1B2", board, false) == exthost::ClaimResult::Claimed);
    claims.releaseAll("native:board#1");
    CHECK(claims.holderName("usb:2341:0043:a1b2").isEmpty());
    CHECK(claims.holderName("hid:hid#vid_046d&pid_c52b#7&1").isEmpty());
    CHECK(claims.holderName("serial:COM9") == "Panel");

    for (const char* bad : {"COM9", "serial:", "tcp:127.0.0.1", "serial:COM\x01", ""}) {
        CAPTURE(bad);
        CHECK(claims.claim(bad, board, false) == exthost::ClaimResult::BadArgument);
    }
    CHECK(claims.claim("serial:" + std::string(300, 'x'), board, false) == exthost::ClaimResult::BadArgument);
    CHECK(claims.claim("serial:COM3", holder("", QStringLiteral("Nobody")), false) ==
          exthost::ClaimResult::BadArgument);
}

TEST_CASE("ADR-0112: a claim that waits asks the holder, which may let go within 3 seconds, and never waits on the "
          "main thread")
{
    app::HardwareClaims claims;
    std::mutex mutex;
    std::vector<std::string> asked;
    bool yields = false;
    const app::ClaimHolder board =
        holder("native:board#1", QStringLiteral("Board"), [&](const std::string& resource, const QString& by) {
            bool agrees = false;
            {
                const std::scoped_lock lock(mutex);
                asked.push_back(resource + " by " + by.toStdString());
                agrees = yields;
            }
            if (agrees) {
                claims.release(resource, "native:board#1");
            }
            return agrees;
        });
    const auto heard = [&] {
        const std::scoped_lock lock(mutex);
        return asked;
    };
    const app::ClaimHolder session = holder("app:aoa:1", QStringLiteral("GameScrcpy"));
    REQUIRE(claims.claim("serial:COM9", board, false) == exthost::ClaimResult::Claimed);

    CHECK(claims.claim("SERIAL:COM9", session, true) == exthost::ClaimResult::Busy);
    CHECK(claimElsewhere(claims, "serial:com9", session, false) == exthost::ClaimResult::Busy);
    CHECK(heard().empty());
    CHECK(claimElsewhere(claims, "serial:com9", session, true) == exthost::ClaimResult::Busy);
    CHECK(heard() == std::vector<std::string>{"serial:COM9 by GameScrcpy"});
    CHECK(claims.holderName("serial:COM9") == "Board");

    {
        const std::scoped_lock lock(mutex);
        yields = true;
    }
    CHECK(claimElsewhere(claims, "serial:com9", session, true) == exthost::ClaimResult::Claimed);
    CHECK(claims.holderName("serial:COM9") == "GameScrcpy");
    CHECK(heard().size() == 2);

    const app::ClaimHolder keeper =
        holder("logic:3", QStringLiteral("Keeper"), [](const std::string&, const QString&) { return true; });
    REQUIRE(claims.claim("ble:AA:BB:CC:DD:EE:FF", keeper, false) == exthost::ClaimResult::Claimed);
    const auto started = std::chrono::steady_clock::now();
    CHECK(claimElsewhere(claims, "ble:AA:BB:CC:DD:EE:FF", session, true) == exthost::ClaimResult::Busy);
    CHECK(std::chrono::steady_clock::now() - started >= app::HardwareClaims::kYieldWait - 50ms);
    CHECK(claims.holderName("ble:AA:BB:CC:DD:EE:FF") == "Keeper");
}

TEST_CASE("ADR-0112: a claim made for later answers at once when it can, and otherwise from its own thread")
{
    app::HardwareClaims claims;
    std::jthread letting;
    const app::ClaimHolder board =
        holder("native:board#1", QStringLiteral("Board"), [&claims, &letting](const std::string& resource, const QString&) {
            letting = std::jthread([&claims, resource] { claims.release(resource, "native:board#1"); });
            return true;
        });
    REQUIRE(claims.claim("serial:COM9", board, false) == exthost::ClaimResult::Claimed);

    std::optional<exthost::ClaimResult> atOnce;
    claims.claimLater("serial:COM3", holder("logic:1", QStringLiteral("Free")),
                      [&atOnce](exthost::ClaimResult result) { atOnce = result; });
    CHECK(atOnce == exthost::ClaimResult::Claimed);

    std::promise<exthost::ClaimResult> waited;
    std::future<exthost::ClaimResult> answer = waited.get_future();
    claims.claimLater("serial:COM9", holder("logic:2", QStringLiteral("Waiter")),
                      [&waited](exthost::ClaimResult result) { waited.set_value(result); });
    REQUIRE(answer.wait_for(10s) == std::future_status::ready);
    CHECK(answer.get() == exthost::ClaimResult::Claimed);
    CHECK(claims.holderName("serial:COM9") == "Waiter");
}

TEST_CASE("ADR-0112: one extension has at most eight claims waiting at a time, and a further one is busy at once")
{
    std::vector<std::promise<exthost::ClaimResult>> promises(app::HardwareClaims::kMostWaiters);
    std::vector<std::future<exthost::ClaimResult>> answers;
    answers.reserve(promises.size());
    for (std::promise<exthost::ClaimResult>& promise : promises) {
        answers.push_back(promise.get_future());
    }
    std::promise<exthost::ClaimResult> otherPromise;
    std::future<exthost::ClaimResult> other = otherPromise.get_future();

    app::HardwareClaims claims;
    const app::ClaimHolder keeper = holder("native:gstest.loop#1", QStringLiteral("Loop"),
                                           [](const std::string&, const QString&) { return true; }, "gstest.loop");
    REQUIRE(claims.claim("serial:COM9", keeper, false) == exthost::ClaimResult::Claimed);
    const app::ClaimHolder elsewhere =
        holder("logic:77", QStringLiteral("Elsewhere"), [](const std::string&, const QString&) { return true; });
    REQUIRE(claims.claim("serial:COM7", elsewhere, false) == exthost::ClaimResult::Claimed);

    for (std::size_t index = 0; index < promises.size(); ++index) {
        claims.claimLater("serial:COM9",
                          holder("native:gstest.loop#" + std::to_string(index + 2), QStringLiteral("Wait"), {},
                                 "gstest.loop"),
                          [&promises, index](exthost::ClaimResult result) { promises[index].set_value(result); });
    }

    std::optional<exthost::ClaimResult> refused;
    const auto started = std::chrono::steady_clock::now();
    claims.claimLater("serial:COM9", holder("native:gstest.loop#99", QStringLiteral("Wait"), {}, "gstest.loop"),
                      [&refused](exthost::ClaimResult result) { refused = result; });
    CHECK(std::chrono::steady_clock::now() - started < 1s);
    CHECK(refused == exthost::ClaimResult::Busy);

    claims.claimLater("serial:COM7", holder("native:gstest.other#1", QStringLiteral("Other"), {}, "gstest.other"),
                      [&otherPromise](exthost::ClaimResult result) { otherPromise.set_value(result); });
    CHECK(other.wait_for(0s) != std::future_status::ready);

    claims.release("serial:COM9", "native:gstest.loop#1");
    std::size_t claimed = 0;
    for (std::future<exthost::ClaimResult>& answer : answers) {
        REQUIRE(answer.wait_for(10s) == std::future_status::ready);
        claimed += answer.get() == exthost::ClaimResult::Claimed ? 1 : 0;
    }
    CHECK(claimed == 1);

    claims.release("serial:COM7", "logic:77");
    REQUIRE(other.wait_for(10s) == std::future_status::ready);
    CHECK(other.get() == exthost::ClaimResult::Claimed);
}

TEST_CASE("ADR-0112: claims still waiting give up at once when GameScrcpy closes")
{
    std::promise<exthost::ClaimResult> waited;
    std::future<exthost::ClaimResult> answer = waited.get_future();
    const auto started = std::chrono::steady_clock::now();
    {
        app::HardwareClaims claims;
        const app::ClaimHolder keeper =
            holder("logic:1", QStringLiteral("Keeper"), [](const std::string&, const QString&) { return true; });
        REQUIRE(claims.claim("serial:COM9", keeper, false) == exthost::ClaimResult::Claimed);
        claims.claimLater("serial:COM9", holder("logic:2", QStringLiteral("Waiter")),
                          [&waited](exthost::ClaimResult result) { waited.set_value(result); });
    }
    CHECK(std::chrono::steady_clock::now() - started < 2s);
    REQUIRE(answer.wait_for(0s) == std::future_status::ready);
    CHECK(answer.get() == exthost::ClaimResult::Busy);
    CHECK(app::HardwareClaims::instance() == nullptr);
}
