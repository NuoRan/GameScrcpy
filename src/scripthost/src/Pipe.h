#pragma once

#include "Wire.h"

#include <windows.h>

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::scripthost::detail {

class PipeWriter
{
public:
    explicit PipeWriter(HANDLE handle) noexcept;
    ~PipeWriter();
    PipeWriter(const PipeWriter&) = delete;
    PipeWriter& operator=(const PipeWriter&) = delete;

    [[nodiscard]] bool send(const std::string& frame);

private:
    std::mutex m_mutex;
    HANDLE m_handle = nullptr;
    bool m_broken = false;
};

class PipeReader
{
public:
    explicit PipeReader(HANDLE handle) noexcept;
    ~PipeReader();
    PipeReader(const PipeReader&) = delete;
    PipeReader& operator=(const PipeReader&) = delete;

    [[nodiscard]] std::optional<Frame> next();

private:
    HANDLE m_handle = nullptr;
    std::string m_buffer;
    std::size_t m_at = 0;
    std::vector<char> m_chunk;
};

[[nodiscard]] std::wstring handleText(HANDLE first, HANDLE second);
[[nodiscard]] bool handlesOf(std::wstring_view text, HANDLE& first, HANDLE& second);

}
