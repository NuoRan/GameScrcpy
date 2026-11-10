#include "HardwareAccess.h"

#include <gs/hardware/FakeBackend.h>
#include <gs/input/InputSource.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using gs::app::HardwareAccess;
using gs::app::HardwareReplies;
using gs::app::LogicCaller;
namespace hardware = gs::hardware;

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

hardware::DeviceInfo board()
{
    hardware::DeviceInfo device;
    device.id = "serial:board";
    device.kind = hardware::Kind::Serial;
    device.name = "Board";
    device.path = "\\\\.\\COM7";
    device.vendorId = 0x1A86;
    device.productId = 0x7523;
    return device;
}

hardware::DeviceInfo pad()
{
    hardware::DeviceInfo device;
    device.id = "hid:pad";
    device.kind = hardware::Kind::Hid;
    device.name = "Pad";
    device.path = "\\\\?\\hid#pad";
    device.vendorId = 0x0FD9;
    device.productId = 0x0080;
    device.usagePage = 0x000C;
    device.usage = 0x0001;
    return device;
}

hardware::DeviceInfo belt()
{
    hardware::DeviceInfo device;
    device.id = hardware::bleId(0xC0FFEE123456ull);
    device.kind = hardware::Kind::Ble;
    device.name = "Belt";
    device.path = hardware::addressText(0xC0FFEE123456ull);
    device.services = {*hardware::canonicalUuid("180d")};
    return device;
}

QString text(const char* json)
{
    return QString::fromUtf8(json);
}

struct Told {
    QString kind;
    QString id;
    QString json;
};

struct Fed {
    QString serial;
    gs::input::DeviceEvent event;
};

const LogicCaller kOwn{.logic = 1};
const LogicCaller kDeck{.logic = 2, .extension = QStringLiteral("com.example.deck")};
const LogicCaller kOther{.logic = 3, .extension = QStringLiteral("com.example.other")};

struct Fixture {
    Application app;
    QTemporaryDir data;
    std::filesystem::path grantsFile = std::filesystem::path(data.path().toStdWString()) / "hardware-grants.json";
    hardware::FakeBackend* backend = nullptr;
    std::unique_ptr<HardwareAccess> access;
    std::vector<std::pair<quint64, QString>> answers;
    std::vector<std::pair<quint64, QString>> refusals;
    std::vector<Told> told;
    std::vector<Fed> fed;
    QStringList seen;

    Fixture()
    {
        REQUIRE(data.isValid());
        auto made = std::make_unique<hardware::FakeBackend>();
        backend = made.get();
        backend->add(board());
        backend->add(pad());
        backend->add(belt());
        access = std::make_unique<HardwareAccess>(std::move(made), grantsFile);
        access->setReplies(HardwareReplies{
            .answer = [this](std::uint64_t, quint64 request, const QString& json) { answers.emplace_back(request, json); },
            .refuse = [this](std::uint64_t, quint64 request,
                             const QString& message) { refusals.emplace_back(request, message); },
            .tell = [this](std::uint64_t, const QString& kind, const QString& id,
                           const QString& json) { told.push_back({kind, id, json}); },
        });
        access->setPermitted({QStringLiteral("com.example.deck")});
        access->setFeed([this](const QString& serial, const gs::input::DeviceEvent& event) { fed.push_back({serial, event}); },
                        [] { return QStringLiteral("PHONE-IN-FRONT"); });
        QObject::connect(access.get(), &HardwareAccess::inputSeen, [this](const QString& name) { seen.append(name); });
    }

    static void settle()
    {
        for (int round = 0; round < 4; ++round) {
            QCoreApplication::processEvents();
        }
    }

    void grant(const LogicCaller& caller, quint64 request, const char* json, const QStringList& ids)
    {
        access->request(caller, "hardware.request", request, text(json));
        REQUIRE(access->picker().value(QStringLiteral("open")).toBool());
        REQUIRE(access->allow(ids));
    }

    QString open(const LogicCaller& caller, quint64 request, const char* json)
    {
        access->request(caller, "hardware.open", request, text(json));
        settle();
        REQUIRE_FALSE(answers.empty());
        REQUIRE(answers.back().first == request);
        return answers.back().second;
    }
};

}

TEST_CASE("SCR-026: an extension without the permission hardware gets GS-API-080 from every device service")
{
    Fixture fixture;
    CHECK(fixture.access->call(kOther, "hardware.list", "{}").code == "GS-API-080");
    CHECK(fixture.access->call(kOther, "hardware.input", R"({"source":"deck","key":"k1","action":"press"})").code ==
          "GS-API-080");
    fixture.access->request(kOther, "hardware.request", 7, text(R"({"kind":"serial"})"));
    fixture.access->request(kOther, "hardware.open", 8, text(R"({"id":"serial:board"})"));
    REQUIRE(fixture.refusals.size() == 2);
    CHECK(fixture.refusals[0].first == 7);
    CHECK(fixture.refusals[0].second.startsWith(QStringLiteral("GS-API-080 ")));
    CHECK(fixture.refusals[1].first == 8);
    CHECK(fixture.refusals[1].second.startsWith(QStringLiteral("GS-API-080 ")));
    CHECK(fixture.access->picker().isEmpty());
    CHECK(fixture.answers.empty());
}

TEST_CASE("SCR-026: a device that was not allowed gives GS-API-081; what the picker allows is remembered for each owner")
{
    Fixture fixture;
    fixture.access->request(kDeck, "hardware.open", 1, text(R"({"id":"serial:board"})"));
    REQUIRE(fixture.refusals.size() == 1);
    CHECK(fixture.refusals[0].second.startsWith(QStringLiteral("GS-API-081 ")));

    fixture.access->request(kDeck, "hardware.request", 2, text(R"({"kind":"serial","filters":[{"vendorId":6790}]})"));
    const QVariantMap picker = fixture.access->picker();
    CHECK(picker.value(QStringLiteral("open")).toBool());
    CHECK(picker.value(QStringLiteral("requester")).toString() == QStringLiteral("com.example.deck"));
    CHECK_FALSE(picker.value(QStringLiteral("own")).toBool());
    CHECK(picker.value(QStringLiteral("kind")).toString() == QStringLiteral("serial"));
    const QVariantList offered = picker.value(QStringLiteral("devices")).toList();
    REQUIRE(offered.size() == 1);
    CHECK(offered[0].toMap().value(QStringLiteral("id")).toString() == QStringLiteral("serial:board"));
    CHECK_FALSE(offered[0].toMap().value(QStringLiteral("granted")).toBool());
    CHECK(fixture.access->allow({QStringLiteral("serial:board"), QStringLiteral("hid:pad")}));
    REQUIRE(fixture.answers.size() == 1);
    CHECK(fixture.answers[0].first == 2);
    CHECK(fixture.answers[0].second == QStringLiteral(R"([{"id":"serial:board","kind":"serial","name":"Board"}])"));
    CHECK(fixture.access->picker().isEmpty());

    const gs::script::ServiceAnswer listed = fixture.access->call(kDeck, "hardware.list", "{}");
    CHECK(listed.code.empty());
    CHECK(listed.json == R"([{"id":"serial:board","kind":"serial","name":"Board","present":true}])");
    CHECK(fixture.access->call(kOwn, "hardware.list", "{}").json == "[]");
    fixture.access->request(kOwn, "hardware.open", 3, text(R"({"id":"serial:board"})"));
    REQUIRE(fixture.refusals.size() == 2);
    CHECK(fixture.refusals[1].first == 3);
    CHECK(fixture.refusals[1].second.startsWith(QStringLiteral("GS-API-081 ")));

    fixture.access->request(kOwn, "hardware.request", 4, text(R"({"kind":"hid"})"));
    const QVariantMap own = fixture.access->picker();
    CHECK(own.value(QStringLiteral("own")).toBool());
    CHECK(own.value(QStringLiteral("devices")).toList().size() == 1);
    fixture.access->cancelPicker();
    REQUIRE(fixture.answers.size() == 2);
    CHECK(fixture.answers[1].first == 4);
    CHECK(fixture.answers[1].second == QStringLiteral("[]"));
    CHECK(fixture.access->grantsOf(QStringLiteral("mine")).empty());

    fixture.access.reset();
    const HardwareAccess again(std::make_unique<hardware::FakeBackend>(), fixture.grantsFile);
    const std::vector<gs::app::HardwareGrant> remembered = again.grantsOf(QStringLiteral("com.example.deck"));
    REQUIRE(remembered.size() == 1);
    CHECK(remembered[0].id == "serial:board");
    CHECK(remembered[0].kind == hardware::Kind::Serial);
    CHECK(remembered[0].name == "Board");
    CHECK(again.grantsOf(QStringLiteral("mine")).empty());
    const QVariantList shown = again.grantMap().value(QStringLiteral("com.example.deck")).toList();
    REQUIRE(shown.size() == 1);
    CHECK_FALSE(shown[0].toMap().value(QStringLiteral("present")).toBool());
}

TEST_CASE("SCR-026: an open serial device writes, hands over what it reads and closes when its logic ends")
{
    Fixture fixture;
    fixture.grant(kOwn, 10, R"({"kind":"serial"})", {QStringLiteral("serial:board")});
    const QString opened =
        fixture.open(kOwn, 11, R"({"id":"serial:board","options":{"baudRate":9600,"parity":"even","dtr":false}})");
    CHECK(opened == QStringLiteral(R"({"handle":"d1","id":"serial:board","kind":"serial","name":"Board"})"));
    const std::shared_ptr<hardware::FakeLink> link = fixture.backend->link("serial:board");
    REQUIRE(link != nullptr);
    CHECK(link->serial().baudRate == 9600);
    CHECK(link->serial().parity == hardware::Parity::Even);
    CHECK_FALSE(link->serial().dtr);
    CHECK(link->serial().rts);
    CHECK(fixture.access->openCount() == 1);

    fixture.access->request(kOwn, "hardware.write", 12, text(R"({"handle":"d1","data":"SGk="})"));
    Fixture::settle();
    CHECK(link->written() == std::vector<hardware::Bytes>{hardware::Bytes{0x48, 0x69}});
    REQUIRE(fixture.answers.back().first == 12);
    CHECK(fixture.answers.back().second == QStringLiteral("null"));

    fixture.access->request(kOwn, "hardware.write", 13, text(R"({"handle":"d1","data":"S@k="})"));
    REQUIRE_FALSE(fixture.refusals.empty());
    CHECK(fixture.refusals.back().first == 13);
    CHECK(fixture.refusals.back().second.startsWith(QStringLiteral("GS-API-085 ")));
    fixture.access->request(LogicCaller{.logic = 9}, "hardware.write", 14, text(R"({"handle":"d1","data":"AA=="})"));
    CHECK(fixture.refusals.back().first == 14);
    CHECK(fixture.refusals.back().second.startsWith(QStringLiteral("GS-API-084 ")));
    fixture.access->request(kOwn, "hardware.open", 15, text(R"({"id":"serial:board"})"));
    CHECK(fixture.refusals.back().first == 15);
    CHECK(fixture.refusals.back().second.startsWith(QStringLiteral("GS-API-083 ")));

    link->sendData({1, 2, 3});
    Fixture::settle();
    REQUIRE_FALSE(fixture.told.empty());
    CHECK(fixture.told.back().kind == QStringLiteral("hardware"));
    CHECK(fixture.told.back().id == QStringLiteral("d1"));
    CHECK(fixture.told.back().json == QStringLiteral(R"({"data":"AQID","type":"data"})"));

    const std::size_t toldBefore = fixture.told.size();
    fixture.access->endLogic(kOwn.logic);
    CHECK(link->closed());
    CHECK(fixture.access->openCount() == 0);
    CHECK(fixture.told.size() == toldBefore);
}

TEST_CASE("SCR-026: a device that goes away while open closes with GS-API-082 and stays allowed as not connected")
{
    Fixture fixture;
    fixture.grant(kOwn, 20, R"({"kind":"serial"})", {QStringLiteral("serial:board")});
    static_cast<void>(fixture.open(kOwn, 21, R"({"id":"serial:board"})"));
    fixture.access->request(kOwn, "hardware.control", 22, text(R"({"handle":"d1","op":"getFeature","report":1})"));
    CHECK(fixture.refusals.back().first == 22);
    CHECK(fixture.refusals.back().second.startsWith(QStringLiteral("GS-API-085 ")));

    fixture.backend->remove("serial:board");
    Fixture::settle();
    REQUIRE_FALSE(fixture.told.empty());
    CHECK(fixture.told.back().id == QStringLiteral("d1"));
    CHECK(fixture.told.back().json ==
          QStringLiteral(R"({"message":"GS-API-082 Board is not connected","reason":"removed","type":"closed"})"));
    CHECK(fixture.access->openCount() == 0);
    CHECK(fixture.access->call(kOwn, "hardware.list", "{}").json ==
          R"([{"id":"serial:board","kind":"serial","name":"Board","present":false}])");
    fixture.access->request(kOwn, "hardware.open", 23, text(R"({"id":"serial:board"})"));
    CHECK(fixture.refusals.back().first == 23);
    CHECK(fixture.refusals.back().second == QStringLiteral("GS-API-082 Board is not connected"));
}

TEST_CASE("SCR-026: HID and Bluetooth devices send, read and notify through hardware.write and hardware.control")
{
    Fixture fixture;
    fixture.grant(kDeck, 30, R"({"kind":"hid"})", {QStringLiteral("hid:pad")});
    fixture.grant(kDeck, 31, R"({"kind":"ble","filters":[{"services":["180d"]}]})",
                  {QString::fromStdString(belt().id)});
    static_cast<void>(fixture.open(kDeck, 32, R"({"id":"hid:pad"})"));
    const QString beltOpened = fixture.open(kDeck, 33, R"({"id":"ble:c0ffee123456"})");
    CHECK(beltOpened == QStringLiteral(R"({"handle":"d2","id":"ble:c0ffee123456","kind":"ble","name":"Belt"})"));
    const std::shared_ptr<hardware::FakeLink> padLink = fixture.backend->link("hid:pad");
    const std::shared_ptr<hardware::FakeLink> beltLink = fixture.backend->link("ble:c0ffee123456");
    REQUIRE(padLink != nullptr);
    REQUIRE(beltLink != nullptr);

    fixture.access->request(kDeck, "hardware.write", 34, text(R"({"handle":"d1","report":2,"data":"qrs="})"));
    padLink->setFeature(3, {9, 8, 7});
    fixture.access->request(kDeck, "hardware.control", 35, text(R"({"handle":"d1","op":"getFeature","report":3})"));
    Fixture::settle();
    CHECK(padLink->reports() == std::vector<hardware::FakeReport>{hardware::FakeReport{std::uint8_t{2}, hardware::Bytes{0xAA, 0xBB}}});
    REQUIRE(fixture.answers.back().first == 35);
    CHECK(fixture.answers.back().second == QStringLiteral(R"({"data":"CQgH"})"));

    const std::string heartRate = *hardware::canonicalUuid("180d");
    const std::string measurement = *hardware::canonicalUuid("2a37");
    beltLink->setValue(heartRate, measurement, {0, 60});
    fixture.access->request(kDeck, "hardware.control", 36,
                            text(R"({"handle":"d2","op":"read","service":"180D","characteristic":"0x2a37"})"));
    fixture.access->request(kDeck, "hardware.control", 37,
                            text(R"({"handle":"d2","op":"subscribe","service":"180d","characteristic":"2a37"})"));
    fixture.access->request(kDeck, "hardware.write", 38,
                            text(R"({"handle":"d2","service":"180d","characteristic":"2a39","data":"AQ==","response":false})"));
    Fixture::settle();
    REQUIRE(fixture.answers.size() >= 3);
    CHECK(fixture.answers[fixture.answers.size() - 3].second == QStringLiteral(R"({"data":"ADw="})"));
    CHECK(beltLink->subscriptions().contains(heartRate + "/" + measurement));
    REQUIRE(beltLink->valueWrites().size() == 1);
    CHECK_FALSE(beltLink->valueWrites()[0].response);

    padLink->sendReport(1, {0, 1});
    beltLink->notify(heartRate, measurement, {0, 72});
    Fixture::settle();
    REQUIRE(fixture.told.size() >= 2);
    CHECK(fixture.told[fixture.told.size() - 2].json == QStringLiteral(R"({"data":"AAE=","report":1,"type":"report"})"));
    CHECK(fixture.told.back().id == QStringLiteral("d2"));
    CHECK(fixture.told.back().json ==
          QString::fromStdString(R"({"characteristic":")" + measurement + R"(","data":"AEg=","service":")" + heartRate +
                                 R"(","type":"notify"})"));
}

TEST_CASE("SCR-026: input from logic reaches the phone of its session or the one in front, and held keys let go when "
          "the logic ends")
{
    Fixture fixture;
    REQUIRE(gs::input::parseSource("Ext.mine.deck.k1").has_value());
    REQUIRE(gs::input::parseSource("Ext.com.example.deck.deck.k2").has_value());
    REQUIRE(gs::input::parseSource("Ext.mine.deck.knob").has_value());

    CHECK(fixture.access->call(kOwn, "hardware.input", R"({"source":"deck","key":"k1","action":"press"})").code.empty());
    CHECK(fixture.access->call(kOwn, "hardware.input", R"({"source":"deck","key":"knob","action":"axis","x":0.75})")
              .code.empty());
    const LogicCaller session{.logic = 2, .extension = QStringLiteral("com.example.deck"), .device = QStringLiteral("SERIAL2")};
    CHECK(fixture.access->call(session, "hardware.input", R"({"source":"deck","key":"k2","action":"press"})").code.empty());
    CHECK(fixture.access->call(kOwn, "hardware.input", R"({"source":"deck","key":"k1","action":"jump"})").code ==
          "GS-API-085");
    CHECK(fixture.access->call(kOwn, "hardware.input", R"({"source":"deck","key":"knob","action":"axis","x":2})").code ==
          "GS-API-085");
    CHECK(fixture.access->call(kOwn, "hardware.input", R"({"source":"Deck","key":"k1","action":"press"})").code ==
          "GS-API-085");
    Fixture::settle();
    REQUIRE(fixture.fed.size() == 3);
    CHECK(fixture.fed[0].serial == QStringLiteral("PHONE-IN-FRONT"));
    CHECK(fixture.fed[0].event.source == *gs::input::knownSource("Ext.mine.deck.k1"));
    CHECK(fixture.fed[0].event.action == gs::input::DeviceAction::Down);
    CHECK(fixture.fed[1].event.action == gs::input::DeviceAction::Axis);
    CHECK(fixture.fed[1].event.value.x == doctest::Approx(0.75));
    CHECK(fixture.fed[2].serial == QStringLiteral("SERIAL2"));
    CHECK(fixture.fed[2].event.source == *gs::input::knownSource("Ext.com.example.deck.deck.k2"));
    CHECK(fixture.seen == QStringList{QStringLiteral("Ext.mine.deck.k1"), QStringLiteral("Ext.mine.deck.knob"),
                                      QStringLiteral("Ext.com.example.deck.deck.k2")});

    fixture.access->endLogic(kOwn.logic);
    REQUIRE(fixture.fed.size() == 5);
    for (std::size_t index = 3; index < 5; ++index) {
        CAPTURE(index);
        CHECK(fixture.fed[index].serial == QStringLiteral("PHONE-IN-FRONT"));
        CHECK(fixture.fed[index].event.value == gs::input::Vec2{});
    }
    CHECK(fixture.fed[3].event.action != fixture.fed[4].event.action);

    CHECK(fixture.access->call(kOwn, "hardware.input", R"({"source":"deck","key":"k1","action":"press"})").code.empty());
    Fixture::settle();
    CHECK(fixture.fed.size() == 5);
}

TEST_CASE("SCR-026: taking a permission or a device back closes what was open and answers what was waiting")
{
    Fixture fixture;
    fixture.grant(kDeck, 40, R"({"kind":"hid"})", {QStringLiteral("hid:pad")});
    static_cast<void>(fixture.open(kDeck, 41, R"({"id":"hid:pad"})"));
    fixture.access->request(kDeck, "hardware.request", 42, text(R"({"kind":"serial"})"));
    CHECK(fixture.access->picker().value(QStringLiteral("open")).toBool());

    fixture.access->setPermitted({});
    REQUIRE_FALSE(fixture.told.empty());
    CHECK(fixture.told.back().json.contains(QStringLiteral(R"("reason":"forgotten")")));
    CHECK(fixture.told.back().json.contains(QStringLiteral("GS-API-080")));
    CHECK(fixture.refusals.back().first == 42);
    CHECK(fixture.refusals.back().second.startsWith(QStringLiteral("GS-API-080 ")));
    CHECK(fixture.access->picker().isEmpty());
    CHECK(fixture.access->openCount() == 0);

    fixture.grant(kOwn, 43, R"({"kind":"serial"})", {QStringLiteral("serial:board")});
    static_cast<void>(fixture.open(kOwn, 44, R"({"id":"serial:board"})"));
    CHECK(fixture.access->forget(QStringLiteral("mine"), QStringLiteral("serial:board")));
    CHECK(fixture.told.back().json.contains(QStringLiteral("GS-API-081")));
    CHECK(fixture.access->openCount() == 0);
    CHECK(fixture.access->grantsOf(QStringLiteral("mine")).empty());
    CHECK_FALSE(fixture.access->forget(QStringLiteral("mine"), QStringLiteral("serial:board")));
}

TEST_CASE("SCR-026: Bluetooth is looked for only while a Bluetooth request is shown, and a cancelled task closes the "
          "picker without an answer")
{
    Fixture fixture;
    CHECK_FALSE(fixture.backend->scanning());
    fixture.access->request(kOwn, "hardware.request", 50, text(R"({"kind":"ble"})"));
    CHECK(fixture.backend->scanning());
    CHECK(fixture.access->picker().value(QStringLiteral("kind")).toString() == QStringLiteral("ble"));
    fixture.access->drop(kOwn, 50);
    CHECK_FALSE(fixture.backend->scanning());
    CHECK(fixture.access->picker().isEmpty());
    CHECK(fixture.answers.empty());
    CHECK(fixture.refusals.empty());

    fixture.access->request(kOwn, "hardware.request", 51, text(R"({"kind":"usb"})"));
    REQUIRE(fixture.refusals.size() == 1);
    CHECK(fixture.refusals[0].second.startsWith(QStringLiteral("GS-API-085 ")));
}
