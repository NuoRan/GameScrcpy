#include "AoaTouchSink.h"

#include <gs/platform/Thread.h>
#include <gs/platform/Usb.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace gs::app {

namespace {

constexpr std::array<std::uint8_t, 71> kMouseDescriptor{
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00,
    0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30,
    0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x06, 0x09, 0x38, 0x15, 0x81,
    0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0x05, 0x0C, 0x0A, 0x38, 0x02, 0x81, 0x06, 0xC0, 0xC0,
};

constexpr std::array<std::uint8_t, 72> kKeyboardDescriptor{
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, aoa::kKeyboardReport, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x75, 0x08, 0x95, 0x01, 0x81, 0x01, 0x75, 0x08, 0x95, 0x06,
    0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0, 0x05, 0x0C, 0x09, 0x01, 0xA1,
    0x01, 0x85, aoa::kConsumerReport, 0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10,
    0x95, 0x01, 0x81, 0x00, 0xC0,
};

struct KeyEntry {
    std::uint16_t keycode = 0;
    HidKey key;
};

constexpr std::uint8_t kKeys = aoa::kKeyboardReport;
constexpr std::uint8_t kMedia = aoa::kConsumerReport;

constexpr std::array<KeyEntry, 30> kNamedKeys{{
    {3, {kMedia, 0x0223}},  {4, {kMedia, 0x0224}},  {19, {kKeys, 0x52}},   {20, {kKeys, 0x51}},   {21, {kKeys, 0x50}},
    {22, {kKeys, 0x4F}},    {24, {kMedia, 0x00E9}}, {25, {kMedia, 0x00EA}}, {26, {kMedia, 0x0030}}, {55, {kKeys, 0x36}},
    {56, {kKeys, 0x37}},    {57, {kKeys, 0xE2}},    {58, {kKeys, 0xE6}},   {59, {kKeys, 0xE1}},   {60, {kKeys, 0xE5}},
    {61, {kKeys, 0x2B}},    {62, {kKeys, 0x2C}},    {66, {kKeys, 0x28}},   {67, {kKeys, 0x2A}},   {68, {kKeys, 0x35}},
    {69, {kKeys, 0x2D}},    {70, {kKeys, 0x2E}},    {71, {kKeys, 0x2F}},   {72, {kKeys, 0x30}},   {73, {kKeys, 0x31}},
    {74, {kKeys, 0x33}},    {75, {kKeys, 0x34}},    {76, {kKeys, 0x38}},   {82, {kMedia, 0x0040}}, {84, {kMedia, 0x0221}},
}};

constexpr std::array<KeyEntry, 28> kMoreKeys{{
    {85, {kMedia, 0x00CD}},  {86, {kMedia, 0x00B7}},  {87, {kMedia, 0x00B5}},  {88, {kMedia, 0x00B6}}, {92, {kKeys, 0x4B}},
    {93, {kKeys, 0x4E}},     {111, {kKeys, 0x29}},    {112, {kKeys, 0x4C}},    {113, {kKeys, 0xE0}},  {114, {kKeys, 0xE4}},
    {115, {kKeys, 0x39}},    {116, {kKeys, 0x47}},    {117, {kKeys, 0xE3}},    {118, {kKeys, 0xE7}},  {120, {kKeys, 0x46}},
    {121, {kKeys, 0x48}},    {122, {kKeys, 0x4A}},    {123, {kKeys, 0x4D}},    {124, {kKeys, 0x49}},  {143, {kKeys, 0x53}},
    {154, {kKeys, 0x54}},    {155, {kKeys, 0x55}},    {156, {kKeys, 0x56}},    {157, {kKeys, 0x57}},  {158, {kKeys, 0x63}},
    {160, {kKeys, 0x58}},    {164, {kMedia, 0x00E2}}, {187, {kMedia, 0x01A2}},
}};

constexpr std::uint16_t kKeycode0 = 7;
constexpr std::uint16_t kKeycode9 = 16;
constexpr std::uint16_t kKeycodeA = 29;
constexpr std::uint16_t kKeycodeZ = 54;
constexpr std::uint16_t kKeycodeF1 = 131;
constexpr std::uint16_t kKeycodeF12 = 142;
constexpr std::uint16_t kKeycodeNumpad0 = 144;
constexpr std::uint16_t kKeycodeNumpad9 = 153;
constexpr std::uint8_t kFirstModifier = 0xE0;
constexpr std::uint8_t kLastModifier = 0xE7;
constexpr std::uint8_t kLeftShift = 0x02;

constexpr std::string_view kDigitsShifted = ")!@#$%^&*(";
constexpr std::array<std::pair<char, std::uint8_t>, 22> kPunctuation{{
    {' ', 0x2C},  {'\n', 0x28}, {'\t', 0x2B}, {'-', 0x2D},  {'_', 0x2D}, {'=', 0x2E}, {'+', 0x2E}, {'[', 0x2F},
    {'{', 0x2F},  {']', 0x30},  {'}', 0x30},  {'\\', 0x31}, {'|', 0x31}, {';', 0x33}, {':', 0x33}, {'\'', 0x34},
    {'"', 0x34},  {'`', 0x35},  {'~', 0x35},  {',', 0x36},  {'<', 0x36}, {'.', 0x37},
}};
constexpr std::string_view kShiftedPunctuation = "_+{}|:\"~<>?";

std::optional<std::pair<std::uint8_t, bool>> typedKeyOf(char character) noexcept
{
    if (character >= 'a' && character <= 'z') {
        return std::pair{static_cast<std::uint8_t>(0x04 + (character - 'a')), false};
    }
    if (character >= 'A' && character <= 'Z') {
        return std::pair{static_cast<std::uint8_t>(0x04 + (character - 'A')), true};
    }
    if (character >= '1' && character <= '9') {
        return std::pair{static_cast<std::uint8_t>(0x1E + (character - '1')), false};
    }
    if (character == '0') {
        return std::pair{std::uint8_t{0x27}, false};
    }
    if (const std::size_t digit = kDigitsShifted.find(character); digit != std::string_view::npos) {
        return std::pair{static_cast<std::uint8_t>(digit == 0 ? 0x27 : 0x1E + digit - 1), true};
    }
    if (character == '>') {
        return std::pair{std::uint8_t{0x37}, true};
    }
    if (character == '/' || character == '?') {
        return std::pair{std::uint8_t{0x38}, character == '?'};
    }
    for (const auto& [typed, usage] : kPunctuation) {
        if (typed == character) {
            return std::pair{usage, kShiftedPunctuation.find(character) != std::string_view::npos};
        }
    }
    return std::nullopt;
}

std::int16_t readSigned(const std::vector<std::uint8_t>& bytes, std::size_t at) noexcept
{
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8U)));
}

void writeSigned(std::uint8_t* out, int value) noexcept
{
    const auto raw = static_cast<std::uint16_t>(static_cast<std::int16_t>(value));
    out[0] = static_cast<std::uint8_t>(raw & 0xFFU);
    out[1] = static_cast<std::uint8_t>(raw >> 8U);
}

bool numberedReports(std::span<const std::uint8_t> descriptor) noexcept
{
    constexpr std::uint8_t kLongItem = 0xFE;
    constexpr std::uint8_t kReportId = 0x84;
    std::size_t at = 0;
    while (at < descriptor.size() && descriptor[at] != kLongItem) {
        const std::uint8_t prefix = descriptor[at];
        if ((prefix & 0xFCU) == kReportId) {
            return true;
        }
        at += 1 + ((prefix & 3U) == 3U ? 4 : (prefix & 3U));
    }
    return false;
}

class UsbAoaLink final : public AoaLink
{
public:
    explicit UsbAoaLink(std::unique_ptr<platform::UsbDevice> device) noexcept
        : m_device(std::move(device))
    {
    }

    [[nodiscard]] std::optional<std::uint16_t> protocol() override
    {
        const std::optional<std::vector<std::uint8_t>> data = m_device->controlIn(aoa::kGetProtocol, 0, 0, 2);
        if (!data || data->size() < 2) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>((*data)[0] | ((*data)[1] << 8U));
    }

    [[nodiscard]] bool send(std::uint8_t request, std::uint16_t value, std::uint16_t index,
                            std::span<const std::uint8_t> data) override
    {
        return m_device->controlOut(request, value, index, data);
    }

    [[nodiscard]] std::size_t packetSize() const override { return m_device->maxPacketSize(); }

    [[nodiscard]] bool gone() const override { return m_device->failure() == platform::UsbFailure::Gone; }

private:
    std::unique_ptr<platform::UsbDevice> m_device;
};

}

std::span<const std::uint8_t> aoaMouseDescriptor() noexcept
{
    return kMouseDescriptor;
}

std::span<const std::uint8_t> aoaKeyboardDescriptor() noexcept
{
    return kKeyboardDescriptor;
}

std::optional<HidKey> hidKeyOf(std::uint32_t androidKeycode) noexcept
{
    if (androidKeycode >= kKeycodeA && androidKeycode <= kKeycodeZ) {
        return HidKey{.report = kKeys, .usage = static_cast<std::uint16_t>(0x04 + androidKeycode - kKeycodeA)};
    }
    if (androidKeycode > kKeycode0 && androidKeycode <= kKeycode9) {
        return HidKey{.report = kKeys, .usage = static_cast<std::uint16_t>(0x1E + androidKeycode - kKeycode0 - 1)};
    }
    if (androidKeycode == kKeycode0) {
        return HidKey{.report = kKeys, .usage = 0x27};
    }
    if (androidKeycode >= kKeycodeF1 && androidKeycode <= kKeycodeF12) {
        return HidKey{.report = kKeys, .usage = static_cast<std::uint16_t>(0x3A + androidKeycode - kKeycodeF1)};
    }
    if (androidKeycode > kKeycodeNumpad0 && androidKeycode <= kKeycodeNumpad9) {
        return HidKey{.report = kKeys,
                      .usage = static_cast<std::uint16_t>(0x59 + androidKeycode - kKeycodeNumpad0 - 1)};
    }
    if (androidKeycode == kKeycodeNumpad0) {
        return HidKey{.report = kKeys, .usage = 0x62};
    }
    for (const KeyEntry& entry : kNamedKeys) {
        if (entry.keycode == androidKeycode) {
            return entry.key;
        }
    }
    for (const KeyEntry& entry : kMoreKeys) {
        if (entry.keycode == androidKeycode) {
            return entry.key;
        }
    }
    return std::nullopt;
}

std::unique_ptr<AoaLink> openAoaLink(const platform::UsbPhone& phone, platform::UsbFailure& failure)
{
    std::unique_ptr<platform::UsbDevice> device = platform::UsbDevice::open(phone, failure);
    if (device == nullptr) {
        return nullptr;
    }
    return std::make_unique<UsbAoaLink>(std::move(device));
}

AoaPort::AoaPort(std::unique_ptr<AoaLink> link, Listener listener)
    : m_link(std::move(link))
    , m_listener(std::move(listener))
    , m_thread([this](const std::stop_token& stop) { run(stop); })
{
}

AoaPort::~AoaPort()
{
    m_thread.request_stop();
    m_wake.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool AoaPort::add(std::uint16_t id, std::span<const std::uint8_t> descriptor)
{
    return queue({.kind = Kind::Register, .id = id, .bytes = {descriptor.begin(), descriptor.end()}});
}

bool AoaPort::event(std::uint16_t id, std::span<const std::uint8_t> report, Merge merge)
{
    return queue({.kind = Kind::Event, .id = id, .bytes = {report.begin(), report.end()}, .merge = merge});
}

bool AoaPort::remove(std::uint16_t id)
{
    return queue({.kind = Kind::Unregister, .id = id});
}

void AoaPort::flush()
{
    std::unique_lock lock(m_mutex);
    m_drained.wait(lock, [this] { return m_queue.empty() && m_idle; });
}

AoaState AoaPort::state() const
{
    const std::scoped_lock lock(m_mutex);
    return m_state;
}

int AoaPort::failedEvents() const
{
    const std::scoped_lock lock(m_mutex);
    return m_failedEvents;
}

bool AoaPort::queue(Item item)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (m_state == AoaState::Gone || m_state == AoaState::Unsupported || m_state == AoaState::Failed) {
            return false;
        }
        if (!m_queue.empty() && item.merge != Merge::None && merged(m_queue.back(), item)) {
            return true;
        }
        if (m_queue.size() >= aoa::kMostQueued && item.kind == Kind::Event) {
            return false;
        }
        m_queue.push_back(std::move(item));
    }
    m_wake.notify_all();
    return true;
}

bool AoaPort::merged(Item& pending, const Item& next)
{
    if (pending.kind != Kind::Event || pending.merge != next.merge || pending.id != next.id ||
        pending.bytes.size() != next.bytes.size()) {
        return false;
    }
    if (next.merge == Merge::Touch && next.bytes.size() == kUhidTouchReportSize) {
        TouchReport older{};
        TouchReport newer{};
        std::ranges::copy(pending.bytes, older.begin());
        std::ranges::copy(next.bytes, newer.begin());
        if (!sameContacts(older, newer)) {
            return false;
        }
        pending.bytes = next.bytes;
        return true;
    }
    if (next.merge == Merge::Motion && next.bytes.size() == aoa::kMouseReportSize) {
        if (pending.bytes[0] != next.bytes[0] || pending.bytes[5] != 0 || pending.bytes[6] != 0 || next.bytes[5] != 0 ||
            next.bytes[6] != 0) {
            return false;
        }
        const int dx = readSigned(pending.bytes, 1) + readSigned(next.bytes, 1);
        const int dy = readSigned(pending.bytes, 3) + readSigned(next.bytes, 3);
        constexpr int kMost = std::numeric_limits<std::int16_t>::max();
        if (dx > kMost || dx < -kMost || dy > kMost || dy < -kMost) {
            return false;
        }
        writeSigned(pending.bytes.data() + 1, dx);
        writeSigned(pending.bytes.data() + 3, dy);
        return true;
    }
    return false;
}

void AoaPort::publish(AoaState state)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (m_state == state) {
            return;
        }
        m_state = state;
    }
    if (m_listener) {
        m_listener(state);
    }
}

void AoaPort::run(const std::stop_token& stop)
{
    platform::setCurrentThreadName("aoa");
    const std::optional<std::uint16_t> version = m_link->protocol();
    if (!version) {
        publish(m_link->gone() ? AoaState::Gone : AoaState::Failed);
    } else if (*version < aoa::kLeastProtocol) {
        publish(AoaState::Unsupported);
    } else {
        publish(AoaState::Ready);
    }
    for (;;) {
        Item item;
        {
            std::unique_lock lock(m_mutex);
            m_idle = true;
            m_drained.notify_all();
            if (!m_wake.wait(lock, stop, [this] { return !m_queue.empty(); })) {
                break;
            }
            item = std::move(m_queue.front());
            m_queue.pop_front();
            m_idle = false;
            if (m_state != AoaState::Ready) {
                m_queue.clear();
                continue;
            }
        }
        if (perform(item)) {
            continue;
        }
        if (m_link->gone()) {
            publish(AoaState::Gone);
            continue;
        }
        if (item.kind == Kind::Register) {
            publish(AoaState::Failed);
        } else if (item.kind == Kind::Event) {
            const std::scoped_lock lock(m_mutex);
            ++m_failedEvents;
        }
    }
    const std::scoped_lock lock(m_mutex);
    m_idle = true;
    m_drained.notify_all();
}

bool AoaPort::perform(const Item& item)
{
    switch (item.kind) {
    case Kind::Register: {
        if (item.bytes.size() > 0xFFFF ||
            !m_link->send(aoa::kRegisterHid, item.id, static_cast<std::uint16_t>(item.bytes.size()), {})) {
            return false;
        }
        const std::size_t chunk = std::max<std::size_t>(m_link->packetSize(), 8);
        for (std::size_t offset = 0; offset < item.bytes.size(); offset += chunk) {
            const std::size_t size = std::min(chunk, item.bytes.size() - offset);
            if (!m_link->send(aoa::kSetHidReportDescriptor, item.id, static_cast<std::uint16_t>(offset),
                              std::span(item.bytes).subspan(offset, size))) {
                return false;
            }
        }
        return true;
    }
    case Kind::Event:
        return m_link->send(aoa::kSendHidEvent, item.id, 0, item.bytes);
    case Kind::Unregister:
        return m_link->send(aoa::kUnregisterHid, item.id, 0, {});
    }
    return false;
}

AoaTouchSink::AoaTouchSink(ControlSink* injection, AoaPort& port, AoaDevices devices)
    : m_injection(injection)
    , m_port(port)
    , m_devices(devices)
{
    bool added = m_port.add(aoa::kTouchscreen, uhidTouchDescriptor());
    if (m_devices.mouse) {
        added = m_port.add(aoa::kMouse, aoaMouseDescriptor()) && added;
    }
    if (m_devices.keyboard) {
        added = m_port.add(aoa::kKeyboard, aoaKeyboardDescriptor()) && added;
    }
    m_attached = added;
}

AoaTouchSink::~AoaTouchSink()
{
    const std::scoped_lock lock(m_mutex);
    detachLocked();
}

bool AoaTouchSink::send(const protocol::TouchFrame& frame)
{
    const std::scoped_lock lock(m_mutex);
    if (!m_attached) {
        return false;
    }
    return m_encoder.encode(frame, frame.videoSession != m_videoSession, m_rotation,
                            [this](const TouchReport& report) { return touch(report); });
}

bool AoaTouchSink::send(const protocol::Key& key)
{
    if (m_injection != nullptr) {
        return m_injection->send(key);
    }
    const std::scoped_lock lock(m_mutex);
    return m_attached && m_devices.keyboard && this->key(key.keycode, key.action == protocol::KeyAction::Down);
}

bool AoaTouchSink::send(const protocol::Scroll& scroll)
{
    if (m_injection != nullptr) {
        return m_injection->send(scroll);
    }
    const auto notches = [](int units) {
        const int steps = units / 256;
        return steps != 0 ? steps : units > 0 ? 1 : units < 0 ? -1 : 0;
    };
    const std::scoped_lock lock(m_mutex);
    if (!m_attached || !m_devices.mouse) {
        return false;
    }
    return mouseReport(0, 0, notches(scroll.vScroll), notches(scroll.hScroll), AoaPort::Merge::None);
}

bool AoaTouchSink::send(const protocol::Text& text)
{
    if (m_injection != nullptr) {
        return m_injection->send(text);
    }
    const std::scoped_lock lock(m_mutex);
    if (!m_attached || !m_devices.keyboard) {
        return false;
    }
    bool typed = true;
    for (const char character : text.text) {
        const std::optional<std::pair<std::uint8_t, bool>> key = typedKeyOf(character);
        if (!key) {
            typed = false;
            continue;
        }
        std::array<std::uint8_t, aoa::kKeyboardReportSize> pressed{};
        pressed[0] = aoa::kKeyboardReport;
        pressed[1] = static_cast<std::uint8_t>(m_modifiers | (key->second ? kLeftShift : 0U));
        pressed[3] = key->first;
        typed = m_port.event(aoa::kKeyboard, pressed) && keyboardReport() && typed;
    }
    return typed;
}

SinkCapabilities AoaTouchSink::capabilities() const
{
    return {.maxContacts = kUhidMaxContacts, .keys = m_injection != nullptr || m_devices.keyboard, .cancel = false};
}

void AoaTouchSink::detached()
{
    {
        const std::scoped_lock lock(m_mutex);
        detachLocked();
    }
    if (m_injection != nullptr) {
        m_injection->detached();
    }
}

bool AoaTouchSink::padFeature(const protocol::UhidFeature&)
{
    const std::scoped_lock lock(m_mutex);
    return m_attached;
}

bool AoaTouchSink::padCreate(const protocol::UhidCreate& create)
{
    const std::scoped_lock lock(m_mutex);
    if (!m_attached || !m_port.add(aoa::kGamepad, create.reportDescriptor)) {
        return false;
    }
    m_padAdded = true;
    m_padId = create.id;
    m_padNumbered = numberedReports(create.reportDescriptor);
    m_padReportSize = 0;
    return true;
}

bool AoaTouchSink::padInput(const protocol::UhidInput& input)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (m_padAdded && input.id == m_padId) {
            if (!m_attached || input.report.empty() || !m_port.event(aoa::kGamepad, input.report)) {
                return false;
            }
            m_padReportSize = input.report.size();
            m_padReportId = m_padNumbered ? input.report.front() : std::uint8_t{0};
            return true;
        }
    }
    return m_injection != nullptr && m_injection->padInput(input);
}

bool AoaTouchSink::padDestroy(const protocol::UhidDestroy& destroy)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (m_padAdded && destroy.id == m_padId) {
            m_padAdded = false;
            m_padReportSize = 0;
            return m_attached && m_port.remove(aoa::kGamepad);
        }
    }
    return m_injection != nullptr && m_injection->padDestroy(destroy);
}

void AoaTouchSink::phaseChanged(session::SessionPhase phase)
{
    const std::scoped_lock lock(m_mutex);
    switch (phase) {
    case session::SessionPhase::Reconnecting:
        releaseLocked();
        break;
    case session::SessionPhase::Stopping:
    case session::SessionPhase::Stopped:
    case session::SessionPhase::Failed:
        detachLocked();
        break;
    default:
        break;
    }
}

void AoaTouchSink::setVideo(std::uint16_t session, int rotation)
{
    const std::scoped_lock lock(m_mutex);
    m_videoSession = session;
    m_rotation = rotation & 3;
}

bool AoaTouchSink::moveMouse(int dx, int dy)
{
    if (dx == 0 && dy == 0) {
        return true;
    }
    constexpr int kMost = std::numeric_limits<std::int16_t>::max();
    const std::scoped_lock lock(m_mutex);
    return m_attached && m_devices.mouse &&
           mouseReport(std::clamp(dx, -kMost, kMost), std::clamp(dy, -kMost, kMost), 0, 0, AoaPort::Merge::Motion);
}

bool AoaTouchSink::setMouseButtons(std::uint8_t buttons)
{
    const std::scoped_lock lock(m_mutex);
    if (!m_attached || !m_devices.mouse) {
        return false;
    }
    const auto next = static_cast<std::uint8_t>(buttons & 0x1FU);
    if (next == m_buttons) {
        return true;
    }
    m_buttons = next;
    return mouseReport(0, 0, 0, 0, AoaPort::Merge::None);
}

bool AoaTouchSink::scrollMouse(int vertical, int horizontal)
{
    const std::scoped_lock lock(m_mutex);
    return m_attached && m_devices.mouse &&
           mouseReport(0, 0, std::clamp(vertical, -127, 127), std::clamp(horizontal, -127, 127), AoaPort::Merge::None);
}

bool AoaTouchSink::pressKey(std::uint32_t keycode)
{
    const std::scoped_lock lock(m_mutex);
    return m_attached && m_devices.keyboard && key(keycode, true) && key(keycode, false);
}

void AoaTouchSink::releaseAll()
{
    const std::scoped_lock lock(m_mutex);
    releaseLocked();
}

int AoaTouchSink::ignoredContacts() const
{
    const std::scoped_lock lock(m_mutex);
    return m_encoder.ignored();
}

bool AoaTouchSink::attached() const
{
    const std::scoped_lock lock(m_mutex);
    return m_attached;
}

bool AoaTouchSink::touch(const TouchReport& report)
{
    const bool moveOnly = m_lastReport && sameContacts(*m_lastReport, report);
    if (!m_port.event(aoa::kTouchscreen, report, moveOnly ? AoaPort::Merge::Touch : AoaPort::Merge::None)) {
        return false;
    }
    m_lastReport = report;
    return true;
}

bool AoaTouchSink::key(std::uint32_t keycode, bool down)
{
    const std::optional<HidKey> hid = hidKeyOf(keycode);
    if (!hid) {
        return false;
    }
    if (hid->report == aoa::kConsumerReport) {
        const std::uint16_t next = down ? hid->usage : std::uint16_t{0};
        if (!down && m_consumer != hid->usage) {
            return true;
        }
        m_consumer = next;
        return consumerReport();
    }
    const auto usage = static_cast<std::uint8_t>(hid->usage);
    if (usage >= kFirstModifier && usage <= kLastModifier) {
        const auto bit = static_cast<std::uint8_t>(1U << (usage - kFirstModifier));
        const auto next = static_cast<std::uint8_t>(down ? (m_modifiers | bit) : (m_modifiers & ~bit));
        if (next == m_modifiers) {
            return true;
        }
        m_modifiers = next;
        return keyboardReport();
    }
    const auto held = std::ranges::find(m_keys, usage);
    if (down) {
        if (held != m_keys.end()) {
            return true;
        }
        const auto free = std::ranges::find(m_keys, std::uint8_t{0});
        if (free == m_keys.end()) {
            return false;
        }
        *free = usage;
    } else {
        if (held == m_keys.end()) {
            return true;
        }
        *held = 0;
    }
    return keyboardReport();
}

bool AoaTouchSink::keyboardReport()
{
    std::array<std::uint8_t, aoa::kKeyboardReportSize> report{};
    report[0] = aoa::kKeyboardReport;
    report[1] = m_modifiers;
    std::size_t at = 3;
    for (const std::uint8_t usage : m_keys) {
        if (usage != 0) {
            report[at++] = usage;
        }
    }
    return m_port.event(aoa::kKeyboard, report);
}

bool AoaTouchSink::consumerReport()
{
    const std::array<std::uint8_t, aoa::kConsumerReportSize> report{
        aoa::kConsumerReport, static_cast<std::uint8_t>(m_consumer & 0xFFU), static_cast<std::uint8_t>(m_consumer >> 8U)};
    return m_port.event(aoa::kKeyboard, report);
}

bool AoaTouchSink::mouseReport(int dx, int dy, int vertical, int horizontal, AoaPort::Merge merge)
{
    std::array<std::uint8_t, aoa::kMouseReportSize> report{};
    report[0] = m_buttons;
    writeSigned(report.data() + 1, dx);
    writeSigned(report.data() + 3, dy);
    report[5] = static_cast<std::uint8_t>(static_cast<std::int8_t>(vertical));
    report[6] = static_cast<std::uint8_t>(static_cast<std::int8_t>(horizontal));
    return m_port.event(aoa::kMouse, report, merge);
}

void AoaTouchSink::releaseLocked()
{
    if (!m_attached) {
        return;
    }
    static_cast<void>(m_encoder.release([this](const TouchReport& report) { return touch(report); }));
    m_encoder.reset();
    m_lastReport.reset();
    if (m_devices.mouse && m_buttons != 0) {
        m_buttons = 0;
        static_cast<void>(mouseReport(0, 0, 0, 0, AoaPort::Merge::None));
    }
    if (m_devices.keyboard) {
        const bool keysHeld = m_modifiers != 0 || std::ranges::any_of(m_keys, [](std::uint8_t usage) { return usage != 0; });
        if (keysHeld) {
            m_modifiers = 0;
            m_keys = {};
            static_cast<void>(keyboardReport());
        }
        if (m_consumer != 0) {
            m_consumer = 0;
            static_cast<void>(consumerReport());
        }
    }
    if (m_padAdded && m_padReportSize > 0) {
        std::vector<std::uint8_t> released(m_padReportSize, 0);
        released[0] = m_padReportId;
        static_cast<void>(m_port.event(aoa::kGamepad, released));
        m_padReportSize = 0;
    }
}

void AoaTouchSink::detachLocked()
{
    if (!m_attached) {
        return;
    }
    releaseLocked();
    if (std::exchange(m_padAdded, false)) {
        static_cast<void>(m_port.remove(aoa::kGamepad));
    }
    static_cast<void>(m_port.remove(aoa::kTouchscreen));
    if (m_devices.mouse) {
        static_cast<void>(m_port.remove(aoa::kMouse));
    }
    if (m_devices.keyboard) {
        static_cast<void>(m_port.remove(aoa::kKeyboard));
    }
    m_attached = false;
}

}
