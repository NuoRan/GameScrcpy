#include "Pipe.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <system_error>

namespace gs::scripthost::detail {

namespace {

constexpr std::size_t kChunkBytes = std::size_t{64} << 10;
constexpr std::size_t kMostWriteBytes = std::size_t{1} << 20;
constexpr std::size_t kSizeBytes = sizeof(std::uint32_t);
constexpr std::size_t kKindBytes = sizeof(std::uint16_t);

bool handleValue(std::wstring_view text, HANDLE& handle)
{
    if (text.empty() || text.size() > 20) {
        return false;
    }
    std::string digits;
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return false;
        }
        digits.push_back(static_cast<char>(c));
    }
    std::uint64_t value = 0;
    if (std::from_chars(digits.data(), digits.data() + digits.size(), value).ec != std::errc{} || value == 0) {
        return false;
    }
    handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
    return true;
}

}

PipeWriter::PipeWriter(HANDLE handle) noexcept
    : m_handle(handle)
{
}

PipeWriter::~PipeWriter()
{
    if (m_handle != nullptr) {
        CloseHandle(m_handle);
    }
}

bool PipeWriter::send(const std::string& frame)
{
    const std::scoped_lock lock(m_mutex);
    if (m_broken || m_handle == nullptr) {
        return false;
    }
    std::size_t at = 0;
    while (at < frame.size()) {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>(std::min(frame.size() - at, kMostWriteBytes));
        if (!WriteFile(m_handle, frame.data() + at, chunk, &written, nullptr) || written == 0) {
            m_broken = true;
            return false;
        }
        at += written;
    }
    return true;
}

PipeReader::PipeReader(HANDLE handle) noexcept
    : m_handle(handle)
{
}

PipeReader::~PipeReader()
{
    if (m_handle != nullptr) {
        CloseHandle(m_handle);
    }
}

std::optional<Frame> PipeReader::next()
{
    for (;;) {
        const std::size_t available = m_buffer.size() - m_at;
        if (available >= kSizeBytes) {
            std::uint32_t size = 0;
            std::memcpy(&size, m_buffer.data() + m_at, kSizeBytes);
            if (size < kKindBytes || size > kMostFrameBytes) {
                return std::nullopt;
            }
            if (available >= kSizeBytes + size) {
                std::uint16_t kind = 0;
                std::memcpy(&kind, m_buffer.data() + m_at + kSizeBytes, kKindBytes);
                Frame frame{static_cast<Kind>(kind), m_buffer.substr(m_at + kSizeBytes + kKindBytes, size - kKindBytes)};
                m_at += kSizeBytes + size;
                if (m_at == m_buffer.size()) {
                    m_buffer.clear();
                    m_at = 0;
                }
                return frame;
            }
        }
        if (m_at > 0) {
            m_buffer.erase(0, m_at);
            m_at = 0;
        }
        if (m_chunk.empty()) {
            m_chunk.resize(kChunkBytes);
        }
        DWORD read = 0;
        if (m_handle == nullptr ||
            !ReadFile(m_handle, m_chunk.data(), static_cast<DWORD>(m_chunk.size()), &read, nullptr) || read == 0) {
            return std::nullopt;
        }
        m_buffer.append(m_chunk.data(), read);
    }
}

std::wstring handleText(HANDLE first, HANDLE second)
{
    return std::to_wstring(reinterpret_cast<std::uintptr_t>(first)) + L"," +
           std::to_wstring(reinterpret_cast<std::uintptr_t>(second));
}

bool handlesOf(std::wstring_view text, HANDLE& first, HANDLE& second)
{
    const std::size_t comma = text.find(L',');
    return comma != std::wstring_view::npos && handleValue(text.substr(0, comma), first) &&
           handleValue(text.substr(comma + 1), second);
}

}
