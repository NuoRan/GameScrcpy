#include "gs/base/Log.h"

#include <atomic>

namespace gs::log {

namespace {

std::atomic<Sink*> g_sink{nullptr};
std::atomic<Level> g_minimumLevel{Level::Info};

}

void setSink(Sink* sink) noexcept
{
    g_sink.store(sink, std::memory_order_release);
}

void setMinimumLevel(Level level) noexcept
{
    g_minimumLevel.store(level, std::memory_order_relaxed);
}

bool isEnabled(Level level) noexcept
{
    return level >= g_minimumLevel.load(std::memory_order_relaxed) &&
           g_sink.load(std::memory_order_acquire) != nullptr;
}

void write(Level level, std::string_view category, std::string_view message) noexcept
{
    if (level < g_minimumLevel.load(std::memory_order_relaxed)) {
        return;
    }
    if (Sink* sink = g_sink.load(std::memory_order_acquire)) {
        sink->write(Record{.level = level, .category = category, .message = message});
    }
}

}
