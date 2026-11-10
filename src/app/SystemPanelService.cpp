#include "SystemPanelService.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>

#include <mutex>
#include <string>
#include <utility>

namespace gs::app {

struct SystemPanelService::Shared {
    std::mutex mutex;
    QObject* context = nullptr;
    Show show;
    std::map<QString, PanelReach> reach;
};

std::optional<protocol::SystemPanelAction> panelActionOf(std::string_view kind) noexcept
{
    if (kind == "notifications") {
        return protocol::SystemPanelAction::ExpandNotifications;
    }
    if (kind == "settings") {
        return protocol::SystemPanelAction::ExpandQuickSettings;
    }
    if (kind == "collapse") {
        return protocol::SystemPanelAction::Collapse;
    }
    return std::nullopt;
}

std::string_view panelKindOf(protocol::SystemPanelAction action) noexcept
{
    switch (action) {
    case protocol::SystemPanelAction::ExpandNotifications:
        return "notifications";
    case protocol::SystemPanelAction::ExpandQuickSettings:
        return "settings";
    case protocol::SystemPanelAction::Collapse:
        return "collapse";
    }
    return {};
}

SystemPanelService::SystemPanelService(QObject& context, Show show) : m_shared(std::make_shared<Shared>())
{
    m_shared->context = &context;
    m_shared->show = std::move(show);
    LogicServices::instance().offerCall(std::string(kService),
                                        [shared = m_shared](const LogicCaller& caller, std::string_view json) {
                                            return answer(*shared, caller, json);
                                        });
}

SystemPanelService::~SystemPanelService()
{
    LogicServices::instance().withdraw(kService);
    const std::scoped_lock lock(m_shared->mutex);
    m_shared->context = nullptr;
}

void SystemPanelService::setReach(std::map<QString, PanelReach> reach)
{
    const std::scoped_lock lock(m_shared->mutex);
    m_shared->reach = std::move(reach);
}

script::ServiceAnswer SystemPanelService::call(const LogicCaller& caller, std::string_view json) const
{
    return answer(*m_shared, caller, json);
}

script::ServiceAnswer SystemPanelService::answer(Shared& shared, const LogicCaller& caller, std::string_view json)
{
    const QJsonDocument asked = QJsonDocument::fromJson(QByteArray(json.data(), static_cast<qsizetype>(json.size())));
    const std::optional<protocol::SystemPanelAction> action =
        panelActionOf(asked.object().value(QStringLiteral("kind")).toString().toStdString());
    if (!action) {
        return {"null", "GS-API-017", "the panel must be \"notifications\", \"settings\" or \"collapse\""};
    }
    if (caller.device.isEmpty()) {
        return {"null", "GS-API-090", "this logic does not run on a phone, so there is no notification shade to reach"};
    }
    const std::scoped_lock lock(shared.mutex);
    const auto found = shared.reach.find(caller.device);
    if (found == shared.reach.end() || shared.context == nullptr) {
        return {"null", "GS-API-090", "the phone of this logic is not connected"};
    }
    if (found->second == PanelReach::NoServer) {
        return {"null", "GS-API-091",
                "the phone is connected without the GameScrcpy server, as in a window without a picture, so its "
                "notification shade cannot be reached"};
    }
    if (found->second == PanelReach::Waiting) {
        return {"false", {}, {}};
    }
    QMetaObject::invokeMethod(
        shared.context, [show = shared.show, serial = caller.device, panel = *action] { show(serial, panel); },
        Qt::QueuedConnection);
    return {"true", {}, {}};
}

}
