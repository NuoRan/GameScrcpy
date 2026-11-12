#include "JsonVisitors.h"

#include <gs/protocol/Codec.h>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace gs::protocol;
using gs::protocol::testing::fromHex;

std::span<const std::uint8_t> asBytes(std::string_view text)
{
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

void decodeEverywhere(std::span<const std::uint8_t> bytes)
{
    for (const auto context :
         {FrameContext::Handshake, FrameContext::Control, FrameContext::Bulk, FrameContext::Companion}) {
        static_cast<void>(decodeToServerFrame(context, bytes));
        static_cast<void>(decodeToClientFrame(context, bytes));
    }
    static_cast<void>(decodeMediaPacket(MediaChannel::Video, bytes));
    static_cast<void>(decodeMediaPacket(MediaChannel::Audio, bytes));
}

}

TEST_CASE("decoders never read out of bounds on mutated or random input")
{
    std::mt19937 rng{20261001U};
    const std::vector<Bytes> seeds{
        fromHex("01010000000b000101000080008000ffff"),
        fromHex("8001000000164753435000010001020304050607080900010203040506"),
        fromHex("0103000000070005616263"),
        fromHex("02000001000000000000000000000000000000000000000e01043809600000000004380960"),
    };

    std::size_t decodes = 0;
    for (const Bytes& seed : seeds) {
        for (int round = 0; round < 500; ++round) {
            Bytes mutated = seed;
            switch (rng() % 3U) {
            case 0:
                if (!mutated.empty()) {
                    auto& byte = mutated[rng() % mutated.size()];
                    byte = static_cast<std::uint8_t>(byte ^ (1U << (rng() % 8U)));
                }
                break;
            case 1:
                mutated.resize(rng() % (mutated.size() + 1));
                break;
            default:
                mutated.insert(mutated.begin() + static_cast<std::ptrdiff_t>(rng() % (mutated.size() + 1)),
                               static_cast<std::uint8_t>(rng()));
                break;
            }
            decodeEverywhere(mutated);
            ++decodes;
        }
    }
    for (int round = 0; round < 2000; ++round) {
        Bytes noise(rng() % 64U);
        for (auto& byte : noise) {
            byte = static_cast<std::uint8_t>(rng());
        }
        decodeEverywhere(noise);
        ++decodes;
    }
    CHECK(decodes == 4000);
}

TEST_CASE("input messages encode into a caller-provided buffer without allocating")
{
    const TouchFrame frame{.videoSession = 7,
                           .points = {TouchPoint{.action = TouchAction::Move, .pointerId = 3,
                                                 .x = 100, .y = 200, .pressure = 0xFFFF}}};
    std::array<std::uint8_t, kFrameHeaderSize + 11> buffer{};
    const auto written = encodeFrameInto(frame, buffer);
    REQUIRE(written.has_value());
    CHECK(*written == buffer.size());
    CHECK(gs::protocol::testing::toHex(buffer) == "01010000000b0007010203006400c8ffff");

    std::array<std::uint8_t, kFrameHeaderSize + 10> tooSmall{};
    const auto rejected = encodeFrameInto(frame, tooSmall);
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error() == EncodeError::BufferTooSmall);
}

TEST_CASE("the encoder refuses to produce frames the decoder would reject")
{
    SUBCASE("pointer id outside 0-31")
    {
        const auto result = encodeFrame(
            TouchFrame{.videoSession = 1, .points = {TouchPoint{.pointerId = 32}}});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EncodeError::ValueOutOfRange);
    }
    SUBCASE("touch frame without points")
    {
        const auto result = encodeFrame(TouchFrame{.videoSession = 1});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EncodeError::ValueOutOfRange);
    }
    SUBCASE("pointer repeated within one touch frame")
    {
        const auto result = encodeFrame(TouchFrame{
            .videoSession = 1,
            .points = {TouchPoint{.pointerId = 4},
                       TouchPoint{.action = TouchAction::Move, .pointerId = 4}}});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EncodeError::ValueOutOfRange);
    }
    SUBCASE("string longer than its limit")
    {
        const auto result = encodeFrame(ClientHello{.clientVersion = std::string(33, 'v')});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EncodeError::FieldTooLong);
    }
    SUBCASE("invalid UTF-8")
    {
        const auto result = encodeFrame(Text{.text = std::string("\xff\xfe")});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EncodeError::MalformedUtf8);
    }
    SUBCASE("reserved capability bits")
    {
        const auto result = encodeFrame(Welcome{.capabilities = 1U << 13U});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EncodeError::ValueOutOfRange);
    }
    SUBCASE("battery level that is neither 0-100 nor unknown")
    {
        const auto result = encodeFrame(DeviceStatus{.batteryPercent = 101});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EncodeError::ValueOutOfRange);
    }
    SUBCASE("CPU use that is neither 0-1000 nor unknown")
    {
        const auto result = encodeFrame(DeviceStatus{.cpuPermille = 1001});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EncodeError::ValueOutOfRange);
    }
    SUBCASE("status period outside 500-60000 ms")
    {
        const auto tooShort = encodeFrame(SubscribeStatus{.on = true, .periodMs = 499});
        REQUIRE_FALSE(tooShort.has_value());
        CHECK(tooShort.error() == EncodeError::ValueOutOfRange);
        const auto tooLong = encodeFrame(SubscribeStatus{.on = true, .periodMs = 60001});
        REQUIRE_FALSE(tooLong.has_value());
        CHECK(tooLong.error() == EncodeError::ValueOutOfRange);
    }
    SUBCASE("enumeration value outside the specification")
    {
        const auto result = encodeFrame(GetClipboard{.copyKey = static_cast<CopyKey>(9)});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EncodeError::ValueOutOfRange);
    }
    SUBCASE("list longer than its limit")
    {
        Welcome welcome;
        welcome.displays.resize(17);
        const auto result = encodeFrame(welcome);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EncodeError::FieldTooLong);
    }
    SUBCASE("media session 0 is reserved")
    {
        const auto result = encodeMediaFrame(MediaChannel::Video, 0, {.keyFrame = true}, 0, 0, {});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == EncodeError::ValueOutOfRange);
    }
}

TEST_CASE("frame headers can be validated before the payload arrives")
{
    const Bytes frame = fromHex("01010000000b000101000080008000ffff");
    const auto header = decodeFrameHeader(FrameContext::Control, Direction::ToServer,
                                          std::span<const std::uint8_t>(frame).first(kFrameHeaderSize));
    REQUIRE(header.has_value());
    CHECK(header->type == static_cast<std::uint16_t>(MessageType::TouchFrame));
    CHECK(header->length == 11);
}

TEST_CASE("the companion context admits its own messages and the keep-alive messages only")
{
    const auto allowed = [](FrameContext context, MessageType type) {
        return isAllowedIn(context, static_cast<std::uint16_t>(type));
    };
    CHECK(allowed(FrameContext::Companion, MessageType::CompanionHello));
    CHECK(allowed(FrameContext::Companion, MessageType::OverlayChanged));
    CHECK(allowed(FrameContext::Companion, MessageType::Ping));
    CHECK(allowed(FrameContext::Companion, MessageType::Pong));
    CHECK(allowed(FrameContext::Companion, MessageType::Goodbye));
    CHECK(allowed(FrameContext::Companion, MessageType::ServerGoodbye));
    CHECK_FALSE(allowed(FrameContext::Companion, MessageType::ReceiverReport));
    CHECK_FALSE(allowed(FrameContext::Companion, MessageType::Notice));
    CHECK_FALSE(allowed(FrameContext::Companion, MessageType::TouchFrame));
    CHECK_FALSE(allowed(FrameContext::Control, MessageType::CursorState));
    CHECK(allowed(FrameContext::Control, MessageType::Ping));
    CHECK(payloadLimit(FrameContext::Companion) == 16U * 1024U);

    const auto tooLong = encodeFrame(CompanionHello{.token = Bytes(kCompanionTokenSize + 1, 0)});
    REQUIRE_FALSE(tooLong.has_value());
    CHECK(tooLong.error() == EncodeError::FieldTooLong);
}

TEST_CASE("a phone status starts out unknown in every item and travels on the control channel")
{
    const auto unknown = encodeFrame(DeviceStatus{});
    REQUIRE(unknown.has_value());
    CHECK(gs::protocol::testing::toHex(*unknown) == "830500000006ff008000ffff");
    const auto decoded = decodeToClientFrame(FrameContext::Control, *unknown);
    REQUIRE(decoded.has_value());
    const auto* status = std::get_if<DeviceStatus>(&*decoded);
    REQUIRE(status != nullptr);
    CHECK(status->batteryPercent == DeviceStatus::kUnknownBattery);
    CHECK_FALSE(status->charging);
    CHECK(status->temperatureDeciC == DeviceStatus::kUnknownTemperature);
    CHECK(status->cpuPermille == DeviceStatus::kUnknownCpu);

    CHECK(isAllowedIn(FrameContext::Control, static_cast<std::uint16_t>(MessageType::SubscribeStatus)));
    CHECK(isAllowedIn(FrameContext::Control, static_cast<std::uint16_t>(MessageType::DeviceStatus)));
    CHECK_FALSE(isAllowedIn(FrameContext::Companion, static_cast<std::uint16_t>(MessageType::DeviceStatus)));
    CHECK((Welcome::kCapabilityMask & Welcome::kDeviceStatus) != 0U);
}

TEST_CASE("the companion's typing and key hint messages stay in their context and within their limits (ADR-0104)")
{
    const auto allowed = [](FrameContext context, MessageType type) {
        return isAllowedIn(context, static_cast<std::uint16_t>(type));
    };
    for (const MessageType type : {MessageType::CommitText, MessageType::EditorAction, MessageType::KeyHintLayout,
                                   MessageType::KeyHintState, MessageType::InputBoxChanged}) {
        CHECK(allowed(FrameContext::Companion, type));
        CHECK_FALSE(allowed(FrameContext::Control, type));
    }

    KeyHintLayout full;
    full.hints.resize(kMaxKeyHints, KeyHint{.shape = KeyHintShape::Key, .label = std::string(kMaxKeyHintLabel, 'k')});
    const auto encoded = encodeFrame(full);
    REQUIRE(encoded.has_value());
    CHECK(encoded->size() <= kFrameHeaderSize + kCompanionPayloadLimit);
    full.hints.push_back({});
    const auto tooMany = encodeFrame(full);
    REQUIRE_FALSE(tooMany.has_value());
    CHECK(tooMany.error() == EncodeError::FieldTooLong);

    const KeyHintLayout longLabel{.hints = {KeyHint{.label = std::string(kMaxKeyHintLabel + 1, 'x')}}};
    const auto labelled = encodeFrame(longLabel);
    REQUIRE_FALSE(labelled.has_value());
    CHECK(labelled.error() == EncodeError::FieldTooLong);
    const auto opaque = encodeFrame(KeyHintState{.shown = true, .opacity = 101});
    REQUIRE_FALSE(opaque.has_value());
    CHECK(opaque.error() == EncodeError::ValueOutOfRange);
    const auto wordy = encodeFrame(CommitText{.text = std::string(kCompanionTextLimit + 1, 'a')});
    REQUIRE_FALSE(wordy.has_value());
    CHECK(wordy.error() == EncodeError::FieldTooLong);
    CHECK(encodeFrame(CommitText{.text = std::string(kCompanionTextLimit, 'a')}).has_value());
}

TEST_CASE("the companion's screenshot messages stay in their context and within their limits (ADR-0105)")
{
    const auto allowed = [](FrameContext context, MessageType type) {
        return isAllowedIn(context, static_cast<std::uint16_t>(type));
    };
    for (const MessageType type :
         {MessageType::ScreenshotRequest, MessageType::ScreenshotChunk, MessageType::ScreenshotFailed}) {
        CHECK(allowed(FrameContext::Companion, type));
        CHECK_FALSE(allowed(FrameContext::Control, type));
        CHECK_FALSE(allowed(FrameContext::Bulk, type));
    }
    const std::size_t chunks = kMaxScreenshotChunks;
    CHECK((chunks - 1) * kMaxScreenshotChunk < kMaxScreenshotBytes);
    CHECK(kMaxScreenshotBytes <= chunks * kMaxScreenshotChunk);

    const ScreenshotRequest request;
    CHECK(request.maxSide == 0);
    CHECK(request.quality == kDefaultScreenshotQuality);
    CHECK(kDefaultScreenshotQuality == 90);
    CHECK(kDefaultScreenshotSide == 1920);

    const ScreenshotChunk last{.requestId = 9,
                               .index = static_cast<std::uint16_t>(kMaxScreenshotChunks - 1),
                               .total = kMaxScreenshotChunks,
                               .data = Bytes(kMaxScreenshotChunk, 0xAB)};
    const auto encoded = encodeFrame(last);
    REQUIRE(encoded.has_value());
    CHECK(encoded->size() <= kFrameHeaderSize + kCompanionPayloadLimit);
    const auto decoded = decodeToClientFrame(FrameContext::Companion, *encoded);
    REQUIRE(decoded.has_value());
    const auto* chunk = std::get_if<ScreenshotChunk>(&*decoded);
    REQUIRE(chunk != nullptr);
    CHECK(*chunk == last);

    const auto beyond = encodeFrame(ScreenshotChunk{.requestId = 1, .index = 3, .total = 3, .data = Bytes(1, 0)});
    REQUIRE_FALSE(beyond.has_value());
    CHECK(beyond.error() == EncodeError::ValueOutOfRange);
    const auto none = encodeFrame(ScreenshotChunk{.requestId = 1, .index = 0, .total = 0, .data = Bytes(1, 0)});
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error() == EncodeError::ValueOutOfRange);
    const auto many = encodeFrame(ScreenshotChunk{.requestId = 1,
                                                  .index = 0,
                                                  .total = static_cast<std::uint16_t>(kMaxScreenshotChunks + 1),
                                                  .data = Bytes(1, 0)});
    REQUIRE_FALSE(many.has_value());
    CHECK(many.error() == EncodeError::ValueOutOfRange);
    const auto oversize =
        encodeFrame(ScreenshotChunk{.requestId = 1, .index = 0, .total = 1, .data = Bytes(kMaxScreenshotChunk + 1, 0)});
    REQUIRE_FALSE(oversize.has_value());
    CHECK(oversize.error() == EncodeError::FieldTooLong);

    const auto dull = encodeFrame(ScreenshotRequest{.requestId = 1, .maxSide = 0, .quality = 0});
    REQUIRE_FALSE(dull.has_value());
    CHECK(dull.error() == EncodeError::ValueOutOfRange);
    const auto sharp = encodeFrame(ScreenshotRequest{.requestId = 1, .maxSide = 0, .quality = 101});
    REQUIRE_FALSE(sharp.has_value());
    CHECK(sharp.error() == EncodeError::ValueOutOfRange);

    const auto wordy = encodeFrame(
        ScreenshotFailed{.requestId = 1, .reason = ScreenshotProblem::Error, .message = std::string(1025, 'x')});
    REQUIRE_FALSE(wordy.has_value());
    CHECK(wordy.error() == EncodeError::FieldTooLong);
    const auto strange =
        encodeFrame(ScreenshotFailed{.requestId = 1, .reason = static_cast<ScreenshotProblem>(4), .message = {}});
    REQUIRE_FALSE(strange.has_value());
    CHECK(strange.error() == EncodeError::ValueOutOfRange);
    CHECK(enumName(ScreenshotProblem::Refused) == "refused");
}

TEST_CASE("ADR-0112: the companion's settings snapshot and setting change stay in their context and within their "
          "ranges")
{
    const auto allowed = [](FrameContext context, MessageType type) {
        return isAllowedIn(context, static_cast<std::uint16_t>(type));
    };
    for (const MessageType type : {MessageType::CompanionSettings, MessageType::CompanionSettingChange}) {
        CHECK(allowed(FrameContext::Companion, type));
        CHECK_FALSE(allowed(FrameContext::Control, type));
        CHECK_FALSE(allowed(FrameContext::Handshake, type));
    }
    CHECK(directionOf(static_cast<std::uint16_t>(MessageType::CompanionSettings)) == Direction::ToServer);
    CHECK(directionOf(static_cast<std::uint16_t>(MessageType::CompanionSettingChange)) == Direction::ToClient);
    CHECK(kCompanionVersion == 2);
    CHECK(kCompanionSettingsVersion == 2);
    CHECK(CompanionHello{}.version == 2);

    const CompanionSettings defaults;
    CHECK(defaults.hintOpacity == kDefaultPhoneHintOpacity);
    CHECK(defaults.ime);
    CHECK(defaults.autoConnect);
    CHECK_FALSE(defaults.cursor);
    CHECK_FALSE(defaults.hints);

    CompanionSettings longest;
    longest.name = std::string(kMaxComputerName, 'n');
    longest.accent = kAccentMask;
    longest.applied = 0xFFFFFFFFU;
    const auto encoded = encodeFrame(longest);
    REQUIRE(encoded.has_value());
    const auto decoded = decodeToServerFrame(FrameContext::Companion, *encoded);
    REQUIRE(decoded.has_value());
    const auto* settings = std::get_if<CompanionSettings>(&*decoded);
    REQUIRE(settings != nullptr);
    CHECK(*settings == longest);

    CompanionSettings named = longest;
    named.name.push_back('n');
    const auto wordy = encodeFrame(named);
    REQUIRE_FALSE(wordy.has_value());
    CHECK(wordy.error() == EncodeError::FieldTooLong);
    CompanionSettings tinted = longest;
    tinted.accent = 0x01FF6B3DU;
    const auto colored = encodeFrame(tinted);
    REQUIRE_FALSE(colored.has_value());
    CHECK(colored.error() == EncodeError::ValueOutOfRange);
    for (const std::uint8_t opacity : {std::uint8_t{9}, std::uint8_t{101}}) {
        CompanionSettings faint = longest;
        faint.hintOpacity = opacity;
        const auto shown = encodeFrame(faint);
        REQUIRE_FALSE(shown.has_value());
        CHECK(shown.error() == EncodeError::ValueOutOfRange);
    }

    CHECK(encodeFrame(CompanionSettingChange{.change = 1, .item = CompanionItem::Cursor, .value = 1}).has_value());
    CHECK(encodeFrame(CompanionSettingChange{.change = 1, .item = CompanionItem::HintOpacity, .value = 10})
              .has_value());
    for (const CompanionSettingChange& wrong :
         {CompanionSettingChange{.change = 0, .item = CompanionItem::Cursor, .value = 1},
          CompanionSettingChange{.change = 1, .item = CompanionItem::Ime, .value = 2},
          CompanionSettingChange{.change = 1, .item = CompanionItem::AutoConnect, .value = 60},
          CompanionSettingChange{.change = 1, .item = CompanionItem::HintOpacity, .value = 9},
          CompanionSettingChange{.change = 1, .item = CompanionItem::HintOpacity, .value = 101},
          CompanionSettingChange{.change = 1, .item = static_cast<CompanionItem>(5), .value = 1}}) {
        const auto refused = encodeFrame(wrong);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error() == EncodeError::ValueOutOfRange);
    }
    CHECK(enumName(CompanionItem::HintOpacity) == "hintOpacity");
    CHECK(enumName(ComputerLink::Lan) == "lan");
}

TEST_CASE("UTF-8 validation is strict")
{
    CHECK(isValidUtf8(asBytes("plain ascii")));
    CHECK(isValidUtf8(asBytes("\xe4\xbd\xa0\xe5\xa5\xbd")));
    CHECK(isValidUtf8(asBytes("\xf0\x9f\x8e\xae")));
    CHECK(isValidUtf8(asBytes("\xf4\x8f\xbf\xbf")));
    CHECK_FALSE(isValidUtf8(asBytes("\xc0\xaf")));
    CHECK_FALSE(isValidUtf8(asBytes("\xe0\x80\xaf")));
    CHECK_FALSE(isValidUtf8(asBytes("\xed\xa0\x80")));
    CHECK_FALSE(isValidUtf8(asBytes("\xf4\x90\x80\x80")));
    CHECK_FALSE(isValidUtf8(asBytes("\xe4\xbd")));
    CHECK_FALSE(isValidUtf8(asBytes("\x80")));
    CHECK_FALSE(isValidUtf8(asBytes("\xf8\x88\x80\x80\x80")));
}
