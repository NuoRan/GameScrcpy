#pragma once

#include "Settings.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace gs::app {

template <typename Document>
class JsonFile
{
public:
    explicit JsonFile(std::filesystem::path path, std::chrono::milliseconds debounce = std::chrono::milliseconds(500),
                      std::function<void()> busy = {});
    ~JsonFile();
    JsonFile(const JsonFile&) = delete;
    JsonFile& operator=(const JsonFile&) = delete;

    [[nodiscard]] const Document& document() const noexcept { return m_document; }
    [[nodiscard]] const std::vector<settings::Diagnostic>& loadDiagnostics() const noexcept { return m_diagnostics; }
    [[nodiscard]] bool readOnly() const noexcept { return m_readOnly; }

    void update(const Document& document);
    void flush();
    [[nodiscard]] std::uint64_t writes() const;

private:
    static constexpr int kAttempts = 3;

    void load();
    void run(const std::stop_token& stop);
    [[nodiscard]] std::error_code write(const std::string& contents) const;

    const std::filesystem::path m_path;
    const std::chrono::milliseconds m_debounce;
    const std::function<void()> m_busy;
    Document m_document;
    std::vector<settings::Diagnostic> m_diagnostics;
    bool m_readOnly = false;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::condition_variable m_written;
    std::optional<std::string> m_pending;
    std::optional<std::string> m_unsaved;
    std::chrono::steady_clock::time_point m_due;
    std::uint64_t m_generation = 0;
    std::uint64_t m_writtenGeneration = 0;
    bool m_flushNow = false;
    int m_attempts = 0;
    std::uint64_t m_writes = 0;

    std::jthread m_writer;
};

extern template class JsonFile<settings::Settings>;
extern template class JsonFile<settings::Devices>;

}
