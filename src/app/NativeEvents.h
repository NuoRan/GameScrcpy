#pragma once

#include "LogicServices.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

namespace gs::app {

class NativeExtensions;

struct EventRoutes {
    std::function<std::vector<LogicCaller>()> callers;
    std::function<void(std::uint64_t logic, const QString& kind, const QString& id, const QString& json)> tell;
    std::function<bool(const QString& listener, const QString& emitter)> hears;
};

class NativeEvents final : public QObject
{
    Q_OBJECT

public:
    explicit NativeEvents(const NativeExtensions& extensions, QObject* parent = nullptr);

    void setRoutes(EventRoutes routes);
    void deliver(const QString& extension, const QString& topic, const QString& json);

private:
    EventRoutes m_routes;
};

}
