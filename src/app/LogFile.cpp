#include "LogFile.h"

#include <format>
#include <system_error>
#include <utility>

namespace gs::app {

LogFile::LogFile(std::filesystem::path directory, std::string name, std::uintmax_t maxBytes, int keep)
    : m_directory(std::move(directory)),
      m_name(std::move(name)),
      m_path(m_directory / (m_name + ".log")),
      m_maxBytes(maxBytes),
      m_keep(keep),
      m_writer([this](const std::stop_token& stop) { run(stop); })
{
}

LogFile::~LogFile()
{
    m_writer.request_stop();
    m_wake.notify_all();
}

void LogFile::append(std::string line) noexcept
{
    try {
        {
            const std::lock_guard lock(m_mutex);
            if (m_queue.size() >= kQueueLimit) {
                ++m_dropped;
                return;
            }
            m_queue.push_back(std::move(line));
        }
        m_wake.notify_one();
    } catch (...) {
    }
}

void LogFile::run(const std::stop_token& stop)
{
    std::error_code error;
    std::filesystem::create_directories(m_directory, error);
    m_size = std::filesystem::exists(m_path, error) ? std::filesystem::file_size(m_path, error) : 0;
    m_file.open(m_path, std::ios::app | std::ios::binary);
    std::vector<std::string> batch;
    for (;;) {
        std::uint64_t dropped = 0;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, stop, [this] { return !m_queue.empty(); });
            if (m_queue.empty()) {
                return;
            }
            batch.swap(m_queue);
            dropped = std::exchange(m_dropped, 0);
        }
        if (dropped > 0) {
            write(std::format("[log] {} lines dropped: logging outran the disk", dropped));
        }
        for (const std::string& line : batch) {
            write(line);
        }
        m_file.flush();
        batch.clear();
    }
}

void LogFile::write(const std::string& line)
{
    if (!m_file.is_open()) {
        return;
    }
    m_file << line << '\n';
    m_size += line.size() + 1;
    if (m_size >= m_maxBytes) {
        rotate();
    }
}

void LogFile::rotate()
{
    m_file.close();
    std::error_code error;
    std::filesystem::remove(rotated(m_keep), error);
    for (int index = m_keep - 1; index >= 1; --index) {
        std::filesystem::rename(rotated(index), rotated(index + 1), error);
    }
    std::filesystem::rename(m_path, rotated(1), error);
    m_file.open(m_path, std::ios::trunc | std::ios::binary);
    m_size = 0;
}

std::filesystem::path LogFile::rotated(int index) const
{
    return m_directory / std::format("{}.{}.log", m_name, index);
}

}
