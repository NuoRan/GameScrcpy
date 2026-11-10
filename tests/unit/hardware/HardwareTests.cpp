#include <gs/hardware/Backend.h>
#include <gs/hardware/Devices.h>
#include <gs/hardware/FakeBackend.h>

#include <doctest/doctest.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace gs::hardware;

namespace {

class RecordedEvents final : public Events
{
public:
    void data(Bytes bytes) override
    {
        const std::scoped_lock lock(m_mutex);
        received.push_back(std::move(bytes));
    }

    void report(std::uint8_t id, Bytes bytes) override
    {
        const std::scoped_lock lock(m_mutex);
        reports.emplace_back(id, std::move(bytes));
    }

    void notified(const std::string& service, const std::string& characteristic, Bytes bytes) override
    {
        const std::scoped_lock lock(m_mutex);
        notifications.push_back({service, characteristic, std::move(bytes), false});
    }

    void closed(Closed reason, const std::string& problem) override
    {
        const std::scoped_lock lock(m_mutex);
        closings.emplace_back(reason, problem);
    }

    std::vector<Bytes> received;
    std::vector<FakeReport> reports;
    std::vector<FakeValueWrite> notifications;
    std::vector<std::pair<Closed, std::string>> closings;

private:
    std::mutex m_mutex;
};

DeviceInfo serialBoard()
{
    DeviceInfo device;
    device.id = "serial:USB\\VID_1A86&PID_7523\\5&2E9E3A0&0&2";
    device.kind = Kind::Serial;
    device.name = "USB-SERIAL CH340 (COM3)";
    device.path = "\\\\.\\COM3";
    device.vendorId = 0x1A86;
    device.productId = 0x7523;
    return device;
}

DeviceInfo deck()
{
    DeviceInfo device;
    device.id = "hid:hid#vid_0fd9&pid_0080#7&1a2b3c4d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}";
    device.kind = Kind::Hid;
    device.name = "Stream Deck MK.2";
    device.path = "\\\\?\\hid#vid_0fd9&pid_0080#7&1a2b3c4d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}";
    device.vendorId = 0x0FD9;
    device.productId = 0x0080;
    device.usagePage = 0x000C;
    device.usage = 0x0001;
    return device;
}

DeviceInfo heartBelt()
{
    DeviceInfo device;
    device.id = bleId(0xC0FFEE123456ull);
    device.kind = Kind::Ble;
    device.name = "Polar H10";
    device.path = addressText(0xC0FFEE123456ull);
    device.services = {*canonicalUuid("180d"), *canonicalUuid("180f")};
    return device;
}

struct Opening {
    std::optional<OpenResult> result;

    Opened callback()
    {
        return [this](OpenResult made) { result = std::move(made); };
    }
};

std::unique_ptr<Device> openOn(FakeBackend& backend, const DeviceInfo& device, std::shared_ptr<Events> events,
                               SerialOptions serial = {})
{
    Opening opening;
    backend.open({device, serial, std::move(events)}, opening.callback());
    REQUIRE(opening.result.has_value());
    INFO(opening.result->problem);
    REQUIRE(opening.result->device != nullptr);
    return std::move(opening.result->device);
}

std::string problemOf(const std::function<void(Completion)>& run)
{
    std::optional<std::string> problem;
    run([&problem](std::string text) { problem = std::move(text); });
    REQUIRE(problem.has_value());
    return *problem;
}

}

TEST_CASE("SCR-026: kinds, uuids, ids and details are written one way")
{
    CHECK(kindName(Kind::Hid) == "hid");
    CHECK(kindNamed("ble") == Kind::Ble);
    CHECK_FALSE(kindNamed("usb").has_value());

    CHECK(canonicalUuid("180D") == "0000180d-0000-1000-8000-00805f9b34fb");
    CHECK(canonicalUuid("0x2a37") == "00002a37-0000-1000-8000-00805f9b34fb");
    CHECK(canonicalUuid("0000FFE0") == "0000ffe0-0000-1000-8000-00805f9b34fb");
    CHECK(canonicalUuid("{6E400001-B5A3-F393-E0A9-E50E24DCCA9E}") == "6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    for (const char* bad : {"", "18", "180g", "6e400001b5a3f393e0a9e50e24dcca9e", "6e400001-b5a3-f393-e0a9-e50e24dcca9"}) {
        CAPTURE(bad);
        CHECK_FALSE(canonicalUuid(bad).has_value());
    }

    CHECK(bleId(0xC0FFEE123456ull) == "ble:c0ffee123456");
    CHECK(bleAddressOf("ble:C0FFEE123456") == 0xC0FFEE123456ull);
    CHECK_FALSE(bleAddressOf("ble:c0ffee12345").has_value());
    CHECK_FALSE(bleAddressOf("hid:c0ffee123456").has_value());
    CHECK(addressText(0xC0FFEE123456ull) == "C0:FF:EE:12:34:56");

    CHECK(detailOf(serialBoard()) == "COM3 \u00B7 1A86:7523");
    CHECK(detailOf(deck()) == "0FD9:0080");
    CHECK(detailOf(heartBelt()) == "C0:FF:EE:12:34:56");

    CHECK(validSerialOptions({}));
    CHECK_FALSE(validSerialOptions({.baudRate = 10}));
    CHECK_FALSE(validSerialOptions({.dataBits = 9}));
    CHECK_FALSE(validSerialOptions({.dataBits = 5, .stopBits = StopBits::Two}));
    CHECK_FALSE(validSerialOptions({.dataBits = 8, .stopBits = StopBits::OneAndHalf}));
    CHECK(validSerialOptions({.dataBits = 5, .stopBits = StopBits::OneAndHalf}));
}

TEST_CASE("SCR-026: filters pick devices of the asked kind by ids, usage, services and name")
{
    const DeviceInfo board = serialBoard();
    const DeviceInfo pad = deck();
    const DeviceInfo belt = heartBelt();

    CHECK(matches(board, Kind::Serial, {}));
    CHECK_FALSE(matches(board, Kind::Hid, {}));
    CHECK(matches(board, Kind::Serial, {Filter{.vendorId = 0x1A86}}));
    CHECK_FALSE(matches(board, Kind::Serial, {Filter{.vendorId = 0x1A86, .productId = 0x5523}}));
    CHECK(matches(board, Kind::Serial, {Filter{.vendorId = 0x10C4}, Filter{.productId = 0x7523}}));

    CHECK(matches(pad, Kind::Hid, {Filter{.usagePage = 0x000C}}));
    CHECK(matches(pad, Kind::Hid, {Filter{.usagePage = 0x000C, .usage = 0x0001}}));
    CHECK_FALSE(matches(pad, Kind::Hid, {Filter{.usagePage = 0xFF00}}));
    CHECK(matches(pad, Kind::Hid, {Filter{.namePrefix = "Stream Deck"}}));
    CHECK_FALSE(matches(pad, Kind::Hid, {Filter{.namePrefix = "stream deck"}}));

    CHECK(matches(belt, Kind::Ble, {Filter{.services = {"180d"}}}));
    CHECK(matches(belt, Kind::Ble, {Filter{.services = {"0000180F-0000-1000-8000-00805F9B34FB", "ffe0"}}}));
    CHECK_FALSE(matches(belt, Kind::Ble, {Filter{.services = {"ffe0"}}}));
    CHECK_FALSE(matches(belt, Kind::Ble, {Filter{.services = {"not a uuid"}}}));
    CHECK(matches(belt, Kind::Ble, {Filter{.services = {"180d"}, .namePrefix = "Polar"}}));
    CHECK_FALSE(matches(belt, Kind::Ble, {Filter{.services = {"180d"}, .namePrefix = "Garmin"}}));
}

TEST_CASE("SCR-026: the fake backend lists what is plugged in and says when that changes")
{
    FakeBackend backend;
    int changes = 0;
    backend.watch([&changes] { ++changes; });
    CHECK(backend.devices().empty());

    backend.add(serialBoard());
    backend.add(deck());
    CHECK(changes == 2);
    REQUIRE(backend.devices().size() == 2);
    CHECK(backend.devices()[0] == serialBoard());

    DeviceInfo renamed = serialBoard();
    renamed.name = "USB-SERIAL CH340 (COM4)";
    backend.add(renamed);
    CHECK(changes == 3);
    CHECK(backend.devices().size() == 2);
    CHECK(backend.devices()[1].name == "USB-SERIAL CH340 (COM4)");

    backend.remove(deck().id);
    CHECK(changes == 4);
    CHECK(backend.devices().size() == 1);

    CHECK_FALSE(backend.scanning());
    backend.scan(true);
    CHECK(backend.scanning());
    backend.scan(false);
    CHECK_FALSE(backend.scanning());
}

TEST_CASE("SCR-026: a serial device opens with its settings, writes, sets its signals and hands over what it reads")
{
    FakeBackend backend;
    backend.add(serialBoard());
    const auto events = std::make_shared<RecordedEvents>();
    const SerialOptions options{.baudRate = 9600, .parity = Parity::Even, .flowControl = FlowControl::Hardware,
                                .dtr = false};
    std::unique_ptr<Device> port = openOn(backend, serialBoard(), events, options);
    const std::shared_ptr<FakeLink> link = backend.link(serialBoard().id);
    REQUIRE(link != nullptr);
    CHECK(link->serial() == options);

    CHECK(problemOf([&](Completion done) { port->write({0x48, 0x69}, std::move(done)); }).empty());
    CHECK(link->written() == std::vector<Bytes>{{0x48, 0x69}});
    CHECK(problemOf([&](Completion done) { port->setSignals(true, std::nullopt, std::move(done)); }).empty());
    CHECK(link->dtr() == true);
    CHECK_FALSE(link->rts().has_value());
    CHECK_FALSE(problemOf([&](Completion done) { port->sendReport(1, {1}, std::move(done)); }).empty());

    link->sendData({0x4F, 0x4B});
    REQUIRE(events->received.size() == 1);
    CHECK(events->received[0] == Bytes{0x4F, 0x4B});

    link->fail("the cable is loose");
    CHECK(problemOf([&](Completion done) { port->write({1}, std::move(done)); }) == "the cable is loose");

    port.reset();
    CHECK(link->closed());
    link->sendData({1});
    CHECK(events->received.size() == 1);
    CHECK(events->closings.empty());
}

TEST_CASE("SCR-026: a HID device sends output and feature reports, reads features back and reports input")
{
    FakeBackend backend;
    backend.add(deck());
    const auto events = std::make_shared<RecordedEvents>();
    std::unique_ptr<Device> pad = openOn(backend, deck(), events);
    const std::shared_ptr<FakeLink> link = backend.link(deck().id);
    REQUIRE(link != nullptr);

    CHECK(problemOf([&](Completion done) { pad->sendReport(2, {0xAA, 0xBB}, std::move(done)); }).empty());
    CHECK(link->reports() == std::vector<FakeReport>{FakeReport{std::uint8_t{2}, Bytes{0xAA, 0xBB}}});
    CHECK(problemOf([&](Completion done) { pad->sendFeature(3, {0x55}, std::move(done)); }).empty());
    CHECK(link->featuresSent() == std::vector<FakeReport>{FakeReport{std::uint8_t{3}, Bytes{0x55}}});

    link->setFeature(5, {9, 8, 7});
    std::optional<std::pair<std::string, Bytes>> got;
    pad->getFeature(5, [&got](std::string problem, Bytes bytes) { got = std::pair{std::move(problem), std::move(bytes)}; });
    REQUIRE(got.has_value());
    CHECK(got->first.empty());
    CHECK(got->second == Bytes{9, 8, 7});
    pad->getFeature(6, [&got](std::string problem, Bytes bytes) { got = std::pair{std::move(problem), std::move(bytes)}; });
    CHECK_FALSE(got->first.empty());

    link->sendReport(1, {0, 1, 0, 0});
    REQUIRE(events->reports.size() == 1);
    CHECK(events->reports[0] == FakeReport{std::uint8_t{1}, Bytes{0, 1, 0, 0}});
    CHECK_FALSE(problemOf([&](Completion done) { pad->write({1}, std::move(done)); }).empty());
}

TEST_CASE("SCR-026: a Bluetooth LE device lists services, reads, writes with and without response and notifies "
          "while subscribed")
{
    FakeBackend backend;
    const auto events = std::make_shared<RecordedEvents>();
    const std::string heartRate = *canonicalUuid("180d");
    const std::string measurement = *canonicalUuid("2a37");
    const std::string control = *canonicalUuid("2a39");
    std::unique_ptr<Device> belt = openOn(backend, heartBelt(), events);
    const std::shared_ptr<FakeLink> link = backend.link(heartBelt().id);
    REQUIRE(link != nullptr);
    link->setServices({{heartRate, {{measurement, {"notify"}}, {control, {"write"}}}}});
    link->setValue(heartRate, control, {0});

    std::vector<BleService> services;
    belt->services([&services](std::string, std::vector<BleService> listed) { services = std::move(listed); });
    REQUIRE(services.size() == 1);
    CHECK(services[0].characteristics.size() == 2);

    std::optional<Bytes> value;
    belt->read(heartRate, control, [&value](std::string problem, Bytes bytes) {
        if (problem.empty()) {
            value = std::move(bytes);
        }
    });
    CHECK(value == Bytes{0});

    CHECK(problemOf([&](Completion done) { belt->writeValue(heartRate, control, {1}, true, std::move(done)); }).empty());
    CHECK(problemOf([&](Completion done) { belt->writeValue(heartRate, control, {2}, false, std::move(done)); })
              .empty());
    CHECK(link->valueWrites() == std::vector<FakeValueWrite>{{heartRate, control, {1}, true},
                                                             {heartRate, control, {2}, false}});

    link->notify(heartRate, measurement, {0x00, 72});
    CHECK(events->notifications.empty());
    CHECK(problemOf([&](Completion done) { belt->subscribe(heartRate, measurement, true, std::move(done)); }).empty());
    link->notify(heartRate, measurement, {0x00, 73});
    REQUIRE(events->notifications.size() == 1);
    CHECK(events->notifications[0].characteristic == measurement);
    CHECK(events->notifications[0].bytes == Bytes{0x00, 73});
    CHECK(problemOf([&](Completion done) { belt->subscribe(heartRate, measurement, false, std::move(done)); }).empty());
    link->notify(heartRate, measurement, {0x00, 74});
    CHECK(events->notifications.size() == 1);
}

TEST_CASE("SCR-026: a device removed while open tells its owner once and refuses what comes after")
{
    FakeBackend backend;
    backend.add(serialBoard());
    const auto events = std::make_shared<RecordedEvents>();
    std::unique_ptr<Device> port = openOn(backend, serialBoard(), events);
    const std::shared_ptr<FakeLink> link = backend.link(serialBoard().id);
    REQUIRE(link != nullptr);

    backend.remove(serialBoard().id);
    REQUIRE(events->closings.size() == 1);
    CHECK(events->closings[0].first == Closed::Removed);
    CHECK(link->closed());
    CHECK_FALSE(problemOf([&](Completion done) { port->write({1}, std::move(done)); }).empty());
    link->drop(Closed::Failed, "again");
    CHECK(events->closings.size() == 1);

    Opening missing;
    backend.open({serialBoard(), {}, events}, missing.callback());
    REQUIRE(missing.result.has_value());
    CHECK(missing.result->device == nullptr);
    CHECK(missing.result->missing);

    backend.add(serialBoard());
    backend.failNextOpen("Access is denied.");
    Opening refused;
    backend.open({serialBoard(), {}, events}, refused.callback());
    REQUIRE(refused.result.has_value());
    CHECK(refused.result->device == nullptr);
    CHECK_FALSE(refused.result->missing);
    CHECK(refused.result->problem == "Access is denied.");
    CHECK(backend.opens() == 3);
}
