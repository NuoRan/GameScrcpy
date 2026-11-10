#pragma once

#include <gs/adb/AdbClient.h>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

class AppSettings;

class WirelessController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString pendingAddress READ pendingAddress NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString problem READ problem NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString action READ action NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString detail READ detail NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString connectedAddress READ connectedAddress NOTIFY stateChanged FINAL)

    Q_PROPERTY(QString pairingAddress READ pairingAddress NOTIFY pairingChanged FINAL)
    Q_PROPERTY(QString pairProblem READ pairProblem NOTIFY pairingChanged FINAL)
    Q_PROPERTY(QString pairAction READ pairAction NOTIFY pairingChanged FINAL)
    Q_PROPERTY(QString pairDetail READ pairDetail NOTIFY pairingChanged FINAL)
    Q_PROPERTY(QString pairedAddress READ pairedAddress NOTIFY pairingChanged FINAL)

    Q_PROPERTY(QVariantList nearby READ nearby NOTIFY nearbyChanged FINAL)
    Q_PROPERTY(bool discovering READ discovering WRITE setDiscovering NOTIFY discoveringChanged FINAL)

    Q_PROPERTY(QString qrText READ qrText NOTIFY qrChanged FINAL)
    Q_PROPERTY(int qrSize READ qrSize NOTIFY qrChanged FINAL)
    Q_PROPERTY(QString qrPath READ qrPath NOTIFY qrChanged FINAL)

public:
    WirelessController(adb::AdbClient adb, AppSettings& settings, QObject* parent = nullptr);
    ~WirelessController() override;

    static WirelessController* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] QString pendingAddress() const { return m_pending; }
    [[nodiscard]] QString problem() const { return m_problem; }
    [[nodiscard]] QString action() const { return m_action; }
    [[nodiscard]] QString detail() const { return m_detail; }
    [[nodiscard]] QString connectedAddress() const { return m_connected; }
    [[nodiscard]] QString pairingAddress() const { return m_pairing; }
    [[nodiscard]] QString pairProblem() const { return m_pairProblem; }
    [[nodiscard]] QString pairAction() const { return m_pairAction; }
    [[nodiscard]] QString pairDetail() const { return m_pairDetail; }
    [[nodiscard]] QString pairedAddress() const { return m_paired; }
    [[nodiscard]] QVariantList nearby() const { return m_nearby; }
    [[nodiscard]] bool discovering() const { return m_discovering; }
    void setDiscovering(bool discovering);
    [[nodiscard]] QString qrText() const { return m_qrText; }
    [[nodiscard]] int qrSize() const { return m_qrSize; }
    [[nodiscard]] QString qrPath() const { return m_qrPath; }

    Q_INVOKABLE void connectTo(const QString& address);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void disconnectDevice(const QString& serial);
    Q_INVOKABLE void pair(const QString& address, const QString& code);
    Q_INVOKABLE void cancelPairing();
    Q_INVOKABLE void showQrCode();
    Q_INVOKABLE void hideQrCode();
    void retranslate();

signals:
    void stateChanged();
    void pairingChanged();
    void nearbyChanged();
    void discoveringChanged();
    void qrChanged();

private:
    void finished(std::uint64_t attempt, const adb::AdbResult<std::string>& serial);
    void startPairing(const QString& address, const QString& code, bool byQrCode);
    void paired(std::uint64_t attempt, const QString& address, bool byQrCode, const adb::AdbResult<std::string>& result);
    void clearPairingOutcome();
    void poll();
    void discovered(const adb::AdbResult<std::vector<adb::WirelessService>>& services);

    static inline WirelessController* s_instance = nullptr;

    adb::AdbClient m_adb;
    AppSettings& m_settings;
    std::uint64_t m_attempt = 0;
    QString m_pending;
    std::optional<adb::AdbError> m_connectError;
    QString m_problem;
    QString m_action;
    QString m_detail;
    QString m_connected;
    std::uint64_t m_pairAttempt = 0;
    QString m_pairing;
    std::optional<adb::AdbError> m_pairError;
    bool m_pairErrorByQr = false;
    QString m_pairProblem;
    QString m_pairAction;
    QString m_pairDetail;
    QString m_paired;
    QVariantList m_nearby;
    bool m_discovering = false;
    QString m_qrName;
    QString m_qrPassword;
    QString m_qrText;
    int m_qrSize = 0;
    QString m_qrPath;
    bool m_qrPairingStarted = false;
    bool m_querying = false;
    QTimer m_discovery;
    std::jthread m_disconnecting;
    std::jthread m_lookup;
    std::jthread m_pairingTask;
    std::jthread m_connecting;
};

}
