#include "gs/adb/AdbClient.h"

#include "HostProtocol.h"
#include "Wire.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <ctime>
#include <format>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace gs::adb {

using detail::bytesOf;
using detail::failure;
using detail::readLengthPrefixed;
using detail::readStatus;
using detail::request;
using platform::TcpStream;

namespace {

constexpr auto kRequestTimeout = std::chrono::seconds(10);
constexpr auto kWirelessTimeout = std::chrono::seconds(45);
constexpr std::size_t kMaxShellOutput = 16U << 20U;
constexpr std::size_t kSyncDataMax = 64U * 1024U;
constexpr std::size_t kSyncPathMax = 1024;
constexpr std::size_t kStdinChunk = 4096;
constexpr std::uint32_t kRegularFile = 0100000;

bool hasId(std::span<const std::uint8_t> bytes, std::string_view id) noexcept
{
    return bytes.size() >= id.size() && std::equal(id.begin(), id.end(), bytes.begin(), [](char c, std::uint8_t b) {
               return static_cast<std::uint8_t>(c) == b;
           });
}

template <typename T>
std::optional<T> parseNumber(std::string_view text, int base) noexcept
{
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
        return std::nullopt;
    }
    return value;
}

AdbResult<TcpStream> connectServer(std::uint16_t port)
{
    return detail::connectServer(port, kRequestTimeout);
}

AdbResult<std::string> hostQuery(std::uint16_t port, std::string_view text)
{
    AdbResult<TcpStream> stream = connectServer(port);
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    if (AdbResult<void> ok = request(*stream, text); !ok) {
        return makeUnexpected(std::move(ok.error()));
    }
    return readLengthPrefixed(*stream);
}

AdbResult<std::string> hostServiceQuery(std::uint16_t port, std::string_view text, const std::stop_token& stop)
{
    AdbResult<TcpStream> stream = detail::connectServer(port, kWirelessTimeout);
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    const std::stop_callback cancel(stop, [&stream] { stream->interrupt(); });
    if (AdbResult<void> ok = request(*stream, text); !ok) {
        return makeUnexpected(std::move(ok.error()));
    }
    return detail::readTextOrFailure(*stream);
}

AdbResult<TcpStream> hostSerialRequest(std::uint16_t port, std::string_view serial, std::string_view service)
{
    AdbResult<TcpStream> stream = connectServer(port);
    if (!stream) {
        return stream;
    }
    if (AdbResult<void> routed = request(*stream, std::format("host-serial:{}:{}", serial, service)); !routed) {
        return makeUnexpected(std::move(routed.error()));
    }
    return stream;
}

AdbResult<TcpStream> openTransport(std::uint16_t port, std::string_view serial)
{
    AdbResult<TcpStream> stream = connectServer(port);
    if (!stream) {
        return stream;
    }
    if (AdbResult<void> switched = request(*stream, std::format("host:transport:{}", serial)); !switched) {
        return makeUnexpected(std::move(switched.error()));
    }
    return stream;
}

AdbResult<TcpStream> openService(std::uint16_t port, std::string_view serial, std::string_view service)
{
    AdbResult<TcpStream> stream = openTransport(port, serial);
    if (!stream) {
        return stream;
    }
    if (AdbResult<void> opened = request(*stream, service); !opened) {
        return makeUnexpected(std::move(opened.error()));
    }
    return stream;
}

void appendSyncHeader(std::vector<std::uint8_t>& out, std::string_view id, std::uint32_t value)
{
    out.insert(out.end(), id.begin(), id.end());
    std::array<std::uint8_t, 4> encoded{};
    detail::storeLe32(value, encoded.data());
    out.insert(out.end(), encoded.begin(), encoded.end());
}

AdbResult<TcpStream> openSync(std::uint16_t port, std::string_view serial, std::string_view remotePath)
{
    if (remotePath.empty() || remotePath.size() > kSyncPathMax) {
        return failure(AdbErrorKind::Protocol, "remote path is empty or too long");
    }
    return openService(port, serial, "sync:");
}

void quitSync(TcpStream& stream)
{
    std::vector<std::uint8_t> quit;
    appendSyncHeader(quit, "QUIT", 0);
    (void)stream.writeAll(quit);
}

constexpr std::uint32_t kTypeMask = 0170000;
constexpr std::uint32_t kDirectoryType = 0040000;
constexpr std::size_t kSyncHeader = 8;
constexpr std::size_t kListEntryV1 = 20;
constexpr std::size_t kListEntryV2 = 76;
constexpr std::size_t kStatV2 = 72;
constexpr std::size_t kMostShellCommand = 32U * 1024U;
constexpr auto kTransferFinishTimeout = std::chrono::seconds(60);
constexpr std::string_view kCancelled = "cancelled";

bool hasFeature(const std::vector<std::string>& features, std::string_view name)
{
    return std::ranges::find(features, name) != features.end();
}

bool sendSyncRequest(TcpStream& stream, std::string_view id, std::string_view path)
{
    std::vector<std::uint8_t> message;
    message.reserve(kSyncHeader + path.size());
    appendSyncHeader(message, id, static_cast<std::uint32_t>(path.size()));
    message.insert(message.end(), path.begin(), path.end());
    return stream.writeAll(message);
}

Unexpected<AdbError> syncFailure(TcpStream& stream, std::uint32_t length, std::string_view during)
{
    if (length > kSyncDataMax) {
        return failure(AdbErrorKind::Protocol, std::format("oversized failure reason during {}", during));
    }
    std::string reason(length, '\0');
    if (!reason.empty() && !stream.readExact(bytesOf(reason))) {
        return failure(AdbErrorKind::Io, std::format("connection closed inside the failure reason during {}", during));
    }
    return failure(AdbErrorKind::Rejected, std::move(reason));
}

Unexpected<AdbError> interrupted(const std::stop_token& stop, std::string_view what)
{
    return failure(AdbErrorKind::Io, stop.stop_requested() ? std::string(kCancelled) : std::string(what));
}

struct SyncStat {
    std::uint32_t mode = 0;
    std::uint64_t size = 0;
};

AdbResult<SyncStat> syncStat(TcpStream& stream, std::string_view remotePath, bool v2)
{
    if (!sendSyncRequest(stream, v2 ? "STA2" : "STAT", remotePath)) {
        return failure(AdbErrorKind::Io, "connection closed during stat");
    }
    std::array<std::uint8_t, kStatV2> reply{};
    const std::size_t size = v2 ? kStatV2 : 16;
    if (!stream.readExact(std::span(reply).first(size))) {
        return failure(AdbErrorKind::Io, "connection closed before the stat reply");
    }
    if (!hasId(reply, v2 ? "STA2" : "STAT")) {
        return failure(AdbErrorKind::Protocol, "unexpected stat reply");
    }
    if (!v2) {
        return SyncStat{detail::loadLe32(reply.data() + 4), detail::loadLe32(reply.data() + 8)};
    }
    if (detail::loadLe32(reply.data() + 4) != 0) {
        return SyncStat{};
    }
    return SyncStat{detail::loadLe32(reply.data() + 24), detail::loadLe64(reply.data() + 40)};
}

std::uint32_t localMode(const std::filesystem::path& local)
{
    std::error_code error;
    const std::filesystem::perms permissions = std::filesystem::status(local, error).permissions();
    if (error) {
        return 0644;
    }
    return (permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none ? 0644U : 0444U;
}

std::uint32_t localModified(const std::filesystem::path& local)
{
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(local, error);
    if (!error) {
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::clock_cast<std::chrono::system_clock>(written).time_since_epoch())
                                 .count();
        if (seconds > 0 && seconds <= std::numeric_limits<std::uint32_t>::max()) {
            return static_cast<std::uint32_t>(seconds);
        }
    }
    return static_cast<std::uint32_t>(std::time(nullptr));
}

std::string trimmed(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    const auto first = std::ranges::find_if(text, [](char c) { return std::isspace(static_cast<unsigned char>(c)) == 0; });
    text.erase(text.begin(), first);
    return text;
}

AdbResult<void> runChecked(const AdbClient& client, std::string_view serial, const std::string& command)
{
    AdbResult<ShellResult> result = client.shell(serial, command);
    if (!result) {
        return makeUnexpected(std::move(result.error()));
    }
    if (result->exitCode != 0) {
        std::string reason = trimmed(result->err.empty() ? std::move(result->out) : std::move(result->err));
        if (reason.empty()) {
            reason = std::format("exit status {}", result->exitCode);
        }
        return failure(AdbErrorKind::Rejected, std::move(reason));
    }
    return {};
}

bool removablePath(std::string_view path)
{
    return path.size() > 1 && path.front() == '/' && path.find_first_not_of('/') != std::string_view::npos;
}

std::string quotedList(std::string_view prefix, std::span<const std::string> paths)
{
    std::string command(prefix);
    for (const std::string& path : paths) {
        command.push_back(' ');
        command.append(shellQuote(path));
    }
    return command;
}

template <typename Build>
AdbResult<void> runBatched(const AdbClient& client, std::string_view serial, std::span<const std::string> paths,
                           Build build)
{
    std::size_t first = 0;
    while (first < paths.size()) {
        std::size_t last = first + 1;
        std::size_t length = shellQuote(paths[first]).size();
        while (last < paths.size() && length + shellQuote(paths[last]).size() + 1 < kMostShellCommand) {
            length += shellQuote(paths[last]).size() + 1;
            ++last;
        }
        if (AdbResult<void> done = runChecked(client, serial, build(paths.subspan(first, last - first))); !done) {
            return done;
        }
        first = last;
    }
    return {};
}

}

ShellProcess::ShellProcess(std::unique_ptr<platform::TcpStream> stream) : m_stream(std::move(stream)) {}
ShellProcess::ShellProcess(ShellProcess&&) noexcept = default;
ShellProcess& ShellProcess::operator=(ShellProcess&&) noexcept = default;
ShellProcess::~ShellProcess() = default;

bool ShellProcess::writeStdin(std::span<const std::uint8_t> bytes)
{
    std::vector<std::uint8_t> packet;
    while (!bytes.empty()) {
        const std::size_t chunk = std::min(bytes.size(), kStdinChunk);
        packet.assign(kShellHeaderSize, 0);
        packet[0] = static_cast<std::uint8_t>(ShellPacketId::Stdin);
        detail::storeLe32(static_cast<std::uint32_t>(chunk), packet.data() + 1);
        packet.insert(packet.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(chunk));
        if (!m_stream->writeAll(packet)) {
            return false;
        }
        bytes = bytes.subspan(chunk);
    }
    return true;
}

bool ShellProcess::closeStdin()
{
    std::array<std::uint8_t, kShellHeaderSize> packet{static_cast<std::uint8_t>(ShellPacketId::CloseStdin)};
    return m_stream->writeAll(packet);
}

std::optional<ShellPacket> ShellProcess::read()
{
    std::array<std::uint8_t, 16 * 1024> buffer{};
    for (;;) {
        if (std::optional<ShellPacket> packet = m_parser.next()) {
            return packet;
        }
        if (m_parser.failed()) {
            return std::nullopt;
        }
        const std::size_t received = m_stream->readSome(buffer);
        if (received == 0) {
            return std::nullopt;
        }
        m_parser.feed(std::span(buffer).first(received));
    }
}

void ShellProcess::interrupt() noexcept
{
    if (m_stream) {
        m_stream->interrupt();
    }
}

std::uint16_t AdbClient::portFromSetting(std::string_view setting) noexcept
{
    const std::optional<std::uint16_t> port = parseNumber<std::uint16_t>(setting, 10);
    return port && *port != 0 ? *port : kDefaultPort;
}

AdbResult<int> AdbClient::serverVersion() const
{
    AdbResult<std::string> text = hostQuery(m_port, "host:version");
    if (!text) {
        return makeUnexpected(std::move(text.error()));
    }
    const std::optional<int> version = parseNumber<int>(*text, 16);
    if (!version) {
        return failure(AdbErrorKind::Protocol, "malformed server version");
    }
    return *version;
}

AdbResult<std::vector<DeviceEntry>> AdbClient::devices() const
{
    AdbResult<std::string> text = hostQuery(m_port, "host:devices-l");
    if (!text) {
        return makeUnexpected(std::move(text.error()));
    }
    return parseDeviceList(*text);
}

AdbResult<std::vector<std::string>> AdbClient::features(std::string_view serial) const
{
    AdbResult<TcpStream> stream = hostSerialRequest(m_port, serial, "features");
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    AdbResult<std::string> text = readLengthPrefixed(*stream);
    if (!text) {
        return makeUnexpected(std::move(text.error()));
    }
    std::vector<std::string> result;
    std::string_view rest = *text;
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        if (comma != 0) {
            result.emplace_back(rest.substr(0, comma));
        }
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
    }
    return result;
}

AdbResult<std::string> AdbClient::connect(std::string_view address, std::stop_token stop) const
{
    AdbResult<std::string> reply = hostServiceQuery(m_port, std::format("host:connect:{}", address), stop);
    if (!reply) {
        return reply;
    }
    for (const std::string_view prefix : {std::string_view("connected to "), std::string_view("already connected to ")}) {
        if (reply->starts_with(prefix)) {
            std::string serial = reply->substr(prefix.size());
            while (!serial.empty() && std::isspace(static_cast<unsigned char>(serial.back())) != 0) {
                serial.pop_back();
            }
            return serial;
        }
    }
    return failure(AdbErrorKind::Rejected, std::move(*reply));
}

AdbResult<std::string> AdbClient::pair(std::string_view address, std::string_view code, std::stop_token stop) const
{
    AdbResult<std::string> reply = hostServiceQuery(m_port, std::format("host:pair:{}:{}", code, address), stop);
    if (!reply) {
        return reply;
    }
    if (reply->starts_with("Successfully paired")) {
        return reply;
    }
    return failure(AdbErrorKind::Rejected, std::move(*reply));
}

AdbResult<std::vector<WirelessService>> AdbClient::wirelessServices() const
{
    AdbResult<std::string> text = hostQuery(m_port, "host:mdns:services");
    if (!text) {
        return makeUnexpected(std::move(text.error()));
    }
    return parseWirelessServices(*text);
}

AdbResult<void> AdbClient::disconnect(std::string_view address) const
{
    AdbResult<std::string> reply = hostQuery(m_port, std::format("host:disconnect:{}", address));
    if (!reply) {
        return makeUnexpected(std::move(reply.error()));
    }
    return {};
}

AdbResult<ShellResult> AdbClient::shell(std::string_view serial, std::string_view command) const
{
    AdbResult<TcpStream> stream = openService(m_port, serial, std::format("shell,v2,raw:{}", command));
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    ShellResult result;
    ShellPacketParser parser;
    std::array<std::uint8_t, 16 * 1024> buffer{};
    for (;;) {
        while (std::optional<ShellPacket> packet = parser.next()) {
            const auto append = [&](std::string& text) {
                text.append(reinterpret_cast<const char*>(packet->data.data()), packet->data.size());
            };
            switch (static_cast<ShellPacketId>(packet->id)) {
            case ShellPacketId::Stdout:
                append(result.out);
                break;
            case ShellPacketId::Stderr:
                append(result.err);
                break;
            case ShellPacketId::Exit:
                if (packet->data.empty()) {
                    return failure(AdbErrorKind::Protocol, "exit packet without a status");
                }
                result.exitCode = packet->data.front();
                return result;
            default:
                break;
            }
            if (result.out.size() + result.err.size() > kMaxShellOutput) {
                return failure(AdbErrorKind::Protocol, "shell output exceeds the limit");
            }
        }
        if (parser.failed()) {
            return failure(AdbErrorKind::Protocol, "malformed shell packet");
        }
        const std::size_t received = stream->readSome(buffer);
        if (received == 0) {
            return failure(AdbErrorKind::Io, "shell ended without an exit status");
        }
        parser.feed(std::span(buffer).first(received));
    }
}

AdbResult<ShellProcess> AdbClient::spawn(std::string_view serial, std::string_view command) const
{
    AdbResult<TcpStream> stream = openService(m_port, serial, std::format("shell,v2,raw:{}", command));
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    stream->setReadTimeout(std::chrono::milliseconds::zero());
    stream->setWriteTimeout(std::chrono::milliseconds::zero());
    return ShellProcess(std::make_unique<TcpStream>(std::move(*stream)));
}

AdbResult<void> AdbClient::push(std::string_view serial, std::span<const std::uint8_t> content,
                                std::string_view remotePath, std::uint32_t permissions) const
{
    AdbResult<TcpStream> stream = openSync(m_port, serial, remotePath);
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    const std::string pathAndMode = std::format("{},{}", remotePath, kRegularFile | (permissions & 0777U));
    std::vector<std::uint8_t> message;
    message.reserve(kSyncDataMax + 64);
    appendSyncHeader(message, "SEND", static_cast<std::uint32_t>(pathAndMode.size()));
    message.insert(message.end(), pathAndMode.begin(), pathAndMode.end());
    while (!content.empty()) {
        const std::size_t chunk = std::min(content.size(), kSyncDataMax);
        appendSyncHeader(message, "DATA", static_cast<std::uint32_t>(chunk));
        message.insert(message.end(), content.begin(), content.begin() + static_cast<std::ptrdiff_t>(chunk));
        content = content.subspan(chunk);
        if (!stream->writeAll(message)) {
            return failure(AdbErrorKind::Io, "connection closed during push");
        }
        message.clear();
    }
    appendSyncHeader(message, "DONE", static_cast<std::uint32_t>(std::time(nullptr)));
    if (!stream->writeAll(message)) {
        return failure(AdbErrorKind::Io, "connection closed during push");
    }

    std::array<std::uint8_t, 8> reply{};
    if (!stream->readExact(reply)) {
        return failure(AdbErrorKind::Io, "connection closed before the push status");
    }
    const std::uint32_t length = detail::loadLe32(reply.data() + 4);
    if (hasId(reply, "OKAY")) {
        quitSync(*stream);
        return {};
    }
    if (hasId(reply, "FAIL") && length <= kSyncDataMax) {
        std::string reason(length, '\0');
        if (!reason.empty() && !stream->readExact(bytesOf(reason))) {
            return failure(AdbErrorKind::Io, "connection closed inside the push status");
        }
        return failure(AdbErrorKind::Rejected, std::move(reason));
    }
    return failure(AdbErrorKind::Protocol, "unexpected push status");
}

AdbResult<std::optional<RemoteFileStat>> AdbClient::stat(std::string_view serial, std::string_view remotePath) const
{
    AdbResult<TcpStream> stream = openSync(m_port, serial, remotePath);
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    std::vector<std::uint8_t> message;
    appendSyncHeader(message, "STAT", static_cast<std::uint32_t>(remotePath.size()));
    message.insert(message.end(), remotePath.begin(), remotePath.end());
    if (!stream->writeAll(message)) {
        return failure(AdbErrorKind::Io, "connection closed during stat");
    }
    std::array<std::uint8_t, 16> reply{};
    if (!stream->readExact(reply)) {
        return failure(AdbErrorKind::Io, "connection closed before the stat reply");
    }
    if (!hasId(reply, "STAT")) {
        return failure(AdbErrorKind::Protocol, "unexpected stat reply");
    }
    quitSync(*stream);
    const RemoteFileStat info{detail::loadLe32(reply.data() + 4), detail::loadLe32(reply.data() + 8),
                              detail::loadLe32(reply.data() + 12)};
    if (info.mode == 0) {
        return std::optional<RemoteFileStat>{};
    }
    return std::optional<RemoteFileStat>{info};
}

AdbResult<std::uint16_t> AdbClient::forward(std::string_view serial, std::string_view remote) const
{
    AdbResult<TcpStream> stream = hostSerialRequest(m_port, serial, std::format("forward:tcp:0;{}", remote));
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    if (AdbResult<void> done = readStatus(*stream); !done) {
        return makeUnexpected(std::move(done.error()));
    }
    AdbResult<std::string> text = readLengthPrefixed(*stream);
    if (!text) {
        return makeUnexpected(std::move(text.error()));
    }
    const std::optional<std::uint16_t> port = parseNumber<std::uint16_t>(*text, 10);
    if (!port || *port == 0) {
        return failure(AdbErrorKind::Protocol, "malformed forwarded port");
    }
    return *port;
}

AdbResult<void> AdbClient::removeForward(std::string_view serial, std::uint16_t localPort) const
{
    AdbResult<TcpStream> stream = hostSerialRequest(m_port, serial, std::format("killforward:tcp:{}", localPort));
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    return readStatus(*stream);
}

AdbResult<void> AdbClient::reverse(std::string_view serial, std::string_view remote, std::uint16_t localPort) const
{
    AdbResult<TcpStream> stream =
        openService(m_port, serial, std::format("reverse:forward:{};tcp:{}", remote, localPort));
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    return readStatus(*stream);
}

AdbResult<void> AdbClient::removeReverse(std::string_view serial, std::string_view remote) const
{
    AdbResult<TcpStream> stream = openService(m_port, serial, std::format("reverse:killforward:{}", remote));
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    return readStatus(*stream);
}

AdbResult<std::vector<ForwardEntry>> AdbClient::forwards() const
{
    AdbResult<std::string> text = hostQuery(m_port, "host:list-forward");
    if (!text) {
        return makeUnexpected(std::move(text.error()));
    }
    return parseForwardList(*text);
}

AdbResult<std::vector<ForwardEntry>> AdbClient::reverses(std::string_view serial) const
{
    AdbResult<TcpStream> stream = openService(m_port, serial, "reverse:list-forward");
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    AdbResult<std::string> text = readLengthPrefixed(*stream);
    if (!text) {
        return makeUnexpected(std::move(text.error()));
    }
    return parseReverseList(*text);
}

AdbResult<std::vector<RemoteEntry>> AdbClient::list(std::string_view serial, std::string_view remoteDir) const
{
    AdbResult<std::vector<std::string>> supported = features(serial);
    if (!supported) {
        return makeUnexpected(std::move(supported.error()));
    }
    const bool v2 = hasFeature(*supported, "ls_v2");
    AdbResult<TcpStream> stream = openSync(m_port, serial, remoteDir);
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    if (!sendSyncRequest(*stream, v2 ? "LIS2" : "LIST", remoteDir)) {
        return failure(AdbErrorKind::Io, "connection closed during list");
    }
    const std::size_t entrySize = v2 ? kListEntryV2 : kListEntryV1;
    std::array<std::uint8_t, kListEntryV2> header{};
    std::vector<RemoteEntry> entries;
    for (;;) {
        if (!stream->readExact(std::span(header).first(kSyncHeader))) {
            return failure(AdbErrorKind::Io, "connection closed inside the list");
        }
        if (hasId(header, "FAIL")) {
            return syncFailure(*stream, detail::loadLe32(header.data() + 4), "list");
        }
        const bool done = hasId(header, "DONE");
        if (!done && !hasId(header, v2 ? "DNT2" : "DENT")) {
            return failure(AdbErrorKind::Protocol, "unexpected list entry");
        }
        if (!stream->readExact(std::span(header).subspan(kSyncHeader, entrySize - kSyncHeader))) {
            return failure(AdbErrorKind::Io, "connection closed inside a list entry");
        }
        if (done) {
            break;
        }
        const std::uint32_t nameLength = detail::loadLe32(header.data() + entrySize - 4);
        if (nameLength > kSyncPathMax) {
            return failure(AdbErrorKind::Protocol, "list entry with a bad name length");
        }
        std::string name(nameLength, '\0');
        if (!name.empty() && !stream->readExact(bytesOf(name))) {
            return failure(AdbErrorKind::Io, "connection closed inside a list entry name");
        }
        RemoteEntry entry;
        if (v2) {
            if (detail::loadLe32(header.data() + 4) != 0) {
                continue;
            }
            entry.mode = detail::loadLe32(header.data() + 24);
            entry.size = detail::loadLe64(header.data() + 40);
            entry.modifiedSeconds = static_cast<std::int64_t>(detail::loadLe64(header.data() + 56));
        } else {
            entry.mode = detail::loadLe32(header.data() + 4);
            entry.size = detail::loadLe32(header.data() + 8);
            entry.modifiedSeconds = detail::loadLe32(header.data() + 12);
        }
        if (name.empty() || name == "." || name == "..") {
            continue;
        }
        entry.name = std::move(name);
        entry.directory = (entry.mode & kTypeMask) == kDirectoryType;
        entries.push_back(std::move(entry));
    }
    quitSync(*stream);
    return entries;
}

AdbResult<void> AdbClient::pushFile(std::string_view serial, const std::filesystem::path& local,
                                    std::string_view remote, TransferProgress progress, std::stop_token stop) const
{
    std::error_code error;
    const std::uint64_t total = std::filesystem::file_size(local, error);
    if (error) {
        return failure(AdbErrorKind::Io, "the local file cannot be read");
    }
    std::ifstream file(local, std::ios::binary);
    if (!file) {
        return failure(AdbErrorKind::Io, "the local file cannot be read");
    }
    const std::string pathAndMode = std::format("{},{}", remote, kRegularFile | localMode(local));
    if (pathAndMode.size() > kSyncPathMax) {
        return failure(AdbErrorKind::Protocol, "remote path is too long");
    }
    const std::uint32_t modified = localModified(local);
    AdbResult<TcpStream> stream = openSync(m_port, serial, remote);
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    const std::stop_callback cancel(stop, [&stream] { stream->interrupt(); });
    std::vector<std::uint8_t> message;
    message.reserve(kSyncDataMax + 64);
    appendSyncHeader(message, "SEND", static_cast<std::uint32_t>(pathAndMode.size()));
    message.insert(message.end(), pathAndMode.begin(), pathAndMode.end());
    std::vector<char> chunk(kSyncDataMax);
    std::uint64_t done = 0;
    if (progress) {
        progress(0, total);
    }
    for (;;) {
        if (stop.stop_requested()) {
            return interrupted(stop, "push stopped");
        }
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto got = static_cast<std::size_t>(file.gcount());
        if (got > 0) {
            appendSyncHeader(message, "DATA", static_cast<std::uint32_t>(got));
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(chunk.data());
            message.insert(message.end(), bytes, bytes + got);
            if (!stream->writeAll(message)) {
                return interrupted(stop, "connection closed during push");
            }
            message.clear();
            done += got;
            if (progress) {
                progress(done, std::max(total, done));
            }
        }
        if (!file) {
            if (!file.eof()) {
                return failure(AdbErrorKind::Io, "the local file cannot be read");
            }
            break;
        }
    }
    if (stop.stop_requested()) {
        return interrupted(stop, "push stopped");
    }
    appendSyncHeader(message, "DONE", modified);
    if (!stream->writeAll(message)) {
        return interrupted(stop, "connection closed during push");
    }
    stream->setReadTimeout(kTransferFinishTimeout);
    std::array<std::uint8_t, kSyncHeader> reply{};
    if (!stream->readExact(reply)) {
        return interrupted(stop, "connection closed before the push status");
    }
    if (hasId(reply, "OKAY")) {
        quitSync(*stream);
        return {};
    }
    if (hasId(reply, "FAIL")) {
        return syncFailure(*stream, detail::loadLe32(reply.data() + 4), "push");
    }
    return failure(AdbErrorKind::Protocol, "unexpected push status");
}

AdbResult<void> AdbClient::pullFile(std::string_view serial, std::string_view remote,
                                    const std::filesystem::path& local, TransferProgress progress,
                                    std::stop_token stop) const
{
    AdbResult<std::vector<std::string>> supported = features(serial);
    if (!supported) {
        return makeUnexpected(std::move(supported.error()));
    }
    const bool statV2 = hasFeature(*supported, "stat_v2");
    const bool recvV2 = hasFeature(*supported, "sendrecv_v2");
    AdbResult<TcpStream> stream = openSync(m_port, serial, remote);
    if (!stream) {
        return makeUnexpected(std::move(stream.error()));
    }
    const std::stop_callback cancel(stop, [&stream] { stream->interrupt(); });
    AdbResult<SyncStat> info = syncStat(*stream, remote, statV2);
    if (!info) {
        if (stop.stop_requested()) {
            return interrupted(stop, {});
        }
        return makeUnexpected(std::move(info.error()));
    }
    if (info->mode == 0) {
        return failure(AdbErrorKind::Rejected, "No such file or directory");
    }
    if ((info->mode & kTypeMask) == kDirectoryType) {
        return failure(AdbErrorKind::Rejected, "Is a directory");
    }
    std::filesystem::path partial = local;
    partial += L".gs-part";
    std::ofstream file(partial, std::ios::binary | std::ios::trunc);
    if (!file) {
        return failure(AdbErrorKind::Io, "the local file cannot be created");
    }
    const auto discard = [&file, &partial] {
        file.close();
        std::error_code ignored;
        std::filesystem::remove(partial, ignored);
    };
    std::vector<std::uint8_t> request;
    appendSyncHeader(request, recvV2 ? "RCV2" : "RECV", static_cast<std::uint32_t>(remote.size()));
    request.insert(request.end(), remote.begin(), remote.end());
    if (recvV2) {
        appendSyncHeader(request, "RCV2", 0);
    }
    if (!stream->writeAll(request)) {
        discard();
        return interrupted(stop, "connection closed during pull");
    }
    const std::uint64_t total = info->size;
    std::uint64_t done = 0;
    if (progress) {
        progress(0, total);
    }
    std::vector<std::uint8_t> chunk(kSyncDataMax);
    std::array<std::uint8_t, kSyncHeader> header{};
    for (;;) {
        if (!stream->readExact(header)) {
            discard();
            return interrupted(stop, "connection closed during pull");
        }
        const std::uint32_t length = detail::loadLe32(header.data() + 4);
        if (hasId(header, "DONE")) {
            break;
        }
        if (hasId(header, "FAIL")) {
            discard();
            return syncFailure(*stream, length, "pull");
        }
        if (!hasId(header, "DATA") || length > kSyncDataMax) {
            discard();
            return failure(AdbErrorKind::Protocol, "unexpected pull data");
        }
        if (!stream->readExact(std::span(chunk).first(length))) {
            discard();
            return interrupted(stop, "connection closed during pull");
        }
        file.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(length));
        if (!file) {
            discard();
            return failure(AdbErrorKind::Io, "the local file cannot be written");
        }
        done += length;
        if (progress) {
            progress(done, std::max(total, done));
        }
        if (stop.stop_requested()) {
            discard();
            return interrupted(stop, "pull stopped");
        }
    }
    file.close();
    if (file.fail()) {
        discard();
        return failure(AdbErrorKind::Io, "the local file cannot be written");
    }
    std::error_code renamed;
    std::filesystem::rename(partial, local, renamed);
    if (renamed) {
        discard();
        return failure(AdbErrorKind::Io, "the local file cannot be replaced");
    }
    quitSync(*stream);
    return {};
}

std::string AdbClient::removeCommand(std::span<const std::string> remotePaths)
{
    return quotedList("rm -rf", remotePaths);
}

std::string AdbClient::renameCommand(std::string_view from, std::string_view to)
{
    return std::format("if [ -e {1} ]; then echo 'already exists' >&2; exit 1; fi; mv {0} {1}", shellQuote(from),
                       shellQuote(to));
}

std::string AdbClient::makeFoldersCommand(std::span<const std::string> remoteDirs)
{
    return quotedList("mkdir -p", remoteDirs);
}

AdbResult<void> AdbClient::removePaths(std::string_view serial, std::span<const std::string> remotePaths) const
{
    if (!std::ranges::all_of(remotePaths, [](const std::string& path) { return removablePath(path); })) {
        return failure(AdbErrorKind::Protocol, "only absolute paths below the root can be removed");
    }
    return runBatched(*this, serial, remotePaths,
                      [](std::span<const std::string> batch) { return removeCommand(batch); });
}

AdbResult<void> AdbClient::renamePath(std::string_view serial, std::string_view from, std::string_view to) const
{
    if (!removablePath(from) || !removablePath(to)) {
        return failure(AdbErrorKind::Protocol, "only absolute paths below the root can be renamed");
    }
    return runChecked(*this, serial, renameCommand(from, to));
}

AdbResult<void> AdbClient::makeFolders(std::string_view serial, std::span<const std::string> remoteDirs) const
{
    if (!std::ranges::all_of(remoteDirs, [](const std::string& path) { return removablePath(path); })) {
        return failure(AdbErrorKind::Protocol, "only absolute paths below the root can be made");
    }
    return runBatched(*this, serial, remoteDirs,
                      [](std::span<const std::string> batch) { return makeFoldersCommand(batch); });
}

}
