#pragma once

#include <gs/base/Expected.h>
#include <gs/esp32/Link.h>
#include <gs/esp32/Port.h>
#include <gs/esp32/TouchMethod.h>
#include <gs/espflash/Firmware.h>
#include <gs/espflash/Flasher.h>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gs::app {

struct Esp32Found;

class Esp32Panel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Esp32Board)
    Q_PROPERTY(QVariantList ports READ ports NOTIFY portsChanged FINAL)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString problem READ problem NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString where READ where NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool serial READ serial NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString chip READ chip NOTIFY boardChanged FINAL)
    Q_PROPERTY(QString firmware READ firmware NOTIFY boardChanged FINAL)
    Q_PROPERTY(QString boardName READ boardName NOTIFY boardChanged FINAL)
    Q_PROPERTY(QString partsText READ partsText NOTIFY boardChanged FINAL)
    Q_PROPERTY(bool hasWifi READ hasWifi NOTIFY boardChanged FINAL)
    Q_PROPERTY(bool upgradable READ upgradable NOTIFY boardChanged FINAL)
    Q_PROPERTY(QString phone READ phone NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString wifi READ wifi NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool wifiConfigured READ wifiConfigured NOTIFY statusChanged FINAL)
    Q_PROPERTY(double roundTripMs READ roundTripMs NOTIFY roundTripChanged FINAL)
    Q_PROPERTY(QString bundledVersion READ bundledVersion CONSTANT FINAL)
    Q_PROPERTY(bool bundled READ bundled CONSTANT FINAL)
    Q_PROPERTY(bool flashing READ flashing NOTIFY flashChanged FINAL)
    Q_PROPERTY(double flashProgress READ flashProgress NOTIFY flashChanged FINAL)
    Q_PROPERTY(QString flashStage READ flashStage NOTIFY flashChanged FINAL)
    Q_PROPERTY(QString flashResult READ flashResult NOTIFY flashChanged FINAL)
    Q_PROPERTY(bool flashFailed READ flashFailed NOTIFY flashChanged FINAL)

public:
    explicit Esp32Panel(QObject* parent = nullptr);
    ~Esp32Panel() override;
    Esp32Panel(const Esp32Panel&) = delete;
    Esp32Panel& operator=(const Esp32Panel&) = delete;

    [[nodiscard]] QVariantList ports() const { return m_ports; }
    [[nodiscard]] QString state() const { return m_state; }
    [[nodiscard]] QString problem() const { return m_problem; }
    [[nodiscard]] QString where() const { return m_where; }
    [[nodiscard]] bool serial() const { return link() != nullptr && !m_wifiLink; }
    [[nodiscard]] QString chip() const { return m_chip; }
    [[nodiscard]] QString firmware() const { return m_firmware; }
    [[nodiscard]] QString boardName() const { return m_boardName; }
    [[nodiscard]] QString partsText() const { return m_partsText; }
    [[nodiscard]] bool hasWifi() const { return m_hasWifi; }
    [[nodiscard]] bool upgradable() const { return m_upgradable; }
    [[nodiscard]] QString phone() const { return m_phone; }
    [[nodiscard]] QString wifi() const { return m_wifi; }
    [[nodiscard]] bool wifiConfigured() const
    {
        return m_statusKnown && m_status.wifi >= GS_ESP32_WIFI_CONNECTING && m_status.wifi <= GS_ESP32_WIFI_FAILED;
    }
    [[nodiscard]] double roundTripMs() const { return m_roundTripMs; }
    [[nodiscard]] QString bundledVersion() const { return m_bundledVersion; }
    [[nodiscard]] bool bundled() const { return m_bundled; }
    [[nodiscard]] bool flashing() const { return m_flashing; }
    [[nodiscard]] double flashProgress() const { return m_flashProgress; }
    [[nodiscard]] QString flashStage() const { return m_flashStage; }
    [[nodiscard]] QString flashResult() const { return m_flashResult; }
    [[nodiscard]] bool flashFailed() const { return m_flashFailed; }

    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE void connectBoard();
    Q_INVOKABLE void disconnectBoard();
    Q_INVOKABLE bool tryTap();
    Q_INVOKABLE QString defaultFlashPort() const;
    Q_INVOKABLE void flash(const QString& port);
    Q_INVOKABLE bool setUpWifi(const QString& ssid, const QString& password);
    Q_INVOKABLE bool forgetWifi();

    [[nodiscard]] static QString openFailureText(esp32::OpenFailure failure, bool wifi = false);
    [[nodiscard]] static QString flashFailureText(espflash::FlashFailure failure);
    [[nodiscard]] static QString flashStageText(espflash::FlashStage stage);
    [[nodiscard]] static QString phoneText(const esp32::BoardStatus& status);
    [[nodiscard]] static QString wifiText(const esp32::BoardStatus& status);
    [[nodiscard]] static QString troubleText(const esp32::BoardTrouble& trouble);
    [[nodiscard]] static QString partsLine(const esp32::BoardInfo& board, const esp32::BoardStatus& status);
    [[nodiscard]] static QVariantList portsOf(const std::vector<esp32::SerialPortInfo>& ports);

signals:
    void portsChanged();
    void stateChanged();
    void boardChanged();
    void statusChanged();
    void roundTripChanged();
    void flashChanged();

private:
    using Opened = Expected<esp32::OpenedLink, esp32::OpenFailure>;
    using Flashed = Expected<espflash::FlashResult, espflash::FlashFailure>;

    [[nodiscard]] esp32::Esp32Settings settings() const;
    [[nodiscard]] std::filesystem::path boardsFile() const;
    [[nodiscard]] std::filesystem::path firmwareFolder() const;
    [[nodiscard]] const esp32::SerialPortInfo* portInfo(const QString& name) const;
    void setState(const QString& next, const QString& reason = {});
    void adopt(std::uint64_t generation, Opened opened);
    void poll();
    void dropLink();
    void liftTap();
    void flashProgressed(espflash::FlashStage stage, std::size_t done, std::size_t total);
    void flashFinished(const Flashed& result);
    [[nodiscard]] esp32::Esp32Link* link() const;
    [[nodiscard]] bool borrow();
    void showHeld(const Esp32Found& found);
    void boardsUpdated();
    void claimsUpdated();
    void activeUpdated();
    void scannedNow(bool complete);

    std::vector<esp32::SerialPortInfo> m_portInfos;
    QVariantList m_ports;
    QString m_state = QStringLiteral("idle");
    QString m_problem;
    QString m_where;
    QString m_chip;
    QString m_firmware;
    QString m_boardName;
    QString m_partsText;
    QString m_phone;
    QString m_wifi;
    QString m_bundledVersion;
    QString m_flashStage;
    QString m_flashResult;
    bool m_hasWifi = false;
    bool m_upgradable = false;
    bool m_bundled = false;
    bool m_flashing = false;
    bool m_flashFailed = false;
    bool m_wifiLink = false;
    bool m_statusKnown = false;
    double m_roundTripMs = 0.0;
    double m_flashProgress = 0.0;
    std::optional<espflash::FirmwareBundle> m_bundle;
    std::unique_ptr<esp32::Esp32Link> m_own;
    std::string m_borrowed;
    bool m_waiting = false;
    bool m_flashClaimed = false;
    std::optional<esp32::BoardInfo> m_board;
    esp32::BoardStatus m_status;
    std::string m_rememberedAddress;
    std::uint64_t m_generation = 0;
    std::uint64_t m_tapGeneration = 0;
    QTimer m_poll;
    std::jthread m_worker;
    std::jthread m_flasher;
};

}
