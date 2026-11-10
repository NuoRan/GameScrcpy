#include "CompanionLink.h"
#include "Settings.h"

#include "unit/adb/FakeAdbServer.h"

#include <gs/platform/Crypto.h>
#include <gs/protocol/Codec.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace std::chrono_literals;
using gs::adb::AdbClient;
using gs::adb::test::FakeAdbServer;
using gs::adb::test::FakeConnection;
using gs::app::CompanionApk;
using gs::app::CompanionEvent;
using gs::app::CompanionLink;
using gs::app::CompanionNotice;
using gs::app::CompanionTiming;
namespace protocol = gs::protocol;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::string_view kSerial = "R58M123ABC";
constexpr std::string_view kSdk = "getprop ro.build.version.sdk";
constexpr std::string_view kListPackages = "pm list packages --show-versioncode com.gamescrcpy.companion";
constexpr std::string_view kPush = "push /data/local/tmp/gs-companion.apk,33188";
constexpr std::string_view kInstall = "pm install -r /data/local/tmp/gs-companion.apk";
constexpr std::string_view kRemove = "rm -f /data/local/tmp/gs-companion.apk";
constexpr std::string_view kAppopsSet = "appops set com.gamescrcpy.companion SYSTEM_ALERT_WINDOW allow";
constexpr std::string_view kAppopsGet = "appops get com.gamescrcpy.companion SYSTEM_ALERT_WINDOW";
constexpr std::string_view kNotifications = "pm grant com.gamescrcpy.companion android.permission.POST_NOTIFICATIONS";
constexpr std::string_view kStart =
    "am start-foreground-service -n com.gamescrcpy.companion/.CompanionService --es token ";
constexpr std::string_view kReady = "am broadcast -n com.gamescrcpy.companion/.ReadyReceiver";
constexpr std::string_view kPermissionPage =
    "am start -a android.settings.action.MANAGE_OVERLAY_PERMISSION -d package:com.gamescrcpy.companion";
constexpr std::string_view kStop = "am stop-service -n com.gamescrcpy.companion/.CompanionService";
constexpr std::string_view kForward = "forward";
constexpr std::string_view kCurrent = "package:com.gamescrcpy.companion versionCode:2\n";
constexpr std::string_view kCurrentIme = "settings get secure default_input_method";
constexpr std::string_view kEnabledImes = "ime list -s";
constexpr std::string_view kImeEnable = "ime enable com.gamescrcpy.companion/.CompanionIme";
constexpr std::string_view kImeSet = "ime set com.gamescrcpy.companion/.CompanionIme";
constexpr std::string_view kImeSettings = "am start -a android.settings.INPUT_METHOD_SETTINGS";
constexpr std::string_view kPhoneIme = "com.example.keyboard/.LatinIme";

std::string le32(std::uint32_t value)
{
    std::string bytes(4, '\0');
    for (int i = 0; i < 4; ++i) {
        bytes[static_cast<std::size_t>(i)] = static_cast<char>(value >> (8 * i));
    }
    return bytes;
}

std::uint32_t readLe32(FakeConnection& c)
{
    const std::string bytes = c.read(4);
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (8 * i);
    }
    return value;
}

std::string shellPacket(char id, std::string_view data)
{
    return std::string(1, id) + le32(static_cast<std::uint32_t>(data.size())) + std::string(data);
}

bool contains(const std::vector<std::string>& commands, std::string_view prefix)
{
    return std::ranges::any_of(commands, [prefix](const std::string& command) { return command.starts_with(prefix); });
}

std::shared_ptr<const CompanionApk> apkOf(std::optional<std::int64_t> version)
{
    auto apk = std::make_shared<CompanionApk>();
    apk->bytes.resize(70'000);
    for (std::size_t i = 0; i < apk->bytes.size(); ++i) {
        apk->bytes[i] = static_cast<std::uint8_t>(i * 7 + 3);
    }
    apk->versionCode = version;
    return apk;
}

class Notices
{
public:
    void add(const CompanionNotice& notice)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_seen.push_back(notice);
        }
        m_changed.notify_all();
    }

    std::optional<CompanionNotice> waitFor(CompanionEvent event, std::chrono::milliseconds timeout = 10s)
    {
        std::unique_lock lock(m_mutex);
        std::optional<CompanionNotice> found;
        m_changed.wait_for(lock, timeout, [&] {
            const auto seen = std::ranges::find(m_seen, event, &CompanionNotice::event);
            if (seen == m_seen.end()) {
                return false;
            }
            found = *seen;
            return true;
        });
        return found;
    }

    [[nodiscard]] std::vector<CompanionEvent> events() const
    {
        const std::lock_guard lock(m_mutex);
        std::vector<CompanionEvent> result;
        for (const CompanionNotice& notice : m_seen) {
            result.push_back(notice.event);
        }
        return result;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<CompanionNotice> m_seen;
};

class FakePhone
{
public:
    explicit FakePhone(std::uint16_t companionPort)
        : m_companionPort(companionPort), m_server([this](FakeConnection& c) { handle(c); })
    {
        reply(std::string(kSdk), "34\n");
        reply(std::string(kInstall), "Success\n");
        reply(std::string(kAppopsGet), "SYSTEM_ALERT_WINDOW: allow; time=+2s114ms ago\n");
        reply(std::string(kStart), "Starting service: Intent { cmp=com.gamescrcpy.companion/.CompanionService "
                                   "(has extras) }\n");
        reply(std::string(kReady), "Broadcasting: Intent { flg=0x400000 cmp=com.gamescrcpy.companion/.ReadyReceiver }\n"
                                   "Broadcast completed: result=1\n");
        reply(std::string(kStop), "Stopping service: Intent { cmp=com.gamescrcpy.companion/.CompanionService }\n"
                                  "Service stopped\n");
    }

    [[nodiscard]] std::uint16_t port() const { return m_server.port(); }

    void reply(std::string command, std::string out)
    {
        const std::lock_guard lock(m_mutex);
        m_replies[std::move(command)] = std::move(out);
    }

    [[nodiscard]] std::vector<std::string> commands() const
    {
        const std::lock_guard lock(m_mutex);
        return m_commands;
    }

    [[nodiscard]] std::string token() const
    {
        const std::lock_guard lock(m_mutex);
        const auto start = std::ranges::find_if(m_commands, [](const std::string& c) { return c.starts_with(kStart); });
        return start == m_commands.end() ? std::string() : start->substr(kStart.size());
    }

    [[nodiscard]] std::string pushed() const
    {
        const std::lock_guard lock(m_mutex);
        return m_pushed;
    }

    void setCurrentIme(std::string ime)
    {
        const std::lock_guard lock(m_mutex);
        m_currentIme = std::move(ime);
    }

    [[nodiscard]] std::string currentIme() const
    {
        const std::lock_guard lock(m_mutex);
        return m_currentIme;
    }

    void setImeRefused(bool refused)
    {
        const std::lock_guard lock(m_mutex);
        m_imeRefused = refused;
    }

    bool waitForCommand(std::string_view command)
    {
        std::unique_lock lock(m_mutex);
        const std::string wanted(command);
        return m_recorded.wait_for(lock, 10s,
                                   [&] { return std::ranges::find(m_commands, wanted) != m_commands.end(); });
    }

private:
    void record(std::string command)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_commands.push_back(std::move(command));
        }
        m_recorded.notify_all();
    }

    std::optional<std::string> imeReply(const std::string& command)
    {
        const std::lock_guard lock(m_mutex);
        if (command == kCurrentIme) {
            return m_currentIme + "\n";
        }
        if (command.starts_with("ime enable ")) {
            const std::string id = command.substr(std::string_view("ime enable ").size());
            return m_imeRefused ? "Unknown input method " + id + " cannot be enabled for user #0\n"
                                : "Input method " + id + ": now enabled\n";
        }
        if (command.starts_with("ime set ")) {
            m_currentIme = command.substr(std::string_view("ime set ").size());
            return "Input method " + m_currentIme + " selected for user #0\n";
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string replyTo(const std::string& command) const
    {
        const std::lock_guard lock(m_mutex);
        std::string_view best;
        std::string out;
        for (const auto& [prefix, text] : m_replies) {
            if (command.starts_with(prefix) && prefix.size() > best.size()) {
                best = prefix;
                out = text;
            }
        }
        return out;
    }

    void handle(FakeConnection& c)
    {
        const std::string first = c.readRequest();
        const std::string hostSerial = "host-serial:" + std::string(kSerial) + ":";
        if (first.starts_with(hostSerial)) {
            const std::string service = first.substr(hostSerial.size());
            if (service == "forward:tcp:0;localabstract:gamescrcpy-companion") {
                record(std::string(kForward));
                c.write("OKAYOKAY" + FakeConnection::prefixed(std::to_string(m_companionPort)));
            } else if (service.starts_with("killforward:tcp:")) {
                record("killforward " + service.substr(std::string_view("killforward:tcp:").size()));
                c.write("OKAYOKAY");
            } else {
                record("unknown " + service);
                c.write("FAIL" + FakeConnection::prefixed("unknown service"));
            }
            return;
        }
        if (first != "host:transport:" + std::string(kSerial)) {
            record("unknown " + first);
            c.write("FAIL" + FakeConnection::prefixed("device not found"));
            return;
        }
        c.write("OKAY");
        const std::string service = c.readRequest();
        if (service == "sync:") {
            c.write("OKAY");
            static_cast<void>(c.read(4));
            const std::string target = c.read(readLe32(c));
            std::string data;
            for (;;) {
                const std::string id = c.read(4);
                const std::uint32_t value = readLe32(c);
                if (id != "DATA") {
                    break;
                }
                data += c.read(value);
            }
            {
                const std::lock_guard lock(m_mutex);
                m_pushed = std::move(data);
            }
            record("push " + target);
            c.write("OKAY" + le32(0));
            static_cast<void>(c.read(8));
            return;
        }
        constexpr std::string_view shell = "shell,v2,raw:";
        if (service.starts_with(shell)) {
            const std::string command = service.substr(shell.size());
            const std::string out = imeReply(command).value_or(replyTo(command));
            record(command);
            c.write("OKAY" + shellPacket(1, out) + shellPacket(3, std::string(1, '\0')));
            return;
        }
        record("unknown " + service);
        c.write("FAIL" + FakeConnection::prefixed("unknown service"));
    }

    const std::uint16_t m_companionPort;
    mutable std::mutex m_mutex;
    std::condition_variable m_recorded;
    std::map<std::string, std::string> m_replies;
    std::vector<std::string> m_commands;
    std::string m_pushed;
    std::string m_currentIme{kPhoneIme};
    bool m_imeRefused = false;
    FakeAdbServer m_server;
};

struct ReceivedCursor {
    protocol::CursorState state;
    Clock::time_point at;
};

class FakeCompanion
{
public:
    FakeCompanion() : m_server([this](FakeConnection& c) { serve(c); }) {}

    [[nodiscard]] std::uint16_t port() const { return m_server.port(); }

    void setPhone(const FakePhone* phone)
    {
        const std::lock_guard lock(m_mutex);
        m_phone = phone;
    }

    void setOverlay(protocol::OverlayStatus overlay)
    {
        const std::lock_guard lock(m_mutex);
        m_overlay = overlay;
    }

    void setRefusing(bool refusing)
    {
        const std::lock_guard lock(m_mutex);
        m_refusing = refusing;
    }

    void setSilent(bool silent)
    {
        const std::lock_guard lock(m_mutex);
        m_silent = silent;
    }

    template <typename M>
    void send(const M& message)
    {
        const std::lock_guard lock(m_mutex);
        if (m_connection != nullptr) {
            write(*m_connection, message);
        }
    }

    [[nodiscard]] std::string helloToken() const
    {
        const std::lock_guard lock(m_mutex);
        return m_helloToken;
    }

    [[nodiscard]] std::vector<ReceivedCursor> cursors() const
    {
        const std::lock_guard lock(m_mutex);
        return m_cursors;
    }

    bool waitForCursors(std::size_t count)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, 10s, [&] { return m_cursors.size() >= count; });
    }

    bool waitForPings(int count)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, 10s, [&] { return m_pings >= count; });
    }

    bool waitForGoodbye()
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, 10s, [&] { return m_goodbye; });
    }

    bool waitForOthers(std::size_t count, std::chrono::milliseconds timeout = 10s)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [&] { return m_others.size() >= count; });
    }

    [[nodiscard]] std::vector<protocol::ToServerMessage> others() const
    {
        const std::lock_guard lock(m_mutex);
        return m_others;
    }

private:
    template <typename M>
    static void write(FakeConnection& c, const M& message)
    {
        const auto frame = protocol::encodeFrame(message);
        if (frame) {
            c.write(std::span<const std::uint8_t>(*frame));
        }
    }

    static std::optional<protocol::ToServerMessage> readMessage(FakeConnection& c)
    {
        const std::string head = c.read(protocol::kFrameHeaderSize);
        if (head.size() != protocol::kFrameHeaderSize) {
            return std::nullopt;
        }
        std::uint32_t length = 0;
        for (std::size_t i = 2; i < protocol::kFrameHeaderSize; ++i) {
            length = (length << 8U) | static_cast<std::uint32_t>(static_cast<unsigned char>(head[i]));
        }
        const std::string payload = c.read(length);
        if (payload.size() != length) {
            return std::nullopt;
        }
        const std::string frame = head + payload;
        auto message = protocol::decodeToServerFrame(
            protocol::FrameContext::Companion,
            std::span(reinterpret_cast<const std::uint8_t*>(frame.data()), frame.size()));
        if (!message) {
            return std::nullopt;
        }
        return std::move(*message);
    }

    void serve(FakeConnection& c)
    {
        const auto first = readMessage(c);
        const auto* hello = first ? std::get_if<protocol::CompanionHello>(&*first) : nullptr;
        if (hello == nullptr) {
            return;
        }
        bool welcome = false;
        {
            const std::lock_guard lock(m_mutex);
            m_helloToken = gs::platform::toHex(hello->token);
            welcome = !m_refusing && m_phone != nullptr && m_helloToken == m_phone->token();
            if (welcome) {
                protocol::CompanionWelcome reply;
                reply.overlay = m_overlay;
                reply.width = 1080;
                reply.height = 2400;
                write(c, reply);
                m_connection = &c;
            } else {
                protocol::ServerGoodbye refusal;
                refusal.reason = protocol::ErrorCode::AuthenticationFailed;
                refusal.message = "wrong token";
                write(c, refusal);
            }
        }
        if (!welcome) {
            static_cast<void>(c.waitForClientClose());
            return;
        }
        for (;;) {
            const auto message = readMessage(c);
            if (!message) {
                break;
            }
            const std::lock_guard lock(m_mutex);
            if (const auto* cursor = std::get_if<protocol::CursorState>(&*message)) {
                m_cursors.push_back({*cursor, Clock::now()});
            } else if (const auto* ping = std::get_if<protocol::Ping>(&*message)) {
                ++m_pings;
                if (!m_silent) {
                    protocol::Pong pong;
                    pong.nonce = ping->nonce;
                    pong.clientTimeUs = ping->clientTimeUs;
                    pong.serverTimeUs = 1;
                    write(c, pong);
                }
            } else if (std::holds_alternative<protocol::Goodbye>(*message)) {
                m_goodbye = true;
                m_changed.notify_all();
                break;
            } else {
                m_others.push_back(*message);
            }
            m_changed.notify_all();
        }
        const std::lock_guard lock(m_mutex);
        m_connection = nullptr;
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    const FakePhone* m_phone = nullptr;
    protocol::OverlayStatus m_overlay = protocol::OverlayStatus::Granted;
    bool m_refusing = false;
    bool m_silent = false;
    FakeConnection* m_connection = nullptr;
    std::string m_helloToken;
    std::vector<ReceivedCursor> m_cursors;
    std::vector<protocol::ToServerMessage> m_others;
    int m_pings = 0;
    bool m_goodbye = false;
    FakeAdbServer m_server;
};

struct Rig {
    FakeCompanion companion;
    FakePhone phone{companion.port()};
    Notices notices;

    Rig() { companion.setPhone(&phone); }

    std::unique_ptr<CompanionLink> link(std::shared_ptr<const CompanionApk> apk, CompanionTiming timing = {})
    {
        return std::make_unique<CompanionLink>(
            AdbClient(phone.port()), std::string(kSerial), std::move(apk),
            [this](const CompanionNotice& notice) { notices.add(notice); }, timing);
    }

    [[nodiscard]] std::string unforward() const { return "killforward " + std::to_string(companion.port()); }
};

}

TEST_CASE("companion link: the installed and the bundled version, the overlay check and the ready reply are read exactly")
{
    using gs::app::broadcastResult;
    using gs::app::bundledVersionCode;
    using gs::app::installedVersionCode;
    using gs::app::overlayAllowed;

    CHECK(installedVersionCode("package:com.gamescrcpy.companion versionCode:3\n") == 3);
    CHECK(installedVersionCode("package:com.gamescrcpy.companion.debug versionCode:9\r\n"
                               "package:com.gamescrcpy.companion versionCode:2\r\n") == 2);
    CHECK_FALSE(installedVersionCode("").has_value());
    CHECK_FALSE(installedVersionCode("package:com.gamescrcpy.companion\n").has_value());
    CHECK_FALSE(installedVersionCode("package:com.gamescrcpy.companionx versionCode:4\n").has_value());
    CHECK_FALSE(installedVersionCode("package:com.gamescrcpy.companion versionCode:x1\n").has_value());

    const std::string metadata = R"({"version": 3, "artifactType": {"type": "APK", "kind": "Directory"},
        "applicationId": "com.gamescrcpy.companion", "variantName": "debug",
        "elements": [{"type": "SINGLE", "filters": [], "attributes": [], "versionCode": 2,
                      "versionName": "2.0.0", "outputFile": "companion-debug.apk"}],
        "elementType": "File"})";
    CHECK(bundledVersionCode(metadata) == 2);
    CHECK(bundledVersionCode(R"({"elements": [{"versionCode": 7}]})") == 7);
    CHECK_FALSE(bundledVersionCode(R"({"applicationId": "com.gamescrcpy.server", "elements": [{"versionCode": 2}]})")
                    .has_value());
    CHECK_FALSE(bundledVersionCode(R"({"elements": []})").has_value());
    CHECK_FALSE(bundledVersionCode(R"({"elements": [{"versionCode": "2"}]})").has_value());
    CHECK_FALSE(bundledVersionCode("{").has_value());

    CHECK(overlayAllowed("SYSTEM_ALERT_WINDOW: allow; time=+3s12ms ago\n"));
    CHECK(overlayAllowed("Uid mode: SYSTEM_ALERT_WINDOW: allow\n"));
    CHECK_FALSE(overlayAllowed("SYSTEM_ALERT_WINDOW: default\n"));
    CHECK_FALSE(overlayAllowed("SYSTEM_ALERT_WINDOW: ignore; rejectTime=+1s ago\n"));
    CHECK_FALSE(overlayAllowed("No operations.\n"));

    CHECK(broadcastResult("Broadcasting: Intent { flg=0x400000 cmp=com.gamescrcpy.companion/.ReadyReceiver }\n"
                          "Broadcast completed: result=1\n") == 1);
    CHECK(broadcastResult("Broadcast completed: result=0") == 0);
    CHECK(broadcastResult("Broadcast completed: result=-1, data=\"x\"") == -1);
    CHECK_FALSE(broadcastResult("Error: Activity not started").has_value());
}

TEST_CASE("companion link: the cursor shows only over the picture of a mirror window in front with the view free")
{
    using gs::app::phoneCursorOf;
    using gs::input::Vec2;

    const protocol::CursorState over = phoneCursorOf(Vec2{0.5, 0.25}, false, true, false);
    CHECK(over.visible);
    CHECK(over.x == 32768);
    CHECK(over.y == 16384);
    CHECK_FALSE(over.pressed);
    CHECK(phoneCursorOf(Vec2{1.0, 0.0}, true, true, false) ==
          protocol::CursorState{.x = 65535, .y = 0, .visible = true, .pressed = true});

    const protocol::CursorState hidden{};
    CHECK(phoneCursorOf(std::nullopt, true, true, false) == hidden);
    CHECK(phoneCursorOf(Vec2{0.5, 0.5}, true, false, false) == hidden);
    CHECK(phoneCursorOf(Vec2{0.5, 0.5}, true, true, true) == hidden);

    const CompanionTiming timing;
    CHECK(timing.cursorGap >= 8333us);
    CHECK(timing.cursorGap <= 8334us);
    CHECK(timing.ping == 2s);
    CHECK(timing.silence == 6s);
    CHECK(timing.reply == 5s);
}

TEST_CASE("companion link: showing the cursor on the phone is off by default and set per phone on the input page")
{
    using namespace gs::app::settings;
    const Entry* cursor = findSetting("companion.cursor");
    REQUIRE(cursor != nullptr);
    CHECK(cursor->page == Page::Input);
    CHECK(cursor->kind == Kind::Boolean);
    CHECK(cursor->facets.scope == Scope::Device);
    CHECK(cursor->facets.live);
    CHECK(cursor->defaultValue == Value(false));
    CHECK_FALSE(Settings{}.companion.cursor);
    CHECK(save(Settings{}).find("\"companion\"") == std::string::npos);

    DeviceSettings device;
    CHECK(setOverride(device, "companion.cursor", Value(true)));
    CHECK(effectiveSettings(Settings{}, device).companion.cursor);
    CHECK_FALSE(effectiveSettings(Settings{}, DeviceSettings{}).companion.cursor);
}

TEST_CASE("companion link: asks before installing, then pushes, installs, grants, starts and connects")
{
    Rig rig;
    const auto apk = apkOf(2);
    auto link = rig.link(apk);
    REQUIRE(rig.notices.waitFor(CompanionEvent::InstallNeeded));
    CHECK(rig.phone.commands() == std::vector<std::string>{std::string(kSdk), std::string(kListPackages)});

    link->allowInstall();
    const auto connected = rig.notices.waitFor(CompanionEvent::Connected);
    REQUIRE(connected);
    CHECK(connected->overlay);
    const std::string token = rig.phone.token();
    CHECK(token.size() == 32);
    CHECK(token.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(rig.companion.helloToken() == token);
    CHECK(rig.phone.pushed() == std::string(apk->bytes.begin(), apk->bytes.end()));
    CHECK(rig.phone.commands() ==
          std::vector<std::string>{std::string(kSdk), std::string(kListPackages), std::string(kPush),
                                   std::string(kInstall), std::string(kRemove), std::string(kAppopsSet),
                                   std::string(kAppopsGet), std::string(kNotifications), std::string(kStart) + token,
                                   std::string(kReady), std::string(kForward)});
    CHECK(rig.notices.events() ==
          std::vector<CompanionEvent>{CompanionEvent::InstallNeeded, CompanionEvent::Installing,
                                      CompanionEvent::Connected});

    link->stop();
    REQUIRE(rig.notices.waitFor(CompanionEvent::Ended));
    CHECK(rig.companion.waitForGoodbye());
    const auto commands = rig.phone.commands();
    REQUIRE(commands.size() == 13);
    CHECK(commands[11] == kStop);
    CHECK(commands[12] == rig.unforward());
}

TEST_CASE("companion link: an older app on the phone is updated without asking, the current one is used as it is")
{
    {
        Rig rig;
        rig.phone.reply(std::string(kListPackages), "package:com.gamescrcpy.companion versionCode:1\n");
        auto link = rig.link(apkOf(2));
        REQUIRE(rig.notices.waitFor(CompanionEvent::Connected));
        CHECK(rig.notices.events() ==
              std::vector<CompanionEvent>{CompanionEvent::Installing, CompanionEvent::Connected});
        CHECK(contains(rig.phone.commands(), kInstall));
    }
    {
        Rig rig;
        rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
        auto link = rig.link(apkOf(2));
        REQUIRE(rig.notices.waitFor(CompanionEvent::Connected));
        CHECK(rig.notices.events() == std::vector<CompanionEvent>{CompanionEvent::Connected});
        CHECK_FALSE(contains(rig.phone.commands(), "push "));
    }
    {
        Rig rig;
        rig.phone.reply(std::string(kListPackages), "package:com.gamescrcpy.companion versionCode:1\n");
        auto link = rig.link(apkOf(std::nullopt));
        REQUIRE(rig.notices.waitFor(CompanionEvent::Connected));
        CHECK_FALSE(contains(rig.phone.commands(), "push "));
    }
}

TEST_CASE("companion link: when the overlay cannot be granted over adb, the phone's settings page opens once")
{
    Rig rig;
    rig.phone.reply(std::string(kSdk), "30\n");
    rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
    rig.phone.reply(std::string(kAppopsGet), "SYSTEM_ALERT_WINDOW: ignore; rejectTime=+1s ago\n");
    rig.companion.setOverlay(protocol::OverlayStatus::NoPermission);
    auto link = rig.link(apkOf(2));
    const auto connected = rig.notices.waitFor(CompanionEvent::Connected);
    REQUIRE(connected);
    CHECK_FALSE(connected->overlay);
    CHECK(rig.notices.events() ==
          std::vector<CompanionEvent>{CompanionEvent::PermissionNeeded, CompanionEvent::Connected});
    const auto commands = rig.phone.commands();
    CHECK(std::ranges::count(commands, std::string(kPermissionPage)) == 1);
    CHECK_FALSE(contains(commands, kNotifications));
    const auto page = std::ranges::find(commands, std::string(kPermissionPage)) - commands.begin();
    const auto start =
        std::ranges::find_if(commands, [](const std::string& c) { return c.starts_with(kStart); }) - commands.begin();
    CHECK(page < start);

    protocol::OverlayChanged granted;
    granted.overlay = protocol::OverlayStatus::Granted;
    granted.width = 1080;
    granted.height = 2400;
    rig.companion.send(granted);
    const auto changed = rig.notices.waitFor(CompanionEvent::OverlayChanged);
    REQUIRE(changed);
    CHECK(changed->overlay);
}

TEST_CASE("companion link: a refused installation and a missing package are reported and nothing starts")
{
    {
        Rig rig;
        rig.phone.reply(std::string(kInstall), "Failure [INSTALL_FAILED_USER_RESTRICTED: Install canceled by user]\n");
        auto link = rig.link(apkOf(2));
        REQUIRE(rig.notices.waitFor(CompanionEvent::InstallNeeded));
        link->allowInstall();
        const auto failed = rig.notices.waitFor(CompanionEvent::InstallFailed);
        REQUIRE(failed);
        CHECK(failed->detail == "Failure [INSTALL_FAILED_USER_RESTRICTED: Install canceled by user]");
        const auto commands = rig.phone.commands();
        CHECK(commands.back() == kRemove);
        CHECK_FALSE(contains(commands, kStart));
        CHECK_FALSE(contains(commands, kForward));
    }
    {
        Rig rig;
        auto link = rig.link(nullptr);
        REQUIRE(rig.notices.waitFor(CompanionEvent::InstallFailed));
        CHECK(rig.notices.events() == std::vector<CompanionEvent>{CompanionEvent::InstallFailed});
        CHECK(rig.phone.commands() == std::vector<std::string>{std::string(kSdk), std::string(kListPackages)});
    }
    {
        Rig rig;
        auto link = rig.link(apkOf(2));
        REQUIRE(rig.notices.waitFor(CompanionEvent::InstallNeeded));
        link->stop();
        REQUIRE(rig.notices.waitFor(CompanionEvent::Ended));
        CHECK_FALSE(contains(rig.phone.commands(), "push "));
    }
}

TEST_CASE("companion link: a companion that is not listening or refuses the token fails and is stopped again")
{
    {
        Rig rig;
        rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
        rig.phone.reply(std::string(kReady), "Broadcasting: Intent { cmp=com.gamescrcpy.companion/.ReadyReceiver }\n"
                                             "Broadcast completed: result=0\n");
        auto link = rig.link(apkOf(2));
        REQUIRE(rig.notices.waitFor(CompanionEvent::Failed));
        const auto commands = rig.phone.commands();
        CHECK_FALSE(contains(commands, kForward));
        CHECK(commands.back() == kStop);
    }
    {
        Rig rig;
        rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
        rig.companion.setRefusing(true);
        auto link = rig.link(apkOf(2));
        const auto failed = rig.notices.waitFor(CompanionEvent::Failed);
        REQUIRE(failed);
        CHECK(failed->detail.find("authenticationFailed") != std::string::npos);
        const auto commands = rig.phone.commands();
        REQUIRE(commands.size() >= 2);
        CHECK(commands[commands.size() - 2] == kStop);
        CHECK(commands.back() == rig.unforward());
    }
}

TEST_CASE("companion link: cursor states go out latest first, at most one per gap, and the same one only once")
{
    Rig rig;
    rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
    CompanionTiming timing;
    timing.cursorGap = 300ms;
    timing.ping = 60s;
    auto link = rig.link(apkOf(2), timing);
    REQUIRE(rig.notices.waitFor(CompanionEvent::Connected));

    const protocol::CursorState first{.x = 100, .y = 200, .visible = true, .pressed = false};
    const protocol::CursorState skipped{.x = 300, .y = 400, .visible = true, .pressed = false};
    const protocol::CursorState latest{.x = 500, .y = 600, .visible = true, .pressed = true};
    link->setCursor(first);
    REQUIRE(rig.companion.waitForCursors(1));
    link->setCursor(skipped);
    link->setCursor(latest);
    REQUIRE(rig.companion.waitForCursors(2));
    link->setCursor(latest);
    link->setCursor(protocol::CursorState{});
    REQUIRE(rig.companion.waitForCursors(3));

    const auto received = rig.companion.cursors();
    REQUIRE(received.size() == 3);
    CHECK(received[0].state == first);
    CHECK(received[1].state == latest);
    CHECK(received[2].state == protocol::CursorState{});
    CHECK(received[1].at - received[0].at >= 250ms);
    CHECK(received[2].at - received[1].at >= 250ms);
}

TEST_CASE("companion link: a mouse moving all the time reaches the phone at most 120 times a second")
{
    Rig rig;
    rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
    auto link = rig.link(apkOf(2));
    REQUIRE(rig.notices.waitFor(CompanionEvent::Connected));

    const Clock::time_point start = Clock::now();
    std::uint16_t x = 0;
    while (Clock::now() - start < 500ms) {
        link->setCursor({.x = ++x, .y = 100, .visible = true, .pressed = false});
    }
    REQUIRE(rig.companion.waitForCursors(2));
    const auto received = rig.companion.cursors();
    const auto elapsed = received.back().at - received.front().at;
    CHECK(received.size() >= 10);
    CHECK(static_cast<double>(received.size() - 1) <=
          std::chrono::duration<double>(elapsed).count() * 120.0 + 2.0);
}

TEST_CASE("companion link: pings keep the link up, a silent companion is lost, and Stop on the phone ends it")
{
    {
        Rig rig;
        rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
        CompanionTiming timing;
        timing.ping = 50ms;
        timing.silence = 400ms;
        auto link = rig.link(apkOf(2), timing);
        REQUIRE(rig.notices.waitFor(CompanionEvent::Connected));
        REQUIRE(rig.companion.waitForPings(12));
        CHECK_FALSE(rig.notices.waitFor(CompanionEvent::Lost, 0ms).has_value());

        rig.companion.setSilent(true);
        const auto lost = rig.notices.waitFor(CompanionEvent::Lost);
        REQUIRE(lost);
        const auto commands = rig.phone.commands();
        REQUIRE(commands.size() >= 2);
        CHECK(commands[commands.size() - 2] == kStop);
        CHECK(commands.back() == rig.unforward());
    }
    {
        Rig rig;
        rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
        auto link = rig.link(apkOf(2));
        REQUIRE(rig.notices.waitFor(CompanionEvent::Connected));
        rig.companion.send(protocol::ServerGoodbye{});
        REQUIRE(rig.notices.waitFor(CompanionEvent::StoppedOnPhone));
        const auto commands = rig.phone.commands();
        CHECK_FALSE(contains(commands, kStop));
        CHECK(commands.back() == rig.unforward());
    }
}

namespace {

std::vector<std::string> after(const std::vector<std::string>& commands, std::string_view first)
{
    const auto at = std::ranges::find(commands, std::string(first));
    return at == commands.end() ? std::vector<std::string>() : std::vector<std::string>(at, commands.end());
}

template <typename M>
std::vector<M> othersOf(const FakeCompanion& companion)
{
    std::vector<M> found;
    for (const protocol::ToServerMessage& message : companion.others()) {
        if (const auto* typed = std::get_if<M>(&message)) {
            found.push_back(*typed);
        }
    }
    return found;
}

}

TEST_CASE("companion link: the ime command results, the input method to go back to and long text are read exactly")
{
    using gs::app::imeEnabled;
    using gs::app::imeSelected;
    using gs::app::previousInputMethod;
    using gs::app::splitCommitText;

    CHECK(imeEnabled("Input method com.gamescrcpy.companion/.CompanionIme: now enabled"));
    CHECK(imeEnabled("Input method com.gamescrcpy.companion/.CompanionIme: already enabled for user #0"));
    CHECK_FALSE(imeEnabled("Unknown input method com.gamescrcpy.companion/.CompanionIme cannot be enabled for user #0"));
    CHECK_FALSE(imeEnabled("Exception occurred while executing 'enable': java.lang.SecurityException"));
    CHECK(imeSelected("Input method com.gamescrcpy.companion/.CompanionIme selected for user #0"));
    CHECK(imeSelected("Input method com.gamescrcpy.companion/.CompanionIme selected"));
    CHECK_FALSE(imeSelected("Unknown input method com.gamescrcpy.companion/.CompanionIme cannot be selected for user #0"));
    CHECK_FALSE(imeSelected("Error: input method not selected"));

    CHECK(previousInputMethod("com.example.keyboard/.LatinIme\n", {}) == "com.example.keyboard/.LatinIme");
    CHECK(previousInputMethod("null\n", "com.gamescrcpy.companion/.CompanionIme\ncom.sohu.inputmethod/.Ime\n") ==
          "com.sohu.inputmethod/.Ime");
    CHECK(previousInputMethod("com.gamescrcpy.companion/.CompanionIme", "com.gamescrcpy.companion/.CompanionIme\n")
              .empty());
    CHECK(previousInputMethod("", "").empty());

    CHECK(splitCommitText("").empty());
    CHECK(splitCommitText("gg") == std::vector<std::string>{"gg"});
    std::string chinese;
    for (int i = 0; i < 2000; ++i) {
        chinese += "字";
    }
    const std::vector<std::string> parts = splitCommitText(chinese);
    REQUIRE(parts.size() == 2);
    CHECK(parts[0].size() == 4095);
    CHECK(parts[1].size() == 6000 - 4095);
    CHECK(parts[0] + parts[1] == chinese);
}

TEST_CASE("companion link: with typing on, the phone switches to the companion input method and back when it stops")
{
    Rig rig;
    rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
    auto link = rig.link(apkOf(2));
    link->setIme(true);
    REQUIRE(rig.notices.waitFor(CompanionEvent::Connected));
    REQUIRE(rig.notices.waitFor(CompanionEvent::ImeSwitched));
    CHECK(rig.phone.currentIme() == gs::app::kCompanionIme);
    CHECK(after(rig.phone.commands(), kCurrentIme) ==
          std::vector<std::string>{std::string(kCurrentIme), std::string(kImeEnable), std::string(kImeSet)});

    link->stop();
    REQUIRE(rig.notices.waitFor(CompanionEvent::Ended));
    CHECK(rig.phone.currentIme() == kPhoneIme);
    const auto commands = rig.phone.commands();
    REQUIRE(commands.size() >= 4);
    CHECK(std::vector<std::string>(commands.end() - 4, commands.end()) ==
          std::vector<std::string>{std::string(kCurrentIme), "ime set " + std::string(kPhoneIme), std::string(kStop),
                                   rig.unforward()});
    CHECK_FALSE(contains(commands, kEnabledImes));
    CHECK_FALSE(contains(commands, kImeSettings));
}

TEST_CASE("companion link: the input method goes back when the companion is lost or the setting is turned off")
{
    {
        Rig rig;
        rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
        CompanionTiming timing;
        timing.ping = 50ms;
        timing.silence = 400ms;
        auto link = rig.link(apkOf(2), timing);
        link->setIme(true);
        REQUIRE(rig.notices.waitFor(CompanionEvent::ImeSwitched));
        rig.companion.setSilent(true);
        REQUIRE(rig.notices.waitFor(CompanionEvent::Lost));
        CHECK(rig.phone.currentIme() == kPhoneIme);
    }
    {
        Rig rig;
        rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
        rig.phone.setCurrentIme("null");
        rig.phone.reply(std::string(kEnabledImes), "com.gamescrcpy.companion/.CompanionIme\ncom.sohu.inputmethod/.Ime\n");
        auto link = rig.link(apkOf(2));
        link->setIme(true);
        REQUIRE(rig.notices.waitFor(CompanionEvent::ImeSwitched));
        CHECK(contains(rig.phone.commands(), kEnabledImes));
        link->setIme(false);
        REQUIRE(rig.phone.waitForCommand("ime set com.sohu.inputmethod/.Ime"));
        CHECK(rig.phone.currentIme() == "com.sohu.inputmethod/.Ime");
        CHECK_FALSE(rig.notices.waitFor(CompanionEvent::Ended, 0ms).has_value());
    }
    {
        Rig rig;
        rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
        auto link = rig.link(apkOf(2));
        link->setIme(true);
        REQUIRE(rig.notices.waitFor(CompanionEvent::ImeSwitched));
        rig.phone.setCurrentIme("com.other.keyboard/.Ime");
        link->stop();
        REQUIRE(rig.notices.waitFor(CompanionEvent::Ended));
        CHECK(rig.phone.currentIme() == "com.other.keyboard/.Ime");
        CHECK_FALSE(contains(rig.phone.commands(), "ime set " + std::string(kPhoneIme)));
    }
}

TEST_CASE("companion link: a phone that refuses the input method opens its keyboard settings once and can try again")
{
    Rig rig;
    rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
    rig.phone.setImeRefused(true);
    auto link = rig.link(apkOf(2));
    link->setIme(true);
    REQUIRE(rig.notices.waitFor(CompanionEvent::ImeRefused));
    CHECK(std::ranges::count(rig.phone.commands(), std::string(kImeSettings)) == 1);
    CHECK_FALSE(contains(rig.phone.commands(), kImeSet));
    CHECK(rig.phone.currentIme() == kPhoneIme);

    rig.phone.setImeRefused(false);
    link->retryIme();
    REQUIRE(rig.notices.waitFor(CompanionEvent::ImeSwitched));
    CHECK(std::ranges::count(rig.phone.commands(), std::string(kImeEnable)) == 2);
    CHECK(std::ranges::count(rig.phone.commands(), std::string(kImeSettings)) == 1);
    CHECK(rig.phone.currentIme() == gs::app::kCompanionIme);

    link->stop();
    REQUIRE(rig.notices.waitFor(CompanionEvent::Ended));
    CHECK(rig.phone.currentIme() == kPhoneIme);
}

TEST_CASE("companion link: typed text goes to the companion as CommitText, in order with Enter, and only while connected")
{
    Rig rig;
    rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
    CompanionTiming timing;
    timing.ping = 60s;
    auto link = rig.link(apkOf(2), timing);
    link->commitText("too early");
    REQUIRE(rig.notices.waitFor(CompanionEvent::Connected));

    std::string chinese;
    for (int i = 0; i < 1500; ++i) {
        chinese += "好";
    }
    link->commitText("你好");
    link->editorAction(protocol::ImeAction::Unspecified);
    link->commitText(chinese);
    REQUIRE(rig.companion.waitForOthers(4));
    const auto others = rig.companion.others();
    REQUIRE(others.size() == 4);
    REQUIRE(std::holds_alternative<protocol::CommitText>(others[0]));
    CHECK(std::get<protocol::CommitText>(others[0]).text == "你好");
    REQUIRE(std::holds_alternative<protocol::EditorAction>(others[1]));
    CHECK(std::get<protocol::EditorAction>(others[1]).action == protocol::ImeAction::Unspecified);
    REQUIRE(std::holds_alternative<protocol::CommitText>(others[2]));
    REQUIRE(std::holds_alternative<protocol::CommitText>(others[3]));
    CHECK(std::get<protocol::CommitText>(others[2]).text + std::get<protocol::CommitText>(others[3]).text == chinese);
    CHECK(std::get<protocol::CommitText>(others[2]).text.size() <= protocol::kCompanionTextLimit);
    CHECK(othersOf<protocol::CommitText>(rig.companion).size() == 3);
}

TEST_CASE("companion link: the key hints go to the phone when it connects and again only when they change")
{
    Rig rig;
    rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
    CompanionTiming timing;
    timing.ping = 60s;
    auto link = rig.link(apkOf(2), timing);
    const protocol::KeyHintLayout first{
        .hints = {protocol::KeyHint{.shape = protocol::KeyHintShape::Ring, .x = 13107, .y = 45875, .width = 9830,
                                    .height = 16384},
                  protocol::KeyHint{.shape = protocol::KeyHintShape::Key, .x = 13107, .y = 37683, .label = "W"}}};
    const protocol::KeyHintState shown{.shown = true, .faded = false, .opacity = 60};
    link->setKeyHints(first);
    link->setKeyHintState(shown);
    REQUIRE(rig.notices.waitFor(CompanionEvent::Connected));
    REQUIRE(rig.companion.waitForOthers(2));
    {
        const auto others = rig.companion.others();
        REQUIRE(std::holds_alternative<protocol::KeyHintLayout>(others[0]));
        CHECK(std::get<protocol::KeyHintLayout>(others[0]) == first);
        REQUIRE(std::holds_alternative<protocol::KeyHintState>(others[1]));
        CHECK(std::get<protocol::KeyHintState>(others[1]) == shown);
    }

    link->setKeyHints(first);
    link->setKeyHintState(shown);
    CHECK_FALSE(rig.companion.waitForOthers(3, 300ms));

    protocol::KeyHintLayout second = first;
    second.hints[1].label = "Z";
    link->setKeyHints(second);
    link->setKeyHintState(protocol::KeyHintState{.shown = true, .faded = true, .opacity = 60});
    REQUIRE(rig.companion.waitForOthers(4));
    CHECK(othersOf<protocol::KeyHintLayout>(rig.companion).back() == second);
    CHECK(othersOf<protocol::KeyHintState>(rig.companion).back().faded);
}

TEST_CASE("companion link: an input box the companion reports reaches the listener with its details")
{
    Rig rig;
    rig.phone.reply(std::string(kListPackages), std::string(kCurrent));
    auto link = rig.link(apkOf(2));
    REQUIRE(rig.notices.waitFor(CompanionEvent::Connected));
    const protocol::InputBoxChanged box{.shown = true, .multiLine = true, .inputType = 0x20001,
                                        .action = protocol::ImeAction::Unspecified};
    rig.companion.send(box);
    const auto heard = rig.notices.waitFor(CompanionEvent::InputBox);
    REQUIRE(heard.has_value());
    REQUIRE(heard->box.has_value());
    CHECK(*heard->box == box);
    CHECK_FALSE(gs::app::isFinal(CompanionEvent::InputBox));
    CHECK_FALSE(gs::app::isFinal(CompanionEvent::ImeRefused));
}
