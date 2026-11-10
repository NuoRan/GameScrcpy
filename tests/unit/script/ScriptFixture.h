#pragma once

#include "gs/script/ScriptHost.h"

#include <gs/base/Clock.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace gs::script::test {

using Match = std::function<bool(const Report&)>;

struct Entry {
    Report report;
    Microseconds at{0};
    std::thread::id thread;
    std::size_t index = 0;
};

class Recorder
{
public:
    [[nodiscard]] ScriptHost::Sink sink()
    {
        return [this](const Report& report) {
            add(report);
        };
    }

    void add(const Report& report)
    {
        const Microseconds at = SteadyClock{}.now();
        {
            const std::scoped_lock lock(m_mutex);
            m_entries.push_back(Entry{.report = report,
                                      .at = at,
                                      .thread = std::this_thread::get_id(),
                                      .index = m_entries.size()});
        }
        m_changed.notify_all();
    }

    [[nodiscard]] std::optional<Entry>
    waitFor(const Match& matches, std::size_t from = 0,
            std::chrono::milliseconds timeout = std::chrono::seconds(30))
    {
        std::unique_lock lock(m_mutex);
        std::optional<Entry> found;
        m_changed.wait_for(lock, timeout, [&] {
            for (std::size_t i = from; i < m_entries.size(); ++i) {
                if (matches(m_entries[i].report)) {
                    found = m_entries[i];
                    return true;
                }
            }
            return false;
        });
        return found;
    }

    [[nodiscard]] bool waitForCount(const Match& matches, std::size_t count, std::size_t from = 0,
                                    std::chrono::milliseconds timeout = std::chrono::seconds(30))
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout,
                                  [&] { return countLocked(matches, from) >= count; });
    }

    [[nodiscard]] std::size_t count(const Match& matches, std::size_t from = 0) const
    {
        const std::scoped_lock lock(m_mutex);
        return countLocked(matches, from);
    }

    [[nodiscard]] std::size_t size() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_entries.size();
    }

    [[nodiscard]] std::vector<Entry> entries() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_entries;
    }

private:
    [[nodiscard]] std::size_t countLocked(const Match& matches, std::size_t from) const
    {
        std::size_t total = 0;
        for (std::size_t i = from; i < m_entries.size(); ++i) {
            if (matches(m_entries[i].report)) {
                ++total;
            }
        }
        return total;
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<Entry> m_entries;
};

inline Match kindIs(ReportKind kind, TaskId task = 0)
{
    return [kind, task](const Report& report) {
        return report.kind == kind && (task == 0 || report.task == task);
    };
}

inline Match logged(std::string text, TaskId task = 0)
{
    return [text = std::move(text), task](const Report& report) {
        return report.kind == ReportKind::Log && report.message == text &&
               (task == 0 || report.task == task);
    };
}

inline Match loadedOrFailed(ScriptId script)
{
    return [script](const Report& report) {
        return report.script == script && report.task == 0 &&
               (report.kind == ReportKind::ScriptLoaded || report.kind == ReportKind::ScriptFailed);
    };
}

inline Match taskEnded(TaskId task)
{
    return [task](const Report& report) {
        return report.task == task &&
               (report.kind == ReportKind::TaskFinished ||
                report.kind == ReportKind::TaskCancelled || report.kind == ReportKind::TaskFailed ||
                report.kind == ReportKind::TaskInterrupted);
    };
}

class ScriptFolder
{
public:
    ScriptFolder()
        : m_root(std::filesystem::temp_directory_path() /
                 ("gs_script_" + std::to_string(std::random_device{}()) +
                  std::to_string(std::random_device{}())))
    {
        std::filesystem::create_directories(m_root);
    }

    ~ScriptFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    ScriptFolder(const ScriptFolder&) = delete;
    ScriptFolder& operator=(const ScriptFolder&) = delete;

    void write(std::string_view name, std::string_view source) const
    {
        const std::filesystem::path path =
            m_root / std::filesystem::path(
                         std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()));
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out.write(source.data(), static_cast<std::streamsize>(source.size()));
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_root; }

    [[nodiscard]] ScriptSource source(std::string entry = "main.js") const
    {
        return ScriptSource{.folder = m_root, .entry = std::move(entry)};
    }

private:
    std::filesystem::path m_root;
};

inline std::optional<Report> loadScript(ScriptHost& host, Recorder& recorder,
                                        const ScriptSource& source, ScriptId* id = nullptr)
{
    const ScriptId script = host.load(source);
    if (id != nullptr) {
        *id = script;
    }
    const auto entry = recorder.waitFor(loadedOrFailed(script));
    if (!entry) {
        return std::nullopt;
    }
    return entry->report;
}

inline double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto index =
        static_cast<std::size_t>(std::lround(fraction * static_cast<double>(values.size() - 1)));
    return values[std::min(index, values.size() - 1)];
}

inline std::string summary(const std::vector<double>& values, std::string_view unit)
{
    return std::format("n={} p50={:.1f}{} p90={:.1f}{} p99={:.1f}{} max={:.1f}{}", values.size(),
                       percentile(values, 0.5), unit, percentile(values, 0.9), unit,
                       percentile(values, 0.99), unit, percentile(values, 1.0), unit);
}

}
