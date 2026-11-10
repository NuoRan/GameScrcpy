#include "AppActions.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using gs::app::AppActions;
using gs::app::PackageAction;
using gs::app::PhoneAccess;
using gs::app::PushProgress;
namespace adb = gs::adb;

using namespace std::chrono_literals;

namespace {

class Application
{
public:
    Application()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_app = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    char m_name[5] = "test";
    std::array<char*, 2> m_argv{m_name, nullptr};
    std::unique_ptr<QCoreApplication> m_app;
};

bool waitFor(const std::function<bool()>& condition)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

adb::ShellResult said(std::string out, int exitCode = 0, std::string err = {})
{
    adb::ShellResult result;
    result.exitCode = exitCode;
    result.out = std::move(out);
    result.err = std::move(err);
    return result;
}

adb::AdbResult<adb::ShellResult> unreachable()
{
    return gs::makeUnexpected(adb::AdbError{adb::AdbErrorKind::Io, "device offline"});
}

std::filesystem::path writeApk(const QTemporaryDir& dir, const QString& name, qsizetype size)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(QByteArray(size, 'a')) == size);
    file.close();
    return std::filesystem::path(path.toStdWString());
}

QString stateOf(const QVariantMap& update)
{
    return update.value(QStringLiteral("state")).toString();
}

QString messageOf(const QVariantMap& update)
{
    return update.value(QStringLiteral("message")).toString();
}

struct Pushed {
    std::string serial;
    std::filesystem::path local;
    std::string remote;
};

struct FakePhone {
    std::vector<std::string> commands;
    std::vector<Pushed> pushes;
    std::function<adb::AdbResult<adb::ShellResult>(const std::string& command)> answer =
        [](const std::string& command) -> adb::AdbResult<adb::ShellResult> {
        return said(command.starts_with("pm ") ? "Success\n" : "");
    };
    bool pushFails = false;
    std::uint64_t chunk = 100;

    PhoneAccess access()
    {
        return {.run = [this](const std::string& serial, const std::string& command, const std::stop_token&) {
                    commands.push_back(serial + ": " + command);
                    return answer(command);
                },
                .push = [this](const std::string& serial, const std::filesystem::path& local, const std::string& remote,
                               PushProgress progress, const std::stop_token&) -> adb::AdbResult<void> {
                    pushes.push_back({.serial = serial, .local = local, .remote = remote});
                    if (pushFails) {
                        return gs::makeUnexpected(adb::AdbError{adb::AdbErrorKind::Rejected, "no space left"});
                    }
                    std::error_code error;
                    std::uint64_t size = std::filesystem::file_size(local, error);
                    if (error) {
                        size = 0;
                    }
                    for (std::uint64_t done = 0; done < size;) {
                        done = std::min(size, done + chunk);
                        if (progress) {
                            progress(done, size);
                        }
                    }
                    return {};
                }};
    }
};

struct GatedPhone {
    std::mutex mutex;
    std::condition_variable_any opened;
    bool open = false;
    std::vector<std::string> log;

    PhoneAccess access()
    {
        return {.run = [this](const std::string& serial, const std::string& command,
                              const std::stop_token& stop) -> adb::AdbResult<adb::ShellResult> {
                    std::unique_lock lock(mutex);
                    log.push_back(serial + ": " + command);
                    if (serial == "A" && command.starts_with("am force-stop") &&
                        !opened.wait(lock, stop, [this] { return open; })) {
                        return gs::makeUnexpected(adb::AdbError{adb::AdbErrorKind::Io, "stopped"});
                    }
                    return said(command.starts_with("pm ") ? "Success\n" : "");
                },
                .push = [](const std::string&, const std::filesystem::path&, const std::string&, PushProgress progress,
                           const std::stop_token&) -> adb::AdbResult<void> {
                    if (progress) {
                        progress(10, 10);
                    }
                    return {};
                }};
    }

    void release()
    {
        {
            const std::lock_guard lock(mutex);
            open = true;
        }
        opened.notify_all();
    }

    [[nodiscard]] std::vector<std::string> seen()
    {
        const std::lock_guard lock(mutex);
        return log;
    }
};

bool contains(const std::vector<std::string>& lines, const std::string& line)
{
    return std::ranges::find(lines, line) != lines.end();
}

}

TEST_CASE("package names are checked before they reach a shell command")
{
    CHECK(gs::app::validPackageName(QStringLiteral("com.example.game_2")));
    CHECK(gs::app::validPackageName(QStringLiteral("A1")));
    CHECK(gs::app::validPackageName(QString(255, u'a')));
    for (const QString& bad :
         {QString(), QStringLiteral("com.example game"), QStringLiteral("com.example;reboot"),
          QStringLiteral("com.example&&reboot"), QStringLiteral("$(reboot)"), QStringLiteral("a/b"),
          QStringLiteral("com.exam-ple"), QStringLiteral("'quoted'"), QStringLiteral("com.example\nreboot"),
          QStringLiteral("com.\u4f8b\u5b50"), QString(256, u'a')}) {
        CHECK_FALSE_MESSAGE(gs::app::validPackageName(bad), bad.toStdString());
        for (const PackageAction action : gs::app::kPackageActions) {
            CHECK_FALSE(gs::app::packageCommand(action, bad).has_value());
        }
    }
    const QString game = QStringLiteral("com.example.game");
    CHECK(gs::app::packageCommand(PackageAction::ForceStop, game).value_or("") == "am force-stop com.example.game");
    CHECK(gs::app::packageCommand(PackageAction::Disable, game).value_or("") ==
          "pm disable-user --user 0 com.example.game");
    CHECK(gs::app::packageCommand(PackageAction::Enable, game).value_or("") == "pm enable com.example.game");
    CHECK(gs::app::packageCommand(PackageAction::ClearData, game).value_or("") == "pm clear com.example.game");
    CHECK(gs::app::packageCommand(PackageAction::Uninstall, game).value_or("") == "pm uninstall com.example.game");
}

TEST_CASE("an install goes through its own temporary file, which is removed afterwards")
{
    CHECK(gs::app::remoteApkPath(7) == "/data/local/tmp/gs-install-7.apk");
    CHECK(gs::app::installCommand(gs::app::remoteApkPath(7)) == "pm install -r -t /data/local/tmp/gs-install-7.apk");
    CHECK(gs::app::removeCommand(gs::app::remoteApkPath(7)) == "rm -f /data/local/tmp/gs-install-7.apk");
    CHECK(gs::app::remoteApkPath(8) != gs::app::remoteApkPath(7));
}

TEST_CASE("only Success and Failure [CODE] are read from what pm prints")
{
    CHECK(gs::app::parsePmResult("Success\n", "").ok);
    CHECK(gs::app::parsePmResult("Performing Streamed Install\r\nSuccess\r\n", "").ok);
    const auto downgrade = gs::app::parsePmResult(
        "", "Failure [INSTALL_FAILED_VERSION_DOWNGRADE: Downgrade detected: Update version code 3 is older than "
            "current 4]\n");
    CHECK_FALSE(downgrade.ok);
    CHECK(downgrade.code == QStringLiteral("INSTALL_FAILED_VERSION_DOWNGRADE"));
    CHECK(gs::app::parsePmResult("Failure [INSTALL_FAILED_ALREADY_EXISTS]", "").code ==
          QStringLiteral("INSTALL_FAILED_ALREADY_EXISTS"));
    CHECK(gs::app::parsePmResult("Failure [not installed for 0]\n", "").code == QStringLiteral("not installed for 0"));
    const auto mixed = gs::app::parsePmResult("Success\nFailure [INSTALL_FAILED_INVALID_APK]\n", "");
    CHECK_FALSE(mixed.ok);
    CHECK(mixed.code == QStringLiteral("INSTALL_FAILED_INVALID_APK"));
    for (const char* other : {"", "Successful\n", "Error: Unable to open file: /data/local/tmp/x.apk\n", "Failed\n"}) {
        const auto result = gs::app::parsePmResult(other, "");
        CHECK_FALSE_MESSAGE(result.ok, other);
        CHECK(result.code.isEmpty());
    }
}

TEST_CASE("each mapped failure code reads as a short reason and any other code is shown as it is")
{
    using gs::app::failureReason;
    CHECK(failureReason(QStringLiteral("INSTALL_FAILED_INSUFFICIENT_STORAGE")) ==
          AppActions::tr("Not enough storage on the phone"));
    const QString signature = AppActions::tr("The signature does not match the installed app");
    CHECK(failureReason(QStringLiteral("INSTALL_FAILED_UPDATE_INCOMPATIBLE")) == signature);
    CHECK(failureReason(QStringLiteral("INSTALL_FAILED_SHARED_USER_INCOMPATIBLE")) == signature);
    CHECK(failureReason(QStringLiteral("INSTALL_PARSE_FAILED_INCONSISTENT_CERTIFICATES")) == signature);
    CHECK(failureReason(QStringLiteral("INSTALL_FAILED_VERSION_DOWNGRADE")) ==
          AppActions::tr("A newer version is already installed"));
    const QString invalid = AppActions::tr("Not a valid APK");
    CHECK(failureReason(QStringLiteral("INSTALL_FAILED_INVALID_APK")) == invalid);
    CHECK(failureReason(QStringLiteral("INSTALL_PARSE_FAILED_NOT_APK")) == invalid);
    CHECK(failureReason(QStringLiteral("INSTALL_PARSE_FAILED_NO_CERTIFICATES")) == invalid);
    CHECK(failureReason(QStringLiteral("INSTALL_FAILED_ALREADY_EXISTS")) == AppActions::tr("The app is already installed"));
    CHECK(failureReason(QStringLiteral("INSTALL_FAILED_USER_RESTRICTED")) ==
          AppActions::tr("The phone does not allow installs from a computer"));
    CHECK(failureReason(QStringLiteral("DELETE_FAILED_USER_RESTRICTED")) ==
          AppActions::tr("The phone does not allow removing this app"));
    CHECK(failureReason(QStringLiteral("INSTALL_FAILED_NO_MATCHING_ABIS")) ==
          QStringLiteral("INSTALL_FAILED_NO_MATCHING_ABIS"));
    CHECK(failureReason(QString()) == AppActions::tr("The phone did not confirm it"));
}

TEST_CASE("an action succeeds by its exit code, or by pm's Success where pm prints one")
{
    using gs::app::judgeAction;
    CHECK(judgeAction(PackageAction::ForceStop, said("")).ok);
    CHECK(judgeAction(PackageAction::Disable, said("Package com.x new state: disabled-user\n")).ok);
    CHECK(judgeAction(PackageAction::Enable, said("Package com.x new state: enabled\n")).ok);
    CHECK_FALSE(judgeAction(PackageAction::Disable, said("", 255, "Exception occurred while executing 'disable-user':\n")).ok);
    CHECK(judgeAction(PackageAction::Uninstall, said("Success\n")).ok);
    const auto refused = judgeAction(PackageAction::Uninstall, said("Failure [DELETE_FAILED_INTERNAL_ERROR]\n", 1));
    CHECK_FALSE(refused.ok);
    CHECK(refused.code == QStringLiteral("DELETE_FAILED_INTERNAL_ERROR"));
    CHECK(judgeAction(PackageAction::ClearData, said("Success\n")).ok);
    CHECK_FALSE(judgeAction(PackageAction::ClearData, said("", 1, "Failed\n")).ok);
    CHECK_FALSE(judgeAction(PackageAction::ClearData, said("")).ok);
}

TEST_CASE("system apps can only be disabled or enabled")
{
    using gs::app::actionAllowed;
    for (const bool enabled : {true, false}) {
        CHECK_FALSE(actionAllowed(PackageAction::ForceStop, true, enabled));
        CHECK_FALSE(actionAllowed(PackageAction::ClearData, true, enabled));
        CHECK_FALSE(actionAllowed(PackageAction::Uninstall, true, enabled));
        CHECK(actionAllowed(PackageAction::ForceStop, false, enabled) == enabled);
        CHECK(actionAllowed(PackageAction::ClearData, false, enabled));
        CHECK(actionAllowed(PackageAction::Uninstall, false, enabled));
        for (const bool system : {true, false}) {
            CHECK(actionAllowed(PackageAction::Disable, system, enabled) == enabled);
            CHECK(actionAllowed(PackageAction::Enable, system, enabled) == !enabled);
        }
    }
    for (const PackageAction action : gs::app::kPackageActions) {
        CHECK(gs::app::packageActionNamed(gs::app::packageActionName(action)) == action);
    }
    CHECK_FALSE(gs::app::packageActionNamed(QStringLiteral("reboot")).has_value());
}

TEST_CASE("a package action runs one command and turns the answer into a reason")
{
    FakePhone phone;
    const QString game = QStringLiteral("com.example.game");
    const auto removed = gs::app::runPackageAction(phone.access(), QStringLiteral("A"), PackageAction::Uninstall, game, {});
    CHECK(removed.ok);
    CHECK(removed.reason.isEmpty());
    CHECK(phone.commands == std::vector<std::string>{"A: pm uninstall com.example.game"});

    phone.answer = [](const std::string&) -> adb::AdbResult<adb::ShellResult> {
        return said("Failure [DELETE_FAILED_USER_RESTRICTED]\n", 1);
    };
    const auto refused = gs::app::runPackageAction(phone.access(), QStringLiteral("A"), PackageAction::Uninstall, game, {});
    CHECK_FALSE(refused.ok);
    CHECK(refused.reason == AppActions::tr("The phone does not allow removing this app"));

    phone.answer = [](const std::string&) { return unreachable(); };
    const auto lost = gs::app::runPackageAction(phone.access(), QStringLiteral("A"), PackageAction::ForceStop, game, {});
    CHECK_FALSE(lost.ok);
    CHECK(lost.reason == AppActions::tr("The phone did not answer"));
    CHECK(lost.detail == QStringLiteral("device offline"));

    const std::size_t sent = phone.commands.size();
    const auto invalid = gs::app::runPackageAction(phone.access(), QStringLiteral("A"), PackageAction::ClearData,
                                                   QStringLiteral("com.example;reboot"), {});
    CHECK_FALSE(invalid.ok);
    CHECK(phone.commands.size() == sent);
}

TEST_CASE("an install copies the APK, installs it, removes the copy and reports each step")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path apk = writeApk(dir, QStringLiteral("game.apk"), 1000);
    FakePhone phone;
    std::vector<QVariantMap> updates;
    gs::app::runInstall(
        phone.access(), {.id = 5, .serial = QStringLiteral("A"), .apk = apk},
        [&updates](const QVariantMap& update) { updates.push_back(update); }, {});

    REQUIRE(phone.pushes.size() == 1);
    CHECK(phone.pushes[0].serial == "A");
    CHECK(phone.pushes[0].local == apk);
    CHECK(phone.pushes[0].remote == "/data/local/tmp/gs-install-5.apk");
    CHECK(phone.commands == std::vector<std::string>{"A: pm install -r -t /data/local/tmp/gs-install-5.apk",
                                                     "A: rm -f /data/local/tmp/gs-install-5.apk"});
    REQUIRE(updates.size() >= 3);
    QStringList states;
    double copied = -1;
    for (const QVariantMap& update : updates) {
        CHECK(update.value(QStringLiteral("id")).toLongLong() == 5);
        CHECK(update.value(QStringLiteral("serial")).toString() == QStringLiteral("A"));
        CHECK(update.value(QStringLiteral("name")).toString() == QStringLiteral("game.apk"));
        CHECK(update.value(QStringLiteral("total")).toDouble() == 1000.0);
        const QString state = stateOf(update);
        if (states.isEmpty() || states.back() != state) {
            states.append(state);
        }
        if (state == QStringLiteral("copying")) {
            CHECK(update.value(QStringLiteral("done")).toDouble() >= copied);
            copied = update.value(QStringLiteral("done")).toDouble();
        }
    }
    CHECK(states == QStringList{QStringLiteral("copying"), QStringLiteral("installing"), QStringLiteral("done")});
    CHECK(updates.front().value(QStringLiteral("done")).toDouble() == 0.0);
    CHECK(copied == 1000.0);
    CHECK(messageOf(updates.back()).isEmpty());
}

TEST_CASE("a failed install names the reason and still removes the copy")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path apk = writeApk(dir, QStringLiteral("game.apk"), 300);
    const auto install = [&apk](FakePhone& phone) {
        std::vector<QVariantMap> updates;
        gs::app::runInstall(
            phone.access(), {.id = 9, .serial = QStringLiteral("A"), .apk = apk},
            [&updates](const QVariantMap& update) { updates.push_back(update); }, {});
        REQUIRE_FALSE(updates.empty());
        CHECK(stateOf(updates.back()) == QStringLiteral("failed"));
        return messageOf(updates.back());
    };
    const std::string removal = "A: rm -f /data/local/tmp/gs-install-9.apk";

    SUBCASE("pm refuses it")
    {
        FakePhone phone;
        phone.answer = [](const std::string& command) -> adb::AdbResult<adb::ShellResult> {
            if (command.starts_with("pm install")) {
                return said("Failure [INSTALL_FAILED_VERSION_DOWNGRADE]\n", 1);
            }
            return said("");
        };
        CHECK(install(phone) == AppActions::tr("A newer version is already installed"));
        REQUIRE_FALSE(phone.commands.empty());
        CHECK(phone.commands.back() == removal);
    }
    SUBCASE("pm answers with neither Success nor Failure")
    {
        FakePhone phone;
        phone.answer = [](const std::string& command) -> adb::AdbResult<adb::ShellResult> {
            return said(command.starts_with("pm install") ? "Error: Unable to open file\n" : "", 0);
        };
        CHECK(install(phone) == AppActions::tr("The phone did not confirm it"));
        CHECK(phone.commands.back() == removal);
    }
    SUBCASE("the copy fails")
    {
        FakePhone phone;
        phone.pushFails = true;
        CHECK(install(phone) == AppActions::tr("The APK could not be copied to the phone"));
        CHECK(phone.commands == std::vector<std::string>{removal});
    }
    SUBCASE("the phone stops answering")
    {
        FakePhone phone;
        phone.answer = [](const std::string& command) {
            return command.starts_with("pm install") ? unreachable() : adb::AdbResult<adb::ShellResult>(said(""));
        };
        CHECK(install(phone) == AppActions::tr("The phone did not finish installing"));
        CHECK(phone.commands.back() == removal);
    }
}

TEST_CASE("a missing APK fails before anything reaches the phone")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    FakePhone phone;
    std::vector<QVariantMap> updates;
    gs::app::runInstall(
        phone.access(),
        {.id = 3, .serial = QStringLiteral("A"), .apk = std::filesystem::path(dir.filePath(QStringLiteral("missing.apk")).toStdWString())},
        [&updates](const QVariantMap& update) { updates.push_back(update); }, {});
    REQUIRE(updates.size() == 1);
    CHECK(stateOf(updates[0]) == QStringLiteral("failed"));
    CHECK(messageOf(updates[0]) == AppActions::tr("The APK file cannot be read"));
    CHECK(updates[0].value(QStringLiteral("name")).toString() == QStringLiteral("missing.apk"));
    CHECK(phone.pushes.empty());
    CHECK(phone.commands.empty());
}

TEST_CASE("jobs for one phone run one after another while other phones go ahead")
{
    Application application;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path apk = writeApk(dir, QStringLiteral("notes.apk"), 10);
    GatedPhone phone;
    AppActions actions(phone.access());
    struct Finished {
        quint64 id = 0;
        QString serial;
        QString package;
        bool ok = false;
    };
    std::vector<Finished> finished;
    std::vector<QVariantMap> installs;
    QObject::connect(&actions, &AppActions::actionFinished,
                     [&finished](quint64 id, const QString& serial, const QString& package, bool ok, const QString&) {
                         finished.push_back({.id = id, .serial = serial, .package = package, .ok = ok});
                     });
    QObject::connect(&actions, &AppActions::installUpdated,
                     [&installs](const QVariantMap& job) { installs.push_back(job); });

    CHECK(actions.act(QStringLiteral("A"), PackageAction::Uninstall, QStringLiteral("bad;name")) == 0);
    const quint64 first = actions.act(QStringLiteral("A"), PackageAction::ForceStop, QStringLiteral("com.one"));
    const quint64 second = actions.act(QStringLiteral("A"), PackageAction::Uninstall, QStringLiteral("com.two"));
    const quint64 other = actions.act(QStringLiteral("B"), PackageAction::ForceStop, QStringLiteral("com.three"));
    const quint64 install = actions.install(QStringLiteral("B"), QString::fromStdWString(apk.wstring()));
    CHECK(first != 0);
    CHECK(second > first);
    CHECK(other > second);
    CHECK(install > other);

    REQUIRE(waitFor([&finished, &installs] {
        return finished.size() == 1 && !installs.empty() && stateOf(installs.back()) == QStringLiteral("done");
    }));
    CHECK(finished[0].id == other);
    CHECK(finished[0].serial == QStringLiteral("B"));
    CHECK(finished[0].ok);
    CHECK(stateOf(installs.front()) == QStringLiteral("copying"));
    CHECK(installs.back().value(QStringLiteral("id")).toLongLong() == static_cast<qint64>(install));
    const std::vector<std::string> waiting = phone.seen();
    CHECK(contains(waiting, "A: am force-stop com.one"));
    CHECK_FALSE(contains(waiting, "A: pm uninstall com.two"));
    CHECK(contains(waiting, "B: pm install -r -t " + gs::app::remoteApkPath(install)));
    CHECK(contains(waiting, "B: rm -f " + gs::app::remoteApkPath(install)));

    phone.release();
    REQUIRE(waitFor([&finished, &actions] { return finished.size() == 3 && actions.idle(); }));
    CHECK(finished[1].id == first);
    CHECK(finished[2].id == second);
    CHECK(finished[2].package == QStringLiteral("com.two"));
    CHECK(finished[2].ok);
}

TEST_CASE("an install the phone cannot take fails without reaching the phone")
{
    Application application;
    FakePhone phone;
    AppActions actions(phone.access());
    std::vector<QVariantMap> updates;
    QObject::connect(&actions, &AppActions::installUpdated,
                     [&updates](const QVariantMap& job) { updates.push_back(job); });
    const quint64 id = actions.refuse(QStringLiteral("gs-demo"), QStringLiteral("C:/apks/game.apk"),
                                      QStringLiteral("This connection cannot install apps"));
    CHECK(id != 0);
    CHECK(updates.empty());
    REQUIRE(waitFor([&updates] { return !updates.empty(); }));
    REQUIRE(updates.size() == 1);
    CHECK(updates[0].value(QStringLiteral("id")).toLongLong() == static_cast<qint64>(id));
    CHECK(updates[0].value(QStringLiteral("serial")).toString() == QStringLiteral("gs-demo"));
    CHECK(updates[0].value(QStringLiteral("name")).toString() == QStringLiteral("game.apk"));
    CHECK(stateOf(updates[0]) == QStringLiteral("failed"));
    CHECK(messageOf(updates[0]) == QStringLiteral("This connection cannot install apps"));
    CHECK(phone.commands.empty());
    CHECK(phone.pushes.empty());
}

TEST_CASE("closing stops a job that is waiting on the phone")
{
    Application application;
    GatedPhone phone;
    bool reported = false;
    {
        AppActions actions(phone.access());
        QObject::connect(&actions, &AppActions::actionFinished,
                         [&reported](quint64, const QString&, const QString&, bool, const QString&) { reported = true; });
        REQUIRE(actions.act(QStringLiteral("A"), PackageAction::ForceStop, QStringLiteral("com.one")) != 0);
        REQUIRE(waitFor([&phone] { return !phone.seen().empty(); }));
    }
    QCoreApplication::processEvents();
    CHECK_FALSE(reported);
}
