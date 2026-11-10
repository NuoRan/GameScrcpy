#pragma once

#include <gs/protocol/Constants.h>
#include <gs/protocol/Enums.h>

#include <gs/base/InplaceVector.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace gs::protocol {

enum class MessageType : std::uint16_t {
    ServerHello = 0x8001,
    ClientHello = 0x0002,
    Welcome = 0x8003,
    HandshakeError = 0x8004,
    ChannelOpen = 0x0005,
    ChannelAccepted = 0x8006,

    TouchFrame = 0x0101,
    Key = 0x0102,
    Text = 0x0103,
    Scroll = 0x0104,
    BackOrScreenOn = 0x0105,
    UhidCreate = 0x0110,
    UhidInput = 0x0111,
    UhidDestroy = 0x0112,
    UhidFeature = 0x0113,

    ConfigureVideo = 0x0201,
    SetVideoBitrate = 0x0202,
    SetMaxFps = 0x0203,
    RequestKeyFrame = 0x0204,
    SetVideoPaused = 0x0205,
    ConfigureAudio = 0x0211,

    SetClipboard = 0x0301,
    GetClipboard = 0x0302,
    SetDisplayPower = 0x0303,
    SetOrientation = 0x0304,
    StartApp = 0x0305,
    SystemPanel = 0x0306,
    RequestAppList = 0x0307,
    RequestAppIcon = 0x0308,
    RequestScreenshot = 0x0309,
    SetShowTouches = 0x030A,
    SubscribeStatus = 0x030B,

    Ping = 0x0401,
    Goodbye = 0x0402,
    ReceiverReport = 0x0403,

    UhidOutput = 0x8110,
    UhidStatus = 0x8111,
    ClipboardChanged = 0x8301,
    ClipboardAck = 0x8302,
    DisplayChanged = 0x8303,
    ForegroundAppChanged = 0x8304,
    DeviceStatus = 0x8305,
    Pong = 0x8401,
    ServerGoodbye = 0x8402,
    Notice = 0x8403,

    AppList = 0x8501,
    AppIcon = 0x8502,
    Screenshot = 0x8503,
    RequestFailed = 0x8504,

    CompanionHello = 0x0601,
    CursorState = 0x0602,
    CompanionWelcome = 0x8601,
    OverlayChanged = 0x8602,
    CommitText = 0x0603,
    EditorAction = 0x0604,
    KeyHintLayout = 0x0605,
    KeyHintState = 0x0606,
    InputBoxChanged = 0x8603,
    ScreenshotRequest = 0x0607,
    ScreenshotChunk = 0x8604,
    ScreenshotFailed = 0x8605,
};

using Nonce = std::array<std::uint8_t, 16>;
using Proof = std::array<std::uint8_t, 32>;
using Bytes = std::vector<std::uint8_t>;

struct Display {
    std::uint32_t displayId = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t rotation = 0;
    std::uint16_t densityDpi = 0;
    std::uint32_t refreshMilliHz = 0;
    std::uint16_t safeLeft = 0;
    std::uint16_t safeTop = 0;
    std::uint16_t safeRight = 0;
    std::uint16_t safeBottom = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("displayId", m.displayId);
        v.u16("width", m.width);
        v.u16("height", m.height);
        v.u8Range("rotation", m.rotation, 0, 3);
        v.u16("densityDpi", m.densityDpi);
        v.u32("refreshMilliHz", m.refreshMilliHz);
        v.u16("safeLeft", m.safeLeft);
        v.u16("safeTop", m.safeTop);
        v.u16("safeRight", m.safeRight);
        v.u16("safeBottom", m.safeBottom);
    }

    friend bool operator==(const Display&, const Display&) = default;
};

struct Encoder {
    VideoCodec codec = VideoCodec::H264;
    std::string name;
    bool hardware = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("codec", m.codec);
        v.str8("name", m.name, 128);
        v.boolean("hardware", m.hardware);
    }

    friend bool operator==(const Encoder&, const Encoder&) = default;
};

struct AppEntry {
    std::string package;
    std::string label;
    std::uint64_t versionCode = 0;
    bool system = false;
    bool enabled = false;
    bool game = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.str8("package", m.package, 255);
        v.str8("label", m.label, 255);
        v.u64("versionCode", m.versionCode);
        v.flag8x3("system", m.system, "enabled", m.enabled, "game", m.game);
    }

    friend bool operator==(const AppEntry&, const AppEntry&) = default;
};

struct ServerHello {
    static constexpr MessageType kType = MessageType::ServerHello;
    static constexpr std::string_view kName = "ServerHello";

    std::uint16_t protocolVersion = kProtocolVersion;
    Nonce nonce{};

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.magic(kMagic);
        v.u16("protocolVersion", m.protocolVersion);
        v.bytes("nonce", m.nonce);
    }

    friend bool operator==(const ServerHello&, const ServerHello&) = default;
};

struct ClientHello {
    static constexpr MessageType kType = MessageType::ClientHello;
    static constexpr std::string_view kName = "ClientHello";

    std::uint16_t protocolVersion = kProtocolVersion;
    std::string clientVersion;
    Proof proof{};

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("protocolVersion", m.protocolVersion);
        v.str8("clientVersion", m.clientVersion, 32);
        v.bytes("proof", m.proof);
    }

    friend bool operator==(const ClientHello&, const ClientHello&) = default;
};

struct Welcome {
    static constexpr MessageType kType = MessageType::Welcome;
    static constexpr std::string_view kName = "Welcome";
    static constexpr std::uint32_t kCapabilityMask = 0x1FFF;
    static constexpr std::uint32_t kClipboard = 1u << 0;
    static constexpr std::uint32_t kText = 1u << 1;
    static constexpr std::uint32_t kUhid = 1u << 2;
    static constexpr std::uint32_t kDisplayPower = 1u << 3;
    static constexpr std::uint32_t kRotation = 1u << 4;
    static constexpr std::uint32_t kApps = 1u << 5;
    static constexpr std::uint32_t kScreenshot = 1u << 6;
    static constexpr std::uint32_t kForegroundApp = 1u << 7;
    static constexpr std::uint32_t kVirtualDisplay = 1u << 8;
    static constexpr std::uint32_t kShowTouches = 1u << 9;
    static constexpr std::uint32_t kIndependentDisplay = 1u << 10;
    static constexpr std::uint32_t kDeviceStatus = 1u << 11;
    static constexpr std::uint32_t kUhidGamepad = 1u << 12;

    std::string serverVersion;
    std::string manufacturer;
    std::string model;
    std::string androidRelease;
    std::uint16_t sdkInt = 0;
    std::string serialNo;
    std::vector<Display> displays;
    std::vector<Encoder> encoders;
    AudioStatus audioStatus = AudioStatus::Available;
    std::uint32_t capabilities = 0;
    bool resumed = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.str8("serverVersion", m.serverVersion, 32);
        v.str8("manufacturer", m.manufacturer, 64);
        v.str8("model", m.model, 64);
        v.str8("androidRelease", m.androidRelease, 32);
        v.u16("sdkInt", m.sdkInt);
        v.str8("serialNo", m.serialNo, 64);
        v.list8("displays", m.displays, 0, 16);
        v.list8("encoders", m.encoders, 0, 32);
        v.enumeration("audioStatus", m.audioStatus);
        v.u32Mask("capabilities", m.capabilities, kCapabilityMask);
        v.boolean("resumed", m.resumed);
    }

    friend bool operator==(const Welcome&, const Welcome&) = default;
};

struct HandshakeError {
    static constexpr MessageType kType = MessageType::HandshakeError;
    static constexpr std::string_view kName = "HandshakeError";

    ErrorCode code = ErrorCode::None;
    std::string message;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("code", m.code);
        v.str16("message", m.message, 1024);
    }

    friend bool operator==(const HandshakeError&, const HandshakeError&) = default;
};

struct ChannelOpen {
    static constexpr MessageType kType = MessageType::ChannelOpen;
    static constexpr std::string_view kName = "ChannelOpen";

    ChannelId channel = ChannelId::Video;
    Proof proof{};

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("channel", m.channel);
        v.bytes("proof", m.proof);
    }

    friend bool operator==(const ChannelOpen&, const ChannelOpen&) = default;
};

struct ChannelAccepted {
    static constexpr MessageType kType = MessageType::ChannelAccepted;
    static constexpr std::string_view kName = "ChannelAccepted";

    ChannelId channel = ChannelId::Video;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("channel", m.channel);
    }

    friend bool operator==(const ChannelAccepted&, const ChannelAccepted&) = default;
};

struct TouchPoint {
    TouchAction action = TouchAction::Down;
    std::uint8_t pointerId = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t pressure = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("action", m.action);
        v.u8Range("pointerId", m.pointerId, 0, kMaxPointerId);
        v.u16("x", m.x);
        v.u16("y", m.y);
        v.u16("pressure", m.pressure);
    }

    friend bool operator==(const TouchPoint&, const TouchPoint&) = default;
};

struct TouchFrame {
    static constexpr MessageType kType = MessageType::TouchFrame;
    static constexpr std::string_view kName = "TouchFrame";
    static constexpr std::size_t kMaxPoints = kMaxPointerId + 1;

    std::uint16_t videoSession = 0;
    InplaceVector<TouchPoint, kMaxPoints> points;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("videoSession", m.videoSession);
        v.list8("points", m.points, 1, kMaxPoints);
    }

    static bool isValid(const TouchFrame& m) noexcept
    {
        std::uint32_t seen = 0;
        for (const TouchPoint& point : m.points) {
            if (point.pointerId > kMaxPointerId) {
                return false;
            }
            const std::uint32_t bit = 1U << point.pointerId;
            if ((seen & bit) != 0U) {
                return false;
            }
            seen |= bit;
        }
        return true;
    }

    friend bool operator==(const TouchFrame&, const TouchFrame&) = default;
};

struct Key {
    static constexpr MessageType kType = MessageType::Key;
    static constexpr std::string_view kName = "Key";

    KeyAction action = KeyAction::Down;
    std::uint32_t keycode = 0;
    std::uint32_t metaState = 0;
    std::uint16_t repeat = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("action", m.action);
        v.u32("keycode", m.keycode);
        v.u32("metaState", m.metaState);
        v.u16("repeat", m.repeat);
    }

    friend bool operator==(const Key&, const Key&) = default;
};

struct Text {
    static constexpr MessageType kType = MessageType::Text;
    static constexpr std::string_view kName = "Text";

    std::string text;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.str16("text", m.text, 4096);
    }

    friend bool operator==(const Text&, const Text&) = default;
};

struct Scroll {
    static constexpr MessageType kType = MessageType::Scroll;
    static constexpr std::string_view kName = "Scroll";

    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t videoSession = 0;
    std::int16_t hScroll = 0;
    std::int16_t vScroll = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("x", m.x);
        v.u16("y", m.y);
        v.u16("videoSession", m.videoSession);
        v.i16("hScroll", m.hScroll);
        v.i16("vScroll", m.vScroll);
    }

    friend bool operator==(const Scroll&, const Scroll&) = default;
};

struct BackOrScreenOn {
    static constexpr MessageType kType = MessageType::BackOrScreenOn;
    static constexpr std::string_view kName = "BackOrScreenOn";

    KeyAction action = KeyAction::Down;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("action", m.action);
    }

    friend bool operator==(const BackOrScreenOn&, const BackOrScreenOn&) = default;
};

struct UhidCreate {
    static constexpr MessageType kType = MessageType::UhidCreate;
    static constexpr std::string_view kName = "UhidCreate";
    static constexpr std::uint8_t kBusUsb = 0x03;
    static constexpr std::uint8_t kBusBluetooth = 0x05;

    std::uint16_t id = 0;
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::string name;
    Bytes reportDescriptor;
    std::uint8_t bus = kBusUsb;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("id", m.id);
        v.u16("vendorId", m.vendorId);
        v.u16("productId", m.productId);
        v.str8("name", m.name, 127);
        v.blob16("reportDescriptor", m.reportDescriptor, 4096);
        v.u8("bus", m.bus);
    }

    friend bool operator==(const UhidCreate&, const UhidCreate&) = default;
};

struct UhidInput {
    static constexpr MessageType kType = MessageType::UhidInput;
    static constexpr std::string_view kName = "UhidInput";

    std::uint16_t id = 0;
    Bytes report;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("id", m.id);
        v.blob16("report", m.report, 512);
    }

    friend bool operator==(const UhidInput&, const UhidInput&) = default;
};

struct UhidDestroy {
    static constexpr MessageType kType = MessageType::UhidDestroy;
    static constexpr std::string_view kName = "UhidDestroy";

    std::uint16_t id = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("id", m.id);
    }

    friend bool operator==(const UhidDestroy&, const UhidDestroy&) = default;
};

struct UhidFeature {
    static constexpr MessageType kType = MessageType::UhidFeature;
    static constexpr std::string_view kName = "UhidFeature";

    std::uint16_t id = 0;
    std::uint8_t reportNumber = 0;
    Bytes data;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("id", m.id);
        v.u8("reportNumber", m.reportNumber);
        v.blob16("data", m.data, 512);
    }

    friend bool operator==(const UhidFeature&, const UhidFeature&) = default;
};

struct ConfigureVideo {
    static constexpr MessageType kType = MessageType::ConfigureVideo;
    static constexpr std::string_view kName = "ConfigureVideo";

    VideoSource source = VideoSource::Display;
    std::uint32_t displayId = 0;
    std::uint16_t virtualWidth = 0;
    std::uint16_t virtualHeight = 0;
    std::uint16_t virtualDensity = 0;
    VideoCodec codec = VideoCodec::H264;
    std::uint16_t maxSize = 0;
    std::uint16_t maxFps = 0;
    std::uint32_t bitrate = 0;
    std::uint16_t intraRefreshMs = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("source", m.source);
        v.u32("displayId", m.displayId);
        v.u16("virtualWidth", m.virtualWidth);
        v.u16("virtualHeight", m.virtualHeight);
        v.u16("virtualDensity", m.virtualDensity);
        v.enumeration("codec", m.codec);
        v.u16("maxSize", m.maxSize);
        v.u16("maxFps", m.maxFps);
        v.u32("bitrate", m.bitrate);
        v.u16("intraRefreshMs", m.intraRefreshMs);
    }

    friend bool operator==(const ConfigureVideo&, const ConfigureVideo&) = default;
};

struct SetVideoBitrate {
    static constexpr MessageType kType = MessageType::SetVideoBitrate;
    static constexpr std::string_view kName = "SetVideoBitrate";

    std::uint32_t bitrate = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("bitrate", m.bitrate);
    }

    friend bool operator==(const SetVideoBitrate&, const SetVideoBitrate&) = default;
};

struct SetMaxFps {
    static constexpr MessageType kType = MessageType::SetMaxFps;
    static constexpr std::string_view kName = "SetMaxFps";

    std::uint16_t maxFps = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("maxFps", m.maxFps);
    }

    friend bool operator==(const SetMaxFps&, const SetMaxFps&) = default;
};

struct RequestKeyFrame {
    static constexpr MessageType kType = MessageType::RequestKeyFrame;
    static constexpr std::string_view kName = "RequestKeyFrame";

    template <typename V, typename Self>
    static void visit(V&, Self&)
    {
    }

    friend bool operator==(const RequestKeyFrame&, const RequestKeyFrame&) = default;
};

struct SetVideoPaused {
    static constexpr MessageType kType = MessageType::SetVideoPaused;
    static constexpr std::string_view kName = "SetVideoPaused";

    bool paused = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("paused", m.paused);
    }

    friend bool operator==(const SetVideoPaused&, const SetVideoPaused&) = default;
};

struct ConfigureAudio {
    static constexpr MessageType kType = MessageType::ConfigureAudio;
    static constexpr std::string_view kName = "ConfigureAudio";

    bool enabled = false;
    std::uint32_t bitrate = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("enabled", m.enabled);
        v.u32("bitrate", m.bitrate);
    }

    friend bool operator==(const ConfigureAudio&, const ConfigureAudio&) = default;
};

struct SetClipboard {
    static constexpr MessageType kType = MessageType::SetClipboard;
    static constexpr std::string_view kName = "SetClipboard";

    std::uint32_t sequence = 0;
    bool paste = false;
    std::string text;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("sequence", m.sequence);
        v.boolean("paste", m.paste);
        v.str32("text", m.text, 262000);
    }

    friend bool operator==(const SetClipboard&, const SetClipboard&) = default;
};

struct GetClipboard {
    static constexpr MessageType kType = MessageType::GetClipboard;
    static constexpr std::string_view kName = "GetClipboard";

    CopyKey copyKey = CopyKey::None;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("copyKey", m.copyKey);
    }

    friend bool operator==(const GetClipboard&, const GetClipboard&) = default;
};

struct SetDisplayPower {
    static constexpr MessageType kType = MessageType::SetDisplayPower;
    static constexpr std::string_view kName = "SetDisplayPower";

    bool on = true;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("on", m.on);
    }

    friend bool operator==(const SetDisplayPower&, const SetDisplayPower&) = default;
};

struct SetOrientation {
    static constexpr MessageType kType = MessageType::SetOrientation;
    static constexpr std::string_view kName = "SetOrientation";

    OrientationMode mode = OrientationMode::Follow;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("mode", m.mode);
    }

    friend bool operator==(const SetOrientation&, const SetOrientation&) = default;
};

struct StartApp {
    static constexpr MessageType kType = MessageType::StartApp;
    static constexpr std::string_view kName = "StartApp";

    std::string package;
    bool forceStop = false;
    bool onVideoDisplay = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.str8("package", m.package, 255);
        v.flag8x2("forceStop", m.forceStop, "onVideoDisplay", m.onVideoDisplay);
    }

    friend bool operator==(const StartApp&, const StartApp&) = default;
};

struct SystemPanel {
    static constexpr MessageType kType = MessageType::SystemPanel;
    static constexpr std::string_view kName = "SystemPanel";

    SystemPanelAction action = SystemPanelAction::ExpandNotifications;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("action", m.action);
    }

    friend bool operator==(const SystemPanel&, const SystemPanel&) = default;
};

struct RequestAppList {
    static constexpr MessageType kType = MessageType::RequestAppList;
    static constexpr std::string_view kName = "RequestAppList";

    std::uint32_t requestId = 0;
    bool includeSystem = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("requestId", m.requestId);
        v.boolean("includeSystem", m.includeSystem);
    }

    friend bool operator==(const RequestAppList&, const RequestAppList&) = default;
};

struct RequestAppIcon {
    static constexpr MessageType kType = MessageType::RequestAppIcon;
    static constexpr std::string_view kName = "RequestAppIcon";

    std::uint32_t requestId = 0;
    std::string package;
    std::uint16_t sizePx = 96;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("requestId", m.requestId);
        v.str8("package", m.package, 255);
        v.u16Range("sizePx", m.sizePx, 16, 512);
    }

    friend bool operator==(const RequestAppIcon&, const RequestAppIcon&) = default;
};

struct RequestScreenshot {
    static constexpr MessageType kType = MessageType::RequestScreenshot;
    static constexpr std::string_view kName = "RequestScreenshot";

    std::uint32_t requestId = 0;
    std::uint32_t displayId = 0;
    ScreenshotFormat format = ScreenshotFormat::Png;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("requestId", m.requestId);
        v.u32("displayId", m.displayId);
        v.enumeration("format", m.format);
    }

    friend bool operator==(const RequestScreenshot&, const RequestScreenshot&) = default;
};

struct SetShowTouches {
    static constexpr MessageType kType = MessageType::SetShowTouches;
    static constexpr std::string_view kName = "SetShowTouches";

    bool on = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("on", m.on);
    }

    friend bool operator==(const SetShowTouches&, const SetShowTouches&) = default;
};

struct SubscribeStatus {
    static constexpr MessageType kType = MessageType::SubscribeStatus;
    static constexpr std::string_view kName = "SubscribeStatus";
    static constexpr std::uint16_t kMinPeriodMs = 500;
    static constexpr std::uint16_t kMaxPeriodMs = 60000;

    bool on = false;
    std::uint16_t periodMs = kMinPeriodMs;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.boolean("on", m.on);
        v.u16Range("periodMs", m.periodMs, kMinPeriodMs, kMaxPeriodMs);
    }

    friend bool operator==(const SubscribeStatus&, const SubscribeStatus&) = default;
};

struct Ping {
    static constexpr MessageType kType = MessageType::Ping;
    static constexpr std::string_view kName = "Ping";

    std::uint32_t nonce = 0;
    std::uint64_t clientTimeUs = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("nonce", m.nonce);
        v.u64("clientTimeUs", m.clientTimeUs);
    }

    friend bool operator==(const Ping&, const Ping&) = default;
};

struct Goodbye {
    static constexpr MessageType kType = MessageType::Goodbye;
    static constexpr std::string_view kName = "Goodbye";

    ErrorCode reason = ErrorCode::None;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("reason", m.reason);
    }

    friend bool operator==(const Goodbye&, const Goodbye&) = default;
};

struct ReceiverReport {
    static constexpr MessageType kType = MessageType::ReceiverReport;
    static constexpr std::string_view kName = "ReceiverReport";

    std::uint16_t videoSession = 0;
    std::uint32_t intervalUs = 0;
    std::uint16_t frames = 0;
    std::uint32_t bytes = 0;
    std::int64_t delayMinUs = 0;
    std::int64_t delayMeanUs = 0;
    std::int64_t delayMaxUs = 0;
    std::uint32_t roundTripUs = 0;
    std::uint32_t lostPackets = 0;
    std::uint32_t recoveredPackets = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("videoSession", m.videoSession);
        v.u32("intervalUs", m.intervalUs);
        v.u16("frames", m.frames);
        v.u32("bytes", m.bytes);
        v.i64("delayMinUs", m.delayMinUs);
        v.i64("delayMeanUs", m.delayMeanUs);
        v.i64("delayMaxUs", m.delayMaxUs);
        v.u32("roundTripUs", m.roundTripUs);
        v.u32("lostPackets", m.lostPackets);
        v.u32("recoveredPackets", m.recoveredPackets);
    }

    friend bool operator==(const ReceiverReport&, const ReceiverReport&) = default;
};

struct UhidOutput {
    static constexpr MessageType kType = MessageType::UhidOutput;
    static constexpr std::string_view kName = "UhidOutput";

    std::uint16_t id = 0;
    Bytes data;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("id", m.id);
        v.blob16("data", m.data, 512);
    }

    friend bool operator==(const UhidOutput&, const UhidOutput&) = default;
};

struct UhidStatus {
    static constexpr MessageType kType = MessageType::UhidStatus;
    static constexpr std::string_view kName = "UhidStatus";

    std::uint16_t id = 0;
    bool recognized = false;
    bool vibration = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("id", m.id);
        v.boolean("recognized", m.recognized);
        v.boolean("vibration", m.vibration);
    }

    friend bool operator==(const UhidStatus&, const UhidStatus&) = default;
};

struct ClipboardChanged {
    static constexpr MessageType kType = MessageType::ClipboardChanged;
    static constexpr std::string_view kName = "ClipboardChanged";

    std::string text;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.str32("text", m.text, 262000);
    }

    friend bool operator==(const ClipboardChanged&, const ClipboardChanged&) = default;
};

struct ClipboardAck {
    static constexpr MessageType kType = MessageType::ClipboardAck;
    static constexpr std::string_view kName = "ClipboardAck";

    std::uint32_t sequence = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("sequence", m.sequence);
    }

    friend bool operator==(const ClipboardAck&, const ClipboardAck&) = default;
};

struct DisplayChanged {
    static constexpr MessageType kType = MessageType::DisplayChanged;
    static constexpr std::string_view kName = "DisplayChanged";

    Display display;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.object("display", m.display);
    }

    friend bool operator==(const DisplayChanged&, const DisplayChanged&) = default;
};

struct ForegroundAppChanged {
    static constexpr MessageType kType = MessageType::ForegroundAppChanged;
    static constexpr std::string_view kName = "ForegroundAppChanged";

    std::string package;
    std::string activity;
    bool game = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.str8("package", m.package, 255);
        v.str8("activity", m.activity, 255);
        v.flag8("game", m.game);
    }

    friend bool operator==(const ForegroundAppChanged&, const ForegroundAppChanged&) = default;
};

struct DeviceStatus {
    static constexpr MessageType kType = MessageType::DeviceStatus;
    static constexpr std::string_view kName = "DeviceStatus";
    static constexpr std::uint8_t kMaxBattery = 100;
    static constexpr std::uint8_t kUnknownBattery = 255;
    static constexpr std::int16_t kUnknownTemperature = std::numeric_limits<std::int16_t>::min();
    static constexpr std::uint16_t kMaxCpu = 1000;
    static constexpr std::uint16_t kUnknownCpu = 65535;

    std::uint8_t batteryPercent = kUnknownBattery;
    bool charging = false;
    std::int16_t temperatureDeciC = kUnknownTemperature;
    std::uint16_t cpuPermille = kUnknownCpu;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u8("batteryPercent", m.batteryPercent);
        v.boolean("charging", m.charging);
        v.i16("temperatureDeciC", m.temperatureDeciC);
        v.u16("cpuPermille", m.cpuPermille);
    }

    static bool isValid(const DeviceStatus& m) noexcept
    {
        return (m.batteryPercent <= kMaxBattery || m.batteryPercent == kUnknownBattery) &&
               (m.cpuPermille <= kMaxCpu || m.cpuPermille == kUnknownCpu);
    }

    friend bool operator==(const DeviceStatus&, const DeviceStatus&) = default;
};

struct Pong {
    static constexpr MessageType kType = MessageType::Pong;
    static constexpr std::string_view kName = "Pong";

    std::uint32_t nonce = 0;
    std::uint64_t clientTimeUs = 0;
    std::uint64_t serverTimeUs = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("nonce", m.nonce);
        v.u64("clientTimeUs", m.clientTimeUs);
        v.u64("serverTimeUs", m.serverTimeUs);
    }

    friend bool operator==(const Pong&, const Pong&) = default;
};

struct ServerGoodbye {
    static constexpr MessageType kType = MessageType::ServerGoodbye;
    static constexpr std::string_view kName = "ServerGoodbye";

    ErrorCode reason = ErrorCode::None;
    std::string message;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("reason", m.reason);
        v.str16("message", m.message, 1024);
    }

    friend bool operator==(const ServerGoodbye&, const ServerGoodbye&) = default;
};

struct Notice {
    static constexpr MessageType kType = MessageType::Notice;
    static constexpr std::string_view kName = "Notice";

    NoticeLevel level = NoticeLevel::Info;
    ErrorCode code = ErrorCode::None;
    std::string message;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("level", m.level);
        v.enumeration("code", m.code);
        v.str16("message", m.message, 1024);
    }

    friend bool operator==(const Notice&, const Notice&) = default;
};

struct AppList {
    static constexpr MessageType kType = MessageType::AppList;
    static constexpr std::string_view kName = "AppList";

    std::uint32_t requestId = 0;
    std::vector<AppEntry> apps;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("requestId", m.requestId);
        v.list16("apps", m.apps, 0, 4096);
    }

    friend bool operator==(const AppList&, const AppList&) = default;
};

struct AppIcon {
    static constexpr MessageType kType = MessageType::AppIcon;
    static constexpr std::string_view kName = "AppIcon";

    std::uint32_t requestId = 0;
    std::string package;
    Bytes png;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("requestId", m.requestId);
        v.str8("package", m.package, 255);
        v.blob32("png", m.png, 1024U * 1024U);
    }

    friend bool operator==(const AppIcon&, const AppIcon&) = default;
};

struct Screenshot {
    static constexpr MessageType kType = MessageType::Screenshot;
    static constexpr std::string_view kName = "Screenshot";

    std::uint32_t requestId = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    ScreenshotFormat format = ScreenshotFormat::Png;
    Bytes data;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("requestId", m.requestId);
        v.u16("width", m.width);
        v.u16("height", m.height);
        v.enumeration("format", m.format);
        v.blob32("data", m.data, 48U * 1024U * 1024U);
    }

    friend bool operator==(const Screenshot&, const Screenshot&) = default;
};

struct RequestFailed {
    static constexpr MessageType kType = MessageType::RequestFailed;
    static constexpr std::string_view kName = "RequestFailed";

    std::uint32_t requestId = 0;
    ErrorCode code = ErrorCode::None;
    std::string message;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("requestId", m.requestId);
        v.enumeration("code", m.code);
        v.str16("message", m.message, 1024);
    }

    friend bool operator==(const RequestFailed&, const RequestFailed&) = default;
};

struct CompanionHello {
    static constexpr MessageType kType = MessageType::CompanionHello;
    static constexpr std::string_view kName = "CompanionHello";

    std::uint16_t version = kCompanionVersion;
    Bytes token;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("version", m.version);
        v.blob8("token", m.token, kCompanionTokenSize);
    }

    friend bool operator==(const CompanionHello&, const CompanionHello&) = default;
};

struct CursorState {
    static constexpr MessageType kType = MessageType::CursorState;
    static constexpr std::string_view kName = "CursorState";

    std::uint16_t x = 0;
    std::uint16_t y = 0;
    bool visible = false;
    bool pressed = false;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("x", m.x);
        v.u16("y", m.y);
        v.flag8x2("visible", m.visible, "pressed", m.pressed);
    }

    friend bool operator==(const CursorState&, const CursorState&) = default;
};

struct CompanionWelcome {
    static constexpr MessageType kType = MessageType::CompanionWelcome;
    static constexpr std::string_view kName = "CompanionWelcome";

    std::uint16_t version = kCompanionVersion;
    OverlayStatus overlay = OverlayStatus::Granted;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t rotation = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u16("version", m.version);
        v.enumeration("overlay", m.overlay);
        v.u16("width", m.width);
        v.u16("height", m.height);
        v.u8Range("rotation", m.rotation, 0, 3);
    }

    friend bool operator==(const CompanionWelcome&, const CompanionWelcome&) = default;
};

struct OverlayChanged {
    static constexpr MessageType kType = MessageType::OverlayChanged;
    static constexpr std::string_view kName = "OverlayChanged";

    OverlayStatus overlay = OverlayStatus::Granted;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t rotation = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("overlay", m.overlay);
        v.u16("width", m.width);
        v.u16("height", m.height);
        v.u8Range("rotation", m.rotation, 0, 3);
    }

    friend bool operator==(const OverlayChanged&, const OverlayChanged&) = default;
};

struct CommitText {
    static constexpr MessageType kType = MessageType::CommitText;
    static constexpr std::string_view kName = "CommitText";

    std::string text;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.str16("text", m.text, kCompanionTextLimit);
    }

    friend bool operator==(const CommitText&, const CommitText&) = default;
};

struct EditorAction {
    static constexpr MessageType kType = MessageType::EditorAction;
    static constexpr std::string_view kName = "EditorAction";

    ImeAction action = ImeAction::Unspecified;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("action", m.action);
    }

    friend bool operator==(const EditorAction&, const EditorAction&) = default;
};

struct KeyHint {
    KeyHintShape shape = KeyHintShape::Tap;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::string label;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("shape", m.shape);
        v.u16("x", m.x);
        v.u16("y", m.y);
        v.u16("width", m.width);
        v.u16("height", m.height);
        v.str8("label", m.label, kMaxKeyHintLabel);
    }

    friend bool operator==(const KeyHint&, const KeyHint&) = default;
};

struct KeyHintLayout {
    static constexpr MessageType kType = MessageType::KeyHintLayout;
    static constexpr std::string_view kName = "KeyHintLayout";

    std::vector<KeyHint> hints;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.list8("hints", m.hints, 0, kMaxKeyHints);
    }

    friend bool operator==(const KeyHintLayout&, const KeyHintLayout&) = default;
};

struct KeyHintState {
    static constexpr MessageType kType = MessageType::KeyHintState;
    static constexpr std::string_view kName = "KeyHintState";

    bool shown = false;
    bool faded = false;
    std::uint8_t opacity = 100;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.flag8x2("shown", m.shown, "faded", m.faded);
        v.u8Range("opacity", m.opacity, 0, 100);
    }

    friend bool operator==(const KeyHintState&, const KeyHintState&) = default;
};

struct InputBoxChanged {
    static constexpr MessageType kType = MessageType::InputBoxChanged;
    static constexpr std::string_view kName = "InputBoxChanged";

    bool shown = false;
    bool multiLine = false;
    std::uint32_t inputType = 0;
    ImeAction action = ImeAction::Unspecified;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.flag8x2("shown", m.shown, "multiLine", m.multiLine);
        v.u32("inputType", m.inputType);
        v.enumeration("action", m.action);
    }

    friend bool operator==(const InputBoxChanged&, const InputBoxChanged&) = default;
};

struct ScreenshotRequest {
    static constexpr MessageType kType = MessageType::ScreenshotRequest;
    static constexpr std::string_view kName = "ScreenshotRequest";

    std::uint32_t requestId = 0;
    std::uint16_t maxSide = 0;
    std::uint8_t quality = kDefaultScreenshotQuality;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("requestId", m.requestId);
        v.u16("maxSide", m.maxSide);
        v.u8Range("quality", m.quality, 1, 100);
    }

    friend bool operator==(const ScreenshotRequest&, const ScreenshotRequest&) = default;
};

struct ScreenshotChunk {
    static constexpr MessageType kType = MessageType::ScreenshotChunk;
    static constexpr std::string_view kName = "ScreenshotChunk";

    std::uint32_t requestId = 0;
    std::uint16_t index = 0;
    std::uint16_t total = 1;
    Bytes data;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("requestId", m.requestId);
        v.u16("index", m.index);
        v.u16Range("total", m.total, 1, kMaxScreenshotChunks);
        v.blob16("data", m.data, kMaxScreenshotChunk);
    }

    static bool isValid(const ScreenshotChunk& m) noexcept { return m.index < m.total; }

    friend bool operator==(const ScreenshotChunk&, const ScreenshotChunk&) = default;
};

struct ScreenshotFailed {
    static constexpr MessageType kType = MessageType::ScreenshotFailed;
    static constexpr std::string_view kName = "ScreenshotFailed";

    std::uint32_t requestId = 0;
    ScreenshotProblem reason = ScreenshotProblem::Error;
    std::string message;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.u32("requestId", m.requestId);
        v.enumeration("reason", m.reason);
        v.str16("message", m.message, 1024);
    }

    friend bool operator==(const ScreenshotFailed&, const ScreenshotFailed&) = default;
};

struct VideoSessionInfo {
    static constexpr std::string_view kName = "VideoSession";

    VideoCodec codec = VideoCodec::H264;
    std::uint16_t frameWidth = 0;
    std::uint16_t frameHeight = 0;
    std::uint32_t displayId = 0;
    std::uint16_t displayWidth = 0;
    std::uint16_t displayHeight = 0;
    std::uint8_t rotation = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("codec", m.codec);
        v.u16("frameWidth", m.frameWidth);
        v.u16("frameHeight", m.frameHeight);
        v.u32("displayId", m.displayId);
        v.u16("displayWidth", m.displayWidth);
        v.u16("displayHeight", m.displayHeight);
        v.u8Range("rotation", m.rotation, 0, 3);
    }

    friend bool operator==(const VideoSessionInfo&, const VideoSessionInfo&) = default;
};

struct AudioSessionInfo {
    static constexpr std::string_view kName = "AudioSession";

    AudioCodec codec = AudioCodec::Opus;
    std::uint32_t sampleRate = 0;
    std::uint8_t channels = 0;

    template <typename V, typename Self>
    static void visit(V& v, Self& m)
    {
        v.enumeration("codec", m.codec);
        v.u32("sampleRate", m.sampleRate);
        v.u8("channels", m.channels);
    }

    friend bool operator==(const AudioSessionInfo&, const AudioSessionInfo&) = default;
};

}
