#pragma once

#include <QString>

#include <filesystem>
#include <string>
#include <vector>

namespace gs::app {

struct ExtensionComponent {
    QString ref;
    QString name;
    QString extensionId;
    QString extensionName;
    std::filesystem::path folder;

    friend bool operator==(const ExtensionComponent&, const ExtensionComponent&) = default;
};

struct ExtensionTask {
    QString id;
    QString name;
    QString extensionId;
    QString extensionName;
    std::filesystem::path folder;

    friend bool operator==(const ExtensionTask&, const ExtensionTask&) = default;
};

struct ExtensionScript {
    QString extensionId;
    QString extensionName;
    QString name;
    std::filesystem::path folder;
    std::string entry;
    bool app = false;
    std::vector<std::string> permissions;

    friend bool operator==(const ExtensionScript&, const ExtensionScript&) = default;
};

[[nodiscard]] inline QString extensionComponentRef(const QString& extensionId, const QString& name)
{
    return QStringLiteral("examples/") + extensionId + u'~' + name;
}

[[nodiscard]] inline QString extensionTaskId(const QString& extensionId, const QString& name)
{
    return extensionId + u'/' + name;
}

}
