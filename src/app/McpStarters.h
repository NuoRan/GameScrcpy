#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>
#include <vector>

namespace gs::app {

struct McpStarter {
    QString id;
    QString slug;
    std::vector<std::pair<QString, QByteArray>> files;
};

[[nodiscard]] QStringList mcpStarterKinds();
[[nodiscard]] QString mcpStarterSlug(const QString& name);
[[nodiscard]] std::optional<McpStarter> mcpStarter(const QString& kind, const QString& name, QString& problem);

}
