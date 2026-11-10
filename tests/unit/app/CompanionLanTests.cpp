#include "CompanionCipher.h"
#include "CompanionLan.h"
#include "CompanionLink.h"
#include "Settings.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <gs/platform/Crypto.h>
#include <gs/protocol/Codec.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace std::chrono_literals;
using namespace gs::app;
namespace protocol = gs::protocol;
using nlohmann::json;

namespace {

std::vector<std::uint8_t> fromHex(std::string_view hex)
{
    const auto nibble = [](char c) {
        return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    };
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>((nibble(hex[i]) << 4U) | nibble(hex[i + 1])));
    }
    return out;
}

template <std::size_t N>
std::array<std::uint8_t, N> fixed(std::string_view hex)
{
    const std::vector<std::uint8_t> bytes = fromHex(hex);
    std::array<std::uint8_t, N> out{};
    std::copy_n(bytes.begin(), std::min(bytes.size(), N), out.begin());
    return out;
}

std::string hexOf(std::span<const std::uint8_t> bytes)
{
    return gs::platform::toHex(bytes);
}

json lanVectors()
{
    std::ifstream file(std::filesystem::path(GS_PROTOCOL_VECTOR_DIR) / "companion" / "lan.json", std::ios::binary);
    REQUIRE(file.good());
    const json document = json::parse(file);
    REQUIRE(document.at("format") == "gamescrcpy.companion-lan-vectors");
    return document.at("vectors").at(0);
}

LanDirection directionOf(const json& record)
{
    return record.at("direction") == "toCompanion" ? LanDirection::ToCompanion : LanDirection::ToComputer;
}

std::shared_ptr<const CompanionApk> apk()
{
    auto loaded = std::make_shared<CompanionApk>();
    loaded->bytes.resize(50'000);
    for (std::size_t i = 0; i < loaded->bytes.size(); ++i) {
        loaded->bytes[i] = static_cast<std::uint8_t>(i * 13 + 5);
    }
    return loaded;
}

class Notices
{
public:
    void add(const CompanionNotice& notice)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_seen.push_back(notice);
        }
        m_changed.notify_all();
    }

    std::optional<CompanionNotice> waitFor(CompanionEvent event, std::chrono::milliseconds timeout = 10s)
    {
        std::unique_lock lock(m_mutex);
        std::optional<CompanionNotice> found;
        m_changed.wait_for(lock, timeout, [&] {
            const auto seen = std::ranges::find(m_seen, event, &CompanionNotice::event);
            if (seen == m_seen.end()) {
                return false;
            }
            found = *seen;
            return true;
        });
        return found;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<CompanionNotice> m_seen;
};

class Arrivals
{
public:
    void add(LanArrival arrival)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_arrivals.push_back(std::move(arrival));
        }
        m_changed.notify_all();
    }

    std::optional<LanArrival> next(std::chrono::milliseconds timeout = 10s)
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait_for(lock, timeout, [this] { return !m_arrivals.empty(); })) {
            return std::nullopt;
        }
        LanArrival arrival = std::move(m_arrivals.front());
        m_arrivals.erase(m_arrivals.begin());
        return arrival;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<LanArrival> m_arrivals;
};

class LanPhone
{
public:
    explicit LanPhone(std::uint16_t port)
    {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
        m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        const DWORD timeout = 5000;
        setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        m_connected = connect(m_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    }

    ~LanPhone()
    {
        closesocket(m_socket);
        WSACleanup();
    }

    LanPhone(const LanPhone&) = delete;
    LanPhone& operator=(const LanPhone&) = delete;

    [[nodiscard]] bool connected() const { return m_connected; }

    bool send(std::span<const std::uint8_t> bytes)
    {
        return ::send(m_socket, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0) ==
               static_cast<int>(bytes.size());
    }

    bool send(std::string_view text)
    {
        return send(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }

    std::vector<std::uint8_t> read(std::size_t size)
    {
        std::vector<std::uint8_t> data(size);
        std::size_t done = 0;
        while (done < size) {
            const int n = recv(m_socket, reinterpret_cast<char*>(data.data() + done), static_cast<int>(size - done), 0);
            if (n <= 0) {
                break;
            }
            done += static_cast<std::size_t>(n);
        }
        data.resize(done);
        return data;
    }

    std::string readAll()
    {
        std::string text;
        char buffer[4096];
        for (;;) {
            const int n = recv(m_socket, buffer, static_cast<int>(sizeof(buffer)), 0);
            if (n <= 0) {
                return text;
            }
            text.append(buffer, static_cast<std::size_t>(n));
        }
    }

    std::optional<LanReply> greet(const LanHello& hello, const LanKey& key)
    {
        const std::vector<std::uint8_t> sent = encodeLanHello(hello);
        if (!send(sent)) {
            return std::nullopt;
        }
        const std::vector<std::uint8_t> answer = read(kLanReplySize);
        const auto reply = decodeLanReply(answer);
        if (!reply) {
            return std::nullopt;
        }
        if (reply->status == LanStatus::Accepted) {
            const LanKey session = lanSessionKey(key, sent, answer);
            m_sealer.emplace(session, LanDirection::ToComputer);
            m_opener.emplace(session, LanDirection::ToCompanion);
        }
        return *reply;
    }

    template <typename M>
    bool sendMessage(const M& message)
    {
        const auto frame = protocol::encodeFrame(message);
        return frame && m_sealer && send(m_sealer->seal(*frame));
    }

    std::optional<protocol::ToServerMessage> readMessage()
    {
        if (!m_opener) {
            return std::nullopt;
        }
        const std::vector<std::uint8_t> head = read(4);
        if (head.size() != 4) {
            return std::nullopt;
        }
        LanPrefix prefix{};
        std::ranges::copy(head, prefix.begin());
        const auto length = LanOpener::recordLength(prefix);
        if (!length) {
            return std::nullopt;
        }
        const std::vector<std::uint8_t> body = read(*length);
        auto frame = m_opener->open(prefix, body);
        if (!frame) {
            return std::nullopt;
        }
        auto message = protocol::decodeToServerFrame(protocol::FrameContext::Companion, *frame);
        if (!message) {
            return std::nullopt;
        }
        return std::move(*message);
    }

    template <typename M>
    std::optional<M> readAs()
    {
        for (;;) {
            auto message = readMessage();
            if (!message) {
                return std::nullopt;
            }
            if (std::holds_alternative<protocol::Ping>(*message)) {
                continue;
            }
            if (auto* typed = std::get_if<M>(&*message)) {
                return std::move(*typed);
            }
            return std::nullopt;
        }
    }

    void close() { shutdown(m_socket, SD_BOTH); }

private:
    SOCKET m_socket = INVALID_SOCKET;
    bool m_connected = false;
    std::optional<LanSealer> m_sealer;
    std::optional<LanOpener> m_opener;
};

LanHello helloFrom(std::string token, const LanId& phone)
{
    LanHello hello{.token = std::move(token), .phone = phone};
    gs::platform::randomBytes(hello.nonce);
    return hello;
}

}

TEST_CASE("companion LAN: the handshakes, session keys and sealed records match the shared vectors (SPEC 11.7)")
{
    const json vectors = lanVectors();
    const auto key = fixed<kLanKeySize>(vectors.at("key").get<std::string>());
    const auto phone = fixed<kLanIdSize>(vectors.at("phone").get<std::string>());
    const auto pc = fixed<kLanIdSize>(vectors.at("pc").get<std::string>());
    CHECK(vectors.at("context") == std::string(kLanContext));
    std::size_t checkedRecords = 0;
    for (const json& handshake : vectors.at("handshakes")) {
        INFO(handshake.at("name").get<std::string>());
        const std::vector<std::uint8_t> hello = fromHex(handshake.at("hello").get<std::string>());
        const std::vector<std::uint8_t> reply = fromHex(handshake.at("reply").get<std::string>());
        const auto decodedHello = decodeLanHello(hello);
        REQUIRE(decodedHello.has_value());
        CHECK(decodedHello->token == handshake.at("token").get<std::string>());
        CHECK(decodedHello->phone == phone);
        CHECK(encodeLanHello(*decodedHello) == hello);
        CHECK(lanHelloSize(hello).value() == hello.size());
        const auto decodedReply = decodeLanReply(reply);
        REQUIRE(decodedReply.has_value());
        CHECK(encodeLanReply(*decodedReply) == reply);
        if (handshake.at("status") != "accepted") {
            CHECK(decodedReply->status == LanStatus::Unknown);
            CHECK(decodedReply->pc == LanId{});
            continue;
        }
        CHECK(decodedReply->status == LanStatus::Accepted);
        CHECK(decodedReply->pc == pc);
        CHECK(hexOf(decodedHello->nonce) == handshake.at("phoneNonce").get<std::string>());
        CHECK(hexOf(decodedReply->nonce) == handshake.at("pcNonce").get<std::string>());
        const LanKey session = lanSessionKey(key, hello, reply);
        CHECK(hexOf(session) == handshake.at("sessionKey").get<std::string>());

        std::map<LanDirection, LanSealer> sealers;
        std::map<LanDirection, LanOpener> openers;
        for (const LanDirection direction : {LanDirection::ToCompanion, LanDirection::ToComputer}) {
            sealers.emplace(direction, LanSealer(session, direction));
            openers.emplace(direction, LanOpener(session, direction));
        }
        for (const json& record : handshake.at("records")) {
            INFO(record.at("message").get<std::string>());
            const LanDirection direction = directionOf(record);
            const std::vector<std::uint8_t> frame = fromHex(record.at("frame").get<std::string>());
            const std::vector<std::uint8_t> sealed = fromHex(record.at("hex").get<std::string>());
            CHECK(hexOf(sealers.at(direction).seal(frame)) == record.at("hex").get<std::string>());
            LanPrefix prefix{};
            std::copy_n(sealed.begin(), 4, prefix.begin());
            CHECK(LanOpener::recordLength(prefix).value() == sealed.size() - 4);
            const auto opened = openers.at(direction).open(prefix, std::span(sealed).subspan(4));
            REQUIRE(opened.has_value());
            CHECK(*opened == frame);
            const bool decodes = direction == LanDirection::ToCompanion
                                     ? protocol::decodeToServerFrame(protocol::FrameContext::Companion, frame).has_value()
                                     : protocol::decodeToClientFrame(protocol::FrameContext::Companion, frame).has_value();
            CHECK(decodes);
            ++checkedRecords;
        }
    }
    CHECK(checkedRecords >= 10);
}

TEST_CASE("companion LAN: malformed hellos, replies and records fail with the error the vectors give")
{
    const json vectors = lanVectors();
    for (const json& entry : vectors.at("malformedHellos")) {
        INFO(entry.at("name").get<std::string>());
        const auto decoded = decodeLanHello(fromHex(entry.at("hex").get<std::string>()));
        REQUIRE_FALSE(decoded.has_value());
        CHECK(lanErrorName(decoded.error()) == entry.at("error").get<std::string>());
    }
    for (const json& entry : vectors.at("malformedReplies")) {
        INFO(entry.at("name").get<std::string>());
        const auto decoded = decodeLanReply(fromHex(entry.at("hex").get<std::string>()));
        REQUIRE_FALSE(decoded.has_value());
        CHECK(lanErrorName(decoded.error()) == entry.at("error").get<std::string>());
    }
    const json& pairing = vectors.at("handshakes").at(0);
    const auto session = fixed<kLanKeySize>(pairing.at("sessionKey").get<std::string>());
    for (const json& entry : vectors.at("malformedRecords")) {
        INFO(entry.at("name").get<std::string>());
        const std::vector<std::uint8_t> bytes = fromHex(entry.at("hex").get<std::string>());
        LanPrefix prefix{};
        std::copy_n(bytes.begin(), 4, prefix.begin());
        LanOpener opener(session, directionOf(entry), entry.at("counter").get<std::uint64_t>());
        const auto opened = opener.open(prefix, std::span(bytes).subspan(4));
        REQUIRE_FALSE(opened.has_value());
        CHECK(lanErrorName(opened.error()) == entry.at("error").get<std::string>());
    }
}

TEST_CASE("companion LAN: AES-256-GCM agrees with the known answers in the shared vectors")
{
    const json vectors = lanVectors();
    for (const json& entry : vectors.at("gcm")) {
        INFO(entry.at("name").get<std::string>());
        const auto key = fixed<kLanKeySize>(entry.at("key").get<std::string>());
        const auto nonce = fixed<gs::platform::kAesGcmNonceSize>(entry.at("nonce").get<std::string>());
        const std::vector<std::uint8_t> sealed = gs::platform::aesGcmSeal(
            key, nonce, fromHex(entry.at("plaintext").get<std::string>()), fromHex(entry.at("aad").get<std::string>()));
        CHECK(hexOf(sealed) == entry.at("ciphertext").get<std::string>() + entry.at("tag").get<std::string>());
    }
}

TEST_CASE("companion LAN: the QR text holds the address, the one-time token and the key, and only the path is sent")
{
    const json vectors = lanVectors();
    for (const json& entry : vectors.at("qr")) {
        const auto key = fixed<kLanKeySize>(entry.at("key").get<std::string>());
        const std::string url = companionQrText(entry.at("host").get<std::string>(), entry.at("port").get<std::uint16_t>(),
                                                entry.at("token").get<std::string>(), key);
        CHECK(url == entry.at("url").get<std::string>());
        const std::string target = requestTargetOf(url);
        CHECK(target == entry.at("target").get<std::string>());
        CHECK(target.find(base64Url(key)) == std::string::npos);
        CHECK(target.find('#') == std::string::npos);
    }
    const std::string token = newLanToken();
    CHECK(token.size() == 22);
    CHECK(token.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") ==
          std::string::npos);
    CHECK(newLanToken() != token);
    CHECK(base64Url(std::vector<std::uint8_t>{0xFB, 0xFF}) == "-_8");
    CHECK(base64Url({}).empty());
    const LanPairing pairing{.phone = newLanId(), .key = newLanKey(), .pc = newLanId()};
    CHECK(pairingOf(pairingText(pairing)) == pairing);
    CHECK_FALSE(pairingOf({.phone = "zz", .key = pairingText(pairing).key, .pc = pairingText(pairing).pc}).has_value());
    CHECK_FALSE(pairingOf({}).has_value());
}

TEST_CASE("companion LAN: the HTTP responder serves the page in the phone's language and the app, and nothing else")
{
    const auto app = apk();
    const std::vector<std::string> tokens{"OtherToken0000000000000", "Tok3n_-abcdefghijklmnop"};
    const auto answer = [&](std::string_view request, const CompanionApk* bundled) {
        const auto parsed = parseHttpRequest(request);
        REQUIRE(parsed.has_value());
        const std::vector<std::uint8_t> bytes = httpResponse(*parsed, tokens, bundled);
        return std::string(bytes.begin(), bytes.end());
    };

    const std::string english =
        answer("GET /c/Tok3n_-abcdefghijklmnop HTTP/1.1\r\nHost: 192.168.1.2\r\nAccept-Language: en-US,en;q=0.9\r\n\r\n",
               app.get());
    CHECK(english.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK(english.find("Content-Type: text/html; charset=utf-8\r\n") != std::string::npos);
    CHECK(english.find("Connection: close\r\n") != std::string::npos);
    CHECK(english.find("href=\"/c/Tok3n_-abcdefghijklmnop/gs-companion.apk\"") != std::string::npos);
    CHECK(english.find("Download GameScrcpy Companion") != std::string::npos);

    const std::string chinese =
        answer("GET /c/Tok3n_-abcdefghijklmnop HTTP/1.1\r\naccept-language: zh-CN,zh;q=0.9,en;q=0.8\r\n\r\n", app.get());
    CHECK(chinese.find("lang=\"zh-CN\"") != std::string::npos);
    CHECK(chinese.find("下载 GameScrcpy 伴侣") != std::string::npos);
    CHECK(answer("GET /c/OtherToken0000000000000/ HTTP/1.1\r\nAccept-Language: fr\r\n\r\n", app.get())
              .find("lang=\"en\"") != std::string::npos);

    const std::string download = answer("GET /c/Tok3n_-abcdefghijklmnop/gs-companion.apk HTTP/1.1\r\n\r\n", app.get());
    CHECK(download.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK(download.find("Content-Type: application/vnd.android.package-archive\r\n") != std::string::npos);
    CHECK(download.find("Content-Length: 50000\r\n") != std::string::npos);
    CHECK(download.ends_with(std::string(app->bytes.begin(), app->bytes.end())));

    const std::string head = answer("HEAD /c/Tok3n_-abcdefghijklmnop/gs-companion.apk HTTP/1.1\r\n\r\n", app.get());
    CHECK(head.find("Content-Length: 50000\r\n") != std::string::npos);
    CHECK(head.ends_with("\r\n\r\n"));

    for (const std::string_view unknown :
         {std::string_view("GET /c/WrongToken000000000000 HTTP/1.1\r\n\r\n"), std::string_view("GET / HTTP/1.1\r\n\r\n"),
          std::string_view("GET /favicon.ico HTTP/1.1\r\n\r\n"),
          std::string_view("GET /c/Tok3n_-abcdefghijklmnop/other.apk HTTP/1.1\r\n\r\n"),
          std::string_view("POST /c/Tok3n_-abcdefghijklmnop HTTP/1.1\r\n\r\n"),
          std::string_view("GET /c/ HTTP/1.1\r\n\r\n")}) {
        CAPTURE(unknown);
        CHECK(answer(unknown, app.get()).starts_with("HTTP/1.1 404 Not Found\r\n"));
    }
    CHECK(answer("GET /c/Tok3n_-abcdefghijklmnop/gs-companion.apk HTTP/1.1\r\n\r\n", nullptr)
              .starts_with("HTTP/1.1 404 Not Found\r\n"));
    CHECK_FALSE(parseHttpRequest("GSCL").has_value());
    CHECK_FALSE(parseHttpRequest("GET /c/x\r\n\r\n").has_value());
    CHECK(routeOf(*parseHttpRequest("GET /c/abc?x=1 HTTP/1.1\r\n\r\n"), "abc") == HttpRoute::Page);
    CHECK(routeOf(*parseHttpRequest("GET /c/abc HTTP/1.1\r\n\r\n"), "") == HttpRoute::NotFound);
    CHECK(mdnsInstanceName(LanId{0xAB}) == "gs-ab000000000000000000000000000000");
}

TEST_CASE("companion LAN: a phone that scans the code pairs and talks to a companion link in encrypted frames")
{
    Arrivals arrivals;
    CompanionLan lan(apk(), [&arrivals](LanArrival arrival) { arrivals.add(std::move(arrival)); }, 0, true);
    REQUIRE(lan.listening());
    REQUIRE(lan.port() != 0);
    const LanKey key = newLanKey();
    const std::string token = newLanToken();
    lan.expect({LanExpectation{.serial = "aoa-R58M", .token = token, .qrKey = key}});

    LanPhone phone(lan.port());
    REQUIRE(phone.connected());
    const LanId phoneId = newLanId();
    const auto reply = phone.greet(helloFrom(token, phoneId), key);
    REQUIRE(reply.has_value());
    CHECK(reply->status == LanStatus::Accepted);
    const auto hello = phone.readAs<protocol::CompanionHello>();
    REQUIRE(hello.has_value());
    CHECK(hello->token.empty());
    CHECK_FALSE(arrivals.next(200ms).has_value());
    protocol::CompanionWelcome welcome;
    welcome.width = 1080;
    welcome.height = 2400;
    REQUIRE(phone.sendMessage(welcome));
    std::optional<LanArrival> arrival = arrivals.next();
    REQUIRE(arrival.has_value());
    CHECK(arrival->serial == "aoa-R58M");
    REQUIRE(arrival->paired.has_value());
    CHECK(arrival->paired->phone == phoneId);
    CHECK(arrival->paired->key == key);
    CHECK(arrival->paired->pc == reply->pc);
    REQUIRE(arrival->channel != nullptr);
    CHECK(arrival->welcome == welcome);

    Notices notices;
    CompanionTiming timing;
    timing.ping = 60s;
    CompanionLink link(std::move(arrival->channel), arrival->welcome, "aoa-R58M",
                       [&notices](const CompanionNotice& notice) { notices.add(notice); }, timing);
    CHECK(link.overLan());
    REQUIRE(notices.waitFor(CompanionEvent::Connected).has_value());

    const protocol::InputBoxChanged box{.shown = true, .inputType = 0x4001, .action = protocol::ImeAction::Send};
    REQUIRE(phone.sendMessage(box));
    const auto heard = notices.waitFor(CompanionEvent::InputBox);
    REQUIRE(heard.has_value());
    REQUIRE(heard->box.has_value());
    CHECK(*heard->box == box);

    link.commitText("你好");
    link.editorAction(protocol::ImeAction::Unspecified);
    const auto text = phone.readAs<protocol::CommitText>();
    REQUIRE(text.has_value());
    CHECK(text->text == "你好");
    const auto action = phone.readAs<protocol::EditorAction>();
    REQUIRE(action.has_value());
    CHECK(action->action == protocol::ImeAction::Unspecified);

    link.setKeyHints(protocol::KeyHintLayout{.hints = {protocol::KeyHint{.shape = protocol::KeyHintShape::Tap,
                                                                         .x = 100, .y = 200, .label = "F"}}});
    link.setKeyHintState(protocol::KeyHintState{.shown = true, .faded = true, .opacity = 60});
    const auto layout = phone.readAs<protocol::KeyHintLayout>();
    REQUIRE(layout.has_value());
    REQUIRE(layout->hints.size() == 1);
    CHECK(layout->hints[0].label == "F");
    const auto state = phone.readAs<protocol::KeyHintState>();
    REQUIRE(state.has_value());
    CHECK(state->opacity == 60);

    link.stop();
    CHECK(phone.readAs<protocol::Goodbye>().has_value());
    phone.close();
    CHECK(notices.waitFor(CompanionEvent::Ended).has_value());
}

TEST_CASE("companion LAN: a paired phone comes back without the code, others are turned away")
{
    Arrivals arrivals;
    CompanionLan lan(apk(), [&arrivals](LanArrival arrival) { arrivals.add(std::move(arrival)); }, 0, true);
    REQUIRE(lan.listening());
    const LanPairing pairing{.phone = newLanId(), .key = newLanKey(), .pc = newLanId()};
    lan.expect({LanExpectation{.serial = "aoa-1", .paired = pairing, .announce = true},
                LanExpectation{.serial = "aoa-2", .token = newLanToken(), .qrKey = newLanKey()}});
    {
        LanPhone phone(lan.port());
        const auto reply = phone.greet(helloFrom("", pairing.phone), pairing.key);
        REQUIRE(reply.has_value());
        CHECK(reply->status == LanStatus::Accepted);
        CHECK(reply->pc == pairing.pc);
        REQUIRE(phone.readAs<protocol::CompanionHello>().has_value());
        REQUIRE(phone.sendMessage(protocol::CompanionWelcome{}));
        const std::optional<LanArrival> arrival = arrivals.next();
        REQUIRE(arrival.has_value());
        CHECK(arrival->serial == "aoa-1");
        CHECK_FALSE(arrival->paired.has_value());
    }
    {
        LanPhone stranger(lan.port());
        const auto reply = stranger.greet(helloFrom("", newLanId()), newLanKey());
        REQUIRE(reply.has_value());
        CHECK(reply->status == LanStatus::Unknown);
        CHECK(reply->pc == LanId{});
        CHECK(stranger.readAll().empty());
    }
    {
        LanPhone guesser(lan.port());
        const auto reply = guesser.greet(helloFrom(newLanToken(), newLanId()), newLanKey());
        REQUIRE(reply.has_value());
        CHECK(reply->status == LanStatus::Unknown);
    }
    {
        LanPhone older(lan.port());
        std::vector<std::uint8_t> hello = encodeLanHello(helloFrom("", pairing.phone));
        hello[4] = 2;
        REQUIRE(older.send(hello));
        const auto reply = decodeLanReply(older.read(kLanReplySize));
        REQUIRE(reply.has_value());
        CHECK(reply->status == LanStatus::VersionMismatch);
    }
    CHECK_FALSE(arrivals.next(300ms).has_value());
}

TEST_CASE("companion LAN: the listener answers a phone's browser with the page and the app over HTTP")
{
    Arrivals arrivals;
    const auto app = apk();
    CompanionLan lan(app, [&arrivals](LanArrival arrival) { arrivals.add(std::move(arrival)); }, 0, true);
    REQUIRE(lan.listening());
    const LanKey key = newLanKey();
    const std::string token = newLanToken();
    lan.expect({LanExpectation{.serial = "aoa-1", .token = token, .qrKey = key}});
    const std::string url = companionQrText("127.0.0.1", lan.port(), token, key);
    const std::string target = requestTargetOf(url);
    {
        LanPhone browser(lan.port());
        const std::string request =
            "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\nAccept-Language: zh-CN\r\nConnection: keep-alive\r\n\r\n";
        CHECK(request.find(base64Url(key)) == std::string::npos);
        REQUIRE(browser.send(request));
        const std::string page = browser.readAll();
        CHECK(page.starts_with("HTTP/1.1 200 OK\r\n"));
        CHECK(page.find(target + "/gs-companion.apk") != std::string::npos);
        CHECK(page.find(base64Url(key)) == std::string::npos);
    }
    {
        LanPhone browser(lan.port());
        REQUIRE(browser.send("GET " + target + "/gs-companion.apk HTTP/1.1\r\n\r\n"));
        const std::string download = browser.readAll();
        CHECK(download.ends_with(std::string(app->bytes.begin(), app->bytes.end())));
    }
    {
        LanPhone browser(lan.port());
        REQUIRE(browser.send(std::string_view("GET /c/nope HTTP/1.1\r\n\r\n")));
        CHECK(browser.readAll().starts_with("HTTP/1.1 404 Not Found\r\n"));
    }
    CHECK_FALSE(arrivals.next(200ms).has_value());
}

TEST_CASE("companion LAN: a phone id without the pairing key never reaches the app, so the companion in use stays")
{
    Arrivals arrivals;
    CompanionLan lan(apk(), [&arrivals](LanArrival arrival) { arrivals.add(std::move(arrival)); }, 0, true);
    REQUIRE(lan.listening());
    const LanPairing pairing{.phone = newLanId(), .key = newLanKey(), .pc = newLanId()};
    lan.expect({LanExpectation{.serial = "aoa-1", .paired = pairing}});

    LanPhone phone(lan.port());
    REQUIRE(phone.greet(helloFrom("", pairing.phone), pairing.key).has_value());
    REQUIRE(phone.readAs<protocol::CompanionHello>().has_value());
    REQUIRE(phone.sendMessage(protocol::CompanionWelcome{}));
    std::optional<LanArrival> arrival = arrivals.next();
    REQUIRE(arrival.has_value());
    Notices notices;
    CompanionTiming timing;
    timing.ping = 60s;
    CompanionLink link(std::move(arrival->channel), arrival->welcome, "aoa-1",
                       [&notices](const CompanionNotice& notice) { notices.add(notice); }, timing);
    REQUIRE(notices.waitFor(CompanionEvent::Connected).has_value());

    {
        LanPhone stranger(lan.port());
        const auto reply = stranger.greet(helloFrom("", pairing.phone), newLanKey());
        REQUIRE(reply.has_value());
        CHECK(reply->status == LanStatus::Accepted);
        CHECK_FALSE(stranger.readAs<protocol::CompanionHello>().has_value());
        CHECK(stranger.sendMessage(protocol::CompanionWelcome{}));
        CHECK_FALSE(arrivals.next(500ms).has_value());
    }
    {
        LanPhone replayer(lan.port());
        REQUIRE(replayer.greet(helloFrom("", pairing.phone), newLanKey()).has_value());
        const LanReply earlier{.status = LanStatus::Accepted, .pc = pairing.pc, .nonce = LanNonce{0x42}};
        const LanKey earlierSession =
            lanSessionKey(pairing.key, encodeLanHello(helloFrom("", pairing.phone)), encodeLanReply(earlier));
        LanSealer stale(earlierSession, LanDirection::ToComputer);
        const auto frame = protocol::encodeFrame(protocol::CompanionWelcome{});
        REQUIRE(frame.has_value());
        CHECK(replayer.send(stale.seal(*frame)));
        CHECK_FALSE(arrivals.next(500ms).has_value());
    }

    CHECK_FALSE(notices.waitFor(CompanionEvent::Lost, 100ms).has_value());
    CHECK_FALSE(notices.waitFor(CompanionEvent::Failed, 100ms).has_value());
    link.setKeyHintState(protocol::KeyHintState{.shown = true, .faded = false, .opacity = 40});
    const auto state = phone.readAs<protocol::KeyHintState>();
    REQUIRE(state.has_value());
    CHECK(state->opacity == 40);

    {
        LanPhone again(lan.port());
        REQUIRE(again.greet(helloFrom("", pairing.phone), pairing.key).has_value());
        REQUIRE(again.readAs<protocol::CompanionHello>().has_value());
        REQUIRE(again.sendMessage(protocol::CompanionWelcome{}));
        const std::optional<LanArrival> proven = arrivals.next();
        REQUIRE(proven.has_value());
        CHECK(proven->serial == "aoa-1");
        CHECK(proven->channel != nullptr);
    }

    link.stop();
    CHECK(phone.readAs<protocol::Goodbye>().has_value());
    phone.close();
    CHECK(notices.waitFor(CompanionEvent::Ended).has_value());
}

TEST_CASE("companion LAN: the pairing key is stored protected by Windows, and a key stored in plain text is "
          "protected when it is read")
{
    const LanKey key = newLanKey();
    const std::string plain = gs::platform::toHex(key);
    const LanPairing pairing{.phone = newLanId(), .key = key, .pc = newLanId()};

    const LanPairingText text = pairingText(pairing);
    CHECK(text.key.starts_with(kLanKeyProtected));
    CHECK(text.key.find(plain) == std::string::npos);
    CHECK(text.key.size() <= settings::DeviceSettings::kMaxCompanionSecret);
    CHECK(pairingOf(text) == pairing);
    CHECK(lanKeyOf(text.key) == key);
    CHECK_FALSE(reprotectedLanKey(text.key).has_value());

    const LanPairingText before{.phone = text.phone, .key = plain, .pc = text.pc};
    CHECK(pairingOf(before) == pairing);
    const std::optional<std::string> migrated = reprotectedLanKey(plain);
    REQUIRE(migrated.has_value());
    CHECK(migrated->starts_with(kLanKeyProtected));
    CHECK(migrated->find(plain) == std::string::npos);
    CHECK(lanKeyOf(*migrated) == key);
    CHECK(pairingOf({.phone = text.phone, .key = *migrated, .pc = text.pc}) == pairing);

    CHECK_FALSE(reprotectedLanKey("").has_value());
    CHECK_FALSE(reprotectedLanKey("zz").has_value());
    CHECK_FALSE(lanKeyOf(kLanKeyProtected).has_value());
    CHECK_FALSE(lanKeyOf(std::string(kLanKeyProtected) + "00ff").has_value());
    std::string tampered = text.key;
    tampered.back() = tampered.back() == '0' ? '1' : '0';
    CHECK_FALSE(lanKeyOf(tampered).has_value());
    CHECK_FALSE(pairingOf({.phone = text.phone, .key = tampered, .pc = text.pc}).has_value());

    settings::Devices devices;
    settings::DeviceSettings device;
    device.companionPhone = text.phone;
    device.companionKey = text.key;
    device.companionPc = text.pc;
    devices.byId["aoa-1"] = device;
    const settings::Loaded<settings::Devices> loaded = settings::load<settings::Devices>(settings::save(devices));
    REQUIRE(loaded.usable);
    CHECK(loaded.document.byId.at("aoa-1").companionKey == text.key);
}
