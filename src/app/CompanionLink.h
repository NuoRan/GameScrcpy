#pragma once

#include "CompanionShots.h"

#include <gs/adb/AdbClient.h>
#include <gs/base/Expected.h>
#include <gs/input/Geometry.h>
#include <gs/platform/TcpStream.h>
#include <gs/protocol/Codec.h>
#include <gs/protocol/Messages.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gs::app {

inline constexpr std::string_view kCompanionPackage = "com.gamescrcpy.companion";
inline constexpr std::string_view kCompanionIme = "com.gamescrcpy.companion/.CompanionIme";

struct CompanionApk {
    std::vector<std::uint8_t> bytes;
    std::optional<std::int64_t> versionCode;

    [[nodiscard]] static std::shared_ptr<const CompanionApk> load(const std::filesystem::path& apk,
                                                                   const std::filesystem::path& metadata);
};

[[nodiscard]] std::optional<std::int64_t> bundledVersionCode(std::string_view metadata);
[[nodiscard]] std::optional<std::int64_t> installedVersionCode(std::string_view packages);
[[nodiscard]] bool overlayAllowed(std::string_view appops) noexcept;
[[nodiscard]] std::optional<int> broadcastResult(std::string_view output) noexcept;
[[nodiscard]] protocol::CursorState phoneCursorOf(std::optional<input::Vec2> position, bool pressed, bool inFront,
                                                  bool locked) noexcept;
[[nodiscard]] bool imeEnabled(std::string_view output) noexcept;
[[nodiscard]] bool imeSelected(std::string_view output) noexcept;
[[nodiscard]] std::string previousInputMethod(std::string_view current, std::string_view enabled);
[[nodiscard]] std::vector<std::string> splitCommitText(std::string_view utf8);

enum class CompanionEvent : std::uint8_t {
    InstallNeeded,
    Installing,
    PermissionNeeded,
    Connected,
    OverlayChanged,
    InputBox,
    ImeSwitched,
    ImeRefused,
    Screenshot,
    ScreenshotFailed,
    InstallFailed,
    Failed,
    Lost,
    StoppedOnPhone,
    Ended,
};

[[nodiscard]] constexpr bool isFinal(CompanionEvent event) noexcept
{
    return event >= CompanionEvent::InstallFailed;
}

struct CompanionNotice {
    CompanionEvent event = CompanionEvent::Ended;
    bool overlay = false;
    std::string detail;
    std::optional<protocol::InputBoxChanged> box;
    std::uint32_t shot = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> picture;
    std::optional<ShotFailure> shotFailure;
};

struct CompanionTiming {
    std::chrono::milliseconds reply{5000};
    std::chrono::milliseconds ping{2000};
    std::chrono::milliseconds silence{6000};
    std::chrono::milliseconds drain{5000};
    std::chrono::microseconds cursorGap{8334};
    std::chrono::milliseconds shotFirst{30000};
    std::chrono::milliseconds shotBetween{5000};
};

enum class CompanionRead : std::uint8_t { Frame, Closed, TimedOut, Malformed };

class CompanionChannel
{
public:
    virtual ~CompanionChannel() = default;

    virtual void setTimeouts(std::chrono::milliseconds read, std::chrono::milliseconds write) = 0;
    [[nodiscard]] virtual bool send(std::span<const std::uint8_t> frame) = 0;
    [[nodiscard]] virtual CompanionRead receive(std::vector<std::uint8_t>& frame) = 0;
    virtual void interrupt() = 0;
};

[[nodiscard]] std::unique_ptr<CompanionChannel> plainCompanionChannel(platform::TcpStream stream);

class CompanionLink final
{
public:
    using Listener = std::function<void(const CompanionNotice& notice)>;

    CompanionLink(adb::AdbClient client, std::string serial, std::shared_ptr<const CompanionApk> apk, Listener listener,
                  CompanionTiming timing = {});
    CompanionLink(std::unique_ptr<CompanionChannel> channel, const protocol::CompanionWelcome& welcome,
                  std::string name, Listener listener, CompanionTiming timing = {});
    ~CompanionLink();
    CompanionLink(const CompanionLink&) = delete;
    CompanionLink& operator=(const CompanionLink&) = delete;

    [[nodiscard]] const std::string& serial() const noexcept { return m_serial; }
    [[nodiscard]] bool overLan() const noexcept { return m_lan; }
    void allowInstall();
    void setCursor(const protocol::CursorState& cursor);
    void setIme(bool on);
    void retryIme();
    void commitText(std::string_view utf8);
    void editorAction(protocol::ImeAction action);
    void setKeyHints(const protocol::KeyHintLayout& layout);
    void setKeyHintState(const protocol::KeyHintState& state);
    void requestScreenshot(std::uint32_t requestId, std::uint16_t maxSide, std::uint8_t quality);
    void stop();

private:
    void run();
    [[nodiscard]] Expected<std::unique_ptr<CompanionChannel>, CompanionNotice> establish();
    [[nodiscard]] bool awaitInstall();
    [[nodiscard]] std::optional<CompanionNotice> install();
    [[nodiscard]] adb::AdbResult<adb::ShellResult> runLong(std::string_view command);
    void grantOverlay();
    void openPermissionPage();
    [[nodiscard]] Expected<std::unique_ptr<CompanionChannel>, CompanionNotice> launch();
    [[nodiscard]] Expected<std::unique_ptr<CompanionChannel>, CompanionNotice> handshake(
        std::unique_ptr<CompanionChannel> channel, std::span<const std::uint8_t> token);
    [[nodiscard]] Expected<std::unique_ptr<CompanionChannel>, CompanionNotice> admitted(
        std::unique_ptr<CompanionChannel> channel, const protocol::ToClientMessage& reply);
    [[nodiscard]] CompanionNotice converse(std::unique_ptr<CompanionChannel> channel);
    [[nodiscard]] CompanionNotice talk(CompanionChannel& channel);
    void listen(CompanionChannel& channel);
    void switchIme();
    void restoreIme();
    void openImeSettings();
    void finish(const CompanionNotice& end);
    void notify(const CompanionNotice& notice);
    [[nodiscard]] bool stopping();

    const bool m_lan = false;
    const adb::AdbClient m_client;
    const std::string m_serial;
    const std::shared_ptr<const CompanionApk> m_apk;
    const Listener m_listener;
    const CompanionTiming m_timing;
    std::unique_ptr<CompanionChannel> m_given;
    const protocol::CompanionWelcome m_welcome{};

    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stopping = false;
    bool m_hurry = false;
    bool m_installAllowed = false;
    bool m_permissionWanted = false;
    bool m_connected = false;
    bool m_imeWanted = false;
    bool m_imeRetry = false;
    std::optional<protocol::CursorState> m_cursor;
    std::deque<std::vector<std::uint8_t>> m_outbox;
    std::optional<protocol::KeyHintLayout> m_hints;
    std::optional<protocol::KeyHintState> m_hintState;
    std::optional<CompanionNotice> m_readerEnd;
    ShotAssembly m_shots;
    adb::ShellProcess* m_process = nullptr;
    CompanionChannel* m_handshaking = nullptr;

    std::uint16_t m_port = 0;
    bool m_serviceStarted = false;
    bool m_permissionPageOpened = false;
    bool m_imeSwitched = false;
    bool m_imeApplied = false;
    bool m_imePageOpened = false;
    std::string m_previousIme;

    std::jthread m_worker;
};

}
