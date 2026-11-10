#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringView>

#include <optional>

namespace gs::app {

[[nodiscard]] QJsonArray mcpPromptList();
[[nodiscard]] std::optional<QJsonObject> mcpPrompt(const QString& name, const QJsonObject& arguments);
[[nodiscard]] QJsonArray mcpResourceList();
[[nodiscard]] std::optional<QJsonObject> mcpResource(const QString& uri);
[[nodiscard]] std::optional<QString> mcpRecipe(QStringView id);
[[nodiscard]] QString mcpRecipeIndex();
[[nodiscard]] QString mcpSkillText();

}
