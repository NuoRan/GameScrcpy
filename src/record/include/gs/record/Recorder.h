#pragma once

#include <gs/base/Clock.h>
#include <gs/base/Expected.h>
#include <gs/protocol/Codec.h>
#include <gs/script/Services.h>
#include <gs/script/Session.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace gs::record {

struct RecorderOptions {
    std::filesystem::path folder;
    script::DeviceInfo device;
    std::int64_t startedAtMs = script::kDefaultSessionStartMs;
    double frameRate = 60.0;
    std::chrono::milliseconds rewriteEvery{3000};
    std::shared_ptr<const Clock> clock;
};

struct RecorderSummary {
    std::filesystem::path folder;
    std::int64_t durationMs = 0;
    std::uintmax_t bytes = 0;
    std::size_t operations = 0;
    std::size_t events = 0;
    int width = 0;
    int height = 0;
    std::size_t videoSessions = 0;
    bool video = false;
    bool audio = false;
    std::string problem;
};

class Recorder
{
public:
    static constexpr std::size_t kMostQueuedBytes = std::size_t{128} << 20;

    [[nodiscard]] static Expected<std::shared_ptr<Recorder>, std::string> start(RecorderOptions options);

    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void video(const protocol::MediaPacket& packet);
    void audio(const protocol::MediaPacket& packet);
    void event(script::SessionEventKind kind, std::string id, bool value = false);
    void operation(script::SessionAction action);

    [[nodiscard]] Microseconds elapsed() const;
    [[nodiscard]] std::size_t operationCount() const;
    [[nodiscard]] bool hasVideo() const;
    [[nodiscard]] std::string problem() const;
    [[nodiscard]] const std::filesystem::path& folder() const noexcept;
    RecorderSummary stop();

private:
    struct State;

    explicit Recorder(std::unique_ptr<State> state);

    std::unique_ptr<State> m_state;
};

}
