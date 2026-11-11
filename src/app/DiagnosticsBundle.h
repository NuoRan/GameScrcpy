#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

class QAbstractItemModel;
class QJSEngine;
class QQmlEngine;

namespace gs::app {

class AppSettings;
class CrashReport;

namespace diagnostics {

inline constexpr std::size_t kShortestSecret = 4;
inline constexpr std::size_t kShortestClipboardLine = 8;
inline constexpr std::size_t kLargestFile = std::size_t{64} << 20;

struct Secrets {
    std::vector<std::string> serials;
    std::vector<std::string> clipboard;
    std::string userName;
    std::string computerName;
};

struct Sources {
    std::string system;
    std::string devices;
    std::vector<std::filesystem::path> settings;
    std::filesystem::path logs;
    std::optional<std::filesystem::path> crash;
    std::string extensions;
    std::vector<std::filesystem::path> extensionCrashes;
};

struct Entry {
    std::string name;
    std::string data;
    bool binary = false;
};

[[nodiscard]] std::vector<std::string> serialsIn(std::string_view text);
[[nodiscard]] std::string scrubSerials(std::string_view text, const std::vector<std::string>& serials);
[[nodiscard]] std::string scrubAddresses(std::string_view text);
[[nodiscard]] std::string scrubUserPaths(std::string_view text, std::string_view userName);
[[nodiscard]] std::string scrubWifiNames(std::string_view text);
[[nodiscard]] std::string scrubClipboard(std::string_view text, const std::vector<std::string>& clipboard);
[[nodiscard]] std::string scrub(std::string_view text, const Secrets& secrets);
[[nodiscard]] std::string mask(std::string data, const Secrets& secrets);
[[nodiscard]] std::vector<Entry> entries(const Sources& sources, Secrets secrets);
[[nodiscard]] std::optional<std::string> zip(const std::vector<Entry>& entries);
[[nodiscard]] std::string describeDevices(const QAbstractItemModel& devices);
[[nodiscard]] std::vector<std::string> serialsOf(const QAbstractItemModel& devices);

}

class DiagnosticsBundle : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged FINAL)

public:
    DiagnosticsBundle(AppSettings& settings, std::filesystem::path dataDirectory, QObject* parent = nullptr);
    ~DiagnosticsBundle() override;

    static DiagnosticsBundle* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    void setDevices(const QAbstractItemModel* devices);
    void setCrashReport(const CrashReport* crashes);
    void setGraphics(QString description);

    [[nodiscard]] bool exporting() const noexcept { return m_exporting; }
    [[nodiscard]] diagnostics::Sources sources() const;
    [[nodiscard]] diagnostics::Secrets secrets() const;

    Q_INVOKABLE QString suggestedName() const;
    Q_INVOKABLE bool exportTo(const QString& fileOrUrl);

signals:
    void exportingChanged();
    void exportFinished(const QVariantMap& result);

private:
    [[nodiscard]] std::string systemText() const;
    void finish(bool saved, const std::filesystem::path& target);

    static inline DiagnosticsBundle* s_instance = nullptr;

    AppSettings& m_settings;
    const std::filesystem::path m_data;
    const QAbstractItemModel* m_devices = nullptr;
    const CrashReport* m_crashes = nullptr;
    QString m_graphics;
    bool m_exporting = false;
    std::jthread m_worker;
};

}
