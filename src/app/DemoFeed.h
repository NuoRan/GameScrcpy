#pragma once

#include <gs/media/FrameSnapshot.h>
#include <gs/protocol/Codec.h>
#include <gs/render/FrameSlot.h>

#include <QString>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace gs::render {
class GraphicsDevice;
}

namespace gs::record {
class Recorder;
}

namespace gs::app {

struct DemoDevice {
    QString recording;
    QString game;

    friend bool operator==(const DemoDevice&, const DemoDevice&) = default;
};

[[nodiscard]] QString demoSerialOf(int number);
[[nodiscard]] std::vector<DemoDevice> demoDevicesOf(const QStringList& recordings, const QStringList& games);

class DemoFeed final
{
public:
    DemoFeed(const render::GraphicsDevice& graphics, std::filesystem::path recording);
    ~DemoFeed();
    DemoFeed(const DemoFeed&) = delete;
    DemoFeed& operator=(const DemoFeed&) = delete;

    [[nodiscard]] bool start();
    void stop();
    void setRecorder(std::shared_ptr<record::Recorder> recorder);

    [[nodiscard]] QString problem() const;
    [[nodiscard]] std::shared_ptr<render::FrameSlot> frames() const noexcept;
    [[nodiscard]] const std::shared_ptr<render::FrameSlot>& benchFrames() const noexcept { return m_bench; }
    void setBenchFed(bool fed) noexcept { m_benchFed.store(fed, std::memory_order_relaxed); }
    [[nodiscard]] const std::shared_ptr<media::FrameSnapshots>& snapshots() const noexcept { return m_snapshots; }
    [[nodiscard]] double framesPerSecond() const noexcept;
    [[nodiscard]] std::uint16_t videoSession() const noexcept { return m_videoSession; }
    [[nodiscard]] std::uint16_t frameWidth() const noexcept { return m_frameWidth; }
    [[nodiscard]] std::uint16_t frameHeight() const noexcept { return m_frameHeight; }
    [[nodiscard]] protocol::DeviceStatus status() const;
    [[nodiscard]] static protocol::DeviceStatus fakeStatus(std::chrono::milliseconds elapsed);

private:
    [[nodiscard]] QString load();
    void run(const std::stop_token& token, std::promise<QString>& opened);
    void setProblem(QString reason);
    [[nodiscard]] std::shared_ptr<record::Recorder> recorder() const;

    const render::GraphicsDevice& m_graphics;
    const std::filesystem::path m_recording;
    const std::shared_ptr<render::FrameSlot> m_slot;
    const std::shared_ptr<media::FrameSnapshots> m_snapshots;
    const std::shared_ptr<render::FrameSlot> m_bench = std::make_shared<render::FrameSlot>();
    std::atomic<bool> m_benchFed{false};
    std::vector<std::uint8_t> m_bytes;
    std::vector<protocol::MediaPacket> m_packets;
    std::vector<std::int64_t> m_delaysUs;
    std::int64_t m_loopUs = 0;
    mutable std::mutex m_mutex;
    QString m_problem;
    mutable std::mutex m_recorderMutex;
    std::shared_ptr<record::Recorder> m_recorder;
    std::atomic<double> m_framesPerSecond{0.0};
    std::uint16_t m_videoSession = 0;
    std::uint16_t m_frameWidth = 0;
    std::uint16_t m_frameHeight = 0;
    std::chrono::steady_clock::time_point m_statusStart = std::chrono::steady_clock::now();
    std::jthread m_player;
};

}
