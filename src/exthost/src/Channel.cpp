#include "Channel.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <new>
#include <system_error>
#include <utility>

namespace gs::exthost::detail {

namespace {

constexpr std::uint32_t kMagic = 0x58455347;
constexpr std::uint32_t kLayoutVersion = 1;

struct Layout {
    std::uint32_t magic = kMagic;
    std::uint32_t version = kLayoutVersion;
    RingHeader toHost;
    RingHeader toApp;
};

constexpr std::size_t kDataOffset = (sizeof(Layout) + 63) / 64 * 64;
constexpr std::size_t kTotalBytes = kDataOffset + 2 * kRingBytes;

struct RecordHead {
    std::uint32_t size = 0;
    std::uint32_t kind = 0;
    std::uint64_t id = 0;
};

static_assert(sizeof(RecordHead) == 16);
static_assert((kRingBytes & (kRingBytes - 1)) == 0);

constexpr std::uint64_t padded(std::size_t size) noexcept
{
    return (static_cast<std::uint64_t>(size) + 7U) & ~std::uint64_t{7};
}

}

void Ring::copyIn(std::uint64_t position, const void* source, std::size_t size)
{
    const auto offset = static_cast<std::size_t>(position & (kRingBytes - 1));
    const std::size_t first = std::min(size, kRingBytes - offset);
    std::memcpy(m_data + offset, source, first);
    if (size > first) {
        std::memcpy(m_data, static_cast<const std::byte*>(source) + first, size - first);
    }
}

void Ring::copyOut(std::uint64_t position, void* target, std::size_t size) const
{
    const auto offset = static_cast<std::size_t>(position & (kRingBytes - 1));
    const std::size_t first = std::min(size, kRingBytes - offset);
    std::memcpy(target, m_data + offset, first);
    if (size > first) {
        std::memcpy(static_cast<std::byte*>(target) + first, m_data, size - first);
    }
}

bool Ring::write(Kind kind, std::uint64_t id, std::string_view payload)
{
    if (m_header == nullptr || payload.size() > kMostPayloadBytes) {
        return false;
    }
    const std::uint64_t head = m_header->head.load(std::memory_order_relaxed);
    const std::uint64_t tail = m_header->tail.load(std::memory_order_acquire);
    const std::uint64_t used = head - tail;
    const std::uint64_t need = sizeof(RecordHead) + padded(payload.size());
    if (used > kRingBytes || need > kRingBytes - used) {
        return false;
    }
    const RecordHead record{static_cast<std::uint32_t>(payload.size()), static_cast<std::uint32_t>(kind), id};
    copyIn(head, &record, sizeof(record));
    if (!payload.empty()) {
        copyIn(head + sizeof(record), payload.data(), payload.size());
    }
    m_header->head.store(head + need, std::memory_order_release);
    return true;
}

ReadResult Ring::read(Message& out)
{
    if (m_header == nullptr) {
        return ReadResult::Broken;
    }
    const std::uint64_t tail = m_header->tail.load(std::memory_order_relaxed);
    const std::uint64_t head = m_header->head.load(std::memory_order_acquire);
    if (head == tail) {
        return ReadResult::Empty;
    }
    const std::uint64_t used = head - tail;
    if (used > kRingBytes || used < sizeof(RecordHead)) {
        return ReadResult::Broken;
    }
    RecordHead record;
    copyOut(tail, &record, sizeof(record));
    const std::uint64_t need = sizeof(RecordHead) + padded(record.size);
    if (record.size > kMostPayloadBytes || need > used) {
        return ReadResult::Broken;
    }
    out.kind = static_cast<Kind>(record.kind);
    out.id = record.id;
    out.payload.resize(record.size);
    if (record.size > 0) {
        copyOut(tail + sizeof(record), out.payload.data(), record.size);
    }
    m_header->tail.store(tail + need, std::memory_order_release);
    return ReadResult::Read;
}

Channel::~Channel()
{
    close();
}

Channel::Channel(Channel&& other) noexcept
    : m_mapping(std::exchange(other.m_mapping, nullptr))
    , m_toHostEvent(std::exchange(other.m_toHostEvent, nullptr))
    , m_toAppEvent(std::exchange(other.m_toAppEvent, nullptr))
    , m_view(std::exchange(other.m_view, nullptr))
    , m_toHost(std::exchange(other.m_toHost, Ring()))
    , m_toApp(std::exchange(other.m_toApp, Ring()))
{
}

Channel& Channel::operator=(Channel&& other) noexcept
{
    if (this != &other) {
        close();
        m_mapping = std::exchange(other.m_mapping, nullptr);
        m_toHostEvent = std::exchange(other.m_toHostEvent, nullptr);
        m_toAppEvent = std::exchange(other.m_toAppEvent, nullptr);
        m_view = std::exchange(other.m_view, nullptr);
        m_toHost = std::exchange(other.m_toHost, Ring());
        m_toApp = std::exchange(other.m_toApp, Ring());
    }
    return *this;
}

void Channel::close() noexcept
{
    if (m_view != nullptr) {
        UnmapViewOfFile(m_view);
        m_view = nullptr;
    }
    for (HANDLE* handle : {&m_mapping, &m_toHostEvent, &m_toAppEvent}) {
        if (*handle != nullptr) {
            CloseHandle(*handle);
            *handle = nullptr;
        }
    }
    m_toHost = Ring();
    m_toApp = Ring();
}

bool Channel::map(bool fresh)
{
    m_view = MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, kTotalBytes);
    if (m_view == nullptr) {
        return false;
    }
    Layout* layout = fresh ? new (m_view) Layout{} : std::launder(static_cast<Layout*>(m_view));
    if (layout->magic != kMagic || layout->version != kLayoutVersion) {
        return false;
    }
    auto* base = static_cast<std::byte*>(m_view);
    m_toHost = Ring(&layout->toHost, base + kDataOffset);
    m_toApp = Ring(&layout->toApp, base + kDataOffset + kRingBytes);
    return true;
}

std::optional<Channel> Channel::create()
{
    Channel channel;
    channel.m_mapping =
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(kTotalBytes), nullptr);
    channel.m_toHostEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    channel.m_toAppEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (channel.m_mapping == nullptr || channel.m_toHostEvent == nullptr || channel.m_toAppEvent == nullptr ||
        !channel.map(true)) {
        return std::nullopt;
    }
    return channel;
}

std::optional<Channel> Channel::open(std::wstring_view handles)
{
    std::array<std::uint64_t, 3> values{};
    std::size_t at = 0;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::size_t end = std::min(handles.find(L',', at), handles.size());
        if (end == at || end - at > 20) {
            return std::nullopt;
        }
        std::string digits;
        for (const wchar_t c : handles.substr(at, end - at)) {
            if (c < L'0' || c > L'9') {
                return std::nullopt;
            }
            digits.push_back(static_cast<char>(c));
        }
        if (std::from_chars(digits.data(), digits.data() + digits.size(), values[index]).ec != std::errc{}) {
            return std::nullopt;
        }
        at = end + 1;
    }
    if (at <= handles.size()) {
        return std::nullopt;
    }
    Channel channel;
    channel.m_mapping = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(values[0]));
    channel.m_toHostEvent = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(values[1]));
    channel.m_toAppEvent = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(values[2]));
    if (!channel.map(false)) {
        return std::nullopt;
    }
    return channel;
}

std::wstring handleText(const std::array<HANDLE, 3>& handles)
{
    std::wstring text;
    for (const HANDLE handle : handles) {
        if (!text.empty()) {
            text.push_back(L',');
        }
        text += std::to_wstring(reinterpret_cast<std::uintptr_t>(handle));
    }
    return text;
}

}
