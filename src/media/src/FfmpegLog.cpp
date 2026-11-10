#include "gs/media/FfmpegLog.h"

#include <gs/base/Log.h>

extern "C" {
#include <libavutil/log.h>
}

#include <cstdarg>
#include <string_view>

namespace gs::media {

namespace {

log::Level levelOf(int ffmpegLevel) noexcept
{
    if (ffmpegLevel <= AV_LOG_ERROR) {
        return log::Level::Error;
    }
    if (ffmpegLevel <= AV_LOG_WARNING) {
        return log::Level::Warning;
    }
    return ffmpegLevel <= AV_LOG_INFO ? log::Level::Debug : log::Level::Trace;
}

void forward(void* object, int ffmpegLevel, const char* format, va_list arguments)
{
    const log::Level level = levelOf(ffmpegLevel);
    if (!log::isEnabled(level)) {
        return;
    }
    thread_local int printPrefix = 1;
    char line[1024];
    av_log_format_line2(object, ffmpegLevel, format, arguments, line, sizeof(line), &printPrefix);
    std::string_view text(line);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    if (!text.empty()) {
        log::write(level, "ffmpeg", text);
    }
}

}

void routeFfmpegLog() noexcept
{
    av_log_set_callback(&forward);
}

}
