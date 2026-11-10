#pragma once

#include "LogicServices.h"

#include <gs/protocol/Enums.h>
#include <gs/script/Services.h>

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

namespace gs::app {

enum class PanelReach : std::uint8_t { NoServer, Waiting, Ready };

[[nodiscard]] std::optional<protocol::SystemPanelAction> panelActionOf(std::string_view kind) noexcept;
[[nodiscard]] std::string_view panelKindOf(protocol::SystemPanelAction action) noexcept;

class SystemPanelService final
{
public:
    using Show = std::function<void(const QString& serial, protocol::SystemPanelAction action)>;

    static constexpr std::string_view kService = "panel.show";

    SystemPanelService(QObject& context, Show show);
    ~SystemPanelService();

    SystemPanelService(const SystemPanelService&) = delete;
    SystemPanelService& operator=(const SystemPanelService&) = delete;

    void setReach(std::map<QString, PanelReach> reach);
    [[nodiscard]] script::ServiceAnswer call(const LogicCaller& caller, std::string_view json) const;

private:
    struct Shared;

    [[nodiscard]] static script::ServiceAnswer answer(Shared& shared, const LogicCaller& caller,
                                                      std::string_view json);

    std::shared_ptr<Shared> m_shared;
};

}
