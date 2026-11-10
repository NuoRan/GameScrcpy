#include "fakes/FakeDeviceServer.h"
#include "stability/FakePhone.h"

#include "SessionController.h"
#include "UhidMouse.h"
#include "Settings.h"

#include <gs/adb/AdbClient.h>
#include <gs/protocol/Messages.h>
#include <gs/session/AdbServerHost.h>
#include <gs/session/SessionState.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QString>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace protocol = gs::protocol;
namespace session = gs::session;
using gs::app::SessionController;
using gs::app::settings::PictureSource;
using gs::app::settings::Settings;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view kSerial = "fake-phone-1";
constexpr std::size_t kServerBytes = 4096;
constexpr std::uint8_t kServerFill = 0x47;

class Application
{
public:
    Application()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_app = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    char m_name[5] = "test";
    std::array<char*, 2> m_argv{m_name, nullptr};
    std::unique_ptr<QCoreApplication> m_app;
};

bool settle(const std::function<bool()>& done, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QCoreApplication::processEvents();
    return done();
}

std::shared_ptr<const session::ServerPackage> fakePackage()
{
    return std::make_shared<const session::ServerPackage>(std::vector<std::uint8_t>(kServerBytes, kServerFill));
}

Settings wantedSettings()
{
    Settings settings;
    settings.video.maxSize = 1920;
    settings.video.maxFps = 120;
    settings.video.bitrateKbps = 20'000;
    settings.audio.enabled = true;
    return settings;
}

class Rig
{
public:
    explicit Rig(session::test::FakeServerOptions options = {})
        : m_device(std::move(options))
        , m_phone(m_device, std::string(kSerial))
        , m_controller(gs::adb::AdbClient(m_phone.port()),
                       QString::fromLatin1(kSerial.data(), static_cast<qsizetype>(kSerial.size())), fakePackage(),
                       nullptr)
    {
        QObject::connect(&m_controller, &SessionController::stateChanged,
                         [this](const session::SessionState& changed) { m_state = changed; });
    }

    [[nodiscard]] session::test::FakeDeviceServer& device() noexcept { return m_device; }
    [[nodiscard]] SessionController& controller() noexcept { return m_controller; }
    [[nodiscard]] bool streaming() const noexcept
    {
        return m_state && m_state->phase == session::SessionPhase::Streaming;
    }

    [[nodiscard]] bool stop()
    {
        m_controller.stop();
        return settle(
            [this] {
                return m_state && (m_state->phase == session::SessionPhase::Stopped ||
                                   m_state->phase == session::SessionPhase::Failed);
            },
            15s);
    }

private:
    session::test::FakeDeviceServer m_device;
    gs::stability::FakePhone m_phone;
    std::optional<session::SessionState> m_state;
    SessionController m_controller;
};

template <typename M>
std::ptrdiff_t firstOf(const std::vector<protocol::ToServerMessage>& messages)
{
    const auto found = std::ranges::find_if(
        messages, [](const protocol::ToServerMessage& message) { return std::holds_alternative<M>(message); });
    return found - messages.begin();
}

}

TEST_CASE("ADR-0108: a phone whose picture comes from a capture card asks for a small paused video from the first "
          "handshake on, keeps it paused after a resumption and turns its sound off")
{
    const Application application;
    Rig rig;
    rig.controller().setPictureSource(PictureSource::Capture);
    rig.controller().start(session::TransportChoice::AdbTunnel, wantedSettings());

    REQUIRE(rig.device().waitForReceived<protocol::SetVideoPaused>(1, 20s));
    const std::vector<protocol::ConfigureVideo> videos = rig.device().received<protocol::ConfigureVideo>();
    REQUIRE(videos.size() == 1);
    CHECK(videos.front().maxSize == 320);
    CHECK(videos.front().maxFps == 1);
    CHECK(videos.front().bitrate > 0);
    CHECK(videos.front().bitrate < 500'000);
    CHECK(rig.device().received<protocol::SetVideoPaused>().front().paused);
    const std::vector<protocol::ToServerMessage> first = rig.device().receivedAll();
    CHECK(firstOf<protocol::ConfigureVideo>(first) < firstOf<protocol::SetVideoPaused>(first));
    const std::vector<protocol::ConfigureAudio> sounds = rig.device().received<protocol::ConfigureAudio>();
    REQUIRE(sounds.size() == 1);
    CHECK_FALSE(sounds.front().enabled);

    rig.device().dropControl();
    REQUIRE(rig.device().waitForHandshakes(2, 20s));
    REQUIRE(rig.device().waitForReceived<protocol::SetVideoPaused>(2, 20s));
    CHECK(rig.device().resumedFlags() == std::vector<bool>{false, true});
    const std::vector<protocol::SetVideoPaused> pauses = rig.device().received<protocol::SetVideoPaused>();
    CHECK(std::ranges::all_of(pauses, [](const protocol::SetVideoPaused& pause) { return pause.paused; }));
    const std::vector<protocol::ConfigureVideo> again = rig.device().received<protocol::ConfigureVideo>();
    REQUIRE(again.size() == 2);
    CHECK(again.back().maxSize == 320);
    CHECK(again.back().maxFps == 1);
    CHECK(std::ranges::none_of(rig.device().received<protocol::ConfigureAudio>(),
                               [](const protocol::ConfigureAudio& sound) { return sound.enabled; }));

    CHECK(rig.stop());
}

TEST_CASE("ADR-0108: a phone shown without a picture also pauses its video but keeps its sound as set")
{
    const Application application;
    Rig rig;
    rig.controller().setPictureSource(PictureSource::None);
    rig.controller().start(session::TransportChoice::AdbTunnel, wantedSettings());

    REQUIRE(rig.device().waitForReceived<protocol::SetVideoPaused>(1, 20s));
    const std::vector<protocol::ConfigureVideo> videos = rig.device().received<protocol::ConfigureVideo>();
    REQUIRE(videos.size() == 1);
    CHECK(videos.front().maxSize == 320);
    CHECK(rig.device().received<protocol::SetVideoPaused>().front().paused);
    const std::vector<protocol::ConfigureAudio> sounds = rig.device().received<protocol::ConfigureAudio>();
    REQUIRE(sounds.size() == 1);
    CHECK(sounds.front().enabled);

    CHECK(rig.stop());
}

TEST_CASE("ADR-0108: a phone that sends its own picture asks for the video and sound as set and never pauses it")
{
    const Application application;
    Rig rig;
    rig.controller().start(session::TransportChoice::AdbTunnel, wantedSettings());

    REQUIRE(rig.device().waitForReceived<protocol::Ping>(1, 20s));
    const std::vector<protocol::ConfigureVideo> videos = rig.device().received<protocol::ConfigureVideo>();
    REQUIRE(videos.size() == 1);
    CHECK(videos.front().maxSize == 1920);
    CHECK(videos.front().maxFps == 120);
    CHECK(videos.front().bitrate == 20'000'000U);
    const std::vector<protocol::ConfigureAudio> sounds = rig.device().received<protocol::ConfigureAudio>();
    REQUIRE(sounds.size() == 1);
    CHECK(sounds.front().enabled);
    CHECK(rig.device().received<protocol::SetVideoPaused>().empty());

    CHECK(rig.stop());
}

TEST_CASE("ADR-0108: a phone shown without a picture gets a virtual mouse that follows the computer's mouse")
{
    const Application application;
    session::test::FakeServerOptions options;
    options.welcome.capabilities |= protocol::Welcome::kUhid;
    Rig rig(std::move(options));
    rig.controller().setPictureSource(PictureSource::None);
    rig.controller().start(session::TransportChoice::AdbTunnel, wantedSettings());

    REQUIRE(settle([&rig] { return rig.streaming(); }, 20s));
    REQUIRE(rig.device().waitForReceived<protocol::UhidCreate>(1, 20s));
    const std::vector<protocol::UhidCreate> created = rig.device().received<protocol::UhidCreate>();
    CHECK(created.front().id == gs::app::kUhidMouseId);

    rig.controller().moveMouse(5, -3);
    rig.controller().setMouseButtons(0x01);
    rig.controller().setMouseButtons(0x01);
    REQUIRE(rig.device().waitForReceived<protocol::UhidInput>(2, 20s));
    const std::vector<protocol::UhidInput> reports = rig.device().received<protocol::UhidInput>();
    REQUIRE(reports.size() == 2);
    CHECK(reports[0].report == std::vector<std::uint8_t>{0x00, 0x05, 0x00, 0xFD, 0xFF, 0x00, 0x00});
    CHECK(reports[1].report == std::vector<std::uint8_t>{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

    CHECK(rig.stop());
}
