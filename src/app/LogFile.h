#pragma once

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gs::app {

class LogFile
{
public:
    LogFile(std::filesystem::path directory, std::string name, std::uintmax_t maxBytes = 4u << 20, int keep = 3);
    ~LogFile();
    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

    void append(std::string line) noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
    static constexpr std::size_t kQueueLimit = 10'000;

    void run(const std::stop_token& stop);
    void write(const std::string& line);
    void rotate();
    [[nodiscard]] std::filesystem::path rotated(int index) const;

    const std::filesystem::path m_directory;
    const std::string m_name;
    const std::filesystem::path m_path;
    const std::uintmax_t m_maxBytes;
    const int m_keep;

    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::vector<std::string> m_queue;
    std::uint64_t m_dropped = 0;

    std::ofstream m_file;
    std::uintmax_t m_size = 0;

    std::jthread m_writer;
};

}
