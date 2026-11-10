#pragma once

#include <gs/script/LogicHost.h>
#include <gs/script/Report.h>
#include <gs/script/Services.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::scripthost::detail {

inline constexpr std::uint32_t kProtocol = 1;
inline constexpr std::size_t kMostFrameBytes = std::size_t{40} << 20;
inline constexpr std::uint32_t kMostItems = 1u << 20;
inline constexpr std::uint64_t kMainTaskBase = std::uint64_t{1} << 40;

enum class Kind : std::uint16_t {
    Hello = 1,
    Ready,
    Load,
    Unload,
    Start,
    Cancel,
    Frame,
    FrameRate,
    ControlEvent,
    StateEvent,
    Event,
    Answer,
    Refuse,
    Breakpoints,
    Resume,
    Report,
    Call,
    Post,
    Reply,
    Stop,
    SelfTest,
    SelfTested,
};

enum class Method : std::uint16_t {
    Device = 1,
    Tap,
    Swipe,
    Lift,
    Key,
    Controls,
    PressControl,
    EnableControl,
    States,
    StateValue,
    SetState,
    LoadStore,
    SaveStore,
    Toast,
    Ask,
    Withdraw,
    Draw,
    Erase,
    Find,
    FindColor,
    PickColor,
    Capture,
    Changed,
    ReadText,
    SaveCapture,
    CancelFind,
    Cursor,
    KeyDown,
    ControlAction,
    TuneControl,
    SaveProfile,
    ControlStatus,
    CameraLocked,
    ClaimKey,
    Type,
    Hold,
    Move,
    LiftAll,
    GlobalValue,
    SetGlobal,
    App,
    Connected,
    AwaitConnection,
    ReadClipboard,
    WriteClipboard,
    Nodes,
    CallNode,
    OffersService,
    CallService,
    RequestService,
    DropService,
    HandOver,
};

class Writer
{
public:
    explicit Writer(Kind kind);

    Writer& u8(std::uint8_t value);
    Writer& u16(std::uint16_t value);
    Writer& u32(std::uint32_t value);
    Writer& u64(std::uint64_t value);
    Writer& i32(std::int32_t value);
    Writer& i64(std::int64_t value);
    Writer& f64(double value);
    Writer& flag(bool value);
    Writer& text(std::string_view value);

    [[nodiscard]] const std::string& frame();

private:
    void raw(const void* data, std::size_t size);

    std::string m_bytes;
};

class Reader
{
public:
    explicit Reader(std::string_view payload) noexcept;

    bool u8(std::uint8_t& value);
    bool u16(std::uint16_t& value);
    bool u32(std::uint32_t& value);
    bool u64(std::uint64_t& value);
    bool i32(std::int32_t& value);
    bool i64(std::int64_t& value);
    bool f64(double& value);
    bool flag(bool& value);
    bool text(std::string& value);

    [[nodiscard]] bool done() const noexcept { return m_ok && m_at == m_payload.size(); }

private:
    bool raw(void* data, std::size_t size);

    std::string_view m_payload;
    std::size_t m_at = 0;
    bool m_ok = true;
};

struct Frame {
    Kind kind = Kind::Stop;
    std::string payload;
};

struct HelloMessage {
    script::Limits limits;
    bool services = true;
    std::string label;
};

struct LoadMessage {
    script::ScriptId script = 0;
    script::ScriptSource source;
};

struct SelfTestProbe {
    std::string token;
    std::string file;
    std::uint16_t port = 0;
};

struct SelfTestOutcome {
    std::string name;
    bool refused = false;
    std::uint32_t error = 0;
    std::string detail;
};

void put(Writer& out, const std::string& value);
void put(Writer& out, const script::Point& value);
void put(Writer& out, const script::Region& value);
void put(Writer& out, const script::Rgb& value);
void put(Writer& out, const script::PictureSize& value);
void put(Writer& out, const script::DeviceInfo& value);
void put(Writer& out, const script::ControlInfo& value);
void put(Writer& out, const script::VarValue& value);
void put(Writer& out, const script::StateInfo& value);
void put(Writer& out, const script::ControlStatus& value);
void put(Writer& out, const script::Prompt& value);
void put(Writer& out, const script::Drawing& value);
void put(Writer& out, const script::FindQuery& value);
void put(Writer& out, const script::CaptureQuery& value);
void put(Writer& out, const script::ChangeQuery& value);
void put(Writer& out, const script::ColorQuery& value);
void put(Writer& out, const script::PixelQuery& value);
void put(Writer& out, const script::TextQuery& value);
void put(Writer& out, const script::NodeParam& value);
void put(Writer& out, const script::NodeInfo& value);
void put(Writer& out, const script::NodeValue& value);
void put(Writer& out, const script::NodeAnswer& value);
void put(Writer& out, const script::ServiceAnswer& value);
void put(Writer& out, const script::Report& value);
void put(Writer& out, const script::Limits& value);
void put(Writer& out, const script::ComponentModule& value);
void put(Writer& out, const HelloMessage& value);
void put(Writer& out, const LoadMessage& value);
void put(Writer& out, const SelfTestProbe& value);
void put(Writer& out, const SelfTestOutcome& value);

bool get(Reader& in, std::string& value);
bool get(Reader& in, script::Point& value);
bool get(Reader& in, script::Region& value);
bool get(Reader& in, script::Rgb& value);
bool get(Reader& in, script::PictureSize& value);
bool get(Reader& in, script::DeviceInfo& value);
bool get(Reader& in, script::ControlInfo& value);
bool get(Reader& in, script::VarValue& value);
bool get(Reader& in, script::StateInfo& value);
bool get(Reader& in, script::ControlStatus& value);
bool get(Reader& in, script::Prompt& value);
bool get(Reader& in, script::Drawing& value);
bool get(Reader& in, script::FindQuery& value);
bool get(Reader& in, script::CaptureQuery& value);
bool get(Reader& in, script::ChangeQuery& value);
bool get(Reader& in, script::ColorQuery& value);
bool get(Reader& in, script::PixelQuery& value);
bool get(Reader& in, script::TextQuery& value);
bool get(Reader& in, script::NodeParam& value);
bool get(Reader& in, script::NodeInfo& value);
bool get(Reader& in, script::NodeValue& value);
bool get(Reader& in, script::NodeAnswer& value);
bool get(Reader& in, script::ServiceAnswer& value);
bool get(Reader& in, script::Report& value);
bool get(Reader& in, script::Limits& value);
bool get(Reader& in, script::ComponentModule& value);
bool get(Reader& in, HelloMessage& value);
bool get(Reader& in, LoadMessage& value);
bool get(Reader& in, SelfTestProbe& value);
bool get(Reader& in, SelfTestOutcome& value);

template <typename T>
void put(Writer& out, const std::vector<T>& values)
{
    out.u32(static_cast<std::uint32_t>(values.size()));
    for (const T& value : values) {
        put(out, value);
    }
}

template <typename T>
bool get(Reader& in, std::vector<T>& values)
{
    std::uint32_t count = 0;
    if (!in.u32(count) || count > kMostItems) {
        return false;
    }
    values.clear();
    values.reserve(std::min<std::uint32_t>(count, 4096));
    for (std::uint32_t index = 0; index < count; ++index) {
        T value{};
        if (!get(in, value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

template <typename T>
void put(Writer& out, const std::optional<T>& value)
{
    out.flag(value.has_value());
    if (value) {
        put(out, *value);
    }
}

template <typename T>
bool get(Reader& in, std::optional<T>& value)
{
    bool present = false;
    if (!in.flag(present)) {
        return false;
    }
    if (!present) {
        value.reset();
        return true;
    }
    T read{};
    if (!get(in, read)) {
        return false;
    }
    value = std::move(read);
    return true;
}

[[nodiscard]] bool synchronous(Method method) noexcept;
[[nodiscard]] std::string utf8Of(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path pathOf(std::string_view utf8);

}
