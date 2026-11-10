#include "LogicServices.h"
#include "SystemPanelService.h"

#include <gs/protocol/Codec.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QObject>
#include <QString>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using gs::app::LogicCaller;
using gs::app::LogicServices;
using gs::app::PanelReach;
using gs::app::SystemPanelService;
using gs::protocol::SystemPanelAction;
using gs::script::ServiceAnswer;

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

struct Shown {
    QString serial;
    SystemPanelAction action = SystemPanelAction::ExpandNotifications;

    friend bool operator==(const Shown&, const Shown&) = default;
};

LogicCaller callerOn(const QString& serial)
{
    return LogicCaller{.logic = 7, .script = QStringLiteral("shooter"), .device = serial};
}

std::string kindJson(const char* kind)
{
    return std::string(R"({"kind":")") + kind + "\"}";
}

}

TEST_CASE("panel.show refuses logic without a phone with GS-API-090 and a phone without the server with GS-API-091 "
          "(ADR-0105)")
{
    const Application application;
    QObject context;
    std::vector<Shown> shown;
    SystemPanelService service(context, [&shown](const QString& serial, SystemPanelAction action) {
        shown.push_back({serial, action});
    });
    service.setReach({{QStringLiteral("usb-phone"), PanelReach::Ready},
                      {QStringLiteral("aoa-phone"), PanelReach::NoServer},
                      {QStringLiteral("reconnecting"), PanelReach::Waiting}});

    const ServiceAnswer alone = service.call(LogicCaller{}, kindJson("notifications"));
    CHECK(alone.code == "GS-API-090");
    CHECK_FALSE(alone.problem.empty());
    CHECK(service.call(callerOn(QStringLiteral("unplugged")), kindJson("notifications")).code == "GS-API-090");

    const ServiceAnswer noPicture = service.call(callerOn(QStringLiteral("aoa-phone")), kindJson("settings"));
    CHECK(noPicture.code == "GS-API-091");
    CHECK_FALSE(noPicture.problem.empty());

    const ServiceAnswer later = service.call(callerOn(QStringLiteral("reconnecting")), kindJson("collapse"));
    CHECK(later.code.empty());
    CHECK(later.json == "false");

    CHECK(service.call(callerOn(QStringLiteral("usb-phone")), kindJson("sideways")).code == "GS-API-017");
    CHECK(service.call(callerOn(QStringLiteral("usb-phone")), "[]").code == "GS-API-017");

    QCoreApplication::processEvents();
    CHECK(shown.empty());
}

TEST_CASE("panel.show asks the phone of the logic for each panel through the service channel, on the owner's thread "
          "(ADR-0105)")
{
    const Application application;
    QObject context;
    std::vector<Shown> shown;
    SystemPanelService service(context, [&shown](const QString& serial, SystemPanelAction action) {
        shown.push_back({serial, action});
    });
    service.setReach(
        {{QStringLiteral("usb-phone"), PanelReach::Ready}, {QStringLiteral("gs-demo"), PanelReach::Ready}});
    REQUIRE(LogicServices::instance().offers(SystemPanelService::kService));

    const LogicCaller caller = callerOn(QStringLiteral("usb-phone"));
    for (const char* kind : {"notifications", "settings", "collapse"}) {
        const ServiceAnswer asked =
            LogicServices::instance().call(caller, SystemPanelService::kService, kindJson(kind));
        CHECK(asked.code.empty());
        CHECK(asked.json == "true");
    }
    CHECK(shown.empty());
    QCoreApplication::processEvents();
    CHECK(shown == std::vector<Shown>{{QStringLiteral("usb-phone"), SystemPanelAction::ExpandNotifications},
                                      {QStringLiteral("usb-phone"), SystemPanelAction::ExpandQuickSettings},
                                      {QStringLiteral("usb-phone"), SystemPanelAction::Collapse}});

    service.setReach({{QStringLiteral("usb-phone"), PanelReach::NoServer}});
    CHECK(service.call(caller, kindJson("notifications")).code == "GS-API-091");
    CHECK(service.call(callerOn(QStringLiteral("gs-demo")), kindJson("notifications")).code == "GS-API-090");
    QCoreApplication::processEvents();
    CHECK(shown.size() == 3);
}

TEST_CASE("panel.show goes away with its owner (ADR-0105)")
{
    const Application application;
    {
        QObject context;
        const SystemPanelService service(context, [](const QString&, SystemPanelAction) {});
        CHECK(LogicServices::instance().offers(SystemPanelService::kService));
    }
    CHECK_FALSE(LogicServices::instance().offers(SystemPanelService::kService));
    const ServiceAnswer missing =
        LogicServices::instance().call(callerOn(QStringLiteral("usb-phone")), SystemPanelService::kService,
                                       kindJson("collapse"));
    CHECK(missing.code == "GS-API-070");
}

TEST_CASE("each panel kind is one SystemPanel action and goes to the phone as the SystemPanel control message "
          "(ADR-0105)")
{
    CHECK(gs::app::panelActionOf("notifications") == SystemPanelAction::ExpandNotifications);
    CHECK(gs::app::panelActionOf("settings") == SystemPanelAction::ExpandQuickSettings);
    CHECK(gs::app::panelActionOf("collapse") == SystemPanelAction::Collapse);
    CHECK_FALSE(gs::app::panelActionOf("quickSettings").has_value());
    CHECK_FALSE(gs::app::panelActionOf("").has_value());
    for (const SystemPanelAction action :
         {SystemPanelAction::ExpandNotifications, SystemPanelAction::ExpandQuickSettings, SystemPanelAction::Collapse}) {
        CHECK(gs::app::panelActionOf(gs::app::panelKindOf(action)) == action);
        gs::protocol::SystemPanel message;
        message.action = action;
        const auto frame = gs::protocol::encodeFrame(message);
        REQUIRE(frame.has_value());
        CHECK(*frame == gs::protocol::Bytes{0x03, 0x06, 0x00, 0x00, 0x00, 0x01, static_cast<std::uint8_t>(action)});
    }
}
