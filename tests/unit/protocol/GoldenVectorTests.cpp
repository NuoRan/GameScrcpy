#include "JsonVisitors.h"

#include <gs/protocol/Codec.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

using namespace gs::protocol;
using gs::protocol::testing::fromHex;
using gs::protocol::testing::fromJson;
using gs::protocol::testing::toHex;
using gs::protocol::testing::toJson;
using nlohmann::json;

struct VectorEntry {
    std::string file;
    json data;
};

std::vector<VectorEntry> loadVectors()
{
    std::vector<std::filesystem::path> paths;
    for (const auto& item : std::filesystem::directory_iterator(GS_PROTOCOL_VECTOR_DIR)) {
        if (item.path().extension() == ".json") {
            paths.push_back(item.path());
        }
    }
    std::sort(paths.begin(), paths.end());

    std::vector<VectorEntry> entries;
    for (const auto& path : paths) {
        std::ifstream stream(path, std::ios::binary);
        const json document = json::parse(stream);
        for (const auto& vector : document.at("vectors")) {
            entries.push_back({path.filename().string(), vector});
        }
    }
    return entries;
}

const std::vector<VectorEntry>& allVectors()
{
    static const std::vector<VectorEntry> entries = loadVectors();
    return entries;
}

bool isMediaChannel(const std::string& channel)
{
    return channel == "video" || channel == "audio";
}

MediaChannel mediaChannelFrom(const std::string& channel)
{
    return channel == "video" ? MediaChannel::Video : MediaChannel::Audio;
}

FrameContext frameContextFrom(const std::string& channel)
{
    if (channel == "handshake") {
        return FrameContext::Handshake;
    }
    if (channel == "control") {
        return FrameContext::Control;
    }
    if (channel == "companion") {
        return FrameContext::Companion;
    }
    REQUIRE(channel == "bulk");
    return FrameContext::Bulk;
}

template <typename M, typename DecodeFn>
void checkFrameMessage(const json& fields, const Bytes& expected, FrameContext context,
                       DecodeFn decode)
{
    const M message = fromJson<M>(fields);
    const auto encoded = encodeFrame(message);
    REQUIRE_MESSAGE(encoded.has_value(), "encode failed: " << errorName(encoded.error()));
    CHECK(toHex(*encoded) == toHex(expected));

    const auto decoded = decode(context, std::span<const std::uint8_t>(expected));
    REQUIRE_MESSAGE(decoded.has_value(), "decode failed: " << errorName(decoded.error()));
    const M* typed = std::get_if<M>(&*decoded);
    REQUIRE(typed != nullptr);
    CHECK(*typed == message);
    CHECK(toJson(*typed) == fields);
}

template <typename... Ms, typename DecodeFn>
bool dispatchFrame(std::type_identity<std::variant<Ms...>>, const std::string& name,
                   const json& fields, const Bytes& expected, FrameContext context,
                   DecodeFn decode)
{
    return ((Ms::kName == name
                 ? (checkFrameMessage<Ms>(fields, expected, context, decode), true)
                 : false) ||
            ...);
}

void checkMediaMessage(const std::string& name, const json& fields, const Bytes& expected,
                       MediaChannel channel)
{
    const auto session = fields.at("session").get<std::uint16_t>();
    gs::Expected<Bytes, EncodeError> encoded = gs::makeUnexpected(EncodeError::ValueOutOfRange);
    if (name == "VideoSession") {
        encoded = encodeVideoSession(session, fromJson<VideoSessionInfo>(fields));
    } else if (name == "AudioSession") {
        encoded = encodeAudioSession(session, fromJson<AudioSessionInfo>(fields));
    } else if (name == "MediaConfig") {
        encoded = encodeMediaConfig(channel, session, fromHex(fields.at("payload").get<std::string>()));
    } else {
        REQUIRE(name == "MediaFrame");
        const MediaFrameFlags flags{.keyFrame = fields.at("keyFrame").get<bool>(),
                                    .discardable = fields.at("discardable").get<bool>()};
        encoded = encodeMediaFrame(channel, session, flags,
                                   std::stoll(fields.at("ptsUs").get<std::string>()),
                                   fields.at("encodeDelayUs").get<std::uint32_t>(),
                                   fromHex(fields.at("payload").get<std::string>()));
    }
    REQUIRE_MESSAGE(encoded.has_value(), "encode failed: " << errorName(encoded.error()));
    CHECK(toHex(*encoded) == toHex(expected));

    const auto decoded = decodeMediaPacket(channel, expected);
    REQUIRE_MESSAGE(decoded.has_value(), "decode failed: " << errorName(decoded.error()));
    CHECK(kMediaHeaderSize + decoded->payload.size() == expected.size());

    json roundTrip;
    if (const auto* video = std::get_if<VideoSessionInfo>(&decoded->session)) {
        roundTrip = toJson(*video);
    } else if (const auto* audio = std::get_if<AudioSessionInfo>(&decoded->session)) {
        roundTrip = toJson(*audio);
    } else {
        roundTrip = json::object();
        roundTrip["payload"] = toHex(decoded->payload);
        if (decoded->header.kind == MediaKind::Frame) {
            roundTrip["keyFrame"] = decoded->header.keyFrame;
            roundTrip["discardable"] = decoded->header.discardable;
            roundTrip["ptsUs"] = std::to_string(decoded->header.ptsUs);
            roundTrip["encodeDelayUs"] = decoded->header.encodeDelayUs;
        }
    }
    roundTrip["session"] = decoded->header.session;
    CHECK(roundTrip == fields);
}

}

TEST_CASE("golden vectors: every message encodes and decodes byte for byte")
{
    std::set<std::string> covered;
    for (const VectorEntry& vectorEntry : allVectors()) {
        const json& entry = vectorEntry.data;
        if (entry.contains("error")) {
            continue;
        }
        const std::string name = entry.at("message").get<std::string>();
        INFO(vectorEntry.file << ": " << entry.at("name").get<std::string>());
        const Bytes expected = fromHex(entry.at("hex").get<std::string>());
        const std::string channel = entry.at("channel").get<std::string>();
        const json& fields = entry.at("fields");

        if (isMediaChannel(channel)) {
            checkMediaMessage(name, fields, expected, mediaChannelFrom(channel));
        } else {
            const FrameContext context = frameContextFrom(channel);
            const bool known =
                entry.at("direction") == "toServer"
                    ? dispatchFrame(std::type_identity<ToServerMessage>{}, name, fields, expected,
                                    context, &decodeToServerFrame)
                    : dispatchFrame(std::type_identity<ToClientMessage>{}, name, fields, expected,
                                    context, &decodeToClientFrame);
            CHECK_MESSAGE(known, "no C++ message named " << name);
        }
        covered.insert(name);
    }
    CHECK(covered.size() == 65);
}

TEST_CASE("golden vectors: malformed inputs fail with the error mandated by SPEC 3.3")
{
    std::size_t checked = 0;
    for (const VectorEntry& vectorEntry : allVectors()) {
        const json& entry = vectorEntry.data;
        if (!entry.contains("error")) {
            continue;
        }
        INFO(vectorEntry.file << ": " << entry.at("name").get<std::string>());
        const Bytes bytes = fromHex(entry.at("hex").get<std::string>());
        const std::string channel = entry.at("channel").get<std::string>();

        std::string actual;
        if (isMediaChannel(channel)) {
            const auto result = decodeMediaPacket(mediaChannelFrom(channel), bytes);
            REQUIRE_FALSE(result.has_value());
            actual = errorName(result.error());
        } else if (entry.at("direction") == "toServer") {
            const auto result = decodeToServerFrame(frameContextFrom(channel), bytes);
            REQUIRE_FALSE(result.has_value());
            actual = errorName(result.error());
        } else {
            const auto result = decodeToClientFrame(frameContextFrom(channel), bytes);
            REQUIRE_FALSE(result.has_value());
            actual = errorName(result.error());
        }
        CHECK(actual == entry.at("error").get<std::string>());
        ++checked;
    }
    CHECK(checked >= 40);
}
