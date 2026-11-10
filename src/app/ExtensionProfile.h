#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <filesystem>

namespace gs::app {

struct ExtensionProfile {
    QString id;
    QString extensionId;
    QString extensionName;
    QString version;
    QByteArray json;
    QStringList patchedBy;
    QVariantList conflicts;
    std::filesystem::path folder;

    friend bool operator==(const ExtensionProfile&, const ExtensionProfile&) = default;
};

}
