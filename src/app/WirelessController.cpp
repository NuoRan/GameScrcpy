#include "WirelessController.h"

#include "AppSettings.h"
#include "DevicePresentation.h"
#include "FailureAdvice.h"
#include "LanHosts.h"
#include "QrPicture.h"

#include <gs/adb/DeviceIdentity.h>

#include <QDebug>
#include <QHostAddress>
#include <QJSEngine>
#include <QMetaObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTcpSocket>

#include <algorithm>
#include <chrono>
#include <format>
#include <string_view>
#include <utility>

namespace gs::app {

namespace {

constexpr std::chrono::milliseconds kDiscoveryInterval{2000};
constexpr std::chrono::milliseconds kNetworkRound{3000};
constexpr std::chrono::minutes kNetworkSearch{5};
constexpr std::size_t kMostLanHosts = 1024;

std::vector<LanAddress> ownAddresses()
{
    std::vector<LanAddress> own;
    for (const QNetworkInterface& face : QNetworkInterface::allInterfaces()) {
        const QNetworkInterface::InterfaceFlags flags = face.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack) || flags.testFlag(QNetworkInterface::IsPointToPoint)
            || face.type() == QNetworkInterface::Virtual) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : face.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol) {
                own.push_back({.ip = ip.toIPv4Address(), .prefix = entry.prefixLength()});
            }
        }
    }
    return own;
}

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
    m_networkRounds.setInterval(kNetworkRound);
    connect(&m_networkRounds, &QTimer::timeout, this, &WirelessController::networkTick);
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

void WirelessController::connectOverNetwork(const QString& serial, const QString& name)
{
    if (serial.isEmpty()) {
        return;
    }
    stopNetworkSearch();
    const std::uint64_t attempt = m_networkAttempt;
    m_networkPhone = name.isEmpty() ? serial : name;
    m_networkAddress.clear();
    m_networkId.clear();
    setNetwork(QStringLiteral("enabling"));
    m_networkTask = std::jthread([this, attempt, serial = serial.toStdString(), client = m_adb] {
        std::string id;
        std::optional<adb::AdbError> problem;
        if (const adb::AdbResult<adb::DeviceInfo> info = adb::readDeviceInfo(client, serial); info) {
            id = info->id;
            if (const adb::AdbResult<std::string> switched = client.tcpip(serial, kNetworkPort); !switched) {
                problem = switched.error();
            }
        } else {
            problem = info.error();
        }
        QMetaObject::invokeMethod(
            this, [this, attempt, id = std::move(id), problem = std::move(problem)] { networkEnabled(attempt, id, problem); },
            Qt::QueuedConnection);
    });
}

void WirelessController::stopNetworkSearch()
{
    ++m_networkAttempt;
    m_networkRounds.stop();
    dropProbes();
    m_identifyTask.request_stop();
    m_identifying = false;
    m_rejected.clear();
    if (!m_networkState.isEmpty()) {
        setNetwork({});
    }
}

void WirelessController::networkEnabled(std::uint64_t attempt, const std::string& id,
                                        const std::optional<adb::AdbError>& problem)
{
    if (attempt != m_networkAttempt) {
        return;
    }
    if (problem || id.empty()) {
        qWarning().noquote() << "network debugging could not be turned on for" << m_networkPhone << ":"
                             << (problem ? QString::fromStdString(problem->message) : QString());
        setNetwork(QStringLiteral("unavailable"), problem ? QString::fromStdString(problem->message) : QString());
        return;
    }
    qInfo().noquote() << m_networkPhone << "listens for adb on port" << kNetworkPort << "until it restarts";
    m_networkId = id;
    m_networkDeadline = std::chrono::steady_clock::now() + kNetworkSearch;
    setNetwork(QStringLiteral("searching"));
    probe();
    m_networkRounds.start();
}

void WirelessController::networkTick()
{
    std::vector<std::uint32_t> open;
    for (QTcpSocket* socket : m_probes) {
        if (socket->state() == QAbstractSocket::ConnectedState) {
            open.push_back(socket->peerAddress().toIPv4Address());
        }
    }
    dropProbes();
    if (m_networkState != u"searching") {
        m_networkRounds.stop();
        return;
    }
    if (!open.empty() && !m_identifying) {
        identify(std::move(open));
    }
    if (std::chrono::steady_clock::now() >= m_networkDeadline) {
        m_networkRounds.stop();
        m_identifyTask.request_stop();
        setNetwork(QStringLiteral("notFound"));
        return;
    }
    probe();
}

void WirelessController::probe()
{
    const std::vector<LanAddress> own = ownAddresses();
    for (const std::uint32_t host : lanHosts(own, kMostLanHosts)) {
        if (std::ranges::find(m_rejected, host) != m_rejected.end()) {
            continue;
        }
        auto* socket = new QTcpSocket(this);
        socket->connectToHost(QHostAddress(host), kNetworkPort);
        m_probes.push_back(socket);
    }
}

void WirelessController::dropProbes()
{
    for (QTcpSocket* socket : std::exchange(m_probes, {})) {
        socket->abort();
        socket->deleteLater();
    }
}

void WirelessController::identify(std::vector<std::uint32_t> open)
{
    m_identifying = true;
    const std::uint64_t attempt = m_networkAttempt;
    m_identifyTask = std::jthread([this, attempt, open = std::move(open), id = m_networkId,
                                   client = m_adb](const std::stop_token& stop) {
        std::vector<std::string> known;
        if (const adb::AdbResult<std::vector<adb::DeviceEntry>> devices = client.devices(); devices) {
            for (const adb::DeviceEntry& device : *devices) {
                known.push_back(device.serial);
            }
        }
        QString found;
        std::vector<std::uint32_t> rejected;
        for (const std::uint32_t host : open) {
            if (stop.stop_requested()) {
                return;
            }
            const std::string address =
                std::format("{}:{}", QHostAddress(host).toString().toStdString(), kNetworkPort);
            const adb::AdbResult<std::string> serial = client.connect(address, stop);
            if (!serial) {
                continue;
            }
            if (client.waitForDevice(*serial, stop)) {
                const adb::AdbResult<adb::DeviceInfo> info = adb::readDeviceInfo(client, *serial);
                if (info && info->id == id) {
                    found = QString::fromStdString(*serial);
                    break;
                }
            }
            if (std::ranges::find(known, *serial) == known.end()) {
                (void)client.disconnect(*serial);
            }
            rejected.push_back(host);
        }
        QMetaObject::invokeMethod(
            this,
            [this, attempt, found, rejected = std::move(rejected)] { identified(attempt, found, rejected); },
            Qt::QueuedConnection);
    });
}

void WirelessController::identified(std::uint64_t attempt, const QString& found,
                                    const std::vector<std::uint32_t>& rejected)
{
    if (attempt != m_networkAttempt) {
        return;
    }
    m_identifying = false;
    m_rejected.insert(m_rejected.end(), rejected.begin(), rejected.end());
    if (found.isEmpty() || m_networkState != u"searching") {
        return;
    }
    m_networkRounds.stop();
    dropProbes();
    m_networkAddress = found;
    m_settings.rememberConnection(found);
    qInfo().noquote() << "found" << m_networkPhone << "on the network at" << found;
    setNetwork(QStringLiteral("connected"));
}

void WirelessController::setNetwork(const QString& state, const QString& detail)
{
    m_networkState = state;
    m_networkDetail = detail;
    emit networkChanged();
}

}
