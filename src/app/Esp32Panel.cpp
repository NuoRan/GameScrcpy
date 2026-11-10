#include "Esp32Panel.h"

#include "AppSettings.h"
#include "Esp32Boards.h"

#include <gs/base/Log.h>
#include <gs/esp32/Boards.h>

#include <QCoreApplication>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace gs::app {

namespace {

constexpr std::string_view kCategory = "esp32";
constexpr auto kPollInterval = std::chrono::milliseconds{250};
constexpr auto kTapHold = std::chrono::milliseconds{80};
constexpr auto kBootWait = std::chrono::milliseconds{1500};
constexpr std::uint16_t kScreenMiddle = 16384;

QString portTitle(const esp32::SerialPortInfo& port)
{
    const QString name = QString::fromStdString(port.name);
    const QString description = QString::fromStdString(port.description);
    if (description.isEmpty()) {
        return name;
    }
    if (description.contains(name)) {
        return description;
    }
    return name + QStringLiteral(" · ") + description;
}

QString kindName(esp32::PortKind kind)
{
    switch (kind) {
    case esp32::PortKind::EspressifUsb:
        return QStringLiteral("espressif");
    case esp32::PortKind::Ch34x:
        return QStringLiteral("ch34x");
    case esp32::PortKind::Cp210x:
        return QStringLiteral("cp210x");
    case esp32::PortKind::Other:
        break;
    }
    return QStringLiteral("other");
}

gs_esp32_touch middleTouch(bool down)
{
    gs_esp32_touch touch{};
    touch.count = 1;
    touch.contacts[0] = {.id = 0, .tip = static_cast<std::uint8_t>(down ? 1 : 0), .x = kScreenMiddle, .y = kScreenMiddle};
    return touch;
}

QString flashOwner()
{
    return QStringLiteral("flash:esp32");
}

Esp32Boards* activeBoards()
{
    Esp32Boards* boards = Esp32Boards::instance();
    return boards != nullptr && boards->active() ? boards : nullptr;
}

}

Esp32Panel::Esp32Panel(QObject* parent)
    : QObject(parent)
{
    m_poll.setInterval(kPollInterval);
    connect(&m_poll, &QTimer::timeout, this, &Esp32Panel::poll);
    Expected<espflash::FirmwareBundle, espflash::FirmwareProblem> bundle = espflash::loadFirmwareBundle(firmwareFolder());
    if (bundle) {
        m_bundledVersion = QString::fromStdString(bundle->version);
        m_bundled = !bundle->images.empty();
        m_bundle = std::move(*bundle);
    }
    refreshPorts();
    if (Esp32Boards* boards = Esp32Boards::instance()) {
        connect(boards, &Esp32Boards::boardsChanged, this, &Esp32Panel::boardsUpdated);
        connect(boards, &Esp32Boards::claimsChanged, this, &Esp32Panel::claimsUpdated);
        connect(boards, &Esp32Boards::activeChanged, this, &Esp32Panel::activeUpdated);
        connect(boards, &Esp32Boards::scanned, this, &Esp32Panel::scannedNow);
    }
}

Esp32Panel::~Esp32Panel()
{
    m_poll.stop();
    m_worker = {};
    m_flasher = {};
    if (m_own) {
        m_own->close();
    }
    if (m_flashClaimed) {
        if (Esp32Boards* boards = Esp32Boards::instance()) {
            boards->release(flashOwner());
        }
    }
}

esp32::Esp32Settings Esp32Panel::settings() const
{
    esp32::Esp32Settings result;
    result.boardsFile = boardsFile();
    if (const AppSettings* app = AppSettings::instance()) {
        const QVariantMap values = app->values();
        result.port = values.value(QStringLiteral("esp32.port")).toString().toStdString();
        result.link = values.value(QStringLiteral("esp32.link")).toString() == QStringLiteral("wifi")
                          ? esp32::LinkMode::Wifi
                          : esp32::LinkMode::Serial;
        result.address = values.value(QStringLiteral("esp32.address")).toString().trimmed().toStdString();
    }
    return result;
}

std::filesystem::path Esp32Panel::boardsFile() const
{
    return dataDirectory() / std::string(esp32::kBoardsFileName);
}

std::filesystem::path Esp32Panel::firmwareFolder() const
{
    return std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) /
           std::string(espflash::kFirmwareFolder);
}

const esp32::SerialPortInfo* Esp32Panel::portInfo(const QString& name) const
{
    const int number = esp32::portNumber(name.toStdString());
    const auto found = std::ranges::find_if(
        m_portInfos, [number](const esp32::SerialPortInfo& port) { return esp32::portNumber(port.name) == number; });
    return found == m_portInfos.end() ? nullptr : &*found;
}

QVariantList Esp32Panel::portsOf(const std::vector<esp32::SerialPortInfo>& ports)
{
    QVariantList listed;
    for (const esp32::SerialPortInfo& port : ports) {
        listed.append(QVariantMap{
            {QStringLiteral("name"), QString::fromStdString(port.name)},
            {QStringLiteral("title"), portTitle(port)},
            {QStringLiteral("kind"), kindName(port.kind)},
        });
    }
    return listed;
}

void Esp32Panel::refreshPorts()
{
    m_portInfos = esp32::listSerialPorts();
    const QVariantList listed = portsOf(m_portInfos);
    if (listed != m_ports) {
        m_ports = listed;
        emit portsChanged();
    }
}

void Esp32Panel::setState(const QString& next, const QString& reason)
{
    if (next == m_state && reason == m_problem) {
        return;
    }
    m_state = next;
    m_problem = reason;
    emit stateChanged();
}

void Esp32Panel::connectBoard()
{
    if (m_flashing || m_state == QStringLiteral("connecting")) {
        return;
    }
    dropLink();
    if (Esp32Boards* boards = activeBoards()) {
        boards->forgive();
        if (!borrow()) {
            m_waiting = true;
            setState(QStringLiteral("connecting"));
        }
        return;
    }
    const esp32::Esp32Settings chosen = settings();
    const std::uint64_t generation = ++m_generation;
    setState(QStringLiteral("connecting"));
    m_worker = std::jthread([this, chosen, generation](const std::stop_token& stop) {
        auto opened = std::make_shared<Opened>(esp32::openBoard(chosen, {}, stop));
        if (stop.stop_requested()) {
            return;
        }
        QMetaObject::invokeMethod(
            this, [this, generation, opened] { adopt(generation, std::move(*opened)); }, Qt::QueuedConnection);
    });
}

void Esp32Panel::adopt(std::uint64_t generation, Opened opened)
{
    if (generation != m_generation) {
        return;
    }
    refreshPorts();
    if (!opened) {
        setState(QStringLiteral("failed"), openFailureText(opened.error(), settings().link == esp32::LinkMode::Wifi));
        return;
    }
    m_own = std::move(opened->link);
    m_wifiLink = opened->wifi;
    m_where = QString::fromStdString(opened->where);
    m_board.reset();
    m_status = {};
    m_statusKnown = false;
    m_rememberedAddress.clear();
    log::info(kCategory, "the panel is connected to the board on {}", opened->where);
    poll();
    m_poll.start();
}

void Esp32Panel::poll()
{
    esp32::Esp32Link* current = link();
    if (current == nullptr) {
        if (m_borrowed.empty()) {
            m_poll.stop();
            return;
        }
        const std::string where = m_borrowed;
        const bool wifi = m_wifiLink;
        dropLink();
        const Esp32Boards* boards = Esp32Boards::instance();
        if (boards != nullptr && boards->held(where, wifi)) {
            setState(QStringLiteral("busy"), tr("In use by a mirror window"));
        } else {
            setState(QStringLiteral("lost"), tr("The board stopped answering. Check its cable, then connect again."));
        }
        return;
    }
    const esp32::LinkState linkState = current->state();
    if (linkState == esp32::LinkState::Lost || linkState == esp32::LinkState::Closed) {
        const esp32::BoardTrouble trouble = current->trouble();
        dropLink();
        const QString text = troubleText(trouble);
        if (text.isEmpty()) {
            setState(QStringLiteral("lost"), tr("The board stopped answering. Check its cable, then connect again."));
        } else {
            log::warning(kCategory, "{}", text.toStdString());
            setState(QStringLiteral("trouble"), text);
        }
        return;
    }
    const std::optional<esp32::BoardInfo> board = current->board();
    if (board != m_board) {
        m_board = board;
        m_chip = board ? QString::fromStdString(esp32::chipName(board->chipId)) : QString();
        m_firmware = board ? QString::fromStdString(board->firmware) : QString();
        m_boardName = board ? QString::fromStdString(board->name) : QString();
        m_partsText = board ? partsLine(*board, current->status()) : QString();
        m_hasWifi = board && (board->features & GS_ESP32_FEATURE_WIFI) != 0;
        const espflash::FirmwareImage* image =
            board && m_bundle ? espflash::imageForChip(*m_bundle, board->chipId) : nullptr;
        m_upgradable = image != nullptr && espflash::compareVersions(m_bundle->version, board->firmware) > 0;
        emit boardChanged();
    }
    if ((linkState == esp32::LinkState::Legacy) != (m_state == QStringLiteral("legacy"))) {
        m_statusKnown = false;
    }
    const esp32::BoardStatus status = current->status();
    if (status != m_status || !m_statusKnown) {
        m_statusKnown = true;
        m_status = status;
        m_phone = linkState == esp32::LinkState::Legacy ? QString() : phoneText(status);
        m_wifi = linkState == esp32::LinkState::Legacy ? QString() : wifiText(status);
        emit statusChanged();
        if (m_board) {
            if (const QString parts = partsLine(*m_board, status); parts != m_partsText) {
                m_partsText = parts;
                emit boardChanged();
            }
        }
        if (board && !m_wifiLink && status.wifi == GS_ESP32_WIFI_CONNECTED) {
            const std::string address = esp32::ipv4Text(status.ipv4);
            if (address != m_rememberedAddress) {
                const std::vector<esp32::KnownBoard> known = esp32::loadBoards(boardsFile());
                if (std::optional<esp32::KnownBoard> remembered = esp32::findBoard(known, esp32::boardId(board->mac))) {
                    remembered->address = address;
                    remembered->name = board->name;
                    if (esp32::rememberBoard(boardsFile(), *remembered)) {
                        m_rememberedAddress = address;
                    }
                }
            }
        }
    }
    const std::optional<std::chrono::microseconds> trip = current->roundTrip();
    const double milliseconds = trip ? static_cast<double>(trip->count()) / 1000.0 : 0.0;
    if (milliseconds != m_roundTripMs) {
        m_roundTripMs = milliseconds;
        emit roundTripChanged();
    }
    const bool legacyLink = linkState == esp32::LinkState::Legacy;
    setState(legacyLink ? QStringLiteral("legacy") : QStringLiteral("ready"),
             legacyLink && current->trouble().problem == esp32::BoardProblem::Silent
                 ? tr("The board did not answer, so it is treated as the old QineTouch firmware and only touches "
                      "work. If it runs the bundled firmware, it is on the wrong port: on an ESP32-S3, connect the "
                      "computer to the port marked COM or UART and the other USB port to the phone.")
                 : QString());
}

void Esp32Panel::dropLink()
{
    m_poll.stop();
    ++m_generation;
    m_worker = {};
    if (m_own) {
        m_own->close();
        m_own.reset();
    }
    m_borrowed.clear();
    m_waiting = false;
    m_wifiLink = false;
    m_statusKnown = false;
    m_where.clear();
    m_board.reset();
    m_status = {};
    const bool hadBoard =
        !m_chip.isEmpty() || !m_firmware.isEmpty() || !m_partsText.isEmpty() || m_hasWifi || m_upgradable;
    m_chip.clear();
    m_firmware.clear();
    m_boardName.clear();
    m_partsText.clear();
    m_hasWifi = false;
    m_upgradable = false;
    m_phone.clear();
    m_wifi.clear();
    m_roundTripMs = 0.0;
    if (hadBoard) {
        emit boardChanged();
    }
    emit statusChanged();
    emit roundTripChanged();
}

void Esp32Panel::disconnectBoard()
{
    dropLink();
    if (!m_flashing) {
        setState(QStringLiteral("idle"));
    }
}

bool Esp32Panel::tryTap()
{
    esp32::Esp32Link* current = link();
    if (current == nullptr || !current->sendTouch(middleTouch(true))) {
        return false;
    }
    m_tapGeneration = m_generation;
    QTimer::singleShot(kTapHold, this, &Esp32Panel::liftTap); // source-rule-allow R9: a tap keeps the finger down for a moment
    return true;
}

void Esp32Panel::liftTap()
{
    if (esp32::Esp32Link* current = link(); current != nullptr && m_tapGeneration == m_generation) {
        (void)current->sendTouch(middleTouch(false));
    }
}

QString Esp32Panel::defaultFlashPort() const
{
    if (link() != nullptr && !m_wifiLink && !m_where.isEmpty()) {
        return m_where;
    }
    const QString configured = settings().port.empty() ? QString() : QString::fromStdString(settings().port);
    if (esp32::validPortName(configured.toStdString())) {
        return configured;
    }
    const auto likely = std::ranges::find_if(
        m_portInfos, [](const esp32::SerialPortInfo& port) { return esp32::probedAutomatically(port.kind); });
    if (likely != m_portInfos.end()) {
        return QString::fromStdString(likely->name);
    }
    return m_portInfos.empty() ? QString() : QString::fromStdString(m_portInfos.front().name);
}

void Esp32Panel::flash(const QString& port)
{
    if (m_flashing) {
        return;
    }
    refreshPorts();
    const QString chosen = port.isEmpty() ? defaultFlashPort() : port;
    if (!esp32::validPortName(chosen.toStdString())) {
        m_flashFailed = true;
        m_flashResult = flashFailureText(espflash::FlashFailure::PortMissing);
        emit flashChanged();
        return;
    }
    Esp32Boards* boards = Esp32Boards::instance();
    if (boards != nullptr && boards->inSession(chosen.toStdString(), false)) {
        m_flashFailed = true;
        m_flashResult = tr("A mirror window is using this board. Close it, then flash again.");
        emit flashChanged();
        return;
    }
    const esp32::SerialPortInfo* info = portInfo(chosen);
    const esp32::PortKind kind = info != nullptr ? info->kind : esp32::PortKind::Other;
    const bool jtag = info != nullptr && esp32::usbSerialJtag(info->vendorId, info->productId);
    esp32::Esp32Link* current = link();
    const bool viaFirmware = current != nullptr && !m_wifiLink && current->state() == esp32::LinkState::Ready &&
                             esp32::portNumber(m_where.toStdString()) == esp32::portNumber(chosen.toStdString()) &&
                             m_board && m_board->chipId != GS_ESP32_CHIP_ESP32 && current->enterDownload();
    dropLink();
    setState(QStringLiteral("flashing"));
    m_flashing = true;
    if (boards != nullptr) {
        boards->claim(flashOwner(), Esp32Claim{.where = chosen.toStdString(), .holder = Esp32Holder::Flash});
        m_flashClaimed = true;
    }
    m_flashFailed = false;
    m_flashResult.clear();
    m_flashProgress = 0.0;
    m_flashStage = flashStageText(espflash::FlashStage::Preparing);
    emit flashChanged();

    espflash::FlashRequest request;
    request.port = chosen.toStdString();
    request.kind = kind;
    request.usbSerialJtag = jtag;
    request.enter = viaFirmware ? espflash::EnterMode::Firmware : espflash::EnterMode::Auto;
    request.firmwareFolder = firmwareFolder();
    m_flasher = std::jthread([this, request](const std::stop_token& stop) {
        espflash::FlashHooks hooks;
        hooks.stop = stop;
        auto result = std::make_shared<Flashed>(espflash::flashFirmware(
            request,
            [this](espflash::FlashStage stage, std::size_t done, std::size_t total) {
                QMetaObject::invokeMethod(
                    this, [this, stage, done, total] { flashProgressed(stage, done, total); }, Qt::QueuedConnection);
            },
            hooks));
        QMetaObject::invokeMethod(this, [this, result] { flashFinished(*result); }, Qt::QueuedConnection);
    });
}

void Esp32Panel::flashProgressed(espflash::FlashStage stage, std::size_t done, std::size_t total)
{
    if (!m_flashing) {
        return;
    }
    static constexpr double kWritingStart = 0.15;
    static constexpr double kWritingEnd = 0.9;
    double progress = m_flashProgress;
    switch (stage) {
    case espflash::FlashStage::Preparing:
    case espflash::FlashStage::Connecting:
        progress = 0.02;
        break;
    case espflash::FlashStage::Detecting:
        progress = 0.08;
        break;
    case espflash::FlashStage::Erasing:
        progress = 0.12;
        break;
    case espflash::FlashStage::Writing:
        progress = kWritingStart + (kWritingEnd - kWritingStart) *
                                       (total > 0 ? static_cast<double>(done) / static_cast<double>(total) : 0.0);
        break;
    case espflash::FlashStage::Verifying:
        progress = 0.93;
        break;
    case espflash::FlashStage::Restarting:
        progress = 0.97;
        break;
    case espflash::FlashStage::Done:
        progress = 1.0;
        break;
    }
    m_flashProgress = std::max(m_flashProgress, progress);
    m_flashStage = flashStageText(stage);
    emit flashChanged();
}

void Esp32Panel::flashFinished(const Flashed& result)
{
    m_flashing = false;
    if (std::exchange(m_flashClaimed, false)) {
        if (Esp32Boards* boards = Esp32Boards::instance()) {
            boards->release(flashOwner());
        }
    }
    if (result) {
        m_flashFailed = false;
        m_flashProgress = 1.0;
        m_flashResult = tr("Firmware %1 is on the %2. The board restarted.")
                            .arg(QString::fromStdString(result->version),
                                 QString::fromUtf8(espflash::chipInfo(result->chip).name.data(),
                                                   static_cast<qsizetype>(espflash::chipInfo(result->chip).name.size())));
        setState(QStringLiteral("idle"));
        emit flashChanged();
        QTimer::singleShot(kBootWait, this, &Esp32Panel::connectBoard); // source-rule-allow R9: the board needs a moment to start after flashing
        return;
    }
    m_flashFailed = true;
    m_flashResult = flashFailureText(result.error());
    log::warning(kCategory, "flashing failed: {}", espflash::failureText(result.error()));
    setState(QStringLiteral("idle"));
    emit flashChanged();
}

bool Esp32Panel::setUpWifi(const QString& ssid, const QString& password)
{
    esp32::Esp32Link* current = link();
    if (current == nullptr || m_wifiLink || current->state() != esp32::LinkState::Ready || !m_board) {
        return false;
    }
    const std::string name = ssid.trimmed().toStdString();
    const std::string secret = password.toStdString();
    const esp32::BoardKey key = esp32::newBoardKey();
    if (!current->setWifi(name, secret, key)) {
        return false;
    }
    esp32::KnownBoard known;
    known.id = esp32::boardId(m_board->mac);
    known.name = m_board->name;
    known.key = key;
    m_rememberedAddress.clear();
    return esp32::rememberBoard(boardsFile(), known);
}

bool Esp32Panel::forgetWifi()
{
    esp32::Esp32Link* current = link();
    if (current == nullptr || m_wifiLink || current->state() != esp32::LinkState::Ready || !m_board) {
        return false;
    }
    if (!current->forgetWifi()) {
        return false;
    }
    (void)esp32::forgetBoard(boardsFile(), esp32::boardId(m_board->mac));
    return true;
}

esp32::Esp32Link* Esp32Panel::link() const
{
    if (m_own) {
        return m_own.get();
    }
    if (m_borrowed.empty()) {
        return nullptr;
    }
    const Esp32Boards* boards = Esp32Boards::instance();
    return boards != nullptr ? boards->link(m_borrowed, m_wifiLink) : nullptr;
}

bool Esp32Panel::borrow()
{
    Esp32Boards* boards = activeBoards();
    if (boards == nullptr) {
        return false;
    }
    const std::optional<Esp32Found> found = boards->matching(settings());
    if (!found) {
        return false;
    }
    if (found->held) {
        m_waiting = false;
        showHeld(*found);
        return true;
    }
    if (!found->connected) {
        return false;
    }
    m_waiting = false;
    m_borrowed = found->where;
    m_wifiLink = found->wifi;
    m_where = QString::fromStdString(found->where);
    m_board.reset();
    m_status = {};
    m_statusKnown = false;
    m_rememberedAddress.clear();
    log::info(kCategory, "the panel shows the board on {} that GameScrcpy found", found->where);
    poll();
    m_poll.start();
    return true;
}

void Esp32Panel::showHeld(const Esp32Found& found)
{
    m_borrowed.clear();
    if (found.flashing && m_flashing) {
        return;
    }
    setState(QStringLiteral("busy"), tr("In use by a mirror window"));
}

void Esp32Panel::boardsUpdated()
{
    if (m_flashing) {
        return;
    }
    if (!m_borrowed.empty()) {
        poll();
    } else if (m_waiting) {
        static_cast<void>(borrow());
    }
}

void Esp32Panel::claimsUpdated()
{
    Esp32Boards* boards = activeBoards();
    if (boards == nullptr || m_flashing) {
        return;
    }
    if (!m_borrowed.empty() && boards->held(m_borrowed, m_wifiLink)) {
        dropLink();
        setState(QStringLiteral("busy"), tr("In use by a mirror window"));
        return;
    }
    if (m_state == QStringLiteral("busy") && !borrow()) {
        m_waiting = true;
        setState(QStringLiteral("connecting"));
    }
}

void Esp32Panel::activeUpdated()
{
    if (m_flashing) {
        return;
    }
    if (activeBoards() != nullptr) {
        const bool wanted = m_own != nullptr || m_state == QStringLiteral("connecting");
        if (!wanted) {
            return;
        }
        dropLink();
        setState(QStringLiteral("idle"));
        connectBoard();
        return;
    }
    if (!m_borrowed.empty() || m_waiting || m_state == QStringLiteral("busy")) {
        dropLink();
        setState(QStringLiteral("idle"));
    }
}

void Esp32Panel::scannedNow(bool complete)
{
    if (!m_waiting || borrow() || !complete) {
        return;
    }
    m_waiting = false;
    const Esp32Boards* boards = Esp32Boards::instance();
    const std::optional<esp32::BoardTrouble> trouble = boards != nullptr ? boards->trouble() : std::nullopt;
    if (trouble) {
        const QString text = troubleText(*trouble);
        if (!text.isEmpty()) {
            log::warning(kCategory, "{}", text.toStdString());
            setState(QStringLiteral("trouble"), text);
            return;
        }
    }
    const std::optional<esp32::OpenFailure> failure = boards != nullptr ? boards->problem() : std::nullopt;
    setState(QStringLiteral("failed"), openFailureText(failure.value_or(esp32::OpenFailure::NoBoard),
                                                       settings().link == esp32::LinkMode::Wifi));
}

QString Esp32Panel::openFailureText(esp32::OpenFailure failure, bool wifi)
{
    switch (failure) {
    case esp32::OpenFailure::NoBoard:
        if (wifi) {
            return tr("No ESP32 touch box answered over Wi-Fi. Check that the board and this computer are on the same "
                      "network, or enter the board's address.");
        }
        return tr("No ESP32 touch box was found. Plug the board's serial port into this computer, or choose its "
                  "port.");
    case esp32::OpenFailure::PortMissing:
        return tr("That serial port is not there. Check the cable or choose another port.");
    case esp32::OpenFailure::PortBusy:
        return tr("Another program is using the serial port. A mirrored phone may be using the board right now.");
    case esp32::OpenFailure::NoAnswer:
        return tr("The board does not answer. On an ESP32-S3, connect the computer to the port marked COM or UART; "
                  "the other USB port goes to the phone. If it still does not answer, flash the firmware.");
    case esp32::OpenFailure::NoKey:
        return tr("Set up Wi-Fi once while the board is on the serial port; then it can be reached over Wi-Fi.");
    case esp32::OpenFailure::BadAddress:
        return tr("That address is not valid.");
    case esp32::OpenFailure::Stopped:
        break;
    }
    return {};
}

QString Esp32Panel::flashFailureText(espflash::FlashFailure failure)
{
    switch (failure) {
    case espflash::FlashFailure::NoFirmware:
        return tr("This copy of GameScrcpy has no ESP32 firmware with it.");
    case espflash::FlashFailure::NoImageForChip:
        return tr("The bundled firmware has nothing for this chip.");
    case espflash::FlashFailure::ImageMissing:
    case espflash::FlashFailure::ImageCorrupt:
        return tr("The bundled firmware file is damaged. Reinstall GameScrcpy.");
    case espflash::FlashFailure::PortMissing:
        return tr("That serial port is not there. Check the cable or choose another port.");
    case espflash::FlashFailure::PortBusy:
        return tr("Another program is using the serial port. Close it, then try again.");
    case espflash::FlashFailure::NoDownloadMode:
        return tr("Could not enter download mode: hold BOOT while plugging in the cable, then try again.");
    case espflash::FlashFailure::UnknownChip:
        return tr("The chip on this board is not one GameScrcpy supports.");
    case espflash::FlashFailure::Refused:
    case espflash::FlashFailure::WriteFailed:
        return tr("Writing the firmware failed. Try again; if it keeps failing, try another cable.");
    case espflash::FlashFailure::VerifyFailed:
        return tr("The written firmware does not match. Flash it again.");
    case espflash::FlashFailure::Lost:
        return tr("The board stopped answering while flashing. Check the cable, then flash again.");
    case espflash::FlashFailure::Cancelled:
        return tr("Flashing was cancelled.");
    }
    return {};
}

QString Esp32Panel::flashStageText(espflash::FlashStage stage)
{
    switch (stage) {
    case espflash::FlashStage::Preparing:
        return tr("Preparing…");
    case espflash::FlashStage::Connecting:
        return tr("Putting the board into download mode…");
    case espflash::FlashStage::Detecting:
        return tr("Identifying the chip…");
    case espflash::FlashStage::Erasing:
        return tr("Erasing…");
    case espflash::FlashStage::Writing:
        return tr("Writing the firmware…");
    case espflash::FlashStage::Verifying:
        return tr("Checking what was written…");
    case espflash::FlashStage::Restarting:
        return tr("Restarting the board…");
    case espflash::FlashStage::Done:
        return tr("Done");
    }
    return {};
}

QString Esp32Panel::phoneText(const esp32::BoardStatus& status)
{
    switch (status.usb) {
    case GS_ESP32_USB_READY:
        return tr("Phone connected over USB");
    case GS_ESP32_USB_SUSPENDED:
        return tr("Phone's USB is asleep");
    case GS_ESP32_USB_DETACHED:
        return tr("Waiting for the phone on USB");
    default:
        break;
    }
    switch (status.ble) {
    case GS_ESP32_BLE_CONNECTED:
        return status.peer.empty() ? tr("Phone connected over Bluetooth")
                                   : tr("Bluetooth: %1").arg(QString::fromStdString(status.peer));
    case GS_ESP32_BLE_ADVERTISING:
        return tr("Waiting for the phone to pair over Bluetooth");
    case GS_ESP32_BLE_IDLE:
        return tr("Bluetooth is off");
    default:
        break;
    }
    return {};
}

QString Esp32Panel::troubleText(const esp32::BoardTrouble& trouble)
{
    switch (trouble.problem) {
    case esp32::BoardProblem::Silent:
        return tr("Nothing answers on this port. On an ESP32-S3, connect the computer to the port marked COM or UART; "
                  "the other USB port goes to the phone.");
    case esp32::BoardProblem::Restarting:
        if (!trouble.resetReason.empty()) {
            return tr("The board keeps restarting (reset reason %1). Flash the firmware again.")
                .arg(QString::fromStdString(trouble.resetReason));
        }
        return tr("The board keeps restarting. Flash the firmware again.");
    case esp32::BoardProblem::DownloadMode:
        return tr("The board is in download mode. Press RST on the board, then connect again.");
    case esp32::BoardProblem::WrongPort:
        return tr("The bytes on this port do not look like the ESP32 touch box: the port or its speed is wrong, or the "
                  "board restarts before it can talk. On an ESP32-S3, connect the computer to the port marked COM or "
                  "UART.");
    case esp32::BoardProblem::OldFirmware:
        return tr("The board runs the old QineTouch firmware: only touches can be sent. Flash the bundled firmware to "
                  "use the keys, the mouse, the gamepad and Wi-Fi.");
    case esp32::BoardProblem::None:
        break;
    }
    return {};
}

QString Esp32Panel::partsLine(const esp32::BoardInfo& board, const esp32::BoardStatus& status)
{
    const std::uint8_t usb = status.usb != GS_ESP32_USB_NONE ? status.usb : board.parts.usb;
    const std::uint8_t ble = status.ble != GS_ESP32_BLE_NONE ? status.ble : board.parts.ble;
    const std::uint8_t wifi = status.wifi != GS_ESP32_WIFI_NONE ? status.wifi : board.wifi;
    const auto problemText = [](std::uint8_t problem) -> QString {
        switch (problem) {
        case GS_ESP32_PART_FAILED:
            return Esp32Panel::tr("did not start");
        case GS_ESP32_PART_PENDING:
            return Esp32Panel::tr("starting");
        case GS_ESP32_PART_UNSUPPORTED:
            return Esp32Panel::tr("not on this board");
        default:
            return {};
        }
    };
    if (!board.parts.known) {
        return {};
    }
    QStringList parts;
    if (board.parts.usbProblem != GS_ESP32_PART_UNSUPPORTED) {
        QString state = problemText(board.parts.usbProblem);
        if (state.isEmpty()) {
            switch (usb) {
            case GS_ESP32_USB_READY:
                state = tr("ready");
                break;
            case GS_ESP32_USB_SUSPENDED:
                state = tr("asleep");
                break;
            case GS_ESP32_USB_DETACHED:
                state = tr("no phone");
                break;
            default:
                state = tr("not started");
                break;
            }
        }
        parts.append(tr("USB to the phone: %1").arg(state));
    }
    if (board.parts.bleProblem != GS_ESP32_PART_UNSUPPORTED) {
        QString state = problemText(board.parts.bleProblem);
        if (state.isEmpty()) {
            switch (ble) {
            case GS_ESP32_BLE_CONNECTED:
                state = tr("phone connected");
                break;
            case GS_ESP32_BLE_ADVERTISING:
                state = tr("waiting for the phone");
                break;
            case GS_ESP32_BLE_IDLE:
                state = tr("off");
                break;
            default:
                state = tr("not started");
                break;
            }
        }
        parts.append(tr("Bluetooth: %1").arg(state));
    }
    if (board.parts.wifiProblem != GS_ESP32_PART_UNSUPPORTED) {
        QString state = problemText(board.parts.wifiProblem);
        if (state.isEmpty()) {
            switch (wifi) {
            case GS_ESP32_WIFI_CONNECTED:
                state = tr("connected");
                break;
            case GS_ESP32_WIFI_CONNECTING:
                state = tr("connecting");
                break;
            case GS_ESP32_WIFI_FAILED:
                state = tr("could not connect");
                break;
            case GS_ESP32_WIFI_OFF:
                state = tr("not set up");
                break;
            default:
                state = tr("not started");
                break;
            }
        }
        parts.append(tr("Wi-Fi: %1").arg(state));
    }
    return parts.join(QStringLiteral(" · "));
}

QString Esp32Panel::wifiText(const esp32::BoardStatus& status)
{
    switch (status.wifi) {
    case GS_ESP32_WIFI_OFF:
        return tr("Wi-Fi not set up");
    case GS_ESP32_WIFI_CONNECTING:
        return tr("Connecting to Wi-Fi…");
    case GS_ESP32_WIFI_CONNECTED:
        return tr("Wi-Fi %1").arg(QString::fromStdString(esp32::ipv4Text(status.ipv4)));
    case GS_ESP32_WIFI_FAILED:
        return tr("Wi-Fi could not connect: check the network name and password");
    default:
        break;
    }
    return {};
}

}
