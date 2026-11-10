#include "WirelessController.h"

#include "AppSettings.h"
#include "DevicePresentation.h"
#include "FailureAdvice.h"
#include "QrPicture.h"

#include <QDebug>
#include <QJSEngine>
#include <QMetaObject>
#include <QRandomGenerator>

#include <algorithm>
#include <chrono>
#include <string_view>
#include <utility>

namespace gs::app {

namespace {

constexpr std::chrono::milliseconds kDiscoveryInterval{2000};

}

WirelessController::WirelessController(adb::AdbClient adb, AppSettings& settings, QObject* parent)
    : QObject(parent),
      m_adb(adb),
      m_settings(settings)
{
    Q_ASSERT(s_instance == nullptr);
    s_instance = this;
    m_discovery.setInterval(kDiscoveryInterval);
    connect(&m_discovery, &QTimer::timeout, this, &WirelessController::poll);
}

WirelessController::~WirelessController()
{
    s_instance = nullptr;
}

WirelessController* WirelessController::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void WirelessController::connectTo(const QString& address)
{
    const QString target = address.trimmed();
    if (target.isEmpty()) {
        return;
    }
    const std::uint64_t attempt = ++m_attempt;
    m_pending = target;
    m_problem.clear();
    m_action.clear();
    m_detail.clear();
    m_connected.clear();
    emit stateChanged();
    m_connecting = std::jthread([this, attempt, target, client = m_adb](const std::stop_token& stop) {
        adb::AdbResult<std::string> serial = client.connect(target.toStdString(), stop);
        if (stop.stop_requested()) {
            return;
        }
        QMetaObject::invokeMethod(
            this, [this, attempt, serial = std::move(serial)] { finished(attempt, serial); }, Qt::QueuedConnection);
    });
}

void WirelessController::cancel()
{
    if (m_pending.isEmpty()) {
        return;
    }
    ++m_attempt;
    m_connecting.request_stop();
    m_pending.clear();
    emit stateChanged();
}

void WirelessController::disconnectDevice(const QString& serial)
{
    m_disconnecting = std::jthread([serial = serial.toStdString(), client = m_adb] {
        (void)client.disconnect(serial);
    });
}

void WirelessController::pair(const QString& address, const QString& code)
{
    startPairing(address, code, false);
}

void WirelessController::startPairing(const QString& address, const QString& code, bool byQrCode)
{
    const QString target = address.trimmed();
    if (target.isEmpty() || code.isEmpty()) {
        return;
    }
    const std::uint64_t attempt = ++m_pairAttempt;
    m_pairing = target;
    m_pairProblem.clear();
    m_pairAction.clear();
    m_pairDetail.clear();
    m_paired.clear();
    emit pairingChanged();
    m_pairingTask = std::jthread([this, attempt, target, code, byQrCode, client = m_adb](const std::stop_token& stop) {
        adb::AdbResult<std::string> result = client.pair(target.toStdString(), code.toStdString(), stop);
        if (stop.stop_requested()) {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, attempt, target, byQrCode, result = std::move(result)] { paired(attempt, target, byQrCode, result); },
            Qt::QueuedConnection);
    });
}

void WirelessController::cancelPairing()
{
    if (m_pairing.isEmpty()) {
        return;
    }
    ++m_pairAttempt;
    m_pairingTask.request_stop();
    m_pairing.clear();
    emit pairingChanged();
}

void WirelessController::showQrCode()
{
    const auto randomText = [](int length) {
        static constexpr std::string_view kAlphabet = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        QString text;
        for (int i = 0; i < length; ++i) {
            text += QChar::fromLatin1(kAlphabet[QRandomGenerator::system()->bounded(int(kAlphabet.size()))]);
        }
        return text;
    };
    m_qrName = QStringLiteral("gamescrcpy-") + randomText(8);
    m_qrPassword = randomText(12);
    m_qrText = QStringLiteral("WIFI:T:ADB;S:%1;P:%2;;").arg(m_qrName, m_qrPassword);
    const QrPicture picture = qrPicture(m_qrText.toStdString());
    m_qrSize = picture.size;
    m_qrPath = QString::fromStdString(picture.path);
    m_qrPairingStarted = false;
    qInfo().noquote() << "pairing by QR code as" << m_qrName;
    clearPairingOutcome();
    emit qrChanged();
}

void WirelessController::hideQrCode()
{
    if (m_qrText.isEmpty()) {
        return;
    }
    m_qrName.clear();
    m_qrPassword.clear();
    m_qrText.clear();
    m_qrSize = 0;
    m_qrPath.clear();
    clearPairingOutcome();
    emit qrChanged();
}

void WirelessController::retranslate()
{
    if (!m_problem.isEmpty() && m_connectError) {
        const Advice advice = FailureAdvice::forWirelessConnect(*m_connectError);
        m_problem = advice.problem;
        m_action = advice.action;
    }
    if (!m_pairProblem.isEmpty() && m_pairError) {
        const Advice advice = FailureAdvice::forPairing(*m_pairError, m_pairErrorByQr);
        m_pairProblem = advice.problem;
        m_pairAction = advice.action;
    }
    emit stateChanged();
    emit pairingChanged();
}

void WirelessController::clearPairingOutcome()
{
    if (m_pairProblem.isEmpty() && m_paired.isEmpty()) {
        return;
    }
    m_pairProblem.clear();
    m_pairAction.clear();
    m_pairDetail.clear();
    m_paired.clear();
    emit pairingChanged();
}

void WirelessController::setDiscovering(bool discovering)
{
    if (discovering == m_discovering) {
        return;
    }
    m_discovering = discovering;
    if (discovering) {
        poll();
        m_discovery.start();
    } else {
        m_discovery.stop();
        if (!m_nearby.isEmpty()) {
            m_nearby.clear();
            emit nearbyChanged();
        }
    }
    emit discoveringChanged();
}

void WirelessController::poll()
{
    if (m_querying) {
        return;
    }
    m_querying = true;
    m_lookup = std::jthread([this, client = m_adb] {
        adb::AdbResult<std::vector<adb::WirelessService>> services = client.wirelessServices();
        QMetaObject::invokeMethod(
            this, [this, services = std::move(services)] { discovered(services); }, Qt::QueuedConnection);
    });
}

void WirelessController::discovered(const adb::AdbResult<std::vector<adb::WirelessService>>& services)
{
    m_querying = false;
    if (!m_discovering) {
        return;
    }
    QVariantList nearby;
    if (services) {
        for (const adb::WirelessService& service : *services) {
            if (service.kind != adb::WirelessService::Kind::Pairing) {
                continue;
            }
            const QString name = QString::fromStdString(service.name);
            nearby.append(QVariantMap{{QStringLiteral("name"), name},
                                      {QStringLiteral("label"), DevicePresentation::serviceLabel(name)},
                                      {QStringLiteral("address"), QString::fromStdString(service.address)}});
        }
    }
    if (nearby != m_nearby) {
        m_nearby = std::move(nearby);
        emit nearbyChanged();
    }

    if (m_qrName.isEmpty()) {
        return;
    }
    const auto scanned = std::ranges::find_if(m_nearby, [this](const QVariant& phone) {
        return phone.toMap().value(QStringLiteral("name")).toString() == m_qrName;
    });
    if (scanned == m_nearby.end()) {
        m_qrPairingStarted = false;
    } else if (!m_qrPairingStarted && m_pairing.isEmpty()) {
        m_qrPairingStarted = true;
        startPairing(scanned->toMap().value(QStringLiteral("address")).toString(), m_qrPassword, true);
    }
}

void WirelessController::finished(std::uint64_t attempt, const adb::AdbResult<std::string>& serial)
{
    if (attempt != m_attempt) {
        return;
    }
    m_pending.clear();
    if (serial) {
        m_connected = QString::fromStdString(*serial);
        m_settings.rememberConnection(m_connected);
    } else {
        m_connectError = serial.error();
        const Advice advice = FailureAdvice::forWirelessConnect(serial.error());
        m_problem = advice.problem;
        m_action = advice.action;
        m_detail = QString::fromStdString(serial.error().message);
    }
    emit stateChanged();
}

void WirelessController::paired(std::uint64_t attempt, const QString& address, bool byQrCode,
                                const adb::AdbResult<std::string>& result)
{
    if (attempt != m_pairAttempt) {
        return;
    }
    m_pairing.clear();
    if (result) {
        m_paired = address;
    } else {
        m_pairError = result.error();
        m_pairErrorByQr = byQrCode;
        const Advice advice = FailureAdvice::forPairing(result.error(), byQrCode);
        m_pairProblem = advice.problem;
        m_pairAction = advice.action;
        m_pairDetail = QString::fromStdString(result.error().message);
    }
    emit pairingChanged();
}

}
