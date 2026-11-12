#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

class AppSettings;

[[nodiscard]] bool isCrashFolder(std::string_view name) noexcept;
[[nodiscard]] std::vector<std::string> crashFolders(const std::filesystem::path& directory);
void keepNewestCrashes(const std::filesystem::path& directory, std::size_t count);

inline constexpr std::size_t kKeptExtensionReports = 20;

void keepNewestCrashesInFolders(const std::filesystem::path& directory, std::size_t count);
[[nodiscard]] std::optional<std::string> unacknowledgedCrash(const std::vector<std::string>& folders,
                                                             std::string_view acknowledged);

class CrashReport : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged FINAL)

public:
    static constexpr std::size_t kKept = 5;

    CrashReport(AppSettings& settings, std::filesystem::path directory, QObject* parent = nullptr);
    ~CrashReport() override;

    static CrashReport* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] bool pending() const noexcept { return m_pending; }
    [[nodiscard]] std::optional<std::filesystem::path> newestFolder() const;
    Q_INVOKABLE void acknowledge();

signals:
    void pendingChanged();

private:
    static inline CrashReport* s_instance = nullptr;

    AppSettings& m_settings;
    const std::filesystem::path m_directory;
    std::optional<std::string> m_newest;
    bool m_pending = false;
};

}
