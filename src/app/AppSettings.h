#pragma once

#include "JsonFile.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

[[nodiscard]] std::filesystem::path dataDirectory();

class AppSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(QString accent READ accent WRITE setAccent NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(QString palette READ palette WRITE setPalette NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool reducedTransparency READ reducedTransparency NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool systemReducedMotion READ systemReducedMotion NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(bool systemReducedTransparency READ systemReducedTransparency NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged FINAL)
    Q_PROPERTY(QVariantMap overrides READ overrides NOTIFY devicesChanged FINAL)
    Q_PROPERTY(bool wirelessCollapsed READ wirelessCollapsed WRITE setWirelessCollapsed NOTIFY wirelessChanged FINAL)
    Q_PROPERTY(QVariantList recentConnections READ recentConnections NOTIFY wirelessChanged FINAL)
    Q_PROPERTY(bool otherVideoExpanded READ otherVideoExpanded WRITE setOtherVideoExpanded NOTIFY otherVideoChanged FINAL)
    Q_PROPERTY(bool mirrorToolbarHintShown READ mirrorToolbarHintShown NOTIFY hintsChanged FINAL)
    Q_PROPERTY(bool setupGuideShown READ setupGuideShown NOTIFY hintsChanged FINAL)
    Q_PROPERTY(bool toursQuiet READ toursQuiet WRITE setToursQuiet NOTIFY toursQuietChanged FINAL)
    Q_PROPERTY(int sponsorMilestone READ sponsorMilestone NOTIFY hintsChanged FINAL)

public:
    explicit AppSettings(const std::filesystem::path& directory, QObject* parent = nullptr);
    ~AppSettings() override;

    static AppSettings* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static AppSettings* instance() noexcept { return s_instance; }

    [[nodiscard]] QString theme() const;
    void setTheme(const QString& theme);
    [[nodiscard]] QString accent() const;
    void setAccent(const QString& accent);
    [[nodiscard]] QString palette() const;
    void setPalette(const QString& palette);
    [[nodiscard]] bool reducedMotion() const;
    [[nodiscard]] bool reducedTransparency() const;
    [[nodiscard]] bool systemReducedMotion() const { return m_systemReducedMotion; }
    [[nodiscard]] bool systemReducedTransparency() const { return m_systemReducedTransparency; }
    void refreshSystem();

    [[nodiscard]] QVariantMap values() const;
    Q_INVOKABLE bool setValue(const QString& path, const QVariant& value);
    [[nodiscard]] QVariantMap overrides() const;
    Q_INVOKABLE bool setDeviceValue(const QString& id, const QString& path, const QVariant& value);
    Q_INVOKABLE void resetDeviceValue(const QString& id, const QString& path);

    Q_INVOKABLE bool placeWindow(QWindow* window, const QString& which = QStringLiteral("hub"));

    [[nodiscard]] settings::DeviceSettings device(const std::string& id) const;
    [[nodiscard]] const std::map<std::string, settings::DeviceSettings, std::less<>>& devices() const;
    bool setDevice(const std::string& id, const settings::DeviceSettings& device);
    [[nodiscard]] settings::Settings effective(const std::string& id) const;

    [[nodiscard]] bool wirelessCollapsed() const;
    void setWirelessCollapsed(bool collapsed);
    [[nodiscard]] QVariantList recentConnections() const;
    Q_INVOKABLE void forgetConnection(const QString& address);
    void rememberConnection(const QString& address);
    void nameConnection(const QString& address, const QString& name);
    [[nodiscard]] bool otherVideoExpanded() const;
    void setOtherVideoExpanded(bool expanded);

    [[nodiscard]] const std::map<std::string, std::string, std::less<>>& shortcuts() const;
    void setShortcuts(const std::map<std::string, std::string, std::less<>>& shortcuts);
    [[nodiscard]] const std::vector<std::string>& recentCommands() const;
    void setRecentCommands(const std::vector<std::string>& ids);
    [[nodiscard]] const settings::ExtensionStates& extensions() const;
    void setExtensions(const settings::ExtensionStates& extensions);
    [[nodiscard]] bool mcpEnabled() const;
    [[nodiscard]] const std::vector<std::string>& mcpClients() const;
    void setMcpClients(const std::vector<std::string>& clients);

    [[nodiscard]] bool mirrorToolbarHintShown() const;
    Q_INVOKABLE void markMirrorToolbarHintShown();
    [[nodiscard]] bool setupGuideShown() const;
    Q_INVOKABLE void markSetupGuideShown();
    [[nodiscard]] QStringList toursSeen() const;
    Q_INVOKABLE bool tourSeen(const QString& key) const;
    Q_INVOKABLE void markTourSeen(const QString& key);
    Q_INVOKABLE void resetTour(const QString& key);
    Q_INVOKABLE void resetTours();
    [[nodiscard]] bool toursQuiet() const noexcept { return m_toursQuiet; }
    void setToursQuiet(bool quiet);
    [[nodiscard]] QString acknowledgedCrash() const;
    void acknowledgeCrash(const QString& name);
    [[nodiscard]] int launches() const;
    void countLaunch();
    [[nodiscard]] int sponsorMilestone() const;
    Q_INVOKABLE void markSponsorAsked(int milestone);
    void flush();

    [[nodiscard]] QString language() const;
    [[nodiscard]] QString interfaceLanguage() const;

signals:
    void appearanceChanged();
    void languageChanged();
    void valuesChanged();
    void devicesChanged();
    void wirelessChanged();
    void otherVideoChanged();
    void hintsChanged();
    void toursChanged();
    void toursQuietChanged();
    void saveBusy();
    void mcpClientsChanged();

private:
    void changeAppearance(const settings::Appearance& appearance);
    void change(const settings::Settings& next);
    void capture(const QString& which);
    void reportBusy();

    struct Placed {
        QPointer<QWindow> window;
        QTimer* settle = nullptr;
    };

    static inline AppSettings* s_instance = nullptr;

    JsonFile<settings::Settings> m_file;
    JsonFile<settings::Devices> m_devices;
    std::map<QString, Placed> m_placed;
    bool m_systemReducedMotion = false;
    bool m_systemReducedTransparency = false;
    bool m_toursQuiet = false;
    std::optional<std::chrono::steady_clock::time_point> m_busyShown;
};

}
