#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gs::script {

inline constexpr std::size_t kMostCaptures = 8;
inline constexpr std::size_t kMostChangeLooks = 256;

[[nodiscard]] std::optional<std::uint32_t> androidKeycode(std::string_view name) noexcept;

using VarValue = std::variant<bool, double, std::string>;

struct Point {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const Point&, const Point&) = default;
};

struct DeviceInfo {
    std::string model;
    int android = 0;
    int width = 0;
    int height = 0;
    int rotation = 0;

    friend bool operator==(const DeviceInfo&, const DeviceInfo&) = default;
};

struct ControlInfo {
    std::string id;
    std::string name;
    std::string type;
    bool enabled = false;
    std::string props;

    friend bool operator==(const ControlInfo&, const ControlInfo&) = default;
};

struct StateInfo {
    std::string id;
    std::string name;
    VarValue value = false;

    friend bool operator==(const StateInfo&, const StateInfo&) = default;
};

struct ControlStatus {
    bool touching = false;
    Point stick;
    bool aiming = false;
    bool walking = false;
    bool cancelled = false;
    int cell = -1;

    friend bool operator==(const ControlStatus&, const ControlStatus&) = default;
};

[[nodiscard]] std::string valueJson(const VarValue& value);

enum class PromptKind : std::uint8_t { Confirm, Choose, Ask };

struct Prompt {
    PromptKind kind = PromptKind::Confirm;
    std::string question;
    std::vector<std::string> choices;
    std::string initial;
    std::string ok;
    std::string cancel;

    friend bool operator==(const Prompt&, const Prompt&) = default;
};

enum class DrawingKind : std::uint8_t { Box, Text, Mark, Image };

struct Drawing {
    DrawingKind kind = DrawingKind::Box;
    Point at;
    double width = 0.0;
    double height = 0.0;
    std::string text;
    std::string color;
    std::string image;
    std::uint64_t capture = 0;

    friend bool operator==(const Drawing&, const Drawing&) = default;
};

struct Region {
    double x = 0.0;
    double y = 0.0;
    double width = 1.0;
    double height = 1.0;

    friend bool operator==(const Region&, const Region&) = default;
};

enum class MatchMode : std::uint8_t { Grey, Color };

struct PictureSize {
    int width = 0;
    int height = 0;

    friend bool operator==(const PictureSize&, const PictureSize&) = default;
};

struct Rgb {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    friend bool operator==(const Rgb&, const Rgb&) = default;
};

struct FindQuery {
    std::string image;
    bool all = false;
    double threshold = 0.8;
    int most = 16;
    Region region;
    double timeoutMs = 2000.0;
    std::string file;
    MatchMode match = MatchMode::Grey;
    PictureSize source;
    std::uint64_t capture = 0;

    friend bool operator==(const FindQuery&, const FindQuery&) = default;
};

struct CaptureQuery {
    Region region;
    double timeoutMs = 2000.0;

    friend bool operator==(const CaptureQuery&, const CaptureQuery&) = default;
};

struct ChangeQuery {
    std::string key;
    Region region;
    double threshold = 0.05;
    double timeoutMs = 2000.0;

    friend bool operator==(const ChangeQuery&, const ChangeQuery&) = default;
};

struct ColorQuery {
    std::string color;
    Rgb rgb;
    double tolerance = 0.1;
    Region region;
    double timeoutMs = 2000.0;

    friend bool operator==(const ColorQuery&, const ColorQuery&) = default;
};

struct PixelQuery {
    Point at;
    std::string color;
    Rgb rgb;
    double tolerance = 0.1;
    double timeoutMs = 2000.0;
    bool match = false;

    friend bool operator==(const PixelQuery&, const PixelQuery&) = default;
};

struct TextQuery {
    std::string text;
    bool find = false;
    Region region;
    std::string language;
    double timeoutMs = 2000.0;

    friend bool operator==(const TextQuery&, const TextQuery&) = default;
};

struct VisionAnswer {
    std::string json;
    std::string problem;

    friend bool operator==(const VisionAnswer&, const VisionAnswer&) = default;
};

enum class NodeType : std::uint8_t { None, Boolean, Number, String, Point, Region, Color, Duration, Json };

struct NodeParam {
    std::string name;
    NodeType type = NodeType::Number;

    friend bool operator==(const NodeParam&, const NodeParam&) = default;
};

struct NodeInfo {
    std::string extension;
    std::string extensionName;
    std::string name;
    std::vector<NodeParam> params;
    NodeType returns = NodeType::None;
    std::string description;
    std::vector<std::string> permissions;
    bool job = false;

    friend bool operator==(const NodeInfo&, const NodeInfo&) = default;
};

struct NodeValue {
    NodeType type = NodeType::None;
    bool flag = false;
    double number = 0.0;
    Region area{0.0, 0.0, 0.0, 0.0};
    std::string text;

    friend bool operator==(const NodeValue&, const NodeValue&) = default;
};

struct NodeAnswer {
    NodeValue value;
    std::string code;
    std::string problem;

    friend bool operator==(const NodeAnswer&, const NodeAnswer&) = default;
};

inline constexpr std::size_t kMostServiceNameBytes = 64;
inline constexpr std::size_t kMostServiceBytes = std::size_t{1} << 20;

struct ServiceAnswer {
    std::string json = "null";
    std::string code;
    std::string problem;

    friend bool operator==(const ServiceAnswer&, const ServiceAnswer&) = default;
};

[[nodiscard]] std::optional<NodeType> nodeTypeNamed(std::string_view name) noexcept;
[[nodiscard]] std::string_view nodeTypeName(NodeType type) noexcept;
[[nodiscard]] std::string_view nodeScriptType(NodeType type) noexcept;
[[nodiscard]] std::string nodeSignature(const NodeInfo& node);
[[nodiscard]] std::string nodeSegment(std::string_view idPart);
[[nodiscard]] std::vector<std::string> nodeOwner(std::string_view extension);
[[nodiscard]] std::string nodePath(std::string_view extension, std::string_view node);

class ScriptServices
{
public:
    virtual ~ScriptServices() = default;

    [[nodiscard]] virtual DeviceInfo device() = 0;
    virtual void tap(std::uint64_t gesture, Point at, double durationMs) = 0;
    virtual void swipe(std::uint64_t gesture, Point from, Point to, double durationMs) = 0;
    virtual void lift(std::uint64_t) {}
    virtual void key(std::string_view name, bool down) = 0;
    [[nodiscard]] virtual std::vector<ControlInfo> controls() = 0;
    [[nodiscard]] virtual bool pressControl(std::string_view id, bool down) = 0;
    [[nodiscard]] virtual bool enableControl(std::string_view id, bool enabled) = 0;
    [[nodiscard]] virtual std::vector<StateInfo> states() = 0;
    [[nodiscard]] virtual std::optional<VarValue> stateValue(std::string_view id) = 0;
    [[nodiscard]] virtual bool setState(std::string_view id, const VarValue& value) = 0;
    [[nodiscard]] virtual std::optional<std::string> loadStore(std::string_view owner) = 0;
    virtual void saveStore(std::string_view owner, std::string_view contents) = 0;
    virtual void toast(std::string_view) {}
    virtual void ask(std::uint64_t, const Prompt&) {}
    virtual void withdraw(std::uint64_t) {}
    virtual void draw(std::uint64_t, const Drawing&) {}
    virtual void erase(std::uint64_t) {}
    virtual void find(std::uint64_t, const FindQuery&) {}
    virtual void findColor(std::uint64_t, const ColorQuery&) {}
    virtual void pickColor(std::uint64_t, const PixelQuery&) {}
    virtual void capture(std::uint64_t, const CaptureQuery&) {}
    virtual void changed(std::uint64_t, const ChangeQuery&) {}
    virtual void readText(std::uint64_t, const TextQuery&) {}
    virtual void saveCapture(std::uint64_t, std::uint64_t, std::string_view) {}
    virtual void cancelFind(std::uint64_t) {}
    [[nodiscard]] virtual std::optional<Point> cursor() { return std::nullopt; }
    [[nodiscard]] virtual bool keyDown(std::string_view) { return false; }
    [[nodiscard]] virtual bool readsInput() { return true; }
    [[nodiscard]] virtual bool controlAction(std::string_view, std::string_view, Point) { return false; }
    [[nodiscard]] virtual std::optional<std::string> tuneControl(std::string_view, std::string_view)
    {
        return std::nullopt;
    }
    virtual void saveProfile(std::uint64_t) {}
    [[nodiscard]] virtual std::optional<ControlStatus> controlStatus(std::string_view) { return std::nullopt; }
    [[nodiscard]] virtual bool cameraLocked() { return false; }
    [[nodiscard]] virtual bool claimKey(std::string_view) { return true; }
    virtual void handOver(std::string_view, bool) {}
    virtual void type(std::string_view) {}
    virtual void hold(std::uint64_t, Point) {}
    virtual void move(std::uint64_t, Point, double) {}
    virtual void liftAll() {}
    [[nodiscard]] virtual std::optional<std::string> globalValue(std::string_view) { return std::nullopt; }
    virtual void setGlobal(std::string_view, std::string_view) {}
    [[nodiscard]] virtual std::string app(std::string_view, std::string_view) { return "null"; }
    [[nodiscard]] virtual bool connected() { return true; }
    virtual void awaitConnection(std::uint64_t) {}
    virtual void readClipboard(std::uint64_t) {}
    virtual void writeClipboard(std::uint64_t, std::string_view) {}
    [[nodiscard]] virtual std::vector<NodeInfo> nodes() { return {}; }
    [[nodiscard]] virtual NodeAnswer callNode(std::string_view, std::string_view, const std::vector<NodeValue>&)
    {
        return {{}, "GS-API-060", "no extension offers this node now"};
    }
    [[nodiscard]] virtual bool offersService(std::string_view) { return false; }
    [[nodiscard]] virtual ServiceAnswer callService(std::string_view, std::string_view)
    {
        return {"null", "GS-API-070", "no part of GameScrcpy offers this service"};
    }
    virtual void requestService(std::uint64_t, std::string_view, std::string_view) {}
    virtual void dropService(std::uint64_t) {}
};

}
