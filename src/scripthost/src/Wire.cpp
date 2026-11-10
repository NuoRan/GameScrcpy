#include "Wire.h"

#include <bit>
#include <cstring>

namespace gs::scripthost::detail {

namespace {

template <typename E>
bool enumIn(Reader& in, E& value, E last)
{
    std::uint8_t number = 0;
    if (!in.u8(number) || number > static_cast<std::uint8_t>(last)) {
        return false;
    }
    value = static_cast<E>(number);
    return true;
}

template <typename E>
void enumOut(Writer& out, E value)
{
    out.u8(static_cast<std::uint8_t>(value));
}

void putMicros(Writer& out, Microseconds value)
{
    out.i64(static_cast<std::int64_t>(value.count()));
}

bool getMicros(Reader& in, Microseconds& value)
{
    std::int64_t count = 0;
    if (!in.i64(count)) {
        return false;
    }
    value = Microseconds{count};
    return true;
}

bool getSize(Reader& in, std::size_t& value)
{
    std::uint64_t number = 0;
    if (!in.u64(number)) {
        return false;
    }
    value = static_cast<std::size_t>(number);
    return true;
}

}

Writer::Writer(Kind kind)
{
    m_bytes.reserve(64);
    m_bytes.resize(sizeof(std::uint32_t));
    u16(static_cast<std::uint16_t>(kind));
}

void Writer::raw(const void* data, std::size_t size)
{
    m_bytes.append(static_cast<const char*>(data), size);
}

Writer& Writer::u8(std::uint8_t value)
{
    raw(&value, sizeof(value));
    return *this;
}

Writer& Writer::u16(std::uint16_t value)
{
    raw(&value, sizeof(value));
    return *this;
}

Writer& Writer::u32(std::uint32_t value)
{
    raw(&value, sizeof(value));
    return *this;
}

Writer& Writer::u64(std::uint64_t value)
{
    raw(&value, sizeof(value));
    return *this;
}

Writer& Writer::i32(std::int32_t value)
{
    raw(&value, sizeof(value));
    return *this;
}

Writer& Writer::i64(std::int64_t value)
{
    raw(&value, sizeof(value));
    return *this;
}

Writer& Writer::f64(double value)
{
    return u64(std::bit_cast<std::uint64_t>(value));
}

Writer& Writer::flag(bool value)
{
    return u8(value ? 1 : 0);
}

Writer& Writer::text(std::string_view value)
{
    u32(static_cast<std::uint32_t>(value.size()));
    raw(value.data(), value.size());
    return *this;
}

const std::string& Writer::frame()
{
    const auto size = static_cast<std::uint32_t>(m_bytes.size() - sizeof(std::uint32_t));
    std::memcpy(m_bytes.data(), &size, sizeof(size));
    return m_bytes;
}

Reader::Reader(std::string_view payload) noexcept
    : m_payload(payload)
{
}

bool Reader::raw(void* data, std::size_t size)
{
    if (!m_ok || m_payload.size() - m_at < size) {
        m_ok = false;
        return false;
    }
    std::memcpy(data, m_payload.data() + m_at, size);
    m_at += size;
    return true;
}

bool Reader::u8(std::uint8_t& value)
{
    return raw(&value, sizeof(value));
}

bool Reader::u16(std::uint16_t& value)
{
    return raw(&value, sizeof(value));
}

bool Reader::u32(std::uint32_t& value)
{
    return raw(&value, sizeof(value));
}

bool Reader::u64(std::uint64_t& value)
{
    return raw(&value, sizeof(value));
}

bool Reader::i32(std::int32_t& value)
{
    return raw(&value, sizeof(value));
}

bool Reader::i64(std::int64_t& value)
{
    return raw(&value, sizeof(value));
}

bool Reader::f64(double& value)
{
    std::uint64_t bits = 0;
    if (!u64(bits)) {
        return false;
    }
    value = std::bit_cast<double>(bits);
    return true;
}

bool Reader::flag(bool& value)
{
    std::uint8_t byte = 0;
    if (!u8(byte) || byte > 1) {
        m_ok = false;
        return false;
    }
    value = byte != 0;
    return true;
}

bool Reader::text(std::string& value)
{
    std::uint32_t size = 0;
    if (!u32(size) || m_payload.size() - m_at < size) {
        m_ok = false;
        return false;
    }
    value.assign(m_payload.data() + m_at, size);
    m_at += size;
    return true;
}

void put(Writer& out, const std::string& value)
{
    out.text(value);
}

bool get(Reader& in, std::string& value)
{
    return in.text(value);
}

void put(Writer& out, const script::Point& value)
{
    out.f64(value.x).f64(value.y);
}

bool get(Reader& in, script::Point& value)
{
    return in.f64(value.x) && in.f64(value.y);
}

void put(Writer& out, const script::Region& value)
{
    out.f64(value.x).f64(value.y).f64(value.width).f64(value.height);
}

bool get(Reader& in, script::Region& value)
{
    return in.f64(value.x) && in.f64(value.y) && in.f64(value.width) && in.f64(value.height);
}

void put(Writer& out, const script::Rgb& value)
{
    out.u8(value.red).u8(value.green).u8(value.blue);
}

bool get(Reader& in, script::Rgb& value)
{
    return in.u8(value.red) && in.u8(value.green) && in.u8(value.blue);
}

void put(Writer& out, const script::PictureSize& value)
{
    out.i32(value.width).i32(value.height);
}

bool get(Reader& in, script::PictureSize& value)
{
    return in.i32(value.width) && in.i32(value.height);
}

void put(Writer& out, const script::DeviceInfo& value)
{
    out.text(value.model).i32(value.android).i32(value.width).i32(value.height).i32(value.rotation);
}

bool get(Reader& in, script::DeviceInfo& value)
{
    return in.text(value.model) && in.i32(value.android) && in.i32(value.width) && in.i32(value.height) &&
           in.i32(value.rotation);
}

void put(Writer& out, const script::ControlInfo& value)
{
    out.text(value.id).text(value.name).text(value.type).flag(value.enabled).text(value.props);
}

bool get(Reader& in, script::ControlInfo& value)
{
    return in.text(value.id) && in.text(value.name) && in.text(value.type) && in.flag(value.enabled) &&
           in.text(value.props);
}

void put(Writer& out, const script::VarValue& value)
{
    if (const bool* on = std::get_if<bool>(&value)) {
        out.u8(0).flag(*on);
    } else if (const double* number = std::get_if<double>(&value)) {
        out.u8(1).f64(*number);
    } else {
        out.u8(2).text(std::get<std::string>(value));
    }
}

bool get(Reader& in, script::VarValue& value)
{
    std::uint8_t tag = 0;
    if (!in.u8(tag)) {
        return false;
    }
    if (tag == 0) {
        bool on = false;
        if (!in.flag(on)) {
            return false;
        }
        value = on;
        return true;
    }
    if (tag == 1) {
        double number = 0.0;
        if (!in.f64(number)) {
            return false;
        }
        value = number;
        return true;
    }
    std::string words;
    if (tag != 2 || !in.text(words)) {
        return false;
    }
    value = std::move(words);
    return true;
}

void put(Writer& out, const script::StateInfo& value)
{
    out.text(value.id).text(value.name);
    put(out, value.value);
}

bool get(Reader& in, script::StateInfo& value)
{
    return in.text(value.id) && in.text(value.name) && get(in, value.value);
}

void put(Writer& out, const script::ControlStatus& value)
{
    out.flag(value.touching);
    put(out, value.stick);
    out.flag(value.aiming).flag(value.walking).flag(value.cancelled).i32(value.cell);
}

bool get(Reader& in, script::ControlStatus& value)
{
    return in.flag(value.touching) && get(in, value.stick) && in.flag(value.aiming) && in.flag(value.walking) &&
           in.flag(value.cancelled) && in.i32(value.cell);
}

void put(Writer& out, const script::Prompt& value)
{
    enumOut(out, value.kind);
    out.text(value.question);
    put(out, value.choices);
    out.text(value.initial).text(value.ok).text(value.cancel);
}

bool get(Reader& in, script::Prompt& value)
{
    return enumIn(in, value.kind, script::PromptKind::Ask) && in.text(value.question) && get(in, value.choices) &&
           in.text(value.initial) && in.text(value.ok) && in.text(value.cancel);
}

void put(Writer& out, const script::Drawing& value)
{
    enumOut(out, value.kind);
    put(out, value.at);
    out.f64(value.width).f64(value.height).text(value.text).text(value.color).text(value.image).u64(value.capture);
}

bool get(Reader& in, script::Drawing& value)
{
    return enumIn(in, value.kind, script::DrawingKind::Image) && get(in, value.at) && in.f64(value.width) &&
           in.f64(value.height) && in.text(value.text) && in.text(value.color) && in.text(value.image) &&
           in.u64(value.capture);
}

void put(Writer& out, const script::FindQuery& value)
{
    out.text(value.image).flag(value.all).f64(value.threshold).i32(value.most);
    put(out, value.region);
    out.f64(value.timeoutMs).text(value.file);
    enumOut(out, value.match);
    put(out, value.source);
    out.u64(value.capture);
}

bool get(Reader& in, script::FindQuery& value)
{
    return in.text(value.image) && in.flag(value.all) && in.f64(value.threshold) && in.i32(value.most) &&
           get(in, value.region) && in.f64(value.timeoutMs) && in.text(value.file) &&
           enumIn(in, value.match, script::MatchMode::Color) && get(in, value.source) && in.u64(value.capture);
}

void put(Writer& out, const script::CaptureQuery& value)
{
    put(out, value.region);
    out.f64(value.timeoutMs);
}

bool get(Reader& in, script::CaptureQuery& value)
{
    return get(in, value.region) && in.f64(value.timeoutMs);
}

void put(Writer& out, const script::ChangeQuery& value)
{
    out.text(value.key);
    put(out, value.region);
    out.f64(value.threshold).f64(value.timeoutMs);
}

bool get(Reader& in, script::ChangeQuery& value)
{
    return in.text(value.key) && get(in, value.region) && in.f64(value.threshold) && in.f64(value.timeoutMs);
}

void put(Writer& out, const script::ColorQuery& value)
{
    out.text(value.color);
    put(out, value.rgb);
    out.f64(value.tolerance);
    put(out, value.region);
    out.f64(value.timeoutMs);
}

bool get(Reader& in, script::ColorQuery& value)
{
    return in.text(value.color) && get(in, value.rgb) && in.f64(value.tolerance) && get(in, value.region) &&
           in.f64(value.timeoutMs);
}

void put(Writer& out, const script::PixelQuery& value)
{
    put(out, value.at);
    out.text(value.color);
    put(out, value.rgb);
    out.f64(value.tolerance).f64(value.timeoutMs).flag(value.match);
}

bool get(Reader& in, script::PixelQuery& value)
{
    return get(in, value.at) && in.text(value.color) && get(in, value.rgb) && in.f64(value.tolerance) &&
           in.f64(value.timeoutMs) && in.flag(value.match);
}

void put(Writer& out, const script::TextQuery& value)
{
    out.text(value.text).flag(value.find);
    put(out, value.region);
    out.text(value.language).f64(value.timeoutMs);
}

bool get(Reader& in, script::TextQuery& value)
{
    return in.text(value.text) && in.flag(value.find) && get(in, value.region) && in.text(value.language) &&
           in.f64(value.timeoutMs);
}

void put(Writer& out, const script::NodeParam& value)
{
    out.text(value.name);
    enumOut(out, value.type);
}

bool get(Reader& in, script::NodeParam& value)
{
    return in.text(value.name) && enumIn(in, value.type, script::NodeType::Json);
}

void put(Writer& out, const script::NodeInfo& value)
{
    out.text(value.extension).text(value.extensionName).text(value.name);
    put(out, value.params);
    enumOut(out, value.returns);
    out.text(value.description);
    put(out, value.permissions);
}

bool get(Reader& in, script::NodeInfo& value)
{
    return in.text(value.extension) && in.text(value.extensionName) && in.text(value.name) &&
           get(in, value.params) && enumIn(in, value.returns, script::NodeType::Json) && in.text(value.description) &&
           get(in, value.permissions);
}

void put(Writer& out, const script::NodeValue& value)
{
    enumOut(out, value.type);
    out.flag(value.flag).f64(value.number);
    put(out, value.area);
    out.text(value.text);
}

bool get(Reader& in, script::NodeValue& value)
{
    return enumIn(in, value.type, script::NodeType::Json) && in.flag(value.flag) && in.f64(value.number) &&
           get(in, value.area) && in.text(value.text);
}

void put(Writer& out, const script::NodeAnswer& value)
{
    put(out, value.value);
    out.text(value.code).text(value.problem);
}

bool get(Reader& in, script::NodeAnswer& value)
{
    return get(in, value.value) && in.text(value.code) && in.text(value.problem);
}

void put(Writer& out, const script::ServiceAnswer& value)
{
    out.text(value.json).text(value.code).text(value.problem);
}

bool get(Reader& in, script::ServiceAnswer& value)
{
    return in.text(value.json) && in.text(value.code) && in.text(value.problem);
}

void put(Writer& out, const script::Report& value)
{
    enumOut(out, value.kind);
    enumOut(out, value.level);
    out.u32(value.script).u64(value.task).text(value.handler).text(value.message);
    out.text(value.location.module).i32(value.location.line).i32(value.location.column).text(value.location.function);
    out.text(value.stack);
    putMicros(out, value.duration);
    putMicros(out, value.limit);
    putMicros(out, value.window);
    out.u64(value.bytes).u64(value.byteLimit);
}

bool get(Reader& in, script::Report& value)
{
    std::int32_t line = 0;
    std::int32_t column = 0;
    std::uint64_t bytes = 0;
    std::uint64_t byteLimit = 0;
    const bool read = enumIn(in, value.kind, script::ReportKind::DebugResumed) &&
                      enumIn(in, value.level, log::Level::Error) && in.u32(value.script) && in.u64(value.task) &&
                      in.text(value.handler) && in.text(value.message) && in.text(value.location.module) &&
                      in.i32(line) && in.i32(column) && in.text(value.location.function) && in.text(value.stack) &&
                      getMicros(in, value.duration) && getMicros(in, value.limit) && getMicros(in, value.window) &&
                      in.u64(bytes) && in.u64(byteLimit);
    value.location.line = line;
    value.location.column = column;
    value.bytes = static_cast<std::size_t>(bytes);
    value.byteLimit = static_cast<std::size_t>(byteLimit);
    return read;
}

void put(Writer& out, const script::Limits& value)
{
    out.u64(value.memoryWarningBytes).u64(value.memoryLimitBytes).u64(value.stackBytes);
    putMicros(out, value.sliceBudget);
    putMicros(out, value.spikeThreshold);
    putMicros(out, value.sustainedWindow);
    putMicros(out, value.sustainedBusy);
}

bool get(Reader& in, script::Limits& value)
{
    return getSize(in, value.memoryWarningBytes) && getSize(in, value.memoryLimitBytes) &&
           getSize(in, value.stackBytes) && getMicros(in, value.sliceBudget) && getMicros(in, value.spikeThreshold) &&
           getMicros(in, value.sustainedWindow) && getMicros(in, value.sustainedBusy);
}

void put(Writer& out, const script::ComponentModule& value)
{
    out.text(value.ref).text(utf8Of(value.folder)).text(value.entry).flag(value.placed);
    put(out, value.children);
}

bool get(Reader& in, script::ComponentModule& value)
{
    std::string folder;
    const bool read = in.text(value.ref) && in.text(folder) && in.text(value.entry) && in.flag(value.placed) &&
                      get(in, value.children);
    value.folder = pathOf(folder);
    return read;
}

void put(Writer& out, const HelloMessage& value)
{
    out.u32(kProtocol);
    put(out, value.limits);
    out.flag(value.services).text(value.label);
}

bool get(Reader& in, HelloMessage& value)
{
    std::uint32_t protocol = 0;
    return in.u32(protocol) && protocol == kProtocol && get(in, value.limits) && in.flag(value.services) &&
           in.text(value.label);
}

void put(Writer& out, const LoadMessage& value)
{
    const script::ScriptSource& source = value.source;
    out.u32(value.script).text(utf8Of(source.folder)).text(source.entry);
    put(out, source.components);
    out.flag(source.files != nullptr);
    if (source.files != nullptr) {
        out.u32(static_cast<std::uint32_t>(source.files->size()));
        for (const auto& [path, contents] : *source.files) {
            out.text(path).text(contents);
        }
    }
    put(out, source.permissions);
    out.flag(source.phone);
    put(out, source.modules);
}

bool get(Reader& in, LoadMessage& value)
{
    script::ScriptSource& source = value.source;
    std::string folder;
    bool hasFiles = false;
    if (!in.u32(value.script) || !in.text(folder) || !in.text(source.entry) || !get(in, source.components) ||
        !in.flag(hasFiles)) {
        return false;
    }
    source.folder = pathOf(folder);
    if (hasFiles) {
        std::uint32_t count = 0;
        if (!in.u32(count) || count > kMostItems) {
            return false;
        }
        script::ScriptFiles files;
        for (std::uint32_t index = 0; index < count; ++index) {
            std::string path;
            std::string contents;
            if (!in.text(path) || !in.text(contents)) {
                return false;
            }
            files.insert_or_assign(std::move(path), std::move(contents));
        }
        source.files = std::make_shared<const script::ScriptFiles>(std::move(files));
    }
    return get(in, source.permissions) && in.flag(source.phone) && get(in, source.modules);
}

void put(Writer& out, const SelfTestProbe& value)
{
    out.text(value.token).text(value.file).u16(value.port);
}

bool get(Reader& in, SelfTestProbe& value)
{
    return in.text(value.token) && in.text(value.file) && in.u16(value.port);
}

void put(Writer& out, const SelfTestOutcome& value)
{
    out.text(value.name).flag(value.refused).u32(value.error).text(value.detail);
}

bool get(Reader& in, SelfTestOutcome& value)
{
    return in.text(value.name) && in.flag(value.refused) && in.u32(value.error) && in.text(value.detail);
}

bool synchronous(Method method) noexcept
{
    switch (method) {
    case Method::Device:
    case Method::Controls:
    case Method::PressControl:
    case Method::EnableControl:
    case Method::States:
    case Method::StateValue:
    case Method::SetState:
    case Method::LoadStore:
    case Method::Cursor:
    case Method::KeyDown:
    case Method::ControlAction:
    case Method::TuneControl:
    case Method::ControlStatus:
    case Method::CameraLocked:
    case Method::ClaimKey:
    case Method::GlobalValue:
    case Method::App:
    case Method::Connected:
    case Method::Nodes:
    case Method::CallNode:
    case Method::OffersService:
    case Method::CallService:
        return true;
    default:
        return false;
    }
}

std::string utf8Of(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path pathOf(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

}
