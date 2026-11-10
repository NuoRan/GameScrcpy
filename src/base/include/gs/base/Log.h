#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace gs::log {

enum class Level : std::uint8_t { Trace, Debug, Info, Warning, Error };

struct Record {
    Level level = Level::Info;
    std::string_view category;
    std::string_view message;
};

class Sink
{
public:
    virtual ~Sink() = default;

    virtual void write(const Record& record) noexcept = 0;
};

void setSink(Sink* sink) noexcept;
void setMinimumLevel(Level level) noexcept;
[[nodiscard]] bool isEnabled(Level level) noexcept;
void write(Level level, std::string_view category, std::string_view message) noexcept;

template <typename... Args>
void message(Level level, std::string_view category, std::format_string<Args...> format,
             Args&&... args) noexcept
{
    if (!isEnabled(level)) {
        return;
    }
    try {
        const std::string text = std::format(format, std::forward<Args>(args)...);
        write(level, category, text);
    } catch (...) {
    }
}

template <typename... Args>
void trace(std::string_view category, std::format_string<Args...> format, Args&&... args) noexcept
{
    message(Level::Trace, category, format, std::forward<Args>(args)...);
}

template <typename... Args>
void debug(std::string_view category, std::format_string<Args...> format, Args&&... args) noexcept
{
    message(Level::Debug, category, format, std::forward<Args>(args)...);
}

template <typename... Args>
void info(std::string_view category, std::format_string<Args...> format, Args&&... args) noexcept
{
    message(Level::Info, category, format, std::forward<Args>(args)...);
}

template <typename... Args>
void warning(std::string_view category, std::format_string<Args...> format, Args&&... args) noexcept
{
    message(Level::Warning, category, format, std::forward<Args>(args)...);
}

template <typename... Args>
void error(std::string_view category, std::format_string<Args...> format, Args&&... args) noexcept
{
    message(Level::Error, category, format, std::forward<Args>(args)...);
}

}
