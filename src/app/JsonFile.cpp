#include "JsonFile.h"

#include <gs/base/Log.h>
#include <gs/platform/AtomicFile.h>

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

}

template <typename Document>
JsonFile<Document>::JsonFile(std::filesystem::path path, std::chrono::milliseconds debounce,
                             std::function<void()> busy)
    : m_path(std::move(path)),
      m_debounce(debounce),
      m_busy(std::move(busy))
{
    load();
    m_writer = std::jthread([this](const std::stop_token& stop) { run(stop); });
}

template <typename Document>
JsonFile<Document>::~JsonFile()
{
    m_writer.request_stop();
    m_wake.notify_all();
}

template <typename Document>
void JsonFile<Document>::load()
{
    std::error_code error;
    if (!std::filesystem::exists(m_path, error)) {
        return;
    }
    std::ifstream in(m_path, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    in.close();

    settings::Loaded<Document> loaded = settings::load<Document>(text);
    for (const settings::Diagnostic& diagnostic : loaded.diagnostics) {
        log::warning("settings", "{}: {} at {} {}", utf8(m_path), settings::problemName(diagnostic.problem),
                     diagnostic.path, diagnostic.detail);
    }
    m_diagnostics = std::move(loaded.diagnostics);
    if (loaded.usable) {
        m_document = std::move(loaded.document);
    } else if (loaded.newer) {
        m_readOnly = true;
        log::warning("settings", "{} comes from a newer GameScrcpy: using the defaults, and not saving over it",
                     utf8(m_path));
    } else {
        std::filesystem::path aside = m_path;
        aside += ".bad";
        std::filesystem::rename(m_path, aside, error);
        log::warning("settings", "{} cannot be read: using the defaults; the file {}", utf8(m_path),
                     error ? "stays where it is" : "is kept as " + utf8(aside.filename()));
    }
}

template <typename Document>
void JsonFile<Document>::update(const Document& document)
{
    if (document == m_document) {
        return;
    }
    m_document = document;
    if (m_readOnly) {
        return;
    }
    std::string contents = settings::save(document);
    {
        const std::lock_guard lock(m_mutex);
        m_pending = std::move(contents);
        m_unsaved.reset();
        m_due = std::chrono::steady_clock::now() + m_debounce;
        ++m_generation;
        m_attempts = 0;
    }
    m_wake.notify_all();
}

template <typename Document>
void JsonFile<Document>::flush()
{
    std::unique_lock lock(m_mutex);
    const std::uint64_t generation = m_generation;
    if (m_writtenGeneration >= generation) {
        return;
    }
    m_flushNow = true;
    m_wake.notify_all();
    m_written.wait(lock, [&] { return m_writtenGeneration >= generation; });
}

template <typename Document>
std::uint64_t JsonFile<Document>::writes() const
{
    const std::lock_guard lock(m_mutex);
    return m_writes;
}

template <typename Document>
void JsonFile<Document>::run(const std::stop_token& stop)
{
    std::unique_lock lock(m_mutex);
    bool lastChance = false;
    for (;;) {
        m_wake.wait(lock, stop, [this] { return m_pending.has_value(); });
        if (!m_pending) {
            if (!stop.stop_requested() || !m_unsaved || std::exchange(lastChance, true)) {
                return;
            }
            m_pending = std::exchange(m_unsaved, std::nullopt);
        }
        while (!stop.stop_requested() && !m_flushNow && std::chrono::steady_clock::now() < m_due) {
            m_wake.wait_until(lock, stop, m_due, [this] { return m_flushNow; });
        }
        std::string contents = std::move(*m_pending);
        m_pending.reset();
        m_flushNow = false;
        const std::uint64_t generation = m_generation;

        lock.unlock();
        const std::error_code error = write(contents);
        const bool busy = platform::fileBusy(error);
        if (busy && m_busy) {
            m_busy();
        }
        lock.lock();

        if (!error) {
            ++m_writes;
            m_unsaved.reset();
        } else if (busy) {
            if (!m_pending) {
                m_unsaved = std::move(contents);
            }
        } else if (!m_pending && ++m_attempts < kAttempts && !stop.stop_requested()) {
            m_pending = std::move(contents);
            m_due = std::chrono::steady_clock::now() + m_debounce;
        }
        m_writtenGeneration = generation;
        m_written.notify_all();
    }
}

template <typename Document>
std::error_code JsonFile<Document>::write(const std::string& contents) const
{
    std::error_code error;
    std::filesystem::create_directories(m_path.parent_path(), error);
    error = platform::writeFileAtomically(m_path, contents);
    if (error) {
        log::warning("settings", "could not save {} ({}error {})", utf8(m_path),
                     platform::fileBusy(error) ? "another program is using it, " : "", error.value());
    }
    return error;
}

template class JsonFile<settings::Settings>;
template class JsonFile<settings::Devices>;

}
