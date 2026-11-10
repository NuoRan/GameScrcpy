#include "fakes/FakeDeviceServer.h"
#include "fakes/FakeServerHost.h"
#include "fakes/RecordingListener.h"

#include "gs/session/Session.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

using namespace gs;
using namespace gs::session;
using namespace gs::session::test;
using namespace std::chrono_literals;

namespace {

protocol::Welcome androidNineWelcome()
{
    protocol::Welcome welcome;
    welcome.serverVersion = "2.0.0";
    welcome.manufacturer = "Xiaomi";
    welcome.model = "MI 8";
    welcome.androidRelease = "9";
    welcome.sdkInt = 28;
    welcome.serialNo = "8f4c2a91";
    protocol::Display display;
    display.displayId = 0;
    display.width = 1080;
    display.height = 2248;
    display.rotation = 0;
    display.densityDpi = 440;
    display.refreshMilliHz = 60000;
    display.safeTop = 89;
    welcome.displays.push_back(display);
    welcome.encoders = {
        protocol::Encoder{.codec = protocol::VideoCodec::H264, .name = "OMX.qcom.video.encoder.avc", .hardware = true},
        protocol::Encoder{.codec = protocol::VideoCodec::H265, .name = "OMX.qcom.video.encoder.hevc", .hardware = true},
        protocol::Encoder{.codec = protocol::VideoCodec::H264, .name = "OMX.google.h264.encoder", .hardware = false}};
    welcome.audioStatus = protocol::AudioStatus::UnsupportedAndroid;
    welcome.capabilities = protocol::Welcome::kClipboard | protocol::Welcome::kText | protocol::Welcome::kRotation;
    return welcome;
}

SessionOptions quickOptions()
{
    SessionOptions options;
    options.clientVersion = "test";
    options.linger = 5s;
    options.handshakeTimeout = 2s;
    options.pingInterval = 50ms;
    options.pongTimeout = 2s;
    options.stopGrace = 1s;
    return options;
}

}

TEST_CASE("SRV-001: the handshake hands over the phone, its displays and its encoders as an Android 9 server sends them")
{
    FakeServerOptions serverOptions;
    serverOptions.welcome = androidNineWelcome();
    FakeDeviceServer server(std::move(serverOptions));
    RecordingListener listener;
    Session session(std::make_unique<FakeServerHost>(server), quickOptions(), listener);
    session.start();
    REQUIRE(listener.waitForPhase(SessionPhase::Streaming));

    const std::vector<protocol::Welcome> welcomes = listener.welcomes();
    REQUIRE(welcomes.size() == 1);
    const protocol::Welcome& welcome = welcomes[0];
    CHECK(welcome == androidNineWelcome());
    CHECK(welcome.sdkInt == 28);
    CHECK(welcome.androidRelease == "9");
    CHECK(welcome.serialNo == "8f4c2a91");
    REQUIRE(welcome.displays.size() == 1);
    CHECK(welcome.displays[0].width == 1080);
    CHECK(welcome.displays[0].height == 2248);
    CHECK(welcome.displays[0].densityDpi == 440);
    CHECK(welcome.displays[0].refreshMilliHz == 60000);
    CHECK(welcome.displays[0].safeTop == 89);
    REQUIRE(welcome.encoders.size() == 3);
    CHECK(welcome.encoders[0].hardware);
    CHECK(welcome.encoders[1].codec == protocol::VideoCodec::H265);
    CHECK_FALSE(welcome.encoders[2].hardware);
    CHECK(welcome.audioStatus == protocol::AudioStatus::UnsupportedAndroid);
    CHECK_FALSE(welcome.resumed);

    session.stop();
    REQUIRE(listener.waitForPhase(SessionPhase::Stopped));
}

TEST_CASE("SRV-009: the server build stays under 200 KB")
{
    const std::filesystem::path package = GS_SERVER_PACKAGE_FILE;
    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(package, error);
    if (error) {
        WARN_MESSAGE(false, "no server build at " << package.string()
                                                  << "; run gradlew :server:assembleRelease to check its size");
        return;
    }
    MESSAGE("server build: " << bytes << " bytes");
    CHECK(bytes > 0U);
    CHECK(bytes < 200U * 1024U);
}
