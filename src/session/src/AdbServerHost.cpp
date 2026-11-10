#include "gs/session/AdbServerHost.h"

#include "DirectTransport.h"
#include "FallbackTransport.h"

#include "gs/adb/DeviceTracker.h"
#include "gs/adb/ShellProtocol.h"
#include "gs/base/Log.h"
#include "gs/platform/Crypto.h"
#include "gs/platform/Thread.h"
#include "gs/protocol/Constants.h"

#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <deque>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

namespace gs::session {

namespace {

constexpr std::string_view kLog = "session";
constexpr std::string_view kServerLog = "server";
constexpr std::chrono::seconds kLaunchTimeout{10};
constexpr std::chrono::seconds kComebackWait{10};
constexpr std::size_t kTailLines = 20;
constexpr std::size_t kMaxLine = 64 * 1024;

SessionFailure failure(FailureKind kind, std::string detail, protocol::ErrorCode code = protocol::ErrorCode::None)
{
    SessionFailure result;
    result.kind = kind;
    result.code = code;
    result.detail = std::move(detail);
    return result;
}

SessionFailure adbFailure(FailureKind kind, const adb::AdbError& error)
{
    const bool noServer = error.kind == adb::AdbErrorKind::ServerUnavailable ||
                          error.kind == adb::AdbErrorKind::ServerTooOld || error.kind == adb::AdbErrorKind::LaunchFailed;
    return failure(noServer ? FailureKind::AdbUnavailable : kind, error.message);
}

SessionFailure cancelled()
{
    return failure(FailureKind::LaunchFailed, "stopped while starting");
}

std::string_view stateName(adb::DeviceState state) noexcept
{
    switch (state) {
    case adb::DeviceState::Device:
        return "online";
    case adb::DeviceState::Offline:
        return "offline";
    case adb::DeviceState::Unauthorized:
        return "unauthorized";
    case adb::DeviceState::Authorizing:
        return "authorizing";
    case adb::DeviceState::Connecting:
        return "connecting";
    case adb::DeviceState::Recovery:
        return "in recovery";
    case adb::DeviceState::Sideload:
        return "in sideload";
    case adb::DeviceState::Bootloader:
        return "in the bootloader";
    case adb::DeviceState::NoPermissions:
        return "not accessible (no permissions)";
    case adb::DeviceState::Host:
    case adb::DeviceState::Unknown:
        break;
    }
    return "in an unknown state";
}

std::string_view trimmed(std::string_view text) noexcept
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

template <typename T>
std::optional<T> parseNumber(std::string_view text) noexcept
{
    text = trimmed(text);
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
        return std::nullopt;
    }
    return value;
}

SessionFailure launchRefused(std::string_view text)
{
    const std::size_t space = text.find(' ');
    const std::string_view message = space == std::string_view::npos ? std::string_view{} : text.substr(space + 1);
    protocol::ErrorCode code = protocol::ErrorCode::InternalError;
    if (const auto raw = parseNumber<std::uint16_t>(text.substr(0, space));
        raw && protocol::isValidEnumValue<protocol::ErrorCode>(*raw)) {
        code = static_cast<protocol::ErrorCode>(*raw);
    }
    const FailureKind kind =
        code == protocol::ErrorCode::UnsupportedAndroid ? FailureKind::UnsupportedAndroid : FailureKind::LaunchFailed;
    return failure(kind, std::format("the server could not start ({}): {}", protocol::enumName(code), message), code);
}

std::pair<log::Level, std::string_view> serverLogLine(std::string_view line) noexcept
{
    constexpr std::string_view prefix = "GameScrcpy server ";
    if (!line.starts_with(prefix)) {
        return {log::Level::Info, line};
    }
    const std::string_view rest = line.substr(prefix.size());
    const std::size_t colon = rest.find(": ");
    if (colon == std::string_view::npos) {
        return {log::Level::Info, line};
    }
    const std::string_view level = rest.substr(0, colon);
    const std::string_view text = rest.substr(colon + 2);
    if (level == "TRACE") {
        return {log::Level::Trace, text};
    }
    if (level == "DEBUG") {
        return {log::Level::Debug, text};
    }
    if (level == "WARNING") {
        return {log::Level::Warning, text};
    }
    if (level == "ERROR") {
        return {log::Level::Error, text};
    }
    return {log::Level::Info, text};
}

template <typename Sink>
void splitLines(std::string& pending, std::string_view data, Sink&& sink)
{
    pending.append(data);
    std::size_t start = 0;
    for (std::size_t newline = pending.find('\n'); newline != std::string::npos;
         newline = pending.find('\n', start)) {
        std::string_view line(pending.data() + start, newline - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        sink(line);
        start = newline + 1;
    }
    pending.erase(0, start);
    if (pending.size() > kMaxLine) {
        sink(std::string_view(pending));
        pending.clear();
    }
}

class UnreachableTransport final : public Transport
{
public:
    explicit UnreachableTransport(SessionFailure failure) : m_failure(std::move(failure)) {}

    [[nodiscard]] Expected<std::unique_ptr<Link>, SessionFailure> establish(const Token&,
                                                                            const std::string&,
                                                                            std::stop_token) override
    {
        return makeUnexpected(m_failure);
    }

private:
    SessionFailure m_failure;
};

}

std::shared_ptr<const ServerPackage> ServerPackage::load(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return nullptr;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return nullptr;
    }
    return std::make_shared<const ServerPackage>(std::move(bytes));
}

ServerPackage::ServerPackage(std::vector<std::uint8_t> bytes) : m_bytes(std::move(bytes))
{
    const platform::Sha256Digest digest = platform::sha256(m_bytes);
    m_remotePath =
        std::format("/data/local/tmp/gs-server-{}.jar", platform::toHex(std::span<const std::uint8_t>(digest).first(8)));
}

class AdbServerHost::Impl
{
public:
    Impl(adb::AdbClient client, std::string serial, std::shared_ptr<const ServerPackage> package,
         TransportChoice choice, DirectRoute direct)
        : m_client(client), m_serial(std::move(serial)), m_package(std::move(package)), m_choice(choice),
          m_direct(std::move(direct))
    {
    }

    [[nodiscard]] TransportChoice choice() const noexcept { return m_choice; }
    [[nodiscard]] const std::string& id() const noexcept { return m_id; }

    [[nodiscard]] Expected<platform::UdpEndpoint, SessionFailure> directEndpoint()
    {
        if (m_direct.endpoint) {
            return *m_direct.endpoint;
        }
        std::optional<std::uint16_t> port;
        std::string announced;
        {
            const std::lock_guard lock(m_mutex);
            port = m_udpPort;
            announced = m_announcedAddress;
        }
        if (!port) {
            return makeUnexpected(failure(FailureKind::ConnectFailed, "the server announced no UDP port"));
        }
        const auto colon = m_serial.rfind(':');
        std::string_view host =
            colon == std::string::npos ? std::string_view() : std::string_view(m_serial).substr(0, colon);
        if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
            host = host.substr(1, host.size() - 2);
        }
        auto endpoint = platform::UdpEndpoint::parse(host, *port);
        if (!endpoint && !announced.empty()) {
            endpoint = platform::UdpEndpoint::parse(announced, *port);
        }
        if (!endpoint) {
            return makeUnexpected(failure(
                FailureKind::DirectNoAddress,
                std::format("serial {} holds no address for WiFi direct and the phone announced none", m_serial)));
        }
        return *endpoint;
    }

    ~Impl() { release(); }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    Expected<void, SessionFailure> start(const LaunchParams& params, Events& events, const std::stop_token& stop)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_events = &events;
        }
        m_params = params;
        m_id = params.socketName.substr(params.socketName.find('_') + 1);
        events.hostPhase(SessionPhase::Deploying);
        if (!m_package) {
            return makeUnexpected(failure(FailureKind::DeployFailed, "the server package could not be read"));
        }
        if (auto prepared = prepare(stop); !prepared) {
            return prepared;
        }
        if (stop.stop_requested()) {
            return makeUnexpected(cancelled());
        }
        events.hostPhase(SessionPhase::Launching);
        if (auto launched = launch(stop); !launched) {
            return launched;
        }
        events.hostPhase(SessionPhase::Connecting);
        return forward();
    }

    Expected<platform::TcpStream, SessionFailure> connect()
    {
        std::optional<platform::TcpStream> stream = platform::TcpStream::connectLoopback(m_port);
        if (!stream) {
            return makeUnexpected(failure(FailureKind::ConnectFailed,
                                          std::format("nothing accepts the forwarded port {}", m_port)));
        }
        return std::move(*stream);
    }

    Expected<void, SessionFailure> restore(bool afterFailure, std::chrono::steady_clock::time_point deadline,
                                           const std::stop_token& stop)
    {
        if (afterFailure) {
            if (auto changed = awaitChange(deadline, stop); !changed) {
                return changed;
            }
        }
        if (m_port != 0) {
            (void)m_client.removeForward(m_serial, m_port);
            m_port = 0;
        }
        m_attemptTransport.reset();
        auto forwarded = forward();
        if (forwarded) {
            m_attemptTransport = onlineTransport();
        }
        return forwarded;
    }

    void stop(std::chrono::milliseconds grace)
    {
        {
            std::unique_lock lock(m_mutex);
            m_stopping = true;
            if (m_process) {
                m_changed.wait_for(lock, grace, [this] { return m_outputEnded; });
            }
        }
        release();
    }

private:
    Expected<void, SessionFailure> prepare(const std::stop_token& stop)
    {
        Expected<void, SessionFailure> prepared = checkAndDeploy();
        if (prepared || !m_attemptTransport || stop.stop_requested() || onlineTransport() == m_attemptTransport) {
            return prepared;
        }
        log::info(kLog, "[{}] adb lost the device while preparing it ({}); waiting for it to come back", m_id,
                  prepared.error().detail);
        if (auto back = awaitChange(std::chrono::steady_clock::now() + kComebackWait, stop, true); !back) {
            if (stop.stop_requested()) {
                return makeUnexpected(cancelled());
            }
            return prepared;
        }
        return checkAndDeploy();
    }

    Expected<void, SessionFailure> checkAndDeploy()
    {
        if (auto checked = checkDevice(); !checked) {
            return checked;
        }
        return deploy();
    }

    Expected<void, SessionFailure> checkDevice()
    {
        m_attemptTransport.reset();
        const auto devices = m_client.devices();
        if (!devices) {
            return makeUnexpected(adbFailure(FailureKind::AdbUnavailable, devices.error()));
        }
        const auto entry = std::ranges::find(*devices, m_serial, &adb::DeviceEntry::serial);
        if (entry == devices->end()) {
            return makeUnexpected(failure(FailureKind::DeviceNotFound, std::format("adb does not list {}", m_serial)));
        }
        if (entry->state == adb::DeviceState::Unauthorized) {
            return makeUnexpected(failure(FailureKind::DeviceUnauthorized, "USB debugging is not allowed on the device yet"));
        }
        if (entry->state != adb::DeviceState::Device) {
            return makeUnexpected(failure(FailureKind::DeviceOffline, std::format("the device is {}", stateName(entry->state))));
        }
        m_attemptTransport = entry->transportId;

        const auto sdk = m_client.shell(m_serial, "getprop ro.build.version.sdk");
        if (!sdk) {
            return makeUnexpected(adbFailure(FailureKind::DeviceOffline, sdk.error()));
        }
        const std::optional<int> level = parseNumber<int>(sdk->out);
        if (!level) {
            return makeUnexpected(failure(FailureKind::DeviceOffline,
                                          std::format("unreadable Android API level '{}'", trimmed(sdk->out))));
        }
        if (*level < AdbServerHost::kMinimumSdk) {
            return makeUnexpected(failure(FailureKind::UnsupportedAndroid,
                                          std::format("Android API {} is older than Android 9 (API {})", *level,
                                                      AdbServerHost::kMinimumSdk)));
        }
        return {};
    }

    Expected<void, SessionFailure> deploy()
    {
        const std::string& remote = m_package->remotePath();
        const auto existing = m_client.stat(m_serial, remote);
        if (!existing) {
            return makeUnexpected(adbFailure(FailureKind::DeployFailed, existing.error()));
        }
        if (*existing && (*existing)->size == m_package->bytes().size()) {
            log::debug(kLog, "[{}] {} is on the device already", m_id, remote);
            return {};
        }
        if (auto pushed = m_client.push(m_serial, m_package->bytes(), remote); !pushed) {
            return makeUnexpected(adbFailure(FailureKind::DeployFailed, pushed.error()));
        }
        log::info(kLog, "[{}] pushed {} ({} bytes)", m_id, remote, m_package->bytes().size());
        const std::string cleanup = std::format(
            "for f in /data/local/tmp/gs-server-*.jar; do [ \"$f\" = {} ] || rm -f \"$f\"; done", adb::shellQuote(remote));
        if (const auto cleaned = m_client.shell(m_serial, cleanup); !cleaned || cleaned->exitCode != 0) {
            log::warning(kLog, "[{}] could not remove old server versions", m_id);
        }
        return {};
    }

    Expected<void, SessionFailure> launch(const std::stop_token& stop)
    {
        const std::string direct = m_choice == TransportChoice::AdbTunnel
                                       ? std::string()
                                       : std::format(" transport=udp udpPort={}", m_direct.devicePort);
        const std::string command =
            std::format("trap '' HUP PIPE; CLASSPATH={} exec app_process / com.gamescrcpy.server.Main {} {} {} {}{}",
                        adb::shellQuote(m_package->remotePath()), protocol::kProtocolVersion,
                        adb::shellQuote("socket=" + m_params.socketName),
                        adb::shellQuote(std::format("lingerMs={}", m_params.linger.count())),
                        adb::shellQuote("log=" + m_params.logLevel), direct);
        auto process = m_client.spawn(m_serial, command);
        if (!process) {
            return makeUnexpected(adbFailure(FailureKind::LaunchFailed, process.error()));
        }
        m_process = std::make_unique<adb::ShellProcess>(std::move(*process));
        const std::string token = "GS_TOKEN=" + platform::toHex(m_params.token) + "\n";
        if (!m_process->writeStdin({reinterpret_cast<const std::uint8_t*>(token.data()), token.size()})) {
            return makeUnexpected(failure(FailureKind::LaunchFailed, "the token could not reach the server"));
        }
        m_output = std::jthread([this] { readOutput(); });

        std::unique_lock lock(m_mutex);
        m_changed.wait_for(lock, stop, kLaunchTimeout, [this] { return m_ready || m_launchFailure || m_outputEnded; });
        if (m_ready) {
            return {};
        }
        if (m_launchFailure) {
            return makeUnexpected(*m_launchFailure);
        }
        if (stop.stop_requested()) {
            return makeUnexpected(cancelled());
        }
        if (m_outputEnded) {
            const std::string ending =
                m_exitCode ? std::format("exited with code {}", *m_exitCode) : std::string("lost its adb connection");
            return makeUnexpected(
                failure(FailureKind::LaunchFailed, std::format("the server {} before it was ready{}", ending, tailLocked())));
        }
        return makeUnexpected(failure(FailureKind::Timeout,
                                      std::format("the server was not ready within {} s{}", kLaunchTimeout.count(), tailLocked())));
    }

    Expected<void, SessionFailure> forward()
    {
        const auto port = m_client.forward(m_serial, "localabstract:" + m_params.socketName);
        if (!port) {
            return makeUnexpected(adbFailure(FailureKind::TunnelFailed, port.error()));
        }
        m_port = *port;
        log::debug(kLog, "[{}] local port {} leads to the server", m_id, m_port);
        return {};
    }

    Expected<void, SessionFailure> awaitChange(std::chrono::steady_clock::time_point deadline, const std::stop_token& stop,
                                               bool away = false)
    {
        const std::optional<std::uint64_t> used = m_attemptTransport;
        bool serverRestarted = false;
        bool wentAway = away;
        std::optional<std::uint64_t> fresh;
        bool exited = false;
        {
            const adb::DeviceTracker tracker(m_client.port(), [&](const adb::TrackerUpdate& update) {
                const std::lock_guard lock(m_mutex);
                if (!update.serverAvailable) {
                    serverRestarted = true;
                    return;
                }
                const auto entry = std::ranges::find(update.devices, m_serial, &adb::DeviceEntry::serial);
                if (entry == update.devices.end() || entry->state != adb::DeviceState::Device) {
                    wentAway = true;
                    return;
                }
                if (!used || serverRestarted || wentAway || entry->transportId != *used) {
                    fresh = entry->transportId;
                    m_changed.notify_all();
                }
            });
            std::unique_lock lock(m_mutex);
            m_changed.wait_until(lock, stop, deadline, [&] { return fresh.has_value() || m_exitCode.has_value(); });
            exited = m_exitCode.has_value();
        }
        if (exited) {
            return makeUnexpected(failure(FailureKind::ServerEnded, "the server exited while the connection was lost"));
        }
        if (!fresh) {
            return makeUnexpected(stop.stop_requested() ? cancelled()
                                                        : failure(FailureKind::ConnectionLost,
                                                                  "the device did not come back on adb"));
        }
        log::info(kLog, "[{}] the device is back on adb", m_id);
        return {};
    }

    std::optional<std::uint64_t> onlineTransport() const
    {
        const auto devices = m_client.devices();
        if (!devices) {
            return std::nullopt;
        }
        const auto entry = std::ranges::find(*devices, m_serial, &adb::DeviceEntry::serial);
        if (entry == devices->end() || entry->state != adb::DeviceState::Device) {
            return std::nullopt;
        }
        return entry->transportId;
    }

    void release()
    {
        if (m_port != 0) {
            (void)m_client.removeForward(m_serial, m_port);
            m_port = 0;
        }
        if (m_process) {
            m_process->interrupt();
        }
        m_output = {};
        m_process.reset();
    }

    void readOutput()
    {
        platform::setCurrentThreadName("gs-server-out");
        std::string out;
        std::string err;
        std::optional<int> exitCode;
        while (std::optional<adb::ShellPacket> packet = m_process->read()) {
            const std::string_view data(reinterpret_cast<const char*>(packet->data.data()), packet->data.size());
            if (packet->id == static_cast<std::uint8_t>(adb::ShellPacketId::Stdout)) {
                splitLines(out, data, [this](std::string_view line) { onStdout(line); });
            } else if (packet->id == static_cast<std::uint8_t>(adb::ShellPacketId::Stderr)) {
                splitLines(err, data, [this](std::string_view line) { onStderr(line); });
            } else if (packet->id == static_cast<std::uint8_t>(adb::ShellPacketId::Exit)) {
                exitCode = packet->data.empty() ? -1 : packet->data[0];
                break;
            }
        }
        if (!err.empty()) {
            onStderr(err);
        }
        ended(exitCode);
    }

    void onStdout(std::string_view line)
    {
        {
            const std::lock_guard lock(m_mutex);
            if (!m_ready && !m_launchFailure) {
                if (line.starts_with("GS_READY ")) {
                    const std::string_view fields = line.substr(9);
                    const std::size_t space = fields.find(' ');
                    const auto version = parseNumber<int>(fields.substr(0, space));
                    if (space != std::string_view::npos && fields.substr(space + 1).starts_with("udp=")) {
                        const std::string_view value = fields.substr(space + 5);
                        m_udpPort = parseNumber<std::uint16_t>(value.substr(0, value.find(' ')));
                        if (const std::size_t ip = value.find(" ip="); ip != std::string_view::npos) {
                            const std::string_view list = value.substr(ip + 4);
                            m_announcedAddress = std::string(list.substr(0, list.find_first_of(", ")));
                        }
                    }
                    if (version == protocol::kProtocolVersion) {
                        m_ready = true;
                    } else {
                        m_launchFailure = failure(FailureKind::LaunchFailed,
                                                  std::format("the server is ready for protocol {}", line.substr(9)),
                                                  protocol::ErrorCode::VersionMismatch);
                    }
                    m_changed.notify_all();
                    return;
                }
                if (line.starts_with("GS_FAILED ")) {
                    m_launchFailure = launchRefused(line.substr(10));
                    m_changed.notify_all();
                    return;
                }
            }
        }
        log::info(kServerLog, "[{}] {}", m_id, line);
    }

    void onStderr(std::string_view line)
    {
        const auto [level, text] = serverLogLine(line);
        log::message(level, kServerLog, "[{}] {}", m_id, text);
        const std::lock_guard lock(m_mutex);
        m_tail.emplace_back(line);
        if (m_tail.size() > kTailLines) {
            m_tail.pop_front();
        }
    }

    void ended(std::optional<int> exitCode)
    {
        Events* events = nullptr;
        std::string tail;
        {
            const std::lock_guard lock(m_mutex);
            m_outputEnded = true;
            m_exitCode = exitCode;
            if (m_ready && exitCode && !m_stopping) {
                events = m_events;
                tail = tailLocked();
            }
            m_changed.notify_all();
        }
        if (exitCode) {
            log::info(kLog, "[{}] the server exited with code {}", m_id, *exitCode);
        } else {
            log::warning(kLog, "[{}] lost the adb connection to the server process", m_id);
        }
        if (events != nullptr) {
            events->serverEnded(failure(FailureKind::ServerEnded,
                                        std::format("the server exited with code {}{}", *exitCode, tail)));
        }
    }

    std::string tailLocked() const
    {
        if (m_tail.empty()) {
            return {};
        }
        std::string text = "; its last output:";
        for (const std::string& line : m_tail) {
            text += "\n  ";
            text += line;
        }
        return text;
    }

    const adb::AdbClient m_client;
    const std::string m_serial;
    const std::shared_ptr<const ServerPackage> m_package;
    const TransportChoice m_choice;
    const DirectRoute m_direct;

    LaunchParams m_params;
    std::string m_id;
    std::uint16_t m_port = 0;
    std::optional<std::uint64_t> m_attemptTransport;
    std::unique_ptr<adb::ShellProcess> m_process;

    std::mutex m_mutex;
    std::condition_variable_any m_changed;
    Events* m_events = nullptr;
    bool m_ready = false;
    std::optional<std::uint16_t> m_udpPort;
    std::string m_announcedAddress;
    std::optional<SessionFailure> m_launchFailure;
    bool m_outputEnded = false;
    std::optional<int> m_exitCode;
    bool m_stopping = false;
    std::deque<std::string> m_tail;

    std::jthread m_output;
};

AdbServerHost::AdbServerHost(adb::AdbClient client, std::string serial, std::shared_ptr<const ServerPackage> package,
                             TransportChoice choice, DirectRoute direct)
    : m_impl(std::make_unique<Impl>(client, std::move(serial), std::move(package), choice, std::move(direct)))
{
}

std::unique_ptr<Transport> AdbServerHost::transport(std::chrono::milliseconds handshakeTimeout)
{
    if (m_impl->choice() == TransportChoice::AdbTunnel) {
        return ServerHost::transport(handshakeTimeout);
    }
    auto endpoint = m_impl->directEndpoint();
    if (m_impl->choice() == TransportChoice::WifiDirect) {
        if (!endpoint) {
            return std::make_unique<UnreachableTransport>(std::move(endpoint.error()));
        }
        return std::make_unique<detail::DirectTransport>(*endpoint);
    }
    if (!endpoint) {
        log::info(kLog, "[{}] {}; using the adb tunnel", m_impl->id(), endpoint.error().detail);
        return ServerHost::transport(handshakeTimeout);
    }
    return std::make_unique<detail::FallbackTransport>(std::make_unique<detail::DirectTransport>(*endpoint),
                                                       ServerHost::transport(handshakeTimeout), m_impl->id());
}

AdbServerHost::~AdbServerHost() = default;

Expected<void, SessionFailure> AdbServerHost::start(const LaunchParams& params, Events& events, std::stop_token stop)
{
    return m_impl->start(params, events, stop);
}

Expected<platform::TcpStream, SessionFailure> AdbServerHost::connect()
{
    return m_impl->connect();
}

Expected<void, SessionFailure> AdbServerHost::restore(bool afterFailure, std::chrono::steady_clock::time_point deadline,
                                                      std::stop_token stop)
{
    return m_impl->restore(afterFailure, deadline, stop);
}

void AdbServerHost::stop(std::chrono::milliseconds grace)
{
    m_impl->stop(grace);
}

}
