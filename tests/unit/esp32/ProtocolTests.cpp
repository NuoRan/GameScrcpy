#include "TestSupport.h"

#include <gs_esp32_protocol.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <string>

using esp32test::Bytes;
using esp32test::fromHex;

namespace {

using Json = nlohmann::json;

const Json& vectors()
{
    static const Json document = [] {
        std::ifstream file(GS_ESP32_VECTORS, std::ios::binary);
        REQUIRE(file.good());
        return Json::parse(file);
    }();
    return document;
}

void copyText(char* out, std::size_t capacity, const std::string& text)
{
    REQUIRE(text.size() < capacity);
    std::memcpy(out, text.data(), text.size());
    out[text.size()] = '\0';
}

template <std::size_t N>
void copyBytes(std::uint8_t (&out)[N], const Bytes& bytes)
{
    REQUIRE(bytes.size() == N);
    std::copy(bytes.begin(), bytes.end(), out);
}

template <std::size_t N>
void copyList(std::uint8_t (&out)[N], const Json& list)
{
    REQUIRE(list.size() == N);
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = list[i].get<std::uint8_t>();
    }
}

template <std::size_t N>
Bytes bytesOf(const std::uint8_t (&in)[N])
{
    return Bytes(in, in + N);
}

Bytes listOf(const Json& list)
{
    Bytes bytes;
    for (const Json& item : list) {
        bytes.push_back(item.get<std::uint8_t>());
    }
    return bytes;
}

Bytes encodePayload(const std::string& kind, const Json& fields)
{
    std::array<std::uint8_t, GS_ESP32_MAX_PAYLOAD> buffer{};
    std::size_t size = 0;
    if (kind == "hello_req") {
        size = gs_esp32_encode_hello_req(buffer.data(), buffer.size(), fields.at("version").get<std::uint8_t>());
    } else if (kind == "touch") {
        gs_esp32_touch touch{};
        for (const Json& contact : fields.at("contacts")) {
            gs_esp32_contact& out = touch.contacts[touch.count];
            out.id = contact.at("id").get<std::uint8_t>();
            out.tip = contact.at("tip").get<std::uint8_t>();
            out.x = contact.at("x").get<std::uint16_t>();
            out.y = contact.at("y").get<std::uint16_t>();
            touch.count = static_cast<std::uint8_t>(touch.count + 1);
        }
        size = gs_esp32_encode_touch(buffer.data(), buffer.size(), &touch);
        if (touch.count == 0) {
            CHECK(size == 1);
        }
    } else if (kind == "keyboard") {
        gs_esp32_keyboard keyboard{};
        keyboard.modifiers = fields.at("modifiers").get<std::uint8_t>();
        copyList(keyboard.keys, fields.at("keys"));
        size = gs_esp32_encode_keyboard(buffer.data(), buffer.size(), &keyboard);
    } else if (kind == "consumer") {
        size = gs_esp32_encode_consumer(buffer.data(), buffer.size(), fields.at("usage").get<std::uint16_t>());
    } else if (kind == "mouse") {
        const gs_esp32_mouse mouse{.buttons = fields.at("buttons").get<std::uint8_t>(),
                                   .dx = fields.at("dx").get<std::int16_t>(),
                                   .dy = fields.at("dy").get<std::int16_t>(),
                                   .wheel = fields.at("wheel").get<std::int8_t>()};
        size = gs_esp32_encode_mouse(buffer.data(), buffer.size(), &mouse);
    } else if (kind == "gamepad") {
        const gs_esp32_gamepad gamepad{.buttons = fields.at("buttons").get<std::uint32_t>(),
                                       .hat = fields.at("hat").get<std::uint8_t>(),
                                       .lx = fields.at("lx").get<std::int16_t>(),
                                       .ly = fields.at("ly").get<std::int16_t>(),
                                       .rx = fields.at("rx").get<std::int16_t>(),
                                       .ry = fields.at("ry").get<std::int16_t>(),
                                       .lt = fields.at("lt").get<std::uint8_t>(),
                                       .rt = fields.at("rt").get<std::uint8_t>()};
        size = gs_esp32_encode_gamepad(buffer.data(), buffer.size(), &gamepad);
    } else if (kind == "ping" || kind == "pong") {
        size = gs_esp32_encode_token(buffer.data(), buffer.size(), fields.at("token").get<std::uint32_t>());
    } else if (kind == "set_baud") {
        const gs_esp32_set_baud request{.baud = fields.at("baud").get<std::uint32_t>(),
                                        .token = fields.at("token").get<std::uint32_t>()};
        size = gs_esp32_encode_set_baud(buffer.data(), buffer.size(), &request);
    } else if (kind == "wifi_set") {
        gs_esp32_wifi_set wifi{};
        copyText(wifi.ssid, sizeof(wifi.ssid), fields.at("ssid").get<std::string>());
        copyText(wifi.password, sizeof(wifi.password), fields.at("password").get<std::string>());
        copyBytes(wifi.key, fromHex(fields.at("key").get<std::string>()));
        size = gs_esp32_encode_wifi_set(buffer.data(), buffer.size(), &wifi);
    } else if (kind == "set_name") {
        char name[GS_ESP32_MAX_NAME + 1]{};
        copyText(name, sizeof(name), fields.at("name").get<std::string>());
        size = gs_esp32_encode_name(buffer.data(), buffer.size(), name);
    } else if (kind == "hello") {
        gs_esp32_hello hello{};
        hello.protocol = fields.at("protocol").get<std::uint8_t>();
        copyText(hello.firmware, sizeof(hello.firmware), fields.at("firmware").get<std::string>());
        hello.chip_id = fields.at("chipId").get<std::uint16_t>();
        hello.chip_revision = fields.at("chipRevision").get<std::uint16_t>();
        hello.features = fields.at("features").get<std::uint32_t>();
        copyBytes(hello.mac, fromHex(fields.at("mac").get<std::string>()));
        hello.wifi = fields.at("wifi").get<std::uint8_t>();
        copyList(hello.ipv4, fields.at("ipv4"));
        copyText(hello.name, sizeof(hello.name), fields.at("name").get<std::string>());
        size = gs_esp32_encode_hello(buffer.data(), buffer.size(), &hello);
    } else if (kind == "status") {
        gs_esp32_status status{};
        status.usb = fields.at("usb").get<std::uint8_t>();
        status.ble = fields.at("ble").get<std::uint8_t>();
        copyText(status.peer, sizeof(status.peer), fields.at("peer").get<std::string>());
        status.wifi = fields.at("wifi").get<std::uint8_t>();
        copyList(status.ipv4, fields.at("ipv4"));
        size = gs_esp32_encode_status(buffer.data(), buffer.size(), &status);
    } else if (kind == "log") {
        gs_esp32_log log{};
        log.level = fields.at("level").get<std::uint8_t>();
        copyText(log.text, sizeof(log.text), fields.at("text").get<std::string>());
        size = gs_esp32_encode_log(buffer.data(), buffer.size(), &log);
    } else if (kind == "error") {
        gs_esp32_error error{};
        error.code = fields.at("code").get<std::uint16_t>();
        copyText(error.text, sizeof(error.text), fields.at("text").get<std::string>());
        size = gs_esp32_encode_error(buffer.data(), buffer.size(), &error);
    } else {
        CHECK(fields.empty());
    }
    return Bytes(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size));
}

bool decodes(const std::string& kind, const Bytes& payload)
{
    const std::uint8_t* data = payload.data();
    const std::size_t size = payload.size();
    if (kind == "hello_req") {
        std::uint8_t version = 0;
        return gs_esp32_decode_hello_req(data, size, &version) != 0;
    }
    if (kind == "touch") {
        gs_esp32_touch touch{};
        return gs_esp32_decode_touch(data, size, &touch) != 0;
    }
    if (kind == "keyboard") {
        gs_esp32_keyboard keyboard{};
        return gs_esp32_decode_keyboard(data, size, &keyboard) != 0;
    }
    if (kind == "consumer") {
        std::uint16_t usage = 0;
        return gs_esp32_decode_consumer(data, size, &usage) != 0;
    }
    if (kind == "mouse") {
        gs_esp32_mouse mouse{};
        return gs_esp32_decode_mouse(data, size, &mouse) != 0;
    }
    if (kind == "gamepad") {
        gs_esp32_gamepad gamepad{};
        return gs_esp32_decode_gamepad(data, size, &gamepad) != 0;
    }
    if (kind == "ping" || kind == "pong") {
        std::uint32_t token = 0;
        return gs_esp32_decode_token(data, size, &token) != 0;
    }
    if (kind == "set_baud") {
        gs_esp32_set_baud request{};
        return gs_esp32_decode_set_baud(data, size, &request) != 0;
    }
    if (kind == "wifi_set") {
        gs_esp32_wifi_set wifi{};
        return gs_esp32_decode_wifi_set(data, size, &wifi) != 0;
    }
    if (kind == "set_name") {
        char name[GS_ESP32_MAX_NAME + 1]{};
        return gs_esp32_decode_name(data, size, name) != 0;
    }
    if (kind == "hello") {
        gs_esp32_hello hello{};
        return gs_esp32_decode_hello(data, size, &hello) != 0;
    }
    if (kind == "status") {
        gs_esp32_status status{};
        return gs_esp32_decode_status(data, size, &status) != 0;
    }
    if (kind == "log") {
        gs_esp32_log log{};
        return gs_esp32_decode_log(data, size, &log) != 0;
    }
    if (kind == "error") {
        gs_esp32_error error{};
        return gs_esp32_decode_error(data, size, &error) != 0;
    }
    FAIL("unknown kind " << kind);
    return false;
}

void checkDecoded(const std::string& kind, const Bytes& payload, const Json& fields)
{
    const std::uint8_t* data = payload.data();
    const std::size_t size = payload.size();
    if (kind == "hello_req") {
        std::uint8_t version = 0;
        REQUIRE(gs_esp32_decode_hello_req(data, size, &version));
        CHECK(version == fields.at("version").get<int>());
    } else if (kind == "touch") {
        gs_esp32_touch touch{};
        REQUIRE(gs_esp32_decode_touch(data, size, &touch));
        const Json& contacts = fields.at("contacts");
        REQUIRE(static_cast<std::size_t>(touch.count) == contacts.size());
        for (std::size_t i = 0; i < contacts.size(); ++i) {
            CHECK(touch.contacts[i].id == contacts[i].at("id").get<int>());
            CHECK(touch.contacts[i].tip == contacts[i].at("tip").get<int>());
            CHECK(touch.contacts[i].x == contacts[i].at("x").get<int>());
            CHECK(touch.contacts[i].y == contacts[i].at("y").get<int>());
        }
    } else if (kind == "keyboard") {
        gs_esp32_keyboard keyboard{};
        REQUIRE(gs_esp32_decode_keyboard(data, size, &keyboard));
        CHECK(keyboard.modifiers == fields.at("modifiers").get<int>());
        CHECK(bytesOf(keyboard.keys) == listOf(fields.at("keys")));
    } else if (kind == "consumer") {
        std::uint16_t usage = 0;
        REQUIRE(gs_esp32_decode_consumer(data, size, &usage));
        CHECK(usage == fields.at("usage").get<int>());
    } else if (kind == "mouse") {
        gs_esp32_mouse mouse{};
        REQUIRE(gs_esp32_decode_mouse(data, size, &mouse));
        CHECK(mouse.buttons == fields.at("buttons").get<int>());
        CHECK(mouse.dx == fields.at("dx").get<int>());
        CHECK(mouse.dy == fields.at("dy").get<int>());
        CHECK(mouse.wheel == fields.at("wheel").get<int>());
    } else if (kind == "gamepad") {
        gs_esp32_gamepad gamepad{};
        REQUIRE(gs_esp32_decode_gamepad(data, size, &gamepad));
        CHECK(gamepad.buttons == fields.at("buttons").get<std::uint32_t>());
        CHECK(gamepad.hat == fields.at("hat").get<int>());
        CHECK(gamepad.lx == fields.at("lx").get<int>());
        CHECK(gamepad.ly == fields.at("ly").get<int>());
        CHECK(gamepad.rx == fields.at("rx").get<int>());
        CHECK(gamepad.ry == fields.at("ry").get<int>());
        CHECK(gamepad.lt == fields.at("lt").get<int>());
        CHECK(gamepad.rt == fields.at("rt").get<int>());
    } else if (kind == "ping" || kind == "pong") {
        std::uint32_t token = 0;
        REQUIRE(gs_esp32_decode_token(data, size, &token));
        CHECK(token == fields.at("token").get<std::uint32_t>());
    } else if (kind == "set_baud") {
        gs_esp32_set_baud request{};
        REQUIRE(gs_esp32_decode_set_baud(data, size, &request));
        CHECK(request.baud == fields.at("baud").get<std::uint32_t>());
        CHECK(request.token == fields.at("token").get<std::uint32_t>());
    } else if (kind == "wifi_set") {
        gs_esp32_wifi_set wifi{};
        REQUIRE(gs_esp32_decode_wifi_set(data, size, &wifi));
        CHECK(std::string(wifi.ssid) == fields.at("ssid").get<std::string>());
        CHECK(std::string(wifi.password) == fields.at("password").get<std::string>());
        CHECK(bytesOf(wifi.key) == fromHex(fields.at("key").get<std::string>()));
    } else if (kind == "set_name") {
        char name[GS_ESP32_MAX_NAME + 1]{};
        REQUIRE(gs_esp32_decode_name(data, size, name));
        CHECK(std::string(name) == fields.at("name").get<std::string>());
    } else if (kind == "hello") {
        gs_esp32_hello hello{};
        REQUIRE(gs_esp32_decode_hello(data, size, &hello));
        CHECK(hello.protocol == fields.at("protocol").get<int>());
        CHECK(std::string(hello.firmware) == fields.at("firmware").get<std::string>());
        CHECK(hello.chip_id == fields.at("chipId").get<int>());
        CHECK(hello.chip_revision == fields.at("chipRevision").get<int>());
        CHECK(hello.features == fields.at("features").get<std::uint32_t>());
        CHECK(bytesOf(hello.mac) == fromHex(fields.at("mac").get<std::string>()));
        CHECK(hello.wifi == fields.at("wifi").get<int>());
        CHECK(bytesOf(hello.ipv4) == listOf(fields.at("ipv4")));
        CHECK(std::string(hello.name) == fields.at("name").get<std::string>());
    } else if (kind == "status") {
        gs_esp32_status status{};
        REQUIRE(gs_esp32_decode_status(data, size, &status));
        CHECK(status.usb == fields.at("usb").get<int>());
        CHECK(status.ble == fields.at("ble").get<int>());
        CHECK(std::string(status.peer) == fields.at("peer").get<std::string>());
        CHECK(status.wifi == fields.at("wifi").get<int>());
        CHECK(bytesOf(status.ipv4) == listOf(fields.at("ipv4")));
    } else if (kind == "log") {
        gs_esp32_log log{};
        REQUIRE(gs_esp32_decode_log(data, size, &log));
        CHECK(log.level == fields.at("level").get<int>());
        CHECK(std::string(log.text) == fields.at("text").get<std::string>());
    } else if (kind == "error") {
        gs_esp32_error error{};
        REQUIRE(gs_esp32_decode_error(data, size, &error));
        CHECK(error.code == fields.at("code").get<int>());
        CHECK(std::string(error.text) == fields.at("text").get<std::string>());
    } else {
        CHECK(payload.empty());
    }
}

struct Parsed {
    std::vector<Bytes> frames;
    std::uint32_t bad = 0;
    std::uint32_t dropped = 0;
};

Parsed parse(const Bytes& data, std::size_t chunk)
{
    gs_esp32_parser parser{};
    gs_esp32_parser_reset(&parser);
    Parsed parsed;
    std::size_t at = 0;
    while (at < data.size()) {
        const std::size_t offered = std::min(chunk, data.size() - at);
        at += gs_esp32_parser_feed(&parser, data.data() + at, offered);
        gs_esp32_frame frame{};
        while (gs_esp32_parser_next(&parser, &frame)) {
            const std::uint8_t* start = frame.payload - GS_ESP32_HEADER_SIZE;
            parsed.frames.emplace_back(start, frame.payload + frame.length + GS_ESP32_CRC_SIZE);
        }
    }
    parsed.bad = parser.bad_frames;
    parsed.dropped = parser.dropped;
    return parsed;
}

}

TEST_CASE("the ESP32 vectors are for protocol 2")
{
    CHECK(vectors().at("format") == "gamescrcpy.esp32-vectors");
    CHECK(vectors().at("protocol") == GS_ESP32_PROTOCOL_VERSION);
}

TEST_CASE("CRC-16/CCITT-FALSE matches the known values (HID-004)")
{
    for (const Json& vector : vectors().at("crc")) {
        const Bytes data = fromHex(vector.at("data").get<std::string>());
        CHECK(gs_esp32_crc16(data.data(), data.size()) == vector.at("crc").get<int>());
    }
    const char check[] = "123456789";
    CHECK(gs_esp32_crc16(reinterpret_cast<const std::uint8_t*>(check), 9) == 0x29B1);
}

TEST_CASE("the shared SHA-256 matches the known digests")
{
    for (const Json& vector : vectors().at("sha256")) {
        const Bytes data = fromHex(vector.at("data").get<std::string>());
        std::uint8_t digest[32]{};
        gs_esp32_sha256_digest(data.data(), data.size(), digest);
        CHECK(bytesOf(digest) == fromHex(vector.at("sha256").get<std::string>()));
    }
}

TEST_CASE("every ESP32 message vector encodes and decodes byte for byte (HID-004)")
{
    REQUIRE(vectors().at("messages").size() >= 25);
    for (const Json& vector : vectors().at("messages")) {
        const std::string name = vector.at("name").get<std::string>();
        const std::string kind = vector.at("kind").get<std::string>();
        INFO(name);
        const Bytes payload = fromHex(vector.at("payload").get<std::string>());
        const Bytes expectedFrame = fromHex(vector.at("frame").get<std::string>());
        const auto type = vector.at("type").get<std::uint8_t>();
        const auto seq = vector.at("seq").get<std::uint8_t>();

        CHECK(encodePayload(kind, vector.at("fields")) == payload);

        std::array<std::uint8_t, GS_ESP32_MAX_FRAME> frame{};
        const std::size_t size =
            gs_esp32_frame_encode(frame.data(), frame.size(), type, seq, payload.data(), payload.size());
        CHECK(Bytes(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(size)) == expectedFrame);

        gs_esp32_frame decoded{};
        REQUIRE(gs_esp32_frame_decode(expectedFrame.data(), expectedFrame.size(), &decoded));
        CHECK(decoded.type == type);
        CHECK(decoded.seq == seq);
        CHECK(Bytes(decoded.payload, decoded.payload + decoded.length) == payload);
        checkDecoded(kind, payload, vector.at("fields"));
    }
}

TEST_CASE("a payload may grow at the end: decoders ignore trailing bytes")
{
    for (const Json& vector : vectors().at("messages")) {
        const std::string kind = vector.at("kind").get<std::string>();
        Bytes payload = fromHex(vector.at("payload").get<std::string>());
        if (payload.empty()) {
            continue;
        }
        INFO(vector.at("name").get<std::string>());
        payload.push_back(0x7E);
        payload.push_back(0x00);
        checkDecoded(kind, payload, vector.at("fields"));
    }
}

TEST_CASE("truncated, out-of-range and malformed payloads are refused")
{
    REQUIRE_FALSE(vectors().at("invalidPayloads").empty());
    for (const Json& vector : vectors().at("invalidPayloads")) {
        INFO(vector.at("name").get<std::string>());
        CHECK_FALSE(decodes(vector.at("kind").get<std::string>(), fromHex(vector.at("payload").get<std::string>())));
    }
}

TEST_CASE("encoders refuse values the protocol does not allow")
{
    std::array<std::uint8_t, GS_ESP32_MAX_PAYLOAD> buffer{};
    gs_esp32_touch touch{};
    touch.count = 11;
    CHECK(gs_esp32_encode_touch(buffer.data(), buffer.size(), &touch) == 0);
    touch.count = 1;
    touch.contacts[0] = {.id = 0, .tip = 1, .x = 32768, .y = 0};
    CHECK(gs_esp32_encode_touch(buffer.data(), buffer.size(), &touch) == 0);
    gs_esp32_gamepad gamepad{};
    gamepad.hat = 9;
    CHECK(gs_esp32_encode_gamepad(buffer.data(), buffer.size(), &gamepad) == 0);
    gs_esp32_wifi_set wifi{};
    CHECK(gs_esp32_encode_wifi_set(buffer.data(), buffer.size(), &wifi) == 0);
    CHECK(gs_esp32_encode_name(buffer.data(), buffer.size(), "") == 0);
    const std::string longName(GS_ESP32_MAX_NAME + 1, 'n');
    CHECK(gs_esp32_encode_name(buffer.data(), buffer.size(), longName.c_str()) == 0);
    CHECK(gs_esp32_encode_token(buffer.data(), 3, 1) == 0);
    std::array<std::uint8_t, GS_ESP32_MAX_FRAME + 1> frame{};
    const Bytes tooLong(GS_ESP32_MAX_PAYLOAD + 1, 0);
    CHECK(gs_esp32_frame_encode(frame.data(), frame.size(), GS_ESP32_MSG_LOG, 0, tooLong.data(), tooLong.size()) == 0);
    CHECK(gs_esp32_frame_encode(frame.data(), 9, GS_ESP32_MSG_PING, 0, buffer.data(), 4) == 0);
}

TEST_CASE("truncated and corrupted frames do not decode")
{
    REQUIRE_FALSE(vectors().at("invalidFrames").empty());
    for (const Json& vector : vectors().at("invalidFrames")) {
        INFO(vector.at("name").get<std::string>());
        const Bytes bytes = fromHex(vector.at("bytes").get<std::string>());
        gs_esp32_frame frame{};
        CHECK_FALSE(gs_esp32_frame_decode(bytes.data(), bytes.size(), &frame));
    }
}

TEST_CASE("the byte-stream parser resynchronises on the magic however the bytes arrive (HID-004)")
{
    REQUIRE_FALSE(vectors().at("streams").empty());
    for (const Json& vector : vectors().at("streams")) {
        INFO(vector.at("name").get<std::string>());
        const Bytes data = fromHex(vector.at("bytes").get<std::string>());
        std::vector<Bytes> expected;
        for (const Json& frame : vector.at("frames")) {
            expected.push_back(fromHex(frame.get<std::string>()));
        }
        for (const std::size_t chunk : {std::size_t{1}, std::size_t{7}, std::size_t{64}, std::size_t{GS_ESP32_MAX_FRAME}}) {
            CAPTURE(chunk);
            const Parsed parsed = parse(data, chunk);
            CHECK(parsed.frames == expected);
            CHECK(parsed.bad == vector.at("badFrames").get<std::uint32_t>());
            CHECK(parsed.dropped == vector.at("dropped").get<std::uint32_t>());
        }
    }
}

TEST_CASE("a Wi-Fi datagram carries a 16-byte HMAC-SHA256 tag; a wrong tag or key is dropped (HID-004)")
{
    REQUIRE_FALSE(vectors().at("tags").empty());
    for (const Json& vector : vectors().at("tags")) {
        INFO(vector.at("name").get<std::string>());
        const Bytes key = fromHex(vector.at("key").get<std::string>());
        const Bytes frame = fromHex(vector.at("frame").get<std::string>());
        const Bytes tag = fromHex(vector.at("tag").get<std::string>());
        const Bytes datagram = fromHex(vector.at("datagram").get<std::string>());
        REQUIRE(key.size() == GS_ESP32_KEY_SIZE);
        REQUIRE(tag.size() == GS_ESP32_TAG_SIZE);

        std::uint8_t computed[GS_ESP32_TAG_SIZE]{};
        gs_esp32_tag(key.data(), frame.data(), frame.size(), computed);
        CHECK(bytesOf(computed) == tag);
        CHECK(gs_esp32_tag_matches(key.data(), frame.data(), frame.size(), tag.data()));

        std::array<std::uint8_t, GS_ESP32_MAX_DATAGRAM> sealed{};
        std::copy(frame.begin(), frame.end(), sealed.begin());
        const std::size_t sealedSize = gs_esp32_datagram_seal(sealed.data(), frame.size(), sealed.size(), key.data());
        CHECK(Bytes(sealed.begin(), sealed.begin() + static_cast<std::ptrdiff_t>(sealedSize)) == datagram);

        gs_esp32_frame opened{};
        REQUIRE(gs_esp32_datagram_open(datagram.data(), datagram.size(), key.data(), &opened));
        CHECK(Bytes(opened.payload - GS_ESP32_HEADER_SIZE, opened.payload + opened.length + GS_ESP32_CRC_SIZE) == frame);

        Bytes forged = datagram;
        forged[GS_ESP32_HEADER_SIZE] = static_cast<std::uint8_t>(forged[GS_ESP32_HEADER_SIZE] ^ 0x01U);
        CHECK_FALSE(gs_esp32_datagram_open(forged.data(), forged.size(), key.data(), &opened));
        Bytes badTag = datagram;
        badTag.back() = static_cast<std::uint8_t>(badTag.back() ^ 0x80U);
        CHECK_FALSE(gs_esp32_datagram_open(badTag.data(), badTag.size(), key.data(), &opened));
        Bytes otherKey = key;
        otherKey[0] = static_cast<std::uint8_t>(otherKey[0] ^ 0xFFU);
        CHECK_FALSE(gs_esp32_datagram_open(datagram.data(), datagram.size(), otherKey.data(), &opened));
        CHECK_FALSE(gs_esp32_datagram_open(datagram.data(), GS_ESP32_FRAME_OVERHEAD + GS_ESP32_TAG_SIZE - 1,
                                           key.data(), &opened));
    }
}

TEST_CASE("the old QineTouch packet is F4, tip, id, X and Y as 32-bit little endian, then the contact count")
{
    for (const Json& vector : vectors().at("legacy")) {
        INFO(vector.at("name").get<std::string>());
        std::uint8_t packet[GS_ESP32_LEGACY_PACKET]{};
        gs_esp32_legacy_packet(packet, vector.at("tip").get<std::uint8_t>(), vector.at("id").get<std::uint8_t>(),
                               vector.at("x").get<std::uint32_t>(), vector.at("y").get<std::uint32_t>(),
                               vector.at("count").get<std::uint8_t>());
        CHECK(bytesOf(packet) == fromHex(vector.at("packet").get<std::string>()));
    }
}
