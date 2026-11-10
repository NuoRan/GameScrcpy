#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <filesystem>
#include <optional>

namespace gs::app {

struct McpBlueprintEdit {
    QString code;
    QString problem;
    QByteArray source;
    QByteArray layout;
    QString layoutFile;
    bool codeChanged = false;
    QJsonObject summary;

    [[nodiscard]] bool ok() const noexcept { return code.isEmpty(); }
};

[[nodiscard]] QString mcpBlueprintFile(const std::filesystem::path& folder, const QString& given, QString& problem);
[[nodiscard]] QJsonObject mcpReadBlueprint(const std::filesystem::path& folder, const QString& file, const QString& page,
                                           const std::optional<QString>& catalog, QString& problem);
[[nodiscard]] McpBlueprintEdit mcpEditBlueprint(const std::filesystem::path& folder, const QString& file,
                                                const QJsonArray& operations);

}
