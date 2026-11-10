#include "gs/media/FfmpegLog.h"

#include <gs/base/Log.h>

extern "C" {
#include <libavutil/log.h>
}

#include <doctest/doctest.h>

#include <mutex>
#include <string>
#include <vector>

namespace {

struct Collector final : gs::log::Sink {
    struct Entry {
        gs::log::Level level;
        std::string category;
        std::string message;
    };

    void write(const gs::log::Record& record) noexcept override
    {
        const std::scoped_lock lock(mutex);
        entries.push_back({record.level, std::string(record.category), std::string(record.message)});
    }

    std::mutex mutex;
    std::vector<Entry> entries;
};

}

TEST_CASE("ffmpeg messages reach gs::log; its information counts as debug")
{
    Collector collector;
    gs::log::setSink(&collector);
    gs::log::setMinimumLevel(gs::log::Level::Trace);
    gs::media::routeFfmpegLog();

    av_log(nullptr, AV_LOG_WARNING, "watch out %d\n", 7);
    av_log(nullptr, AV_LOG_INFO, "chatter\n");
    av_log(nullptr, AV_LOG_ERROR, "broken");

    av_log_set_callback(av_log_default_callback);
    gs::log::setMinimumLevel(gs::log::Level::Info);
    gs::log::setSink(nullptr);

    REQUIRE(collector.entries.size() == 3);
    CHECK(collector.entries[0].level == gs::log::Level::Warning);
    CHECK(collector.entries[0].category == "ffmpeg");
    CHECK(collector.entries[0].message == "watch out 7");
    CHECK(collector.entries[1].level == gs::log::Level::Debug);
    CHECK(collector.entries[1].message == "chatter");
    CHECK(collector.entries[2].level == gs::log::Level::Error);
    CHECK(collector.entries[2].message == "broken");
}

TEST_CASE("ffmpeg messages below the minimum level are not even formatted")
{
    Collector collector;
    gs::log::setSink(&collector);
    gs::media::routeFfmpegLog();

    av_log(nullptr, AV_LOG_INFO, "chatter\n");
    av_log(nullptr, AV_LOG_WARNING, "kept\n");

    av_log_set_callback(av_log_default_callback);
    gs::log::setSink(nullptr);

    REQUIRE(collector.entries.size() == 1);
    CHECK(collector.entries[0].message == "kept");
}
