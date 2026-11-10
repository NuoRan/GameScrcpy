#include "Esp32Boards.h"

#include <gs/platform/Thread.h>
#include <gs/platform/Usb.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QtLogging>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>

namespace gs::app {

namespace {

constexpr QStringView kSerialPrefix = u"esp32-";
constexpr std::chrono::milliseconds kSettleMargin{3000};
constexpr std::chrono::milliseconds kSettleSlice{50};
constexpr std::chrono::milliseconds kWorkerPatience{3000};
constexpr std::chrono::milliseconds kWifiRetry{15000};
constexpr std::chrono::milliseconds kBootHello{1000};
constexpr int kQuietStrikes = 2;

bool automaticPort(std::string_view port) noexcept
{
    return port.empty() || port == esp32::kAutomaticPort;
}

bool automaticSerial(const esp32::Esp32Settings& settings) noexcept
{
    return settings.link == esp32::LinkMode::Serial && automaticPort(settings.port);
}

esp32::LinkState settle(const esp32::Esp32Link& link, std::chrono::milliseconds timeout, const std::stop_token& stop)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    esp32::LinkState state = link.state();
    while (state == esp32::LinkState::Opening && !stop.stop_requested() &&
           std::chrono::steady_clock::now() < deadline) {
        state = link.waitSettled(kSettleSlice);
    }
    return state;
}

esp32::OpenFailure failureOf(esp32::SerialFailure failure) noexcept
{
    switch (failure) {
    case esp32::SerialFailure::Busy:
        return esp32::OpenFailure::PortBusy;
    case esp32::SerialFailure::NotFound:
        return esp32::OpenFailure::PortMissing;
    case esp32::SerialFailure::None:
    case esp32::SerialFailure::Refused:
        break;
    }
    return esp32::OpenFailure::NoAnswer;
}

bool blocks(const Esp32Claim& blocker, const Esp32Candidate& candidate) noexcept
{
    if (candidate.wifi) {
        return blocker.wifi;
    }
    return esp32Covers(blocker, candidate.where, false);
}

bool samePlace(const Esp32Found& found, std::string_view where, bool wifi) noexcept
{
    if (found.wifi != wifi) {
        return false;
    }
    return wifi ? found.where == where : esp32SamePort(found.where, where);
}

bool portListed(const std::vector<esp32::SerialPortInfo>& ports, std::string_view where) noexcept
{
    return std::ranges::any_of(ports,
                               [where](const esp32::SerialPortInfo& port) { return esp32SamePort(port.name, where); });
}

}

QString esp32SerialOf(const QString& where)
{
    QString serial = kSerialPrefix.toString() + where;
    serial.replace(u':', u'-');
    return serial;
}

bool isEsp32Serial(QStringView serial) noexcept
{
    return serial.startsWith(kSerialPrefix);
}

std::string esp32TouchOptions(std::string_view where, bool wifi, const std::filesystem::path& boardsFile)
{
    const QString place = QString::fromUtf8(where.data(), static_cast<qsizetype>(where.size()));
    const QString automatic =
        QString::fromLatin1(esp32::kAutomaticPort.data(), static_cast<qsizetype>(esp32::kAutomaticPort.size()));
    const QJsonObject options{
        {QStringLiteral("port"), wifi ? automatic : place},
        {QStringLiteral("link"), wifi ? QStringLiteral("wifi") : QStringLiteral("serial")},
        {QStringLiteral("address"), wifi ? place : QString()},
        {QStringLiteral("boardsFile"), QString::fromStdWString(boardsFile.wstring())}};
    return QJsonDocument(options).toJson(QJsonDocument::Compact).toStdString();
}

bool esp32SamePort(std::string_view a, std::string_view b) noexcept
{
    const int number = esp32::portNumber(a);
    return number > 0 && number == esp32::portNumber(b);
}

Esp32Claim esp32ClaimOf(const esp32::Esp32Settings& settings)
{
    if (settings.link == esp32::LinkMode::Wifi) {
        return Esp32Claim{.where = settings.address, .wifi = true, .every = true};
    }
    const bool automatic = automaticPort(settings.port);
    return Esp32Claim{.where = automatic ? std::string() : settings.port, .wifi = false, .every = automatic};
}

bool esp32Covers(const Esp32Claim& claim, std::string_view where, bool wifi) noexcept
{
    if (claim.wifi != wifi) {
        return false;
    }
    return wifi || claim.every || esp32SamePort(claim.where, where);
}

std::vector<Esp32Candidate> esp32Candidates(const esp32::Esp32Settings& settings,
                                           const std::vector<esp32::SerialPortInfo>& ports,
                                           const std::vector<Esp32Claim>& blockers)
{
    std::vector<Esp32Candidate> candidates;
    const auto add = [&candidates, &blockers](Esp32Candidate candidate) {
        if (std::ranges::none_of(blockers, [&candidate](const Esp32Claim& blocker) { return blocks(blocker, candidate); })) {
            candidates.push_back(std::move(candidate));
        }
    };
    if (settings.link == esp32::LinkMode::Wifi) {
        add(Esp32Candidate{.where = settings.address, .wifi = true, .kind = esp32::PortKind::Other, .chosen = true});
        return candidates;
    }
    if (!automaticPort(settings.port)) {
        if (!esp32::validPortName(settings.port)) {
            return candidates;
        }
        Esp32Candidate chosen{.where = settings.port, .wifi = false, .kind = esp32::PortKind::Other, .chosen = true};
        const auto listed = std::ranges::find_if(
            ports, [&settings](const esp32::SerialPortInfo& port) { return esp32SamePort(port.name, settings.port); });
        if (listed != ports.end()) {
            chosen.where = listed->name;
            chosen.kind = listed->kind;
        }
        add(std::move(chosen));
        return candidates;
    }
    for (const esp32::SerialPortInfo& port : ports) {
        if (esp32::probedAutomatically(port.kind)) {
            add(Esp32Candidate{.where = port.name, .wifi = false, .kind = port.kind, .chosen = false});
        }
    }
    return candidates;
}

Esp32Probed probeEsp32(const Esp32Candidate& candidate, const esp32::Esp32Settings& settings,
                       esp32::LinkEvents events, const std::stop_token& stop, esp32::BoardTrouble& trouble)
{
    trouble = {};
    if (candidate.wifi) {
        esp32::Esp32Settings wifi = settings;
        wifi.link = esp32::LinkMode::Wifi;
        wifi.address = candidate.where;
        Expected<esp32::OpenedLink, esp32::OpenFailure> opened = esp32::openBoard(wifi, std::move(events), stop);
        if (!opened) {
            return makeUnexpected(opened.error());
        }
        return Esp32Probe{
            .link = std::move(opened->link), .where = opened->where, .wifi = true, .kind = esp32::PortKind::Other};
    }
    esp32::SerialFailure failure = esp32::SerialFailure::None;
    std::unique_ptr<esp32::Port> port = esp32::openSerialPort(candidate.where, GS_ESP32_HANDSHAKE_BAUD, failure);
    if (port == nullptr) {
        return makeUnexpected(failureOf(failure));
    }
    esp32::LinkOptions options;
    options.fastBaud = esp32::bridge(candidate.kind);
    options.allowLegacy = candidate.chosen;
    if (!candidate.chosen) {
        options.helloWait = std::max(options.helloWait, kBootHello);
    }
    auto link = std::make_unique<esp32::Esp32Link>(std::move(port), options, std::move(events));
    const esp32::LinkState state = settle(*link, options.helloWait + kSettleMargin, stop);
    if (state != esp32::LinkState::Ready && state != esp32::LinkState::Legacy) {
        trouble = link->trouble();
        return makeUnexpected(stop.stop_requested() ? esp32::OpenFailure::Stopped : esp32::OpenFailure::NoAnswer);
    }
    return Esp32Probe{.link = std::move(link), .where = candidate.where, .wifi = false, .kind = candidate.kind};
}

Esp32Boards::Esp32Boards(Esp32Hooks hooks, QObject* parent)
    : QObject(parent)
    , m_hooks(std::move(hooks))
{
    m_gate->owner = this;
    s_instance = this;
    m_retry.setInterval(kWifiRetry);
    connect(&m_retry, &QTimer::timeout, this, &Esp32Boards::rescan);
}

Esp32Boards::~Esp32Boards()
{
    {
        const std::scoped_lock lock(m_gate->mutex);
        m_gate->owner = nullptr;
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
    m_retry.stop();
    m_watcher.reset();
    {
        const std::scoped_lock lock(m_mutex);
        m_cancel.request_stop();
    }
    m_thread.request_stop();
    m_wake.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_inbox.clear();
    m_entries.clear();
}

void Esp32Boards::post(const std::shared_ptr<Gate>& gate, std::function<void(Esp32Boards&)> work)
{
    const std::scoped_lock lock(gate->mutex);
    Esp32Boards* owner = gate->owner;
    if (owner == nullptr) {
        return;
    }
    QMetaObject::invokeMethod(owner, [owner, work = std::move(work)] { work(*owner); }, Qt::QueuedConnection);
}

void Esp32Boards::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    m_thread = std::jthread([this](const std::stop_token& stop) { run(stop); });
    if (m_hooks.watchUsb) {
        const std::shared_ptr<Gate> gate = m_gate;
        m_watcher = std::make_unique<platform::UsbWatcher>(
            [gate] { post(gate, [](Esp32Boards& boards) { boards.rescan(); }); });
    }
    requestScan();
}

void Esp32Boards::setActive(bool active)
{
    if (active == m_active) {
        return;
    }
    m_active = active;
    ++m_generation;
    if (!active) {
        closeAll();
        m_problem.reset();
        m_trouble.reset();
    }
    qInfo("ESP32 touch boxes: %s", active ? "looking for boards" : "not looked for, the extension is off");
    emit activeChanged();
    requestScan();
    updateRetry();
    emit boardsChanged();
}

void Esp32Boards::setSettings(esp32::Esp32Settings settings)
{
    if (settings.port == m_settings.port && settings.link == m_settings.link &&
        settings.address == m_settings.address && settings.boardsFile == m_settings.boardsFile) {
        return;
    }
    m_settings = std::move(settings);
    ++m_generation;
    closeAll();
    m_dismissed.clear();
    m_quiet.clear();
    m_strikes.clear();
    m_problem.reset();
    m_trouble.reset();
    requestScan();
    updateRetry();
    emit boardsChanged();
}

void Esp32Boards::rescan()
{
    requestScan();
}

std::vector<Esp32Found> Esp32Boards::boards() const
{
    std::vector<Esp32Found> found;
    found.reserve(m_entries.size());
    for (const Entry& entry : m_entries) {
        found.push_back(entry.found);
    }
    return found;
}

std::optional<Esp32Found> Esp32Boards::board(std::string_view where, bool wifi) const
{
    const Entry* entry = entryAt(where, wifi);
    return entry != nullptr ? std::optional(entry->found) : std::nullopt;
}

std::optional<Esp32Found> Esp32Boards::matching(const esp32::Esp32Settings& settings) const
{
    const Esp32Claim wanted = esp32ClaimOf(settings);
    const Entry* chosen = nullptr;
    for (const Entry& entry : m_entries) {
        if (!esp32Covers(wanted, entry.found.where, entry.found.wifi)) {
            continue;
        }
        if (chosen == nullptr || (entry.link != nullptr && chosen->link == nullptr)) {
            chosen = &entry;
        }
    }
    return chosen != nullptr ? std::optional(chosen->found) : std::nullopt;
}

esp32::Esp32Link* Esp32Boards::link(std::string_view where, bool wifi) const
{
    const Entry* entry = entryAt(where, wifi);
    return entry != nullptr ? entry->link.get() : nullptr;
}

void Esp32Boards::claim(const QString& owner, Esp32Claim claim)
{
    settleWorker();
    bool complete = false;
    const bool adopted = adopt(complete);
    for (Entry& entry : m_entries) {
        if (entry.link != nullptr && esp32Covers(claim, entry.found.where, entry.found.wifi)) {
            updateFound(entry);
            entry.link.reset();
            qInfo("ESP32 touch box on %s: handed over to %s", entry.found.where.c_str(), qUtf8Printable(owner));
        }
    }
    m_claims.insert_or_assign(owner, std::move(claim));
    ++m_claimsVersion;
    for (Entry& entry : m_entries) {
        updateFound(entry);
    }
    requestScan();
    updateRetry();
    emit claimsChanged();
    emit boardsChanged();
    if (adopted) {
        emit scanned(complete);
    }
}

void Esp32Boards::release(const QString& owner)
{
    if (m_claims.erase(owner) == 0) {
        return;
    }
    ++m_claimsVersion;
    for (Entry& entry : m_entries) {
        updateFound(entry);
    }
    requestScan();
    updateRetry();
    emit claimsChanged();
    emit boardsChanged();
}

bool Esp32Boards::held(std::string_view where, bool wifi) const
{
    return std::ranges::any_of(m_claims, [where, wifi](const std::pair<const QString, Esp32Claim>& claim) {
        return esp32Covers(claim.second, where, wifi);
    });
}

bool Esp32Boards::inSession(std::string_view where, bool wifi) const
{
    return std::ranges::any_of(m_claims, [where, wifi](const std::pair<const QString, Esp32Claim>& claim) {
        return claim.second.holder == Esp32Holder::Session && esp32Covers(claim.second, where, wifi);
    });
}

void Esp32Boards::dismiss(std::string_view where, bool wifi)
{
    settleWorker();
    bool complete = false;
    static_cast<void>(adopt(complete));
    m_dismissed.push_back(Esp32Claim{.where = std::string(where), .wifi = wifi});
    std::erase_if(m_entries, [this](const Entry& entry) {
        return dismissed(entry.found.where, entry.found.wifi) && claimOver(entry) == nullptr;
    });
    qInfo("ESP32 touch box on %s: let go until it is plugged in again", std::string(where).c_str());
    requestScan();
    updateRetry();
    emit boardsChanged();
}

void Esp32Boards::forgive()
{
    m_dismissed.clear();
    m_quiet.clear();
    m_strikes.clear();
    settleWorker();
    bool complete = false;
    const bool adopted = adopt(complete);
    requestScan();
    updateRetry();
    if (adopted) {
        emit boardsChanged();
        emit scanned(complete);
    }
}

void Esp32Boards::run(const std::stop_token& stop)
{
    platform::setCurrentThreadName("esp32 boards");
    for (;;) {
        Job job;
        std::stop_token cancel;
        {
            std::unique_lock lock(m_mutex);
            m_scanning = false;
            m_idle.notify_all();
            if (!m_wake.wait(lock, stop, [this] { return m_dirty; })) {
                return;
            }
            m_dirty = false;
            m_scanning = true;
            m_cancel = std::stop_source();
            cancel = m_cancel.get_token();
            job = m_job;
        }
        Scan scan = scanOnce(job, cancel);
        {
            const std::scoped_lock lock(m_mutex);
            m_inbox.push_back(std::move(scan));
        }
        post(m_gate, [](Esp32Boards& boards) { boards.drain(); });
    }
}

Esp32Boards::Scan Esp32Boards::scanOnce(const Job& job, const std::stop_token& cancel) const
{
    Scan scan;
    scan.generation = job.generation;
    scan.claims = job.claims;
    if (!job.active) {
        scan.complete = true;
        return scan;
    }
    if (job.settings.link == esp32::LinkMode::Serial && m_hooks.list) {
        scan.ports = m_hooks.list();
    }
    const std::vector<Esp32Candidate> candidates = esp32Candidates(job.settings, scan.ports, job.blockers);
    const bool automatic = automaticSerial(job.settings);
    bool busy = false;
    std::optional<esp32::OpenFailure> last;
    for (const Esp32Candidate& candidate : candidates) {
        if (cancel.stop_requested()) {
            return scan;
        }
        esp32::BoardTrouble trouble;
        Esp32Probed probed = m_hooks.probe(candidate, job.settings, linkEvents(), cancel, trouble);
        if (probed) {
            qInfo("ESP32 touch box found on %s", probed->where.c_str());
            scan.found.push_back(std::move(*probed));
            continue;
        }
        if (probed.error() == esp32::OpenFailure::Stopped) {
            return scan;
        }
        if (trouble.problem != esp32::BoardProblem::None) {
            if (!scan.trouble || candidate.chosen ||
                (scan.trouble->problem == esp32::BoardProblem::Silent &&
                 trouble.problem != esp32::BoardProblem::Silent)) {
                scan.trouble = trouble;
            }
        }
        busy = busy || probed.error() == esp32::OpenFailure::PortBusy;
        last = probed.error();
        if (automatic && probed.error() == esp32::OpenFailure::NoAnswer) {
            scan.silent.push_back(candidate.where);
        }
    }
    if (cancel.stop_requested()) {
        return scan;
    }
    scan.complete = true;
    if (!scan.found.empty()) {
        return scan;
    }
    if (esp32Candidates(job.settings, scan.ports, {}).empty()) {
        scan.problem = automaticPort(job.settings.port) ? esp32::OpenFailure::NoBoard : esp32::OpenFailure::PortMissing;
    } else if (!candidates.empty()) {
        scan.problem = automatic ? (busy ? esp32::OpenFailure::PortBusy : esp32::OpenFailure::NoBoard) : last;
    }
    return scan;
}

esp32::LinkEvents Esp32Boards::linkEvents() const
{
    const std::shared_ptr<Gate> gate = m_gate;
    const auto changed = [gate] { post(gate, [](Esp32Boards& boards) { boards.refresh(); }); };
    esp32::LinkEvents events;
    events.state = [changed](esp32::LinkState) { changed(); };
    events.hello = [changed](const esp32::BoardInfo&, const esp32::BoardStatus&) { changed(); };
    events.status = [changed](const esp32::BoardStatus&) { changed(); };
    return events;
}

Esp32Boards::Job Esp32Boards::jobNow() const
{
    Job job;
    job.active = m_active;
    job.generation = m_generation;
    job.claims = m_claimsVersion;
    job.settings = m_settings;
    for (const Entry& entry : m_entries) {
        if (entry.link != nullptr || claimOver(entry) != nullptr) {
            job.blockers.push_back(Esp32Claim{.where = entry.found.where, .wifi = entry.found.wifi});
        }
    }
    for (const auto& claimed : m_claims) {
        job.blockers.push_back(claimed.second);
    }
    job.blockers.insert(job.blockers.end(), m_dismissed.begin(), m_dismissed.end());
    if (automaticSerial(m_settings)) {
        job.blockers.insert(job.blockers.end(), m_quiet.begin(), m_quiet.end());
    }
    return job;
}

void Esp32Boards::requestScan()
{
    if (!m_started || !m_active) {
        return;
    }
    {
        const std::scoped_lock lock(m_mutex);
        m_job = jobNow();
        m_dirty = true;
    }
    m_wake.notify_all();
}

void Esp32Boards::settleWorker()
{
    std::unique_lock lock(m_mutex);
    m_dirty = false;
    m_cancel.request_stop();
    static_cast<void>(m_idle.wait_for(lock, kWorkerPatience, [this] { return !m_scanning; }));
}

bool Esp32Boards::adopt(bool& complete)
{
    std::vector<Scan> scans;
    {
        const std::scoped_lock lock(m_mutex);
        scans.swap(m_inbox);
    }
    if (scans.empty()) {
        return false;
    }
    for (Scan& scan : scans) {
        if (scan.generation != m_generation || !m_active) {
            continue;
        }
        for (Esp32Probe& probe : scan.found) {
            const bool claimed = std::ranges::any_of(m_claims, [&probe](const std::pair<const QString, Esp32Claim>& each) {
                return esp32Covers(each.second, probe.where, probe.wifi);
            });
            if (claimed || dismissed(probe.where, probe.wifi)) {
                continue;
            }
            Entry* entry = entryAt(probe.where, probe.wifi);
            if (entry == nullptr) {
                m_entries.push_back(Entry{.found = Esp32Found{.where = probe.where, .wifi = probe.wifi, .kind = probe.kind}});
                entry = &m_entries.back();
            } else if (entry->link != nullptr) {
                continue;
            }
            entry->found.where = probe.where;
            entry->found.kind = probe.kind;
            entry->link = std::move(probe.link);
        }
        if (!scan.complete || scan.claims != m_claimsVersion) {
            continue;
        }
        complete = true;
        m_problem = scan.problem;
        m_trouble = scan.trouble;
        for (const Esp32Probe& probe : scan.found) {
            m_strikes.erase(esp32::portNumber(probe.where));
        }
        for (const std::string& silent : scan.silent) {
            const bool quiet = std::ranges::any_of(
                m_quiet, [&silent](const Esp32Claim& each) { return esp32SamePort(each.where, silent); });
            if (!quiet && ++m_strikes[esp32::portNumber(silent)] >= kQuietStrikes) {
                m_quiet.push_back(Esp32Claim{.where = silent});
            }
        }
        if (automaticSerial(m_settings)) {
            std::erase_if(m_quiet, [&scan](const Esp32Claim& quiet) { return !portListed(scan.ports, quiet.where); });
            std::erase_if(m_strikes, [&scan](const std::pair<const int, int>& strike) {
                return std::ranges::none_of(scan.ports, [&strike](const esp32::SerialPortInfo& port) {
                    return esp32::portNumber(port.name) == strike.first;
                });
            });
        }
        if (m_settings.link == esp32::LinkMode::Serial) {
            std::erase_if(m_dismissed, [&scan](const Esp32Claim& place) {
                return !place.wifi && !portListed(scan.ports, place.where);
            });
        }
        std::erase_if(m_entries,
                      [this](const Entry& entry) { return entry.link == nullptr && claimOver(entry) == nullptr; });
    }
    for (Entry& entry : m_entries) {
        updateFound(entry);
    }
    return true;
}

void Esp32Boards::drain()
{
    bool complete = false;
    if (!adopt(complete)) {
        return;
    }
    updateRetry();
    emit boardsChanged();
    emit scanned(complete);
}

void Esp32Boards::refresh()
{
    bool lost = false;
    for (std::size_t index = m_entries.size(); index-- > 0;) {
        Entry& entry = m_entries[index];
        if (entry.link == nullptr) {
            continue;
        }
        const esp32::LinkState state = entry.link->state();
        if (state != esp32::LinkState::Lost && state != esp32::LinkState::Closed) {
            continue;
        }
        qInfo("ESP32 touch box on %s: the board stopped answering", entry.found.where.c_str());
        entry.link.reset();
        lost = true;
        if (claimOver(entry) == nullptr) {
            m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }
    for (Entry& entry : m_entries) {
        updateFound(entry);
    }
    if (lost) {
        requestScan();
        updateRetry();
    }
    emit boardsChanged();
}

void Esp32Boards::closeAll()
{
    settleWorker();
    bool complete = false;
    static_cast<void>(adopt(complete));
    m_entries.clear();
}

void Esp32Boards::updateRetry()
{
    const bool wanted = m_started && m_active && m_settings.link == esp32::LinkMode::Wifi &&
                        std::ranges::none_of(m_entries, [](const Entry& entry) { return entry.found.wifi; });
    if (wanted && !m_retry.isActive()) {
        m_retry.start();
    } else if (!wanted) {
        m_retry.stop();
    }
}

void Esp32Boards::updateFound(Entry& entry) const
{
    QString owner;
    const Esp32Claim* claim = claimOver(entry, &owner);
    entry.found.held = claim != nullptr;
    entry.found.flashing = claim != nullptr && claim->holder == Esp32Holder::Flash;
    entry.found.heldBy = owner;
    entry.found.connected = entry.link != nullptr;
    if (entry.link != nullptr) {
        entry.found.info = entry.link->board();
        entry.found.status = entry.link->status();
        entry.found.legacy = entry.link->state() == esp32::LinkState::Legacy;
    }
}

const Esp32Claim* Esp32Boards::claimOver(const Entry& entry, QString* owner) const
{
    for (const auto& [key, claim] : m_claims) {
        if (esp32Covers(claim, entry.found.where, entry.found.wifi)) {
            if (owner != nullptr) {
                *owner = key;
            }
            return &claim;
        }
    }
    return nullptr;
}

bool Esp32Boards::dismissed(std::string_view where, bool wifi) const
{
    return std::ranges::any_of(m_dismissed, [where, wifi](const Esp32Claim& place) { return esp32Covers(place, where, wifi); });
}

Esp32Boards::Entry* Esp32Boards::entryAt(std::string_view where, bool wifi)
{
    const auto found =
        std::ranges::find_if(m_entries, [where, wifi](const Entry& entry) { return samePlace(entry.found, where, wifi); });
    return found != m_entries.end() ? &*found : nullptr;
}

const Esp32Boards::Entry* Esp32Boards::entryAt(std::string_view where, bool wifi) const
{
    const auto found =
        std::ranges::find_if(m_entries, [where, wifi](const Entry& entry) { return samePlace(entry.found, where, wifi); });
    return found != m_entries.end() ? &*found : nullptr;
}

}
