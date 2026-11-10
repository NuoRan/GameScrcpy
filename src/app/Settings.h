#pragma once

#include "TranslateNoop.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gs::app::settings {

enum class Page : std::uint8_t { None, General, Video, Audio, Input, Connection, Appearance, Advanced, Ai, Assistant };

enum class Scope : std::uint8_t { Global, Device };

struct Facets {
    std::string_view title;
    std::string_view description;
    std::string_view keywords;
    Scope scope = Scope::Global;
    bool live = false;
};

inline constexpr std::string_view kFormat = "gamescrcpy.settings";
inline constexpr int kVersion = 1;

enum class Language : std::uint8_t { System, ChineseSimplified, English };
inline constexpr std::array<std::string_view, 3> kLanguageNames{"system", "zh_CN", "en"};

struct General {
    Language language = Language::System;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.choice("language", m.language, kLanguageNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Interface language"),
                  .description = GS_TRANSLATE_NOOP(
                      "Settings", "Following Windows shows Chinese on a Chinese Windows and English elsewhere."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "language chinese english locale"),
                  .live = true});
    }

    friend bool operator==(const General&, const General&) = default;
};

struct Tray {
    bool closeToTray = true;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("closeToTray", m.closeToTray,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Close to the tray while tasks have a schedule"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Closing the main window keeps GameScrcpy running in "
                                                                "the notification area while any task has a "
                                                                "schedule, so the tasks still start on time. Quit from "
                                                                "the menu of its icon there."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "tray notification area background minimize close "
                                                             "schedule timer task quit"),
                   .live = true});
    }

    friend bool operator==(const Tray&, const Tray&) = default;
};

enum class ThemeMode : std::uint8_t { Dark, Light, System };
inline constexpr std::array<std::string_view, 3> kThemeModeNames{"dark", "light", "system"};

enum class Accent : std::uint8_t { Ember, Teal, Azure, Lime, Rose, Amber };
inline constexpr std::array<std::string_view, 6> kAccentNames{"ember", "teal", "azure", "lime", "rose", "amber"};

enum class SystemToggle : std::uint8_t { System, On, Off };
inline constexpr std::array<std::string_view, 3> kSystemToggleNames{"system", "on", "off"};

struct Appearance {
    static constexpr std::size_t kMaxPaletteLength = 64 + 1 + 4 * 64;

    ThemeMode theme = ThemeMode::Dark;
    Accent accent = Accent::Ember;
    SystemToggle reducedMotion = SystemToggle::System;
    SystemToggle reducedTransparency = SystemToggle::System;
    std::string palette;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.choice("theme", m.theme, kThemeModeNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Theme"),
                  .description = GS_TRANSLATE_NOOP("Settings", "Dark, light, or as Windows is set."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "dark light mode color scheme"),
                  .live = true});
        v.choice("accent", m.accent, kAccentNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Accent color"),
                  .description = GS_TRANSLATE_NOOP("Settings", "Marks the main action, the selection and the focus."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "color highlight"),
                  .live = true});
        v.choice("reducedMotion", m.reducedMotion, kSystemToggleNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Reduce motion"),
                  .description = GS_TRANSLATE_NOOP("Settings", "Turns animations off. Following Windows uses the "
                                                               "animation effects switch in Windows settings."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "animation motion accessibility"),
                  .live = true});
        v.choice("reducedTransparency", m.reducedTransparency, kSystemToggleNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Reduce transparency"),
                  .description = GS_TRANSLATE_NOOP("Settings", "Draws solid surfaces instead of see-through ones. "
                                                               "Following Windows uses the transparency effects "
                                                               "switch in Windows settings."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "transparent opacity blur accessibility"),
                  .live = true});
        v.text("palette", m.palette, kMaxPaletteLength);
    }

    friend bool operator==(const Appearance&, const Appearance&) = default;
};

enum class Codec : std::uint8_t { H264, H265, Av1 };
inline constexpr std::array<std::string_view, 3> kCodecNames{"h264", "h265", "av1"};

struct Video {
    std::int32_t maxSize = 0;
    std::int32_t maxFps = 0;
    Codec codec = Codec::H264;
    std::int32_t bitrateKbps = 8000;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.integer("maxSize", m.maxSize, 0, 8192,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Maximum resolution"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Limits the longer side of the picture; its "
                                                                "proportions stay."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "size resolution scale 1080p 1920"),
                   .scope = Scope::Device});
        v.integer("maxFps", m.maxFps, 0, 240,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Frame rate limit"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Follow phone sends as many frames as the phone's "
                                                                "screen shows, whether 60, 90, 120 or 144 Hz. A lower "
                                                                "limit saves bandwidth and battery."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "fps frames refresh smooth"),
                   .scope = Scope::Device});
        v.choice("codec", m.codec, kCodecNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Video codec"),
                  .description = GS_TRANSLATE_NOOP("Settings", "H.265 and AV1 need less bandwidth where the phone's "
                                                               "encoder has them; H.264 works everywhere."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "h264 h265 hevc av1 encoder format"),
                  .scope = Scope::Device});
        v.integer("bitrateKbps", m.bitrateKbps, 500, 100'000,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Bit rate limit"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Lowered by itself when the network gets worse, and "
                                                                "back to this when it recovers."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "bitrate quality bandwidth mbps"),
                   .scope = Scope::Device});
    }

    friend bool operator==(const Video&, const Video&) = default;
};

enum class TitleBarMode : std::uint8_t { Fixed, Auto };
inline constexpr std::array<std::string_view, 2> kTitleBarModeNames{"fixed", "auto"};

enum class ToolbarMode : std::uint8_t { Dwell, Pinned, Shortcut };
inline constexpr std::array<std::string_view, 3> kToolbarModeNames{"dwell", "pinned", "shortcut"};

enum class StatusCapsule : std::uint8_t { Auto, Always, Hidden };
inline constexpr std::array<std::string_view, 3> kStatusCapsuleNames{"auto", "always", "hidden"};

enum class EditingClose : std::uint8_t { Ask, ExitEditing, CloseWindow };
inline constexpr std::array<std::string_view, 3> kEditingCloseNames{"ask", "exitEditing", "closeWindow"};

struct Display {
    bool lowLatency = false;
    std::int32_t sharpness = 0;
    TitleBarMode titleBar = TitleBarMode::Fixed;
    ToolbarMode toolbar = ToolbarMode::Pinned;
    StatusCapsule statusCapsule = StatusCapsule::Auto;
    bool phoneStatus = false;
    bool cameraGlow = true;
    EditingClose closeWhileEditing = EditingClose::Ask;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("lowLatency", m.lowLatency,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Low-latency mode"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Shows each picture as soon as it arrives instead of "
                                                                "waiting for the screen's refresh: the least delay, "
                                                                "with occasional tearing. Applies to mirror windows "
                                                                "opened afterwards."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "latency delay lag vsync tearing"),
                   .scope = Scope::Device});
        v.integer("sharpness", m.sharpness, 0, 100,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Sharpening"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Restores detail when the picture is shown larger "
                                                                "than the phone sends it."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "sharpen sharpness detail clarity blur"),
                   .scope = Scope::Device,
                   .live = true});
        v.choice("titleBar", m.titleBar, kTitleBarModeNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Title bar"),
                  .description = GS_TRANSLATE_NOOP("Settings", "Fixed keeps the window's title and buttons in a bar "
                                                               "above the picture, so nothing covers the top of the "
                                                               "game; otherwise they appear over the picture together "
                                                               "with the toolbar."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "title bar caption hide frame window buttons"),
                  .live = true});
        v.choice("toolbar", m.toolbar, kToolbarModeNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Toolbar"),
                  .description = GS_TRANSLATE_NOOP("Settings", "Rest the pointer at the top of the mirror window, or "
                                                               "touch the grip there, to show the toolbar; a press "
                                                               "always goes to the phone. Pinned keeps it in a bar "
                                                               "above the picture."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "toolbar title bar pin buttons"),
                  .live = true});
        v.choice("statusCapsule", m.statusCapsule, kStatusCapsuleNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Status capsule"),
                  .description = GS_TRANSLATE_NOOP("Settings", "Shows the frame rate, delay and connection method in "
                                                               "a corner of the mirror window. Automatic shows them "
                                                               "for a few seconds when something changes, and keeps "
                                                               "them while there is a problem."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "fps latency statistics overlay"),
                  .live = true});
        v.boolean("phoneStatus", m.phoneStatus,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Phone status"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Shows the phone's battery level, charging, battery "
                                                                "temperature and CPU use in one line at the top right "
                                                                "of the picture, updated every 2 seconds."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "battery charge charging temperature heat cpu load usage "
                                                             "phone status overlay"),
                   .scope = Scope::Device,
                   .live = true});
        v.boolean("cameraGlow", m.cameraGlow,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Glow while the camera is locked"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Lights the edges of the mirror window while the "
                                                                "mouse turns the camera, so you can tell that the "
                                                                "lock is on."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "camera lock glow edge border outline orange frame"),
                   .live = true});
        v.choice("closeWhileEditing", m.closeWhileEditing, kEditingCloseNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Closing while editing keys"),
                  .description = GS_TRANSLATE_NOOP("Settings", "What the close button of the mirror window does while "
                                                               "you edit the key mapping: ask each time, only leave "
                                                               "editing, or close the mirror window."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "close window edit editing key mapping exit leave ask "
                                                            "remember"),
                  .live = true});
    }

    friend bool operator==(const Display&, const Display&) = default;
};

struct Audio {
    bool enabled = true;
    std::int32_t bitrateKbps = 128;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("enabled", m.enabled,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Sound"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Plays the phone's sound on this computer (Android 11 "
                                                                "and later)."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "audio sound volume"),
                   .scope = Scope::Device});
        v.integer("bitrateKbps", m.bitrateKbps, 32, 512,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Sound bit rate"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Higher sounds better and needs a little more "
                                                                "bandwidth."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "audio quality opus kbps"),
                   .scope = Scope::Device});
    }

    friend bool operator==(const Audio&, const Audio&) = default;
};

struct Keymap {
    bool autoProfile = true;
    bool hints = true;
    std::int32_t hintOpacity = 60;
    bool hintIntro = true;
    std::int32_t hintIntroMs = 3000;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("autoProfile", m.autoProfile,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Switch profiles with the game"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Uses the profile of the game in front on the phone, "
                                                                "or the one you last chose for it."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "keymap profile automatic game app switch"),
                   .live = true});
        v.boolean("hints", m.hints,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Show key hints"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Shows on the picture which key does what."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "keymap overlay labels keys show"),
                   .live = true});
        v.integer("hintOpacity", m.hintOpacity, 10, 100,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Opacity"),
                   .description = GS_TRANSLATE_NOOP("Settings", "How strongly the hints show over the game once "
                                                                "they have faded."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "keymap hint transparency opacity fade"),
                   .live = true});
        v.boolean("hintIntro", m.hintIntro,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Show in full when the profile changes"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Shows the hints fully for a few seconds when a "
                                                                "profile opens or changes, then fades them."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "keymap hint fade start"),
                   .live = true});
        v.integer("hintIntroMs", m.hintIntroMs, 500, 10'000,
                  {.title = GS_TRANSLATE_NOOP("Settings", "How long to show in full"),
                   .description = GS_TRANSLATE_NOOP("Settings", "How long the hints stay fully shown before they fade "
                                                                "to the set opacity."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "keymap hint fade duration seconds"),
                   .live = true});
    }

    friend bool operator==(const Keymap&, const Keymap&) = default;
};

enum class TouchMethod : std::uint8_t { Inject, Uhid, Aoa, None };
inline constexpr std::array<std::string_view, 4> kTouchMethodNames{"inject", "uhid", "aoa", "none"};

struct Touch {
    static constexpr std::size_t kMaxExtensionLength = 64 + 1 + 64;
    static constexpr std::size_t kMaxDeviceLength = 512;

    TouchMethod method = TouchMethod::Inject;
    std::string extension;
    std::string device;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.choice("method", m.method, kTouchMethodNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Touch method"),
                  .description = GS_TRANSLATE_NOOP("Settings", "How taps and swipes reach the phone. Normal works on "
                                                               "every phone; a UHID touch screen is a virtual touch "
                                                               "screen the phone treats as its own; AOA is a touch "
                                                               "screen, mouse and keyboard on the USB cable that work "
                                                               "even without USB debugging; None only shows the "
                                                               "picture. Takes effect the next time the phone "
                                                               "connects."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "touch uhid inject injection multitouch fingers hid aoa "
                                                            "usb otg accessory winusb zadig driver none no touch "
                                                            "view only"),
                  .scope = Scope::Device});
        v.text("extension", m.extension, kMaxExtensionLength,
               {.title = GS_TRANSLATE_NOOP("Settings", "Touch method from an extension"),
                .description = GS_TRANSLATE_NOOP("Settings", "A touch method an extension adds, chosen after Normal, "
                                                             "UHID and AOA. While the extension is off or fails, "
                                                             "touches go the normal way. Takes effect the next time "
                                                             "the phone connects."),
                .keywords = GS_TRANSLATE_NOOP("Settings", "touch extension dll native esp32 board method"),
                .scope = Scope::Device});
        v.text("device", m.device, kMaxDeviceLength,
               {.title = GS_TRANSLATE_NOOP("Settings", "Device for the touch method"),
                .description = GS_TRANSLATE_NOOP("Settings", "The serial, USB or Bluetooth device a touch method "
                                                             "from an extension sends the touches through, for "
                                                             "methods that ask for one. Takes effect the next time "
                                                             "the phone connects."),
                .keywords = GS_TRANSLATE_NOOP("Settings", "touch device serial usb hid bluetooth box port"),
                .scope = Scope::Device});
    }

    friend bool operator==(const Touch&, const Touch&) = default;
};

enum class PictureSource : std::uint8_t { Phone, Capture, None };
inline constexpr std::array<std::string_view, 3> kPictureSourceNames{"phone", "capture", "none"};

struct Picture {
    PictureSource source = PictureSource::Phone;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.choice("source", m.source, kPictureSourceNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Picture source"),
                  .description = GS_TRANSLATE_NOOP("Settings", "Where the mirror window's picture comes from. Phone "
                                                               "mirroring sends the phone's own screen over USB "
                                                               "debugging or wireless debugging; Capture card shows "
                                                               "the HDMI picture of the capture card chosen for this "
                                                               "phone, while taps and keys still go the way the touch "
                                                               "method says; No picture only controls the phone while "
                                                               "you look at it. Takes effect the next time the phone "
                                                               "is mirrored."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "picture source capture card hdmi screen mirror none no "
                                                            "picture"),
                  .scope = Scope::Device});
    }

    friend bool operator==(const Picture&, const Picture&) = default;
};

struct Capture {
    static constexpr std::size_t kMaxFolderLength = 1024;

    std::string folder;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("folder", m.folder, kMaxFolderLength,
               {.title = GS_TRANSLATE_NOOP("Settings", "Recording location"),
                .description = GS_TRANSLATE_NOOP("Settings", "The folder new recordings go into, each as a folder of "
                                                             "its own; exported videos are saved next to it. "
                                                             "Empty uses GameScrcpy in your Videos folder."),
                .keywords = GS_TRANSLATE_NOOP("Settings", "recording record video folder location save path mp4"),
                .live = true});
    }

    friend bool operator==(const Capture&, const Capture&) = default;
};

struct Companion {
    bool cursor = false;
    bool ime = true;
    bool hints = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("cursor", m.cursor,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Show the cursor on the phone"),
                   .description = GS_TRANSLATE_NOOP("Settings", "While the mouse is over the picture, the phone's own "
                                                                "screen shows where it points, through the GameScrcpy "
                                                                "Companion app. The first time, GameScrcpy offers to "
                                                                "install the app on the phone."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "cursor pointer mouse arrow companion overlay phone"),
                   .scope = Scope::Device,
                   .live = true});
        v.boolean("ime", m.ime,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Type with this computer's keyboard"),
                   .description = GS_TRANSLATE_NOOP("Settings", "While mirroring, the phone uses the GameScrcpy "
                                                                "Companion input method: when an input box opens, the "
                                                                "keys pause and what you type, Chinese included, goes "
                                                                "straight into the box. The phone's own keyboard comes "
                                                                "back when mirroring ends."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "keyboard typing input method ime chat text chinese "
                                                             "companion"),
                   .scope = Scope::Device,
                   .live = true});
        v.boolean("hints", m.hints,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Show key hints on the phone"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Draws the key hints on the phone's own screen too, "
                                                                "as in the mirror window. Without a picture, such as "
                                                                "over AOA, they show there by themselves."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "key hints keymap overlay labels phone companion"),
                   .scope = Scope::Device,
                   .live = true});
    }

    friend bool operator==(const Companion&, const Companion&) = default;
};

struct WindowPlacement {
    static constexpr std::int64_t kLimit = 100'000;

    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    bool maximized = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.integer("x", m.x, -kLimit, kLimit);
        v.integer("y", m.y, -kLimit, kLimit);
        v.integer("width", m.width, 0, kLimit);
        v.integer("height", m.height, 0, kLimit);
        v.boolean("maximized", m.maximized);
    }

    friend bool operator==(const WindowPlacement&, const WindowPlacement&) = default;
};

struct RecentConnection {
    static constexpr std::size_t kMaxNameLength = 256;
    static constexpr std::int64_t kMaxTime = std::int64_t{1} << 40;

    std::string name;
    std::int64_t lastUsed = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("name", m.name, kMaxNameLength);
        v.integer("lastUsed", m.lastUsed, 0, kMaxTime);
    }

    friend bool operator==(const RecentConnection&, const RecentConnection&) = default;
};

struct Wireless {
    static constexpr std::size_t kMaxRecent = 8;
    static constexpr std::size_t kMaxAddressLength = 128;

    bool collapsed = false;
    std::map<std::string, RecentConnection, std::less<>> recent;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("collapsed", m.collapsed);
        v.map("recent", m.recent, kMaxAddressLength);
    }

    friend bool operator==(const Wireless&, const Wireless&) = default;
};

struct OtherVideo {
    bool expanded = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("expanded", m.expanded);
    }

    friend bool operator==(const OtherVideo&, const OtherVideo&) = default;
};

void rememberConnection(Wireless& wireless, const std::string& address, std::int64_t now);

inline constexpr std::size_t kMaxCommandIdLength = 64;

struct Palette {
    static constexpr std::size_t kMaxRecent = 8;

    std::vector<std::string> recent;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.textList("recent", m.recent, kMaxRecent, kMaxCommandIdLength);
    }

    friend bool operator==(const Palette&, const Palette&) = default;
};

struct Hints {
    static constexpr std::size_t kMaxCrashLength = 64;
    static constexpr std::size_t kMaxTours = 16;
    static constexpr std::size_t kMaxTourNameLength = 32;
    static constexpr std::int32_t kMaxLaunches = 1'000'000'000;

    bool mirrorToolbar = false;
    bool setupGuide = false;
    std::string crashNotice;
    std::vector<std::string> tours;
    std::int32_t launches = 0;
    std::int32_t sponsorAsked = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("mirrorToolbar", m.mirrorToolbar);
        v.boolean("setupGuide", m.setupGuide);
        v.text("crashNotice", m.crashNotice, kMaxCrashLength);
        v.textList("tours", m.tours, kMaxTours, kMaxTourNameLength);
        v.integer("launches", m.launches, 0, kMaxLaunches);
        v.integer("sponsorAsked", m.sponsorAsked, 0, kMaxLaunches);
    }

    friend bool operator==(const Hints&, const Hints&) = default;
};

struct Mcp {
    static constexpr std::size_t kMaxClients = 32;
    static constexpr std::size_t kMaxClientLength = 256;

    bool enabled = false;
    std::vector<std::string> clients;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("enabled", m.enabled,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Allow AI to connect through MCP"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Lets AI clients on this computer, such as Claude "
                                                                "Desktop or Cursor, use GameScrcpy through MCP. Each "
                                                                "client asks you once; every change to a profile and "
                                                                "every script run on a phone asks you again. No "
                                                                "network port is opened."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "mcp ai assistant agent claude cursor model llm"),
                   .live = true});
        v.textList("clients", m.clients, kMaxClients, kMaxClientLength);
    }

    friend bool operator==(const Mcp&, const Mcp&) = default;
};

enum class AiService : std::uint8_t { OpenAi, Anthropic, OpenAiResponses };
inline constexpr std::array<std::string_view, 3> kAiServiceNames{"openai", "anthropic", "openaiResponses"};
inline constexpr std::string_view kOpenAiAddress = "https://api.openai.com/v1";
inline constexpr std::string_view kAnthropicAddress = "https://api.anthropic.com";

enum class AiEffort : std::uint8_t { Automatic, Minimal, Low, Medium, High };
inline constexpr std::array<std::string_view, 5> kAiEffortNames{"auto", "minimal", "low", "medium", "high"};

struct AiEndpoint {
    static constexpr std::size_t kMaxAddressLength = 2048;
    static constexpr std::size_t kMaxModelLength = 256;

    std::string address;
    std::string model;
    bool thinking = false;
    AiEffort effort = AiEffort::Automatic;
    bool longContext = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("address", m.address, kMaxAddressLength,
               {.title = GS_TRANSLATE_NOOP("Settings", "Address"),
                .description = GS_TRANSLATE_NOOP("Settings", "Where the service answers; empty uses the service's "
                                                             "own address. The assistant connects only to this "
                                                             "address, through the Windows proxy when there is one."),
                .keywords = GS_TRANSLATE_NOOP("Settings", "ai assistant address url endpoint base api server ollama "
                                                          "proxy"),
                .live = true});
        v.text("model", m.model, kMaxModelLength,
               {.title = GS_TRANSLATE_NOOP("Settings", "Model"),
                .description = GS_TRANSLATE_NOOP("Settings", "The model's name as the service writes it, such as "
                                                             "gpt-4.1, deepseek-chat, qwen-plus, llama3.1 or "
                                                             "claude-sonnet-4-5. A model that understands pictures "
                                                             "can also look at the phone's screen."),
                .keywords = GS_TRANSLATE_NOOP("Settings", "ai assistant model name gpt claude deepseek qwen kimi "
                                                          "llama"),
                .live = true});
        v.boolean("thinking", m.thinking,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Deep thinking"),
                   .description = GS_TRANSLATE_NOOP("Settings", "New conversations let the model think before it "
                                                                "answers, when the model can; the assistant shows the "
                                                                "thinking above the answer. Slower and uses more of "
                                                                "your quota."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "ai assistant think thinking reasoning claude qwen "
                                                             "openai slow quota"),
                   .live = true});
        v.choice("effort", m.effort, kAiEffortNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Reasoning effort"),
                  .description = GS_TRANSLATE_NOOP("Settings", "How hard new conversations let the model think, for "
                                                               "models that take an effort; automatic leaves it to "
                                                               "the service. Higher is slower and uses more of your "
                                                               "quota."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "ai assistant reasoning effort strength minimal low "
                                                            "medium high think gpt claude"),
                  .live = true});
        v.boolean("longContext", m.longContext,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Long context"),
                   .description = GS_TRANSLATE_NOOP("Settings", "New conversations ask models that offer it for a "
                                                                "context of a million tokens, so long conversations "
                                                                "forget less. Costs more past the usual length, and "
                                                                "the account must be allowed to use it."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "ai assistant long context max million tokens 1m "
                                                             "claude sonnet"),
                   .live = true});
    }

    friend bool operator==(const AiEndpoint&, const AiEndpoint&) = default;
};

struct Assistant {
    AiService service = AiService::OpenAi;
    AiEndpoint openai;
    AiEndpoint anthropic;
    AiEndpoint openaiResponses;
    bool explain = true;
    bool scripts = false;
    bool bubble = true;
    bool mirrorBubble = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.choice("service", m.service, kAiServiceNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "Model service"),
                  .description = GS_TRANSLATE_NOOP("Settings", "Compatible with OpenAI covers OpenAI, DeepSeek, Qwen, "
                                                               "Kimi, a local Ollama and many more through the Chat "
                                                               "Completions protocol; OpenAI Responses is OpenAI's "
                                                               "newer protocol; Anthropic is Claude. Each keeps its own "
                                                               "address, model and API key."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "ai assistant llm chat model service openai anthropic "
                                                            "claude deepseek qwen kimi ollama api key"),
                  .live = true});
        v.object("openai", m.openai, Page::Assistant);
        v.object("anthropic", m.anthropic, Page::Assistant);
        v.object("openaiResponses", m.openaiResponses, Page::Assistant);
        v.boolean("explain", m.explain,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Explain what is sent"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Before the first message, shows what the assistant "
                                                                "sends to the service and what it never sends."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "ai assistant privacy data sent disclosure"),
                   .live = true});
        v.boolean("scripts", m.scripts,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Let logic call AI"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Logic can ask the AI service you set up, for example "
                                                                "about a part of the picture. Each call spends your "
                                                                "quota; each logic may ask at most 10 times a minute."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "ai logic script ask question picture quota api model "
                                                             "llm"),
                   .live = true});
        v.boolean("bubble", m.bubble,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Assistant bubble"),
                   .description = GS_TRANSLATE_NOOP("Settings", "A round button in the bottom right corner of each "
                                                                "window opens the assistant. Its shortcut opens it "
                                                                "too, even with the bubble hidden."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "ai assistant bubble chat corner button helper"),
                   .live = true});
        v.boolean("mirrorBubble", m.mirrorBubble,
                  {.title = GS_TRANSLATE_NOOP("Settings", "Bubble in mirror windows"),
                   .description = GS_TRANSLATE_NOOP("Settings", "Shows the bubble in mirror windows too, where the "
                                                                "assistant can look at the picture to answer. The More "
                                                                "menu of a mirror window switches it as well."),
                   .keywords = GS_TRANSLATE_NOOP("Settings", "ai assistant bubble mirror window picture frame"),
                   .live = true});
    }

    friend bool operator==(const Assistant&, const Assistant&) = default;
};

enum class Esp32Connection : std::uint8_t { Serial, Wifi };
inline constexpr std::array<std::string_view, 2> kEsp32ConnectionNames{"serial", "wifi"};
inline constexpr std::string_view kEsp32AutomaticPort = "auto";

struct Esp32 {
    static constexpr std::size_t kMaxPortLength = 16;
    static constexpr std::size_t kMaxAddressLength = 255;

    std::string port = std::string(kEsp32AutomaticPort);
    Esp32Connection link = Esp32Connection::Serial;
    std::string address;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("port", m.port, kMaxPortLength,
               {.title = GS_TRANSLATE_NOOP("Settings", "ESP32 serial port"),
                .description = GS_TRANSLATE_NOOP("Settings", "The serial port of the ESP32 touch box. Automatic tries "
                                                             "the Espressif, CH34x and CP210x ports one after another."),
                .keywords = GS_TRANSLATE_NOOP("Settings", "esp32 serial port com usb touch box"),
                .scope = Scope::Device});
        v.choice("link", m.link, kEsp32ConnectionNames,
                 {.title = GS_TRANSLATE_NOOP("Settings", "ESP32 connection"),
                  .description = GS_TRANSLATE_NOOP("Settings", "Serial uses the board's USB serial port, with the least "
                                                               "delay. Wi-Fi needs the board on the same network and adds "
                                                               "1 to 3 ms."),
                  .keywords = GS_TRANSLATE_NOOP("Settings", "esp32 wifi wireless serial udp connection touch box"),
                  .scope = Scope::Device});
        v.text("address", m.address, kMaxAddressLength,
               {.title = GS_TRANSLATE_NOOP("Settings", "ESP32 address"),
                .description = GS_TRANSLATE_NOOP("Settings", "The board's address on the network. Empty finds it "
                                                             "automatically."),
                .keywords = GS_TRANSLATE_NOOP("Settings", "esp32 wifi address ip mdns touch box"),
                .scope = Scope::Device});
    }

    friend bool operator==(const Esp32&, const Esp32&) = default;
};

using Scalar = std::variant<bool, std::int64_t, double, std::string>;
using Scalars = std::map<std::string, Scalar, std::less<>>;

inline constexpr std::size_t kMaxExtensionIdLength = 64;
inline constexpr std::size_t kMaxExtensionKeyLength = 64;
inline constexpr std::size_t kMaxExtensionTextCharacters = 4096;
inline constexpr std::size_t kMaxExtensionTextLength = 4 * kMaxExtensionTextCharacters;

struct GlobalAddOn {
    static constexpr std::size_t kMaxParamsLength = 16 * 1024;

    bool on = false;
    std::string params;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("on", m.on);
        v.text("params", m.params, kMaxParamsLength);
    }

    friend bool operator==(const GlobalAddOn&, const GlobalAddOn&) = default;
};

struct ExtensionState {
    static constexpr std::size_t kMaxVersionLength = 64;
    static constexpr std::size_t kMaxGranted = 32;
    static constexpr std::size_t kMaxPermissionLength = 64;

    bool enabled = false;
    std::string version;
    std::vector<std::string> granted;
    Scalars values;
    std::map<std::string, GlobalAddOn, std::less<>> addOns;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("enabled", m.enabled);
        v.text("version", m.version, kMaxVersionLength);
        v.textList("granted", m.granted, kMaxGranted, kMaxPermissionLength);
        v.scalars("values", m.values, kMaxExtensionKeyLength, kMaxExtensionTextLength);
        v.map("addOns", m.addOns, kMaxExtensionKeyLength);
    }

    friend bool operator==(const ExtensionState&, const ExtensionState&) = default;
};

using ExtensionStates = std::map<std::string, ExtensionState, std::less<>>;

struct Settings {
    static constexpr std::size_t kMaxCommandIdLength = settings::kMaxCommandIdLength;
    static constexpr std::size_t kMaxKeysLength = 64;

    General general;
    Tray tray;
    Appearance appearance;
    Video video;
    Display display;
    Audio audio;
    Keymap keymap;
    Touch touch;
    Picture picture;
    Capture capture;
    Companion companion;
    WindowPlacement hubWindow;
    WindowPlacement assistantWindow;
    Wireless wireless;
    OtherVideo otherVideo;
    Palette palette;
    Hints hints;
    std::map<std::string, std::string, std::less<>> shortcuts;
    ExtensionStates extensions;
    Mcp mcp;
    Assistant assistant;
    Esp32 esp32;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.constantText("format", kFormat);
        v.constantInteger("version", kVersion);
        v.object("general", m.general, Page::Appearance);
        v.object("tray", m.tray, Page::General);
        v.object("appearance", m.appearance, Page::Appearance);
        v.object("video", m.video, Page::Video);
        v.object("display", m.display, Page::Video);
        v.object("audio", m.audio, Page::Audio);
        v.object("keymap", m.keymap, Page::Input);
        v.object("touch", m.touch, Page::Input);
        v.object("picture", m.picture, Page::Input);
        v.object("capture", m.capture, Page::Video);
        v.object("companion", m.companion, Page::Input);
        v.object("hubWindow", m.hubWindow);
        v.object("assistantWindow", m.assistantWindow);
        v.object("wireless", m.wireless);
        v.object("otherVideo", m.otherVideo);
        v.object("palette", m.palette);
        v.object("hints", m.hints);
        v.textMap("shortcuts", m.shortcuts, kMaxCommandIdLength, kMaxKeysLength);
        v.map("extensions", m.extensions, kMaxExtensionIdLength);
        v.object("mcp", m.mcp, Page::Ai);
        v.object("assistant", m.assistant, Page::Assistant);
        v.object("esp32", m.esp32, Page::Connection);
    }

    friend bool operator==(const Settings&, const Settings&) = default;
};

enum class Kind : std::uint8_t { Choice, Integer, Boolean, Text };

using Value = std::variant<bool, std::int64_t, std::string>;

struct Entry {
    std::string path;
    Page page = Page::None;
    Kind kind = Kind::Boolean;
    Facets facets;
    std::vector<std::string_view> choices;
    std::int64_t min = 0;
    std::int64_t max = 0;
    std::size_t maxLength = 0;
    Value defaultValue;
};

[[nodiscard]] const std::vector<Entry>& catalog();
[[nodiscard]] const Entry* findSetting(std::string_view path);
[[nodiscard]] std::optional<Value> valueOf(const Settings& settings, std::string_view path);
[[nodiscard]] bool assign(Settings& settings, std::string_view path, const Value& value);

inline constexpr std::string_view kDevicesFormat = "gamescrcpy.devices";
inline constexpr int kDevicesVersion = 1;

enum class Transport : std::uint8_t { AdbTunnel, WifiDirect, Auto, Default };
inline constexpr std::array<std::string_view, 4> kTransportNames{"adbTunnel", "wifiDirect", "auto", "default"};

struct DeviceExtension {
    Scalars values;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.scalars("values", m.values, kMaxExtensionKeyLength, kMaxExtensionTextLength);
    }

    friend bool operator==(const DeviceExtension&, const DeviceExtension&) = default;
};

enum class ControlOrientation : std::uint8_t { Profile, Portrait, LandscapeLeft, LandscapeRight };
inline constexpr std::array<std::string_view, 4> kControlOrientationNames{"profile", "portrait", "landscapeLeft",
                                                                          "landscapeRight"};

enum class CaptureLandscape : std::uint8_t { Left, Right };
inline constexpr std::array<std::string_view, 2> kCaptureLandscapeNames{"left", "right"};

enum class CaptureKind : std::uint8_t { Automatic, CaptureCard, NotCaptureCard };
inline constexpr std::array<std::string_view, 3> kCaptureKindNames{"automatic", "captureCard", "notCaptureCard"};

struct CaptureSettings {
    static constexpr std::size_t kMaxTouchLength = 64 + 1 + 64;
    static constexpr std::size_t kMaxPhoneLength = 128;
    static constexpr std::size_t kMaxFormatLength = 64;
    static constexpr std::int64_t kCropScale = 10000;

    std::string touch = "none";
    std::string aoaPhone;
    std::string format;
    bool sound = true;
    CaptureLandscape landscape = CaptureLandscape::Left;
    std::int32_t cropX = 0;
    std::int32_t cropY = 0;
    std::int32_t cropWidth = 0;
    std::int32_t cropHeight = 0;
    CaptureKind kind = CaptureKind::Automatic;
    std::string phone;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.text("touch", m.touch, kMaxTouchLength);
        v.text("aoaPhone", m.aoaPhone, kMaxPhoneLength);
        v.text("format", m.format, kMaxFormatLength);
        v.boolean("sound", m.sound);
        v.choice("landscape", m.landscape, kCaptureLandscapeNames);
        v.integer("cropX", m.cropX, 0, kCropScale);
        v.integer("cropY", m.cropY, 0, kCropScale);
        v.integer("cropWidth", m.cropWidth, 0, kCropScale);
        v.integer("cropHeight", m.cropHeight, 0, kCropScale);
        v.choice("kind", m.kind, kCaptureKindNames);
        v.text("phone", m.phone, kMaxPhoneLength);
    }

    friend bool operator==(const CaptureSettings&, const CaptureSettings&) = default;
};

struct DeviceSettings {
    static constexpr std::size_t kMaxProfileIdLength = 128;
    static constexpr std::size_t kMaxPackageLength = 255;
    static constexpr std::size_t kMaxClosedStarts = 256;
    static constexpr std::int64_t kMaxScreenSide = 16384;
    static constexpr std::size_t kMaxCompanionHex = 64;
    static constexpr std::size_t kMaxCompanionSecret = 2048;

    Transport transport = Transport::Default;
    std::string profile;
    std::map<std::string, std::string, std::less<>> apps;
    std::vector<std::string> closedStarts;
    std::map<std::string, Value, std::less<>> overrides;
    std::map<std::string, DeviceExtension, std::less<>> extensions;
    ControlOrientation controlOrientation = ControlOrientation::Profile;
    std::int32_t screenWidth = 0;
    std::int32_t screenHeight = 0;
    std::string companionPhone;
    std::string companionKey;
    std::string companionPc;
    CaptureSettings capture;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.choice("transport", m.transport, kTransportNames);
        v.text("profile", m.profile, kMaxProfileIdLength);
        v.textMap("apps", m.apps, kMaxPackageLength, kMaxProfileIdLength);
        v.textList("closedStarts", m.closedStarts, kMaxClosedStarts, kMaxPackageLength);
        v.overrides("overrides", m.overrides);
        v.map("extensions", m.extensions, kMaxExtensionIdLength);
        v.choice("controlOrientation", m.controlOrientation, kControlOrientationNames);
        v.integer("screenWidth", m.screenWidth, 0, kMaxScreenSide);
        v.integer("screenHeight", m.screenHeight, 0, kMaxScreenSide);
        v.text("companionPhone", m.companionPhone, kMaxCompanionHex);
        v.text("companionKey", m.companionKey, kMaxCompanionSecret);
        v.text("companionPc", m.companionPc, kMaxCompanionHex);
        v.object("capture", m.capture);
    }

    friend bool operator==(const DeviceSettings&, const DeviceSettings&) = default;
};

[[nodiscard]] Settings effectiveSettings(const Settings& global, const DeviceSettings& device);
[[nodiscard]] bool setOverride(DeviceSettings& device, std::string_view path, const Value& value);
bool clearOverride(DeviceSettings& device, std::string_view path);
bool rememberClosedStart(DeviceSettings& device, std::string_view package);

struct Devices {
    static constexpr std::size_t kMaxIdLength = 256;

    std::map<std::string, DeviceSettings, std::less<>> byId;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.constantText("format", kDevicesFormat);
        v.constantInteger("version", kDevicesVersion);
        v.map("devices", m.byId, kMaxIdLength);
    }

    friend bool operator==(const Devices&, const Devices&) = default;
};

enum class Severity : std::uint8_t { Error, Warning };

enum class Problem : std::uint8_t {
    TooLarge,
    MalformedJson,
    WrongFormat,
    WrongVersion,
    WrongType,
    OutOfRange,
    UnknownName,
    UnknownField,
};

struct Diagnostic {
    Severity severity = Severity::Error;
    Problem problem = Problem::WrongType;
    std::string path;
    std::string detail;

    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

template <typename Document>
struct Loaded {
    Document document;
    std::vector<Diagnostic> diagnostics;
    bool usable = true;
    bool newer = false;
};

template <typename Document>
[[nodiscard]] Loaded<Document> load(std::string_view json);

template <typename Document>
[[nodiscard]] std::string save(const Document& document);

[[nodiscard]] std::string_view problemName(Problem problem) noexcept;

}
