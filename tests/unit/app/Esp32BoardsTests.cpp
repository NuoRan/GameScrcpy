#include "Esp32Boards.h"
#include "Esp32Panel.h"

#include "unit/esp32/FakeBoard.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace esp32 = gs::esp32;
using esp32test::FakeBoard;
using gs::app::Esp32Boards;
using gs::app::Esp32Candidate;
using gs::app::Esp32Claim;
using gs::app::Esp32Found;
using gs::app::Esp32Hooks;
using gs::app::Esp32Holder;
using gs::app::Esp32Panel;
using gs::app::Esp32Probe;
using gs::app::Esp32Probed;
using namespace std::chrono_literals;

namespace {

std::unique_ptr<QCoreApplication> ensureApp()
{
    static int argc = 1;
    static char name[] = "test";
    static char* argv[] = {name, nullptr};
    if (QCoreApplication::instance() != nullptr) {
        return nullptr;
    }
    return std::make_unique<QCoreApplication>(argc, argv);
}

bool settle(const std::function<bool()>& done, std::chrono::milliseconds timeout = 8000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QCoreApplication::processEvents();
    return done();
}

esp32::SerialPortInfo portOf(const std::string& name, esp32::PortKind kind)
{
    return esp32::SerialPortInfo{.name = name, .description = "test port", .vendorId = 0, .productId = 0, .kind = kind};
}

esp32::Esp32Settings automatic()
{
    esp32::Esp32Settings settings;
    settings.port = std::string(esp32::kAutomaticPort);
    settings.link = esp32::LinkMode::Serial;
    return settings;
}

std::vector<std::string> placesOf(const std::vector<Esp32Candidate>& candidates)
{
    std::vector<std::string> places;
    for (const Esp32Candidate& candidate : candidates) {
        places.push_back(candidate.where);
    }
    return places;
}

QJsonObject optionsOf(const std::string& json)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
}

class BoardPlug final : public esp32::Port
{
public:
    BoardPlug(std::shared_ptr<FakeBoard> board, std::shared_ptr<std::atomic<bool>> unplugged)
        : m_board(std::move(board))
        , m_unplugged(std::move(unplugged))
    {
    }

    [[nodiscard]] bool write(std::span<const std::uint8_t> bytes) override
    {
        return !m_unplugged->load() && m_board->write(bytes);
    }

    [[nodiscard]] std::size_t read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) override
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        do {
            if (m_interrupted.load() || m_unplugged->load()) {
                return 0;
            }
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            const std::size_t got = m_board->read(buffer, std::clamp(left, 0ms, 10ms));
            if (got > 0) {
                return got;
            }
        } while (std::chrono::steady_clock::now() < deadline);
        return 0;
    }

    [[nodiscard]] bool setBaud(std::uint32_t baud) override { return m_board->setBaud(baud); }
    [[nodiscard]] bool setDtr(bool) override { return true; }
    [[nodiscard]] bool setRts(bool) override { return true; }
    void discardInput() override { m_board->discardInput(); }
    void interrupt() override { m_interrupted.store(true); }
    [[nodiscard]] bool datagrams() const override { return false; }
    [[nodiscard]] bool gone() const override { return m_unplugged->load(); }

private:
    std::shared_ptr<FakeBoard> m_board;
    std::shared_ptr<std::atomic<bool>> m_unplugged;
    std::atomic<bool> m_interrupted{false};
};

class Bench
{
public:
    void plug(const std::string& name, esp32::PortKind kind, bool answers)
    {
        const std::scoped_lock lock(m_mutex);
        m_ports.push_back(portOf(name, kind));
        if (answers) {
            m_boards[name] = std::make_shared<FakeBoard>(FakeBoard::Options{});
            m_plugs[name] = std::make_shared<std::atomic<bool>>(false);
        }
    }

    void plugSaying(const std::string& name, esp32::PortKind kind, esp32test::Bytes says)
    {
        const std::scoped_lock lock(m_mutex);
        m_ports.push_back(portOf(name, kind));
        FakeBoard::Options options;
        options.answers = false;
        options.says = std::move(says);
        m_boards[name] = std::make_shared<FakeBoard>(options);
        m_plugs[name] = std::make_shared<std::atomic<bool>>(false);
    }

    void unplug(const std::string& name)
    {
        const std::scoped_lock lock(m_mutex);
        std::erase_if(m_ports, [&name](const esp32::SerialPortInfo& port) { return port.name == name; });
        if (const auto plugged = m_plugs.find(name); plugged != m_plugs.end()) {
            plugged->second->store(true);
            m_plugs.erase(plugged);
        }
        m_boards.erase(name);
    }

    [[nodiscard]] std::shared_ptr<FakeBoard> board(const std::string& name)
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_boards.find(name);
        return found != m_boards.end() ? found->second : nullptr;
    }

    [[nodiscard]] int probesOf(const std::string& name)
    {
        const std::scoped_lock lock(m_mutex);
        return static_cast<int>(std::ranges::count(m_probed, name));
    }

    [[nodiscard]] Esp32Hooks hooks()
    {
        Esp32Hooks hooks;
        hooks.watchUsb = false;
        hooks.list = [this] {
            const std::scoped_lock lock(m_mutex);
            return m_ports;
        };
        hooks.probe = [this](const Esp32Candidate& candidate, const esp32::Esp32Settings&, esp32::LinkEvents events,
                             const std::stop_token& stop, esp32::BoardTrouble& trouble) -> Esp32Probed {
            std::shared_ptr<FakeBoard> fake;
            std::shared_ptr<std::atomic<bool>> unplugged;
            {
                const std::scoped_lock lock(m_mutex);
                m_probed.push_back(candidate.where);
                if (const auto found = m_boards.find(candidate.where); found != m_boards.end()) {
                    fake = found->second;
                    unplugged = m_plugs[candidate.where];
                }
            }
            if (fake == nullptr) {
                trouble.problem = esp32::BoardProblem::Silent;
                return gs::makeUnexpected(esp32::OpenFailure::NoAnswer);
            }
            esp32::LinkOptions options;
            options.allowLegacy = false;
            auto link = std::make_unique<esp32::Esp32Link>(std::make_unique<BoardPlug>(fake, unplugged), options,
                                                           std::move(events));
            esp32::LinkState state = link->state();
            const auto deadline = std::chrono::steady_clock::now() + 3s;
            while (state == esp32::LinkState::Opening && !stop.stop_requested() &&
                   std::chrono::steady_clock::now() < deadline) {
                state = link->waitSettled(50ms);
            }
            if (state != esp32::LinkState::Ready) {
                trouble = link->trouble();
                return gs::makeUnexpected(stop.stop_requested() ? esp32::OpenFailure::Stopped
                                                                : esp32::OpenFailure::NoAnswer);
            }
            return Esp32Probe{.link = std::move(link), .where = candidate.where, .wifi = false, .kind = candidate.kind};
        };
        return hooks;
    }

private:
    std::mutex m_mutex;
    std::vector<esp32::SerialPortInfo> m_ports;
    std::map<std::string, std::shared_ptr<FakeBoard>> m_boards;
    std::map<std::string, std::shared_ptr<std::atomic<bool>>> m_plugs;
    std::vector<std::string> m_probed;
};

class Scans
{
public:
    explicit Scans(Esp32Boards& boards)
    {
        QObject::connect(&boards, &Esp32Boards::scanned, &boards, [this](bool complete) {
            if (complete) {
                ++m_complete;
            }
        });
    }

    [[nodiscard]] int complete() const noexcept { return m_complete; }

private:
    int m_complete = 0;
};

void lookFor(Esp32Boards& boards, const esp32::Esp32Settings& settings)
{
    boards.setSettings(settings);
    boards.start();
    boards.setActive(true);
}

}

TEST_CASE("ESP32 rows, claims and touch options name a board by its serial port or its address (HID-008)")
{
    CHECK(gs::app::esp32SerialOf(QStringLiteral("COM5")) == QStringLiteral("esp32-COM5"));
    CHECK(gs::app::esp32SerialOf(QStringLiteral("192.168.1.20:4545")) == QStringLiteral("esp32-192.168.1.20-4545"));
    CHECK(gs::app::isEsp32Serial(u"esp32-COM5"));
    CHECK_FALSE(gs::app::isEsp32Serial(u"emulator-5554"));
    CHECK(gs::app::esp32SamePort("COM5", "com5"));
    CHECK_FALSE(gs::app::esp32SamePort("COM5", "COM15"));
    CHECK_FALSE(gs::app::esp32SamePort("", ""));

    const Esp32Claim every = gs::app::esp32ClaimOf(automatic());
    CHECK(every == Esp32Claim{.where = "", .wifi = false, .every = true});
    CHECK(gs::app::esp32Covers(every, "COM3", false));
    CHECK_FALSE(gs::app::esp32Covers(every, "192.168.1.20:4545", true));

    esp32::Esp32Settings chosen = automatic();
    chosen.port = "COM5";
    const Esp32Claim one = gs::app::esp32ClaimOf(chosen);
    CHECK(one == Esp32Claim{.where = "COM5"});
    CHECK(gs::app::esp32Covers(one, "com5", false));
    CHECK_FALSE(gs::app::esp32Covers(one, "COM6", false));

    esp32::Esp32Settings wifi = automatic();
    wifi.link = esp32::LinkMode::Wifi;
    wifi.address = "esp32.local";
    const Esp32Claim air = gs::app::esp32ClaimOf(wifi);
    CHECK(air.wifi);
    CHECK(gs::app::esp32Covers(air, "192.168.1.20:4545", true));
    CHECK_FALSE(gs::app::esp32Covers(air, "COM5", false));

    const std::filesystem::path boardsFile(L"C:/data/esp32-boards.json");
    const QJsonObject serial = optionsOf(gs::app::esp32TouchOptions("COM5", false, boardsFile));
    CHECK(serial.value(QStringLiteral("port")).toString() == QStringLiteral("COM5"));
    CHECK(serial.value(QStringLiteral("link")).toString() == QStringLiteral("serial"));
    CHECK(serial.value(QStringLiteral("address")).toString().isEmpty());
    CHECK(serial.value(QStringLiteral("boardsFile")).toString() == QStringLiteral("C:/data/esp32-boards.json"));
    const QJsonObject remote = optionsOf(gs::app::esp32TouchOptions("192.168.1.20:4545", true, boardsFile));
    CHECK(remote.value(QStringLiteral("port")).toString() == QStringLiteral("auto"));
    CHECK(remote.value(QStringLiteral("link")).toString() == QStringLiteral("wifi"));
    CHECK(remote.value(QStringLiteral("address")).toString() == QStringLiteral("192.168.1.20:4545"));
}

TEST_CASE("the boards looked for are the ESP32-like ports, the chosen port or the Wi-Fi address, minus those in use (HID-008)")
{
    const std::vector<esp32::SerialPortInfo> ports{portOf("COM3", esp32::PortKind::Other),
                                                   portOf("COM5", esp32::PortKind::EspressifUsb),
                                                   portOf("COM7", esp32::PortKind::Ch34x),
                                                   portOf("COM8", esp32::PortKind::Cp210x)};
    const std::vector<std::string> likely{"COM5", "COM7", "COM8"};
    const std::vector<Esp32Candidate> all = gs::app::esp32Candidates(automatic(), ports, {});
    CHECK(placesOf(all) == likely);
    REQUIRE(all.size() == 3);
    CHECK(all[1].kind == esp32::PortKind::Ch34x);
    CHECK_FALSE(all[1].chosen);

    const std::vector<std::string> free{"COM5", "COM8"};
    CHECK(placesOf(gs::app::esp32Candidates(automatic(), ports, {Esp32Claim{.where = "COM7"}})) == free);
    CHECK(gs::app::esp32Candidates(automatic(), ports, {gs::app::esp32ClaimOf(automatic())}).empty());

    esp32::Esp32Settings chosen = automatic();
    chosen.port = "COM9";
    const std::vector<Esp32Candidate> unlisted = gs::app::esp32Candidates(chosen, ports, {});
    REQUIRE(unlisted.size() == 1);
    CHECK(unlisted[0] == Esp32Candidate{.where = "COM9", .wifi = false, .kind = esp32::PortKind::Other, .chosen = true});
    chosen.port = "COM7";
    const std::vector<Esp32Candidate> listed = gs::app::esp32Candidates(chosen, ports, {});
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].kind == esp32::PortKind::Ch34x);
    chosen.port = "USB1";
    CHECK(gs::app::esp32Candidates(chosen, ports, {}).empty());

    esp32::Esp32Settings wifi = automatic();
    wifi.link = esp32::LinkMode::Wifi;
    wifi.address = "192.168.1.20:4545";
    const std::vector<Esp32Candidate> remote = gs::app::esp32Candidates(wifi, ports, {});
    REQUIRE(remote.size() == 1);
    CHECK(remote[0] == Esp32Candidate{.where = "192.168.1.20:4545", .wifi = true, .kind = esp32::PortKind::Other,
                                      .chosen = true});
    CHECK(gs::app::esp32Candidates(wifi, ports, {Esp32Claim{.where = "10.0.0.9:4545", .wifi = true}}).empty());
    CHECK(gs::app::esp32Candidates(wifi, ports, {Esp32Claim{.where = "COM5"}}).size() == 1);
}

TEST_CASE("a board that answers on an ESP32-like port gets an entry, its phone's state follows and unplugging removes it "
          "(HID-008)")
{
    const auto app = ensureApp();
    Bench bench;
    bench.plug("COM3", esp32::PortKind::Other, false);
    bench.plug("COM7", esp32::PortKind::EspressifUsb, true);
    Esp32Boards boards(bench.hooks());
    int changes = 0;
    QObject::connect(&boards, &Esp32Boards::boardsChanged, &boards, [&changes] { ++changes; });
    lookFor(boards, automatic());

    REQUIRE(settle([&boards] { return boards.link("COM7", false) != nullptr; }));
    REQUIRE(boards.boards().size() == 1);
    const Esp32Found found = boards.boards().front();
    CHECK(found.where == "COM7");
    CHECK_FALSE(found.wifi);
    CHECK(found.connected);
    CHECK_FALSE(found.held);
    REQUIRE(found.info.has_value());
    CHECK(found.info->chipId == GS_ESP32_CHIP_ESP32S3);
    CHECK(found.info->firmware == "1.0.0");
    CHECK(bench.probesOf("COM3") == 0);
    CHECK(boards.matching(automatic()).has_value());
    CHECK(changes > 0);

    bench.board("COM7")->pushStatus(GS_ESP32_USB_READY, GS_ESP32_BLE_NONE, "");
    CHECK(settle([&boards] {
        const std::optional<Esp32Found> board = boards.board("COM7", false);
        return board.has_value() && board->status.usb == GS_ESP32_USB_READY;
    }));
    CHECK(Esp32Panel::phoneText(boards.board("COM7", false)->status) == QStringLiteral("Phone connected over USB"));

    bench.unplug("COM7");
    CHECK(settle([&boards] { return boards.boards().empty(); }));
}

TEST_CASE("a claim closes the board's port and keeps it out of the scans until it is released (HID-008)")
{
    const auto app = ensureApp();
    Bench bench;
    bench.plug("COM7", esp32::PortKind::EspressifUsb, true);
    Esp32Boards boards(bench.hooks());
    Scans scans(boards);
    lookFor(boards, automatic());
    REQUIRE(settle([&boards] { return boards.link("COM7", false) != nullptr; }));
    int claims = 0;
    QObject::connect(&boards, &Esp32Boards::claimsChanged, &boards, [&claims] { ++claims; });

    boards.claim(QStringLiteral("esp32-COM7"), Esp32Claim{.where = "COM7"});
    CHECK(claims == 1);
    CHECK(boards.link("COM7", false) == nullptr);
    CHECK(boards.held("COM7", false));
    CHECK(boards.inSession("COM7", false));
    const std::optional<Esp32Found> held = boards.board("COM7", false);
    REQUIRE(held.has_value());
    CHECK(held->held);
    CHECK_FALSE(held->connected);
    CHECK(held->heldBy == QStringLiteral("esp32-COM7"));
    CHECK(held->info.has_value());

    const int probes = bench.probesOf("COM7");
    const int before = scans.complete();
    boards.rescan();
    REQUIRE(settle([&scans, before] { return scans.complete() > before; }));
    CHECK(bench.probesOf("COM7") == probes);
    CHECK(boards.link("COM7", false) == nullptr);

    boards.release(QStringLiteral("esp32-COM7"));
    CHECK(claims == 2);
    CHECK_FALSE(boards.held("COM7", false));
    REQUIRE(settle([&boards] { return boards.link("COM7", false) != nullptr; }));
    CHECK(bench.probesOf("COM7") == probes + 1);

    boards.claim(QStringLiteral("R58N123ABC"), gs::app::esp32ClaimOf(automatic()));
    CHECK(boards.link("COM7", false) == nullptr);
    CHECK(boards.inSession("COM7", false));
    boards.release(QStringLiteral("R58N123ABC"));
    REQUIRE(settle([&boards] { return boards.link("COM7", false) != nullptr; }));

    boards.claim(QStringLiteral("flash:esp32"), Esp32Claim{.where = "COM7", .holder = Esp32Holder::Flash});
    CHECK(boards.held("COM7", false));
    CHECK_FALSE(boards.inSession("COM7", false));
    REQUIRE(boards.board("COM7", false).has_value());
    CHECK(boards.board("COM7", false)->flashing);
    boards.release(QStringLiteral("flash:esp32"));
    CHECK(settle([&boards] { return boards.link("COM7", false) != nullptr; }));
}

TEST_CASE("an ESP32-like port that does not answer twice is left alone until it is plugged in again (HID-008)")
{
    const auto app = ensureApp();
    Bench bench;
    bench.plug("COM9", esp32::PortKind::Ch34x, false);
    Esp32Boards boards(bench.hooks());
    Scans scans(boards);
    lookFor(boards, automatic());
    REQUIRE(settle([&scans] { return scans.complete() >= 1; }));
    CHECK(bench.probesOf("COM9") == 1);
    REQUIRE(boards.problem().has_value());
    CHECK(*boards.problem() == esp32::OpenFailure::NoBoard);

    boards.rescan();
    REQUIRE(settle([&scans] { return scans.complete() >= 2; }));
    CHECK(bench.probesOf("COM9") == 2);
    boards.rescan();
    REQUIRE(settle([&scans] { return scans.complete() >= 3; }));
    CHECK(bench.probesOf("COM9") == 2);

    bench.unplug("COM9");
    boards.rescan();
    REQUIRE(settle([&scans] { return scans.complete() >= 4; }));
    bench.plug("COM9", esp32::PortKind::Ch34x, false);
    boards.rescan();
    REQUIRE(settle([&scans] { return scans.complete() >= 5; }));
    CHECK(bench.probesOf("COM9") == 3);
    CHECK(boards.boards().empty());
}

TEST_CASE("disconnecting a board from its card lets go of it until its port goes away or it is asked for again "
          "(HID-008)")
{
    const auto app = ensureApp();
    Bench bench;
    bench.plug("COM7", esp32::PortKind::EspressifUsb, true);
    Esp32Boards boards(bench.hooks());
    Scans scans(boards);
    lookFor(boards, automatic());
    REQUIRE(settle([&boards] { return boards.link("COM7", false) != nullptr; }));

    boards.dismiss("COM7", false);
    CHECK(boards.boards().empty());
    const int probes = bench.probesOf("COM7");
    const int before = scans.complete();
    boards.rescan();
    REQUIRE(settle([&scans, before] { return scans.complete() > before; }));
    CHECK(bench.probesOf("COM7") == probes);
    CHECK(boards.boards().empty());

    boards.forgive();
    REQUIRE(settle([&boards] { return boards.link("COM7", false) != nullptr; }));

    boards.dismiss("COM7", false);
    bench.unplug("COM7");
    const int unplugged = scans.complete();
    boards.rescan();
    REQUIRE(settle([&scans, unplugged] { return scans.complete() > unplugged; }));
    bench.plug("COM7", esp32::PortKind::EspressifUsb, true);
    boards.rescan();
    CHECK(settle([&boards] { return boards.link("COM7", false) != nullptr; }));
}

TEST_CASE("changing the ESP32 settings starts over and turning the extension off forgets the boards (HID-008)")
{
    const auto app = ensureApp();
    Bench bench;
    bench.plug("COM7", esp32::PortKind::EspressifUsb, true);
    Esp32Boards boards(bench.hooks());
    Scans scans(boards);
    lookFor(boards, automatic());
    REQUIRE(settle([&boards] { return boards.link("COM7", false) != nullptr; }));

    esp32::Esp32Settings chosen = automatic();
    chosen.port = "COM8";
    const int before = scans.complete();
    boards.setSettings(chosen);
    CHECK(boards.boards().empty());
    REQUIRE(settle([&scans, before] { return scans.complete() > before; }));
    CHECK(boards.boards().empty());
    CHECK(bench.probesOf("COM8") == 1);
    REQUIRE(boards.problem().has_value());
    CHECK(*boards.problem() == esp32::OpenFailure::NoAnswer);

    boards.setSettings(automatic());
    REQUIRE(settle([&boards] { return boards.link("COM7", false) != nullptr; }));
    boards.setActive(false);
    CHECK_FALSE(boards.active());
    CHECK(boards.boards().empty());
    const int probes = bench.probesOf("COM7");
    boards.rescan();
    QCoreApplication::processEvents();
    CHECK(bench.probesOf("COM7") == probes);
}

TEST_CASE("the ESP32 panel borrows the board the program found, steps aside while a mirror window uses it and will "
          "not flash it then (HID-008)")
{
    const auto app = ensureApp();
    Bench bench;
    bench.plug("COM7", esp32::PortKind::EspressifUsb, true);
    Esp32Boards boards(bench.hooks());
    lookFor(boards, automatic());
    REQUIRE(settle([&boards] { return boards.link("COM7", false) != nullptr; }));

    Esp32Panel panel;
    panel.connectBoard();
    REQUIRE(settle([&panel] { return panel.state() == QStringLiteral("ready"); }));
    CHECK(panel.where() == QStringLiteral("COM7"));
    CHECK(panel.chip() == QStringLiteral("ESP32-S3"));
    CHECK(panel.firmware() == QStringLiteral("1.0.0"));
    CHECK(panel.serial());
    CHECK(bench.probesOf("COM7") == 1);

    boards.claim(QStringLiteral("esp32-COM7"), Esp32Claim{.where = "COM7"});
    CHECK(panel.state() == QStringLiteral("busy"));
    CHECK(panel.problem() == QStringLiteral("In use by a mirror window"));
    CHECK_FALSE(panel.serial());
    CHECK_FALSE(panel.tryTap());

    panel.flash(QStringLiteral("COM7"));
    CHECK_FALSE(panel.flashing());
    CHECK(panel.flashFailed());
    CHECK(panel.flashResult() == QStringLiteral("A mirror window is using this board. Close it, then flash again."));

    boards.release(QStringLiteral("esp32-COM7"));
    REQUIRE(settle([&panel] { return panel.state() == QStringLiteral("ready"); }));
    CHECK(boards.link("COM7", false) != nullptr);
    CHECK(panel.serial());

    panel.disconnectBoard();
    CHECK(panel.state() == QStringLiteral("idle"));
    CHECK(boards.link("COM7", false) != nullptr);
}

TEST_CASE("a board that keeps restarting is reported as such, not as old firmware (issues 3 and 8)")
{
    const auto app = ensureApp();
    Bench bench;
    bench.plugSaying("COM9", esp32::PortKind::Ch34x,
                     esp32test::textBytes("ESP-ROM:esp32s3-20210327\r\n"
                                          "rst:0x3 (RTC_SW_SYS_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"));
    Esp32Boards boards(bench.hooks());
    Scans scans(boards);
    lookFor(boards, automatic());
    REQUIRE(settle([&scans] { return scans.complete() >= 1; }));
    REQUIRE(boards.trouble().has_value());
    CHECK(boards.trouble()->problem == esp32::BoardProblem::Restarting);
    CHECK(boards.trouble()->resetReason == "0x3 (RTC_SW_SYS_RST)");
    CHECK(boards.boards().empty());
}

TEST_CASE("the panel names a restarting board instead of warning about old firmware (issues 3 and 8)")
{
    const auto app = ensureApp();
    Bench bench;
    bench.plugSaying("COM9", esp32::PortKind::Ch34x, esp32test::textBytes("ESP-ROM:esp32s3-20210327\r\n"));
    Esp32Boards boards(bench.hooks());
    lookFor(boards, automatic());
    Esp32Panel panel;
    panel.connectBoard();
    REQUIRE(settle([&panel] { return panel.state() == QStringLiteral("trouble"); }, 10s));
    CHECK(panel.problem().contains(QStringLiteral("keeps restarting")));
    CHECK_FALSE(panel.problem().contains(QStringLiteral("QineTouch")));
}

TEST_CASE("the panel's sentences say what is wrong and which port a two-port board needs")
{
    const auto app = ensureApp();
    esp32::BoardTrouble trouble;
    trouble.problem = esp32::BoardProblem::Silent;
    const QString silent = Esp32Panel::troubleText(trouble);
    CHECK(silent.contains(QStringLiteral("Nothing answers")));
    CHECK(silent.contains(QStringLiteral("COM or UART")));
    trouble.problem = esp32::BoardProblem::Restarting;
    trouble.resetReason = "0x3 (RTC_SW_SYS_RST)";
    CHECK(Esp32Panel::troubleText(trouble).contains(QStringLiteral("keeps restarting")));
    CHECK(Esp32Panel::troubleText(trouble).contains(QStringLiteral("0x3 (RTC_SW_SYS_RST)")));
    trouble.problem = esp32::BoardProblem::DownloadMode;
    CHECK(Esp32Panel::troubleText(trouble).contains(QStringLiteral("download mode")));
    trouble.problem = esp32::BoardProblem::WrongPort;
    CHECK(Esp32Panel::troubleText(trouble).contains(QStringLiteral("COM or UART")));
    trouble.problem = esp32::BoardProblem::None;
    CHECK(Esp32Panel::troubleText(trouble).isEmpty());
}

TEST_CASE("the panel lists the part states the board reported in its HELLO")
{
    const auto app = ensureApp();
    esp32::BoardInfo board;
    const esp32::BoardStatus unknown;
    CHECK(Esp32Panel::partsLine(board, unknown).isEmpty());
    board.parts.known = true;
    board.parts.usb = GS_ESP32_USB_READY;
    board.parts.usbProblem = GS_ESP32_PART_OK;
    board.parts.bleProblem = GS_ESP32_PART_UNSUPPORTED;
    board.parts.wifiProblem = GS_ESP32_PART_FAILED;
    const QString line = Esp32Panel::partsLine(board, unknown);
    CHECK(line.contains(QStringLiteral("USB to the phone: ready")));
    CHECK(line.contains(QStringLiteral("Wi-Fi: did not start")));
    CHECK_FALSE(line.contains(QStringLiteral("Bluetooth")));
}

TEST_CASE("the panel shows the live phone and Wi-Fi state for parts that started, not the HELLO's first guess")
{
    const auto app = ensureApp();
    esp32::BoardInfo board;
    board.parts.known = true;
    board.parts.usb = GS_ESP32_USB_NONE;
    board.parts.usbProblem = GS_ESP32_PART_OK;
    board.parts.wifiProblem = GS_ESP32_PART_OK;
    board.wifi = GS_ESP32_WIFI_NONE;
    CHECK(Esp32Panel::partsLine(board, {}).contains(QStringLiteral("USB to the phone: not started")));
    esp32::BoardStatus live;
    live.usb = GS_ESP32_USB_DETACHED;
    live.wifi = GS_ESP32_WIFI_OFF;
    const QString line = Esp32Panel::partsLine(board, live);
    CHECK(line.contains(QStringLiteral("USB to the phone: no phone")));
    CHECK(line.contains(QStringLiteral("Wi-Fi: not set up")));
    live.usb = GS_ESP32_USB_READY;
    CHECK(Esp32Panel::partsLine(board, live).contains(QStringLiteral("USB to the phone: ready")));
}
