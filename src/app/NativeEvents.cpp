#include "NativeEvents.h"

#include "NativeExtensions.h"

#include <utility>

namespace gs::app {

NativeEvents::NativeEvents(const NativeExtensions& extensions, QObject* parent)
    : QObject(parent)
    , m_routes{.callers = [] { return LogicServices::instance().running(); },
               .tell = [](std::uint64_t logic, const QString& kind, const QString& id, const QString& json) {
                   static_cast<void>(LogicServices::instance().tell(logic, kind, id, json));
               }}
{
    connect(&extensions, &NativeExtensions::extensionEvent, this, &NativeEvents::deliver);
}

void NativeEvents::setRoutes(EventRoutes routes)
{
    m_routes = std::move(routes);
}

void NativeEvents::deliver(const QString& extension, const QString& topic, const QString& json)
{
    if (!m_routes.callers || !m_routes.tell) {
        return;
    }
    for (const LogicCaller& caller : m_routes.callers()) {
        if (caller.extension == extension) {
            m_routes.tell(caller.logic, QStringLiteral("extEvent"), topic, json);
        }
    }
}

}
