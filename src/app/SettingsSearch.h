#pragma once

#include <QString>
#include <QStringList>

namespace gs::app {

[[nodiscard]] bool matchesQuery(const QStringList& texts, const QString& query);

}
