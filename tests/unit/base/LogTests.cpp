#include "gs/base/Log.h"

#include <doctest/doctest.h>

#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Entry {
    gs::log::Level level;
    std::string category;
    std::string message;
};

class RecordingSink final : public gs::log::Sink
{
public:
    void write(const gs::log::Record& record) noexcept override
    {
        const std::scoped_lock lock{m_mutex};
        m_entries.push_back(
            Entry{record.level, std::string(record.category), std::string(record.message)});
    }

    std::vector<Entry> entries() const
    {
        const std::scoped_lock lock{m_mutex};
        return m_entries;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
};

class ScopedSink
{
public:
    ScopedSink(gs::log::Sink& sink, gs::log::Level minimum)
    {
        gs::log::setSink(&sink);
        gs::log::setMinimumLevel(minimum);
    }

    ~ScopedSink()
    {
        gs::log::setSink(nullptr);
        gs::log::setMinimumLevel(gs::log::Level::Info);
    }

    ScopedSink(const ScopedSink&) = delete;
    ScopedSink& operator=(const ScopedSink&) = delete;
};

}

TEST_CASE("log records below the minimum level are dropped")
{
    RecordingSink sink;
    const ScopedSink scope{sink, gs::log::Level::Info};

    gs::log::debug("test", "hidden {}", 1);
    gs::log::info("session", "connected to {} in {} ms", "emulator-5556", 42);
    gs::log::error("protocol", "decode failed");

    const auto entries = sink.entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].level == gs::log::Level::Info);
    CHECK(entries[0].category == "session");
    CHECK(entries[0].message == "connected to emulator-5556 in 42 ms");
    CHECK(entries[1].level == gs::log::Level::Error);
    CHECK(entries[1].message == "decode failed");
}

TEST_CASE("without a sink no level is enabled")
{
    gs::log::setSink(nullptr);
    CHECK_FALSE(gs::log::isEnabled(gs::log::Level::Error));
    gs::log::error("test", "dropped {}", 1);
}

TEST_CASE("a log sink receives records from many threads")
{
    RecordingSink sink;
    const ScopedSink scope{sink, gs::log::Level::Trace};
    {
        std::vector<std::jthread> workers;
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([t] {
                for (int i = 0; i < 1000; ++i) {
                    gs::log::trace("worker", "{} {}", t, i);
                }
            });
        }
    }
    CHECK(sink.entries().size() == 4000);
}
