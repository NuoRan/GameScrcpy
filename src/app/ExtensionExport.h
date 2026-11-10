#pragma once

#include <gs/input/Profile.h>

#include <QString>
#include <QVariantMap>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::app {

struct ExportedComponent {
    QString ref;
    std::filesystem::path folder;
};

struct ExportedTask {
    QString id;
    QString name;
    std::filesystem::path folder;
    QVariantMap values;
    QString onError;
    bool wake = false;
    bool sleepAfter = false;
};

struct ExportedProfile {
    QString id;
    QString name;
    std::filesystem::path folder;
    input::Profile profile;
};

[[nodiscard]] std::optional<std::filesystem::path> exportTarget(const QString& fileOrUrl);
[[nodiscard]] std::string exportedExtensionId(const QString& name, std::string_view kind);
[[nodiscard]] bool exportComponentPackage(const QString& name, const QString& description,
                                          const std::vector<ExportedComponent>& components,
                                          const std::filesystem::path& target);
[[nodiscard]] bool exportTaskPackage(const ExportedTask& task, const std::filesystem::path& target);
[[nodiscard]] bool exportProfilePackage(const ExportedProfile& exported,
                                        const std::vector<ExportedComponent>& components,
                                        const std::filesystem::path& target);

}
