#pragma once

#include "gs/session/Session.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace gs::session::test {

struct VideoRecord {
    protocol::MediaKind kind = protocol::MediaKind::Frame;
    std::uint16_t session = 0;
    std::size_t size = 0;
};

class RecordingListener final : public SessionListener
{
public:
    void onStateChanged(const SessionState& state) override { record([&] { m_states.push_back(state); }); }
    void onTransport(TransportKind kind) override { record([&] { m_transports.push_back(kind); }); }
    void onWelcome(const protocol::Welcome& welcome) override { record([&] { m_welcomes.push_back(welcome); }); }
    void onMessage(const protocol::ToClientMessage& message) override { record([&] { m_messages.push_back(message); }); }
    void onHeartbeat(const Heartbeat& heartbeat) override { record([&] { m_heartbeats.push_back(heartbeat); }); }
    void onVideoOpened() override
    {
        record([&] {
            m_videoThreadCorrect = m_videoThreadCorrect && m_videoThread == std::thread::id{};
            m_videoThread = std::this_thread::get_id();
            ++m_videoOpenings;
        });
    }
    void onVideoClosed() override
    {
        record([&] {
            m_videoThreadCorrect = m_videoThreadCorrect && m_videoThread == std::this_thread::get_id();
            m_videoThread = {};
            ++m_videoClosings;
        });
    }
    void onVideoPacket(const protocol::MediaPacket& packet) override
    {
        record([&] {
            m_videoThreadCorrect = m_videoThreadCorrect && m_videoThread == std::this_thread::get_id();
            m_video.push_back({packet.header.kind, packet.header.session, packet.payload.size()});
        });
    }
    void onAudioOpened() override
    {
        record([&] {
            m_audioThreadCorrect = m_audioThreadCorrect && m_audioThread == std::thread::id{};
            m_audioThread = std::this_thread::get_id();
            ++m_audioOpenings;
        });
    }
    void onAudioClosed() override
    {
        record([&] {
            m_audioThreadCorrect = m_audioThreadCorrect && m_audioThread == std::this_thread::get_id();
            m_audioThread = {};
            ++m_audioClosings;
        });
    }
    void onAudioPacket(const protocol::MediaPacket& packet) override
    {
        record([&] {
            m_audioThreadCorrect = m_audioThreadCorrect && m_audioThread == std::this_thread::get_id() &&
                                   m_audioThread != m_videoThread;
            m_audio.push_back({packet.header.kind, packet.header.session, packet.payload.size()});
        });
    }
    void onBulkOpened() override { record([&] { ++m_bulkOpenings; }); }
    void onBulkClosed() override { record([&] { ++m_bulkClosings; }); }

    bool waitForPhase(SessionPhase phase, std::size_t occurrences = 1,
                      std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] {
            return static_cast<std::size_t>(std::ranges::count_if(
                       m_states, [&](const SessionState& state) { return state.phase == phase; })) >= occurrences;
        });
    }

    bool waitForEnd(std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] {
            return !m_states.empty() &&
                   (m_states.back().phase == SessionPhase::Stopped || m_states.back().phase == SessionPhase::Failed);
        });
    }

    bool waitForHeartbeats(std::size_t count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] { return m_heartbeats.size() >= count; });
    }

    bool waitForVideo(std::size_t count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] { return m_video.size() >= count; });
    }

    bool waitForAudio(std::size_t count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] { return m_audio.size() >= count; });
    }

    bool waitForMessages(std::size_t count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        return waitFor(timeout, [&] { return m_messages.size() >= count; });
    }

    [[nodiscard]] std::vector<SessionPhase> phases() const
    {
        const std::lock_guard lock(m_mutex);
        std::vector<SessionPhase> phases;
        for (const SessionState& state : m_states) {
            phases.push_back(state.phase);
        }
        return phases;
    }

    [[nodiscard]] std::optional<SessionState> first(SessionPhase phase) const
    {
        const std::lock_guard lock(m_mutex);
        const auto found = std::ranges::find_if(m_states, [&](const SessionState& state) { return state.phase == phase; });
        if (found == m_states.end()) {
            return std::nullopt;
        }
        return *found;
    }

    [[nodiscard]] std::optional<SessionFailure> failure() const
    {
        const auto failed = first(SessionPhase::Failed);
        return failed ? failed->failure : std::nullopt;
    }

    [[nodiscard]] std::vector<TransportKind> transports() const { return copy(m_transports); }
    [[nodiscard]] std::vector<protocol::Welcome> welcomes() const { return copy(m_welcomes); }
    [[nodiscard]] std::vector<Heartbeat> heartbeats() const { return copy(m_heartbeats); }
    [[nodiscard]] std::vector<VideoRecord> video() const { return copy(m_video); }
    [[nodiscard]] std::vector<protocol::ToClientMessage> messages() const { return copy(m_messages); }

    [[nodiscard]] std::size_t videoOpenings() const { return locked([&] { return m_videoOpenings; }); }
    [[nodiscard]] std::size_t videoClosings() const { return locked([&] { return m_videoClosings; }); }
    [[nodiscard]] bool videoThreadCorrect() const { return locked([&] { return m_videoThreadCorrect; }); }

    [[nodiscard]] std::vector<VideoRecord> audio() const { return copy(m_audio); }
    [[nodiscard]] std::size_t audioOpenings() const { return locked([&] { return m_audioOpenings; }); }
    [[nodiscard]] std::size_t audioClosings() const { return locked([&] { return m_audioClosings; }); }
    [[nodiscard]] bool audioThreadCorrect() const { return locked([&] { return m_audioThreadCorrect; }); }
    [[nodiscard]] std::size_t bulkOpenings() const { return locked([&] { return m_bulkOpenings; }); }
    [[nodiscard]] std::size_t bulkClosings() const { return locked([&] { return m_bulkClosings; }); }

private:
    template <typename Read>
    auto locked(Read read) const
    {
        const std::lock_guard lock(m_mutex);
        return read();
    }

    template <typename Change>
    void record(Change&& change)
    {
        {
            const std::lock_guard lock(m_mutex);
            change();
        }
        m_changed.notify_all();
    }

    template <typename Predicate>
    bool waitFor(std::chrono::milliseconds timeout, Predicate predicate)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, predicate);
    }

    template <typename T>
    std::vector<T> copy(const std::vector<T>& items) const
    {
        const std::lock_guard lock(m_mutex);
        return items;
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<SessionState> m_states;
    std::vector<TransportKind> m_transports;
    std::vector<protocol::Welcome> m_welcomes;
    std::vector<protocol::ToClientMessage> m_messages;
    std::vector<Heartbeat> m_heartbeats;
    std::vector<VideoRecord> m_video;
    std::thread::id m_videoThread;
    std::size_t m_videoOpenings = 0;
    std::size_t m_videoClosings = 0;
    bool m_videoThreadCorrect = true;
    std::vector<VideoRecord> m_audio;
    std::thread::id m_audioThread;
    std::size_t m_audioOpenings = 0;
    std::size_t m_audioClosings = 0;
    bool m_audioThreadCorrect = true;
    std::size_t m_bulkOpenings = 0;
    std::size_t m_bulkClosings = 0;
};

}
