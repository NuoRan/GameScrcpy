#include "CompanionLink.h"

#include <gs/adb/ShellProtocol.h>
#include <gs/base/Log.h>
#include <gs/platform/Crypto.h>
#include <gs/platform/Thread.h>
#include <gs/protocol/Codec.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <fstream>
#include <iterator>
#include <type_traits>
#include <utility>
#include <variant>

namespace gs::app {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::string_view kLog = "companion";
constexpr std::string_view kService = "com.gamescrcpy.companion/.CompanionService";
constexpr std::string_view kReadyReceiver = "com.gamescrcpy.companion/.ReadyReceiver";
constexpr std::string_view kSocket = "localabstract:gamescrcpy-companion";
constexpr std::string_view kRemoteApk = "/data/local/tmp/gs-companion.apk";
constexpr std::string_view kCurrentIme = "settings get secure default_input_method";
constexpr std::string_view kEnabledImes = "ime list -s";
constexpr std::string_view kImeSettings = "am start -a android.settings.INPUT_METHOD_SETTINGS";
constexpr int kNotificationsSdk = 33;
constexpr int kReady = 1;

std::string_view trimmed(std::string_view text) noexcept
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

template <typename T>
std::optional<T> numberOf(std::string_view text) noexcept
{
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
        return std::nullopt;
    }
    return value;
}

std::string outputOf(const adb::ShellResult& result)
{
    std::string text(trimmed(result.out));
    const std::string_view errors = trimmed(result.err);
    if (!errors.empty()) {
        text += text.empty() ? "" : " ";
        text += errors;
    }
    return text;
}

CompanionNotice noticeOf(CompanionEvent event, std::string detail = {})
{
    CompanionNotice notice;
    notice.event = event;
    notice.detail = std::move(detail);
    return notice;
}

std::uint64_t steadyMicros() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
}

std::string_view nameOf(const protocol::ToClientMessage& message)
{
    return std::visit([](const auto& m) { return std::decay_t<decltype(m)>::kName; }, message);
}

class PlainChannel final : public CompanionChannel
{
public:
    explicit PlainChannel(platform::TcpStream stream) noexcept
        : m_stream(std::move(stream))
    {
    }

    void setTimeouts(std::chrono::milliseconds read, std::chrono::milliseconds write) override
    {
        m_stream.setReadTimeout(read);
        m_stream.setWriteTimeout(write);
    }

    bool send(std::span<const std::uint8_t> frame) override { return m_stream.writeAll(frame); }

    CompanionRead receive(std::vector<std::uint8_t>& frame) override
    {
        frame.resize(protocol::kFrameHeaderSize);
        if (!m_stream.readExact(frame)) {
            return ended();
        }
        const auto header =
            protocol::decodeFrameHeader(protocol::FrameContext::Companion, protocol::Direction::ToClient, frame);
        if (!header) {
            return CompanionRead::Malformed;
        }
        frame.resize(protocol::kFrameHeaderSize + header->length);
        if (!m_stream.readExact(std::span<std::uint8_t>(frame).subspan(protocol::kFrameHeaderSize))) {
            return ended();
        }
        return CompanionRead::Frame;
    }

    void interrupt() override { m_stream.interrupt(); }

private:
    [[nodiscard]] CompanionRead ended() const noexcept
    {
        return m_stream.readTimedOut() ? CompanionRead::TimedOut : CompanionRead::Closed;
    }

    platform::TcpStream m_stream;
};

Expected<protocol::ToClientMessage, CompanionRead> readMessage(CompanionChannel& channel,
                                                               std::vector<std::uint8_t>& buffer)
{
    const CompanionRead read = channel.receive(buffer);
    if (read != CompanionRead::Frame) {
        return makeUnexpected(read);
    }
    auto message = protocol::decodeToClientFrame(protocol::FrameContext::Companion, buffer);
    if (!message) {
        return makeUnexpected(CompanionRead::Malformed);
    }
    return std::move(*message);
}

template <typename M>
bool writeFrame(CompanionChannel& channel, const M& message)
{
    const auto frame = protocol::encodeFrame(message);
    return frame && channel.send(*frame);
}

CompanionNotice shotNotice(const ShotOutcome& outcome)
{
    CompanionNotice notice = noticeOf(
        outcome.picture != nullptr ? CompanionEvent::Screenshot : CompanionEvent::ScreenshotFailed, outcome.detail);
    notice.shot = outcome.requestId;
    notice.picture = outcome.picture;
    notice.shotFailure = outcome.failure;
    return notice;
}

ShotTiming shotTimingOf(const CompanionTiming& timing) noexcept
{
    return ShotTiming{.first = timing.shotFirst, .between = timing.shotBetween};
}

std::string readEndText(CompanionRead end)
{
    switch (end) {
    case CompanionRead::Closed:
        return "the companion closed the connection";
    case CompanionRead::TimedOut:
        return "the companion did not answer in time";
    case CompanionRead::Frame:
    case CompanionRead::Malformed:
        break;
    }
    return "the companion sent a malformed frame";
}

}

std::unique_ptr<CompanionChannel> plainCompanionChannel(platform::TcpStream stream)
{
    return std::make_unique<PlainChannel>(std::move(stream));
}

std::shared_ptr<const CompanionApk> CompanionApk::load(const std::filesystem::path& apk,
                                                       const std::filesystem::path& metadata)
{
    std::ifstream file(apk, std::ios::binary);
    if (!file) {
        return nullptr;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return nullptr;
    }
    auto loaded = std::make_shared<CompanionApk>();
    loaded->bytes = std::move(bytes);
    if (std::ifstream described(metadata, std::ios::binary); described) {
        const std::string text((std::istreambuf_iterator<char>(described)), std::istreambuf_iterator<char>());
        loaded->versionCode = bundledVersionCode(text);
    }
    return loaded;
}

std::optional<std::int64_t> bundledVersionCode(std::string_view metadata)
{
    const nlohmann::json document = nlohmann::json::parse(metadata, nullptr, false);
    if (!document.is_object()) {
        return std::nullopt;
    }
    if (const auto id = document.find("applicationId");
        id != document.end() && (!id->is_string() || id->get<std::string>() != kCompanionPackage)) {
        return std::nullopt;
    }
    const auto elements = document.find("elements");
    if (elements == document.end() || !elements->is_array() || elements->empty() || !elements->front().is_object()) {
        return std::nullopt;
    }
    const nlohmann::json& first = elements->front();
    const auto code = first.find("versionCode");
    if (code == first.end() || !code->is_number_integer()) {
        return std::nullopt;
    }
    return code->get<std::int64_t>();
}

std::optional<std::int64_t> installedVersionCode(std::string_view packages)
{
    constexpr std::string_view prefix = "package:";
    constexpr std::string_view marker = "versionCode:";
    while (!packages.empty()) {
        const std::size_t end = packages.find('\n');
        const std::string_view line = trimmed(packages.substr(0, end));
        packages = end == std::string_view::npos ? std::string_view() : packages.substr(end + 1);
        if (!line.starts_with(prefix)) {
            continue;
        }
        const std::string_view rest = line.substr(prefix.size());
        const std::size_t space = rest.find(' ');
        if (space == std::string_view::npos || rest.substr(0, space) != kCompanionPackage) {
            continue;
        }
        const std::size_t at = rest.find(marker, space);
        if (at == std::string_view::npos) {
            continue;
        }
        const std::string_view value = rest.substr(at + marker.size());
        if (const auto code = numberOf<std::int64_t>(value.substr(0, value.find(' ')))) {
            return code;
        }
    }
    return std::nullopt;
}

bool overlayAllowed(std::string_view appops) noexcept
{
    return appops.find("SYSTEM_ALERT_WINDOW: allow") != std::string_view::npos;
}

std::optional<int> broadcastResult(std::string_view output) noexcept
{
    constexpr std::string_view marker = "Broadcast completed: result=";
    const std::size_t at = output.find(marker);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view rest = output.substr(at + marker.size());
    return numberOf<int>(rest.substr(0, rest.find_first_not_of("-0123456789")));
}

protocol::CursorState phoneCursorOf(std::optional<input::Vec2> position, bool pressed, bool inFront,
                                    bool locked) noexcept
{
    protocol::CursorState cursor;
    if (!position || !inFront || locked) {
        return cursor;
    }
    cursor.visible = true;
    cursor.x = input::fixedCoordinate(position->x);
    cursor.y = input::fixedCoordinate(position->y);
    cursor.pressed = pressed;
    return cursor;
}

bool imeEnabled(std::string_view output) noexcept
{
    return output.find("now enabled") != std::string_view::npos ||
           output.find("already enabled") != std::string_view::npos;
}

bool imeSelected(std::string_view output) noexcept
{
    return output.find("selected") != std::string_view::npos && output.find("Unknown") == std::string_view::npos &&
           output.find("cannot") == std::string_view::npos && output.find("Error") == std::string_view::npos;
}

std::string previousInputMethod(std::string_view current, std::string_view enabled)
{
    const auto usable = [](std::string_view id) {
        return !id.empty() && id != "null" && id != kCompanionIme && id.find('/') != std::string_view::npos;
    };
    if (const std::string_view now = trimmed(current); usable(now)) {
        return std::string(now);
    }
    while (!enabled.empty()) {
        const std::size_t end = enabled.find('\n');
        const std::string_view line = trimmed(enabled.substr(0, end));
        enabled = end == std::string_view::npos ? std::string_view() : enabled.substr(end + 1);
        if (usable(line)) {
            return std::string(line);
        }
    }
    return {};
}

std::vector<std::string> splitCommitText(std::string_view utf8)
{
    std::vector<std::string> parts;
    while (!utf8.empty()) {
        std::size_t size = std::min(utf8.size(), protocol::kCompanionTextLimit);
        while (size > 0 && size < utf8.size() && (static_cast<unsigned char>(utf8[size]) & 0xC0U) == 0x80U) {
            --size;
        }
        if (size == 0) {
            size = std::min(utf8.size(), protocol::kCompanionTextLimit);
        }
        parts.emplace_back(utf8.substr(0, size));
        utf8.remove_prefix(size);
    }
    return parts;
}

CompanionLink::CompanionLink(adb::AdbClient client, std::string serial, std::shared_ptr<const CompanionApk> apk,
                             Listener listener, CompanionTiming timing)
    : m_client(client), m_serial(std::move(serial)), m_apk(std::move(apk)), m_listener(std::move(listener)),
      m_timing(timing), m_shots(shotTimingOf(timing)), m_worker([this] { run(); })
{
}

CompanionLink::CompanionLink(std::unique_ptr<CompanionChannel> channel, const protocol::CompanionWelcome& welcome,
                             std::string name, Listener listener, CompanionTiming timing)
    : m_lan(true), m_serial(std::move(name)), m_listener(std::move(listener)), m_timing(timing),
      m_given(std::move(channel)), m_welcome(welcome), m_shots(shotTimingOf(timing)), m_worker([this] { run(); })
{
}

CompanionLink::~CompanionLink()
{
    {
        const std::lock_guard lock(m_mutex);
        m_stopping = true;
        m_hurry = true;
        if (m_process != nullptr) {
            m_process->interrupt();
        }
        if (m_handshaking != nullptr) {
            m_handshaking->interrupt();
        }
    }
    m_wake.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void CompanionLink::allowInstall()
{
    {
        const std::lock_guard lock(m_mutex);
        m_installAllowed = true;
    }
    m_wake.notify_all();
}

void CompanionLink::setCursor(const protocol::CursorState& cursor)
{
    {
        const std::lock_guard lock(m_mutex);
        if (m_cursor == cursor) {
            return;
        }
        m_cursor = cursor;
    }
    m_wake.notify_all();
}

void CompanionLink::setIme(bool on)
{
    {
        const std::lock_guard lock(m_mutex);
        if (m_imeWanted == on) {
            return;
        }
        m_imeWanted = on;
    }
    m_wake.notify_all();
}

void CompanionLink::retryIme()
{
    {
        const std::lock_guard lock(m_mutex);
        m_imeRetry = true;
    }
    m_wake.notify_all();
}

void CompanionLink::commitText(std::string_view utf8)
{
    std::vector<std::vector<std::uint8_t>> frames;
    for (std::string& part : splitCommitText(utf8)) {
        protocol::CommitText message;
        message.text = std::move(part);
        if (auto frame = protocol::encodeFrame(message)) {
            frames.push_back(std::move(*frame));
        }
    }
    {
        const std::lock_guard lock(m_mutex);
        if (!m_connected || m_stopping) {
            return;
        }
        for (std::vector<std::uint8_t>& frame : frames) {
            m_outbox.push_back(std::move(frame));
        }
    }
    m_wake.notify_all();
}

void CompanionLink::editorAction(protocol::ImeAction action)
{
    protocol::EditorAction message;
    message.action = action;
    auto frame = protocol::encodeFrame(message);
    if (!frame) {
        return;
    }
    {
        const std::lock_guard lock(m_mutex);
        if (!m_connected || m_stopping) {
            return;
        }
        m_outbox.push_back(std::move(*frame));
    }
    m_wake.notify_all();
}

void CompanionLink::setKeyHints(const protocol::KeyHintLayout& layout)
{
    {
        const std::lock_guard lock(m_mutex);
        if (m_hints == layout) {
            return;
        }
        m_hints = layout;
    }
    m_wake.notify_all();
}

void CompanionLink::setKeyHintState(const protocol::KeyHintState& state)
{
    {
        const std::lock_guard lock(m_mutex);
        if (m_hintState == state) {
            return;
        }
        m_hintState = state;
    }
    m_wake.notify_all();
}

void CompanionLink::requestScreenshot(std::uint32_t requestId, std::uint16_t maxSide, std::uint8_t quality)
{
    protocol::ScreenshotRequest message;
    message.requestId = requestId;
    message.maxSide = maxSide;
    message.quality = std::clamp<std::uint8_t>(quality, 1, 100);
    auto frame = protocol::encodeFrame(message);
    std::optional<ShotOutcome> ended;
    {
        const std::lock_guard lock(m_mutex);
        if (!frame || !m_connected || m_stopping) {
            ended = ShotOutcome{.requestId = requestId,
                                .failure = ShotFailure::Lost,
                                .detail = "the companion is not connected"};
        } else {
            ended = m_shots.begin(requestId, Clock::now());
            m_outbox.push_back(std::move(*frame));
        }
    }
    m_wake.notify_all();
    if (ended) {
        notify(shotNotice(*ended));
    }
}

void CompanionLink::stop()
{
    {
        const std::lock_guard lock(m_mutex);
        m_stopping = true;
        if (m_process != nullptr) {
            m_process->interrupt();
        }
    }
    m_wake.notify_all();
}

bool CompanionLink::stopping()
{
    const std::lock_guard lock(m_mutex);
    return m_stopping;
}

void CompanionLink::notify(const CompanionNotice& notice)
{
    {
        const std::lock_guard lock(m_mutex);
        if (m_hurry) {
            return;
        }
    }
    if (m_listener) {
        m_listener(notice);
    }
}

void CompanionLink::run()
{
    platform::setCurrentThreadName("gs-companion");
    auto connected = m_lan ? admitted(std::move(m_given), m_welcome) : establish();
    const CompanionNotice end = connected ? converse(std::move(*connected)) : std::move(connected.error());
    finish(end);
    if (end.event == CompanionEvent::Ended) {
        log::info(kLog, "[{}] the companion is off", m_serial);
    } else {
        log::warning(kLog, "[{}] the companion ended: {}", m_serial,
                     end.detail.empty() ? std::string_view("stopped on the phone") : std::string_view(end.detail));
    }
    notify(end);
}

Expected<std::unique_ptr<CompanionChannel>, CompanionNotice> CompanionLink::establish()
{
    const auto sdk = m_client.shell(m_serial, "getprop ro.build.version.sdk");
    if (!sdk) {
        return makeUnexpected(
            noticeOf(CompanionEvent::Failed, std::format("the phone did not answer: {}", sdk.error().message)));
    }
    const int level = numberOf<int>(trimmed(sdk->out)).value_or(0);
    const auto listed =
        m_client.shell(m_serial, std::format("pm list packages --show-versioncode {}", kCompanionPackage));
    if (!listed) {
        return makeUnexpected(noticeOf(
            CompanionEvent::Failed, std::format("the installed apps could not be listed: {}", listed.error().message)));
    }
    const std::optional<std::int64_t> installed = installedVersionCode(listed->out);
    const std::optional<std::int64_t> bundled = m_apk != nullptr ? m_apk->versionCode : std::nullopt;
    if (!installed || (bundled && *installed < *bundled)) {
        if (m_apk == nullptr) {
            return makeUnexpected(
                noticeOf(CompanionEvent::InstallFailed, "gs-companion.apk is missing next to GameScrcpy"));
        }
        if (!installed) {
            notify(noticeOf(CompanionEvent::InstallNeeded));
            if (!awaitInstall()) {
                return makeUnexpected(noticeOf(CompanionEvent::Ended));
            }
        }
        log::info(kLog, "[{}] installing the companion app version {} over {}", m_serial, bundled.value_or(0),
                  installed ? std::to_string(*installed) : std::string("nothing"));
        notify(noticeOf(CompanionEvent::Installing));
        if (std::optional<CompanionNotice> failed = install()) {
            return makeUnexpected(std::move(*failed));
        }
    }
    if (stopping()) {
        return makeUnexpected(noticeOf(CompanionEvent::Ended));
    }
    grantOverlay();
    if (level >= kNotificationsSdk) {
        static_cast<void>(m_client.shell(
            m_serial, std::format("pm grant {} android.permission.POST_NOTIFICATIONS", kCompanionPackage)));
    }
    if (stopping()) {
        return makeUnexpected(noticeOf(CompanionEvent::Ended));
    }
    return launch();
}

bool CompanionLink::awaitInstall()
{
    std::unique_lock lock(m_mutex);
    m_wake.wait(lock, [this] { return m_installAllowed || m_stopping; });
    return !m_stopping;
}

std::optional<CompanionNotice> CompanionLink::install()
{
    if (const auto pushed = m_client.push(m_serial, m_apk->bytes, kRemoteApk); !pushed) {
        return noticeOf(CompanionEvent::InstallFailed,
                        std::format("the app could not be copied to the phone: {}", pushed.error().message));
    }
    const auto result = runLong(std::format("pm install -r {}", kRemoteApk));
    static_cast<void>(m_client.shell(m_serial, std::format("rm -f {}", kRemoteApk)));
    if (!result) {
        return stopping() ? noticeOf(CompanionEvent::Ended)
                          : noticeOf(CompanionEvent::InstallFailed, result.error().message);
    }
    const std::string output = outputOf(*result);
    if (output.find("Success") == std::string::npos) {
        return noticeOf(CompanionEvent::InstallFailed, output);
    }
    log::info(kLog, "[{}] the companion app is installed", m_serial);
    return std::nullopt;
}

adb::AdbResult<adb::ShellResult> CompanionLink::runLong(std::string_view command)
{
    auto process = m_client.spawn(m_serial, command);
    if (!process) {
        return makeUnexpected(std::move(process.error()));
    }
    {
        const std::lock_guard lock(m_mutex);
        if (m_stopping) {
            return makeUnexpected(adb::AdbError{adb::AdbErrorKind::Io, "stopped"});
        }
        m_process = &*process;
    }
    adb::ShellResult result;
    while (const std::optional<adb::ShellPacket> packet = process->read()) {
        const std::string_view data(reinterpret_cast<const char*>(packet->data.data()), packet->data.size());
        if (packet->id == static_cast<std::uint8_t>(adb::ShellPacketId::Stdout)) {
            result.out.append(data);
        } else if (packet->id == static_cast<std::uint8_t>(adb::ShellPacketId::Stderr)) {
            result.err.append(data);
        } else if (packet->id == static_cast<std::uint8_t>(adb::ShellPacketId::Exit)) {
            result.exitCode = packet->data.empty() ? 0 : packet->data.front();
            break;
        }
    }
    {
        const std::lock_guard lock(m_mutex);
        m_process = nullptr;
    }
    if (result.exitCode < 0) {
        return makeUnexpected(adb::AdbError{adb::AdbErrorKind::Io, "the command ended without an exit status"});
    }
    return result;
}

void CompanionLink::grantOverlay()
{
    static_cast<void>(
        m_client.shell(m_serial, std::format("appops set {} SYSTEM_ALERT_WINDOW allow", kCompanionPackage)));
    const auto checked = m_client.shell(m_serial, std::format("appops get {} SYSTEM_ALERT_WINDOW", kCompanionPackage));
    if (checked && overlayAllowed(checked->out)) {
        return;
    }
    log::info(kLog, "[{}] the overlay permission could not be granted over adb", m_serial);
    openPermissionPage();
}

void CompanionLink::openPermissionPage()
{
    if (std::exchange(m_permissionPageOpened, true)) {
        return;
    }
    if (!m_lan) {
        static_cast<void>(m_client.shell(
            m_serial, std::format("am start -a android.settings.action.MANAGE_OVERLAY_PERMISSION -d package:{}",
                                  kCompanionPackage)));
    }
    notify(noticeOf(CompanionEvent::PermissionNeeded));
}

Expected<std::unique_ptr<CompanionChannel>, CompanionNotice> CompanionLink::launch()
{
    std::array<std::uint8_t, protocol::kCompanionTokenSize> token{};
    platform::randomBytes(token);
    const auto started = m_client.shell(
        m_serial, std::format("am start-foreground-service -n {} --es token {}", kService, platform::toHex(token)));
    if (!started) {
        return makeUnexpected(noticeOf(
            CompanionEvent::Failed, std::format("the companion could not be started: {}", started.error().message)));
    }
    m_serviceStarted = true;
    if (const std::string output = outputOf(*started);
        started->exitCode != 0 || output.find("Error") != std::string::npos) {
        return makeUnexpected(
            noticeOf(CompanionEvent::Failed, std::format("the companion could not be started: {}", output)));
    }
    const auto ready = m_client.shell(m_serial, std::format("am broadcast -n {}", kReadyReceiver));
    if (!ready || broadcastResult(ready->out) != kReady) {
        return makeUnexpected(noticeOf(CompanionEvent::Failed, "the companion did not start listening"));
    }
    const auto port = m_client.forward(m_serial, kSocket);
    if (!port) {
        return makeUnexpected(noticeOf(
            CompanionEvent::Failed, std::format("adb could not forward to the companion: {}", port.error().message)));
    }
    m_port = *port;
    std::optional<platform::TcpStream> stream = platform::TcpStream::connectLoopback(m_port);
    if (!stream) {
        return makeUnexpected(
            noticeOf(CompanionEvent::Failed, std::format("nothing accepts the forwarded port {}", m_port)));
    }
    return handshake(plainCompanionChannel(std::move(*stream)), token);
}

Expected<std::unique_ptr<CompanionChannel>, CompanionNotice> CompanionLink::handshake(
    std::unique_ptr<CompanionChannel> channel, std::span<const std::uint8_t> token)
{
    if (channel == nullptr) {
        return makeUnexpected(noticeOf(CompanionEvent::Failed, "there is no connection to the companion"));
    }
    channel->setTimeouts(m_timing.reply, m_timing.reply);
    {
        const std::lock_guard lock(m_mutex);
        if (m_hurry) {
            return makeUnexpected(noticeOf(CompanionEvent::Ended));
        }
        m_handshaking = channel.get();
    }
    protocol::CompanionHello hello;
    hello.token.assign(token.begin(), token.end());
    std::vector<std::uint8_t> buffer;
    Expected<protocol::ToClientMessage, CompanionRead> reply = makeUnexpected(CompanionRead::Closed);
    if (writeFrame(*channel, hello)) {
        reply = readMessage(*channel, buffer);
    }
    {
        const std::lock_guard lock(m_mutex);
        m_handshaking = nullptr;
    }
    if (!reply) {
        return makeUnexpected(stopping() ? noticeOf(CompanionEvent::Ended)
                                         : noticeOf(CompanionEvent::Failed, readEndText(reply.error())));
    }
    return admitted(std::move(channel), *reply);
}

Expected<std::unique_ptr<CompanionChannel>, CompanionNotice> CompanionLink::admitted(
    std::unique_ptr<CompanionChannel> channel, const protocol::ToClientMessage& reply)
{
    if (channel == nullptr) {
        return makeUnexpected(noticeOf(CompanionEvent::Failed, "there is no connection to the companion"));
    }
    if (const auto* goodbye = std::get_if<protocol::ServerGoodbye>(&reply)) {
        return makeUnexpected(noticeOf(CompanionEvent::Failed,
                                       std::format("the companion refused the connection ({}): {}",
                                                   protocol::enumName(goodbye->reason), goodbye->message)));
    }
    const auto* welcome = std::get_if<protocol::CompanionWelcome>(&reply);
    if (welcome == nullptr) {
        return makeUnexpected(
            noticeOf(CompanionEvent::Failed, std::format("expected CompanionWelcome, got {}", nameOf(reply))));
    }
    if (welcome->version != protocol::kCompanionVersion) {
        return makeUnexpected(noticeOf(CompanionEvent::Failed,
                                       std::format("the companion speaks version {}, this client version {}",
                                                   welcome->version, protocol::kCompanionVersion)));
    }
    const bool granted = welcome->overlay == protocol::OverlayStatus::Granted;
    log::info(kLog, "[{}] the companion is connected{}{}", m_serial, m_lan ? " over the local network" : "",
              granted ? "" : ", without the overlay permission");
    {
        const std::lock_guard lock(m_mutex);
        m_connected = true;
    }
    CompanionNotice connected = noticeOf(CompanionEvent::Connected);
    connected.overlay = granted;
    notify(connected);
    if (!granted) {
        openPermissionPage();
    }
    return std::move(channel);
}

CompanionNotice CompanionLink::converse(std::unique_ptr<CompanionChannel> channel)
{
    channel->setTimeouts(m_timing.silence, m_timing.reply);
    CompanionChannel& open = *channel;
    std::jthread reader([this, &open] { listen(open); });
    const CompanionNotice end = talk(open);
    if (end.event == CompanionEvent::Ended) {
        std::unique_lock lock(m_mutex);
        m_wake.wait_until(lock, Clock::now() + m_timing.drain, [this] { return m_readerEnd.has_value() || m_hurry; });
    }
    open.interrupt();
    reader.join();
    std::optional<ShotOutcome> lost;
    {
        const std::lock_guard lock(m_mutex);
        m_connected = false;
        m_outbox.clear();
        lost = m_shots.abandon("the connection to the companion ended");
    }
    if (lost) {
        notify(shotNotice(*lost));
    }
    return end;
}

CompanionNotice CompanionLink::talk(CompanionChannel& channel)
{
    std::optional<protocol::CursorState> sent;
    std::optional<protocol::KeyHintLayout> sentHints;
    std::optional<protocol::KeyHintState> sentHintState;
    Clock::time_point sentAt{};
    Clock::time_point pingAt = Clock::now() + m_timing.ping;
    std::uint32_t nonce = 0;
    for (;;) {
        std::optional<protocol::CursorState> cursor;
        std::optional<protocol::KeyHintLayout> hints;
        std::optional<protocol::KeyHintState> hintState;
        std::deque<std::vector<std::uint8_t>> outbox;
        std::optional<bool> ime;
        std::optional<ShotOutcome> expired;
        bool ping = false;
        bool permission = false;
        bool leaving = false;
        {
            std::unique_lock lock(m_mutex);
            for (;;) {
                if (m_readerEnd) {
                    return *m_readerEnd;
                }
                if (m_stopping) {
                    leaving = true;
                    break;
                }
                const Clock::time_point now = Clock::now();
                const Clock::time_point cursorAt = sentAt + m_timing.cursorGap;
                const bool fresh = m_cursor.has_value() && m_cursor != sent;
                if (fresh && now >= cursorAt) {
                    cursor = m_cursor;
                }
                if (m_hints && m_hints != sentHints) {
                    hints = m_hints;
                }
                if (m_hintState && m_hintState != sentHintState) {
                    hintState = m_hintState;
                }
                outbox.swap(m_outbox);
                if (!m_lan) {
                    if (m_imeWanted && (!m_imeApplied || m_imeRetry)) {
                        ime = true;
                    } else if (!m_imeWanted && m_imeApplied) {
                        ime = false;
                    }
                    m_imeRetry = false;
                }
                ping = now >= pingAt;
                permission = std::exchange(m_permissionWanted, false);
                expired = m_shots.expire(now);
                if (cursor || hints || hintState || !outbox.empty() || ime || ping || permission || expired) {
                    break;
                }
                Clock::time_point due = fresh ? std::min(pingAt, cursorAt) : pingAt;
                if (const std::optional<Clock::time_point> shotDue = m_shots.deadline()) {
                    due = std::min(due, *shotDue);
                }
                m_wake.wait_until(lock, due);
            }
        }
        if (expired) {
            notify(shotNotice(*expired));
        }
        if (leaving) {
            static_cast<void>(writeFrame(channel, protocol::Goodbye{}));
            return noticeOf(CompanionEvent::Ended);
        }
        if (permission) {
            openPermissionPage();
        }
        if (ime) {
            m_imeApplied = *ime;
            if (*ime) {
                switchIme();
            } else {
                restoreIme();
            }
        }
        for (const std::vector<std::uint8_t>& frame : outbox) {
            if (!channel.send(frame)) {
                return noticeOf(CompanionEvent::Lost, "the connection to the companion broke");
            }
        }
        if (hints) {
            if (!writeFrame(channel, *hints)) {
                return noticeOf(CompanionEvent::Lost, "the connection to the companion broke");
            }
            sentHints = std::move(hints);
        }
        if (hintState) {
            if (!writeFrame(channel, *hintState)) {
                return noticeOf(CompanionEvent::Lost, "the connection to the companion broke");
            }
            sentHintState = hintState;
        }
        if (cursor) {
            if (!writeFrame(channel, *cursor)) {
                return noticeOf(CompanionEvent::Lost, "the connection to the companion broke");
            }
            sent = cursor;
            sentAt = Clock::now();
        }
        if (ping) {
            protocol::Ping message;
            message.nonce = ++nonce;
            message.clientTimeUs = steadyMicros();
            if (!writeFrame(channel, message)) {
                return noticeOf(CompanionEvent::Lost, "the connection to the companion broke");
            }
            pingAt = Clock::now() + m_timing.ping;
        }
    }
}

void CompanionLink::listen(CompanionChannel& channel)
{
    platform::setCurrentThreadName("gs-companion-in");
    std::vector<std::uint8_t> buffer;
    CompanionNotice end;
    for (;;) {
        auto message = readMessage(channel, buffer);
        if (!message) {
            end = noticeOf(message.error() == CompanionRead::Malformed ? CompanionEvent::Failed : CompanionEvent::Lost,
                           readEndText(message.error()));
            break;
        }
        if (std::holds_alternative<protocol::Pong>(*message)) {
            continue;
        }
        if (const auto* changed = std::get_if<protocol::OverlayChanged>(&*message)) {
            CompanionNotice overlay = noticeOf(CompanionEvent::OverlayChanged);
            overlay.overlay = changed->overlay == protocol::OverlayStatus::Granted;
            notify(overlay);
            if (!overlay.overlay) {
                {
                    const std::lock_guard lock(m_mutex);
                    m_permissionWanted = true;
                }
                m_wake.notify_all();
            }
            continue;
        }
        if (const auto* box = std::get_if<protocol::InputBoxChanged>(&*message)) {
            CompanionNotice changed = noticeOf(CompanionEvent::InputBox);
            changed.box = *box;
            notify(changed);
            continue;
        }
        if (const auto* chunk = std::get_if<protocol::ScreenshotChunk>(&*message)) {
            std::optional<ShotOutcome> outcome;
            {
                const std::lock_guard lock(m_mutex);
                outcome = m_shots.add(*chunk, Clock::now());
            }
            m_wake.notify_all();
            if (outcome) {
                notify(shotNotice(*outcome));
            }
            continue;
        }
        if (const auto* refusal = std::get_if<protocol::ScreenshotFailed>(&*message)) {
            std::optional<ShotOutcome> outcome;
            {
                const std::lock_guard lock(m_mutex);
                outcome = m_shots.fail(*refusal);
            }
            m_wake.notify_all();
            if (outcome) {
                notify(shotNotice(*outcome));
            }
            continue;
        }
        if (const auto* goodbye = std::get_if<protocol::ServerGoodbye>(&*message)) {
            end = goodbye->reason == protocol::ErrorCode::None
                      ? noticeOf(CompanionEvent::StoppedOnPhone)
                      : noticeOf(CompanionEvent::Failed,
                                 std::format("the companion ended the connection ({}): {}",
                                             protocol::enumName(goodbye->reason), goodbye->message));
            break;
        }
        end = noticeOf(CompanionEvent::Failed, std::format("unexpected {} from the companion", nameOf(*message)));
        break;
    }
    {
        const std::lock_guard lock(m_mutex);
        m_readerEnd = std::move(end);
    }
    m_wake.notify_all();
}

void CompanionLink::switchIme()
{
    const auto current = m_client.shell(m_serial, kCurrentIme);
    if (!current) {
        log::warning(kLog, "[{}] the phone's input method could not be read: {}", m_serial, current.error().message);
        return;
    }
    std::string previous = previousInputMethod(current->out, {});
    if (previous.empty()) {
        if (const auto listed = m_client.shell(m_serial, kEnabledImes)) {
            previous = previousInputMethod(current->out, listed->out);
        }
    }
    const auto enabled = m_client.shell(m_serial, std::format("ime enable {}", kCompanionIme));
    if (!enabled || !imeEnabled(outputOf(*enabled))) {
        log::info(kLog, "[{}] the phone refused to turn on the companion input method: {}", m_serial,
                  enabled ? outputOf(*enabled) : enabled.error().message);
        openImeSettings();
        return;
    }
    const auto selected = m_client.shell(m_serial, std::format("ime set {}", kCompanionIme));
    if (!selected || !imeSelected(outputOf(*selected))) {
        log::info(kLog, "[{}] the phone refused to switch to the companion input method: {}", m_serial,
                  selected ? outputOf(*selected) : selected.error().message);
        openImeSettings();
        return;
    }
    if (!m_imeSwitched) {
        m_previousIme = std::move(previous);
    }
    m_imeSwitched = true;
    log::info(kLog, "[{}] typing goes through the companion input method instead of {}", m_serial,
              m_previousIme.empty() ? std::string_view("an unknown one") : std::string_view(m_previousIme));
    notify(noticeOf(CompanionEvent::ImeSwitched));
}

void CompanionLink::restoreIme()
{
    if (!std::exchange(m_imeSwitched, false)) {
        return;
    }
    const auto current = m_client.shell(m_serial, kCurrentIme);
    if (!current || trimmed(current->out) != kCompanionIme || m_previousIme.empty()) {
        return;
    }
    const auto restored = m_client.shell(m_serial, std::format("ime set {}", m_previousIme));
    log::info(kLog, "[{}] the phone's input method is {} again{}", m_serial, m_previousIme,
              restored && imeSelected(outputOf(*restored)) ? "" : ", or it could not be switched back");
}

void CompanionLink::openImeSettings()
{
    if (!std::exchange(m_imePageOpened, true)) {
        static_cast<void>(m_client.shell(m_serial, kImeSettings));
    }
    notify(noticeOf(CompanionEvent::ImeRefused));
}

void CompanionLink::finish(const CompanionNotice& end)
{
    if (m_lan) {
        return;
    }
    m_imeApplied = false;
    restoreIme();
    if (m_serviceStarted && end.event != CompanionEvent::StoppedOnPhone) {
        static_cast<void>(m_client.shell(m_serial, std::format("am stop-service -n {}", kService)));
    }
    if (m_port != 0) {
        static_cast<void>(m_client.removeForward(m_serial, m_port));
        m_port = 0;
    }
}

}
