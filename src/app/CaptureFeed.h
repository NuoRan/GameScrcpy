#pragma once

#include <gs/capture/CaptureSource.h>
#include <gs/capture/CropDetector.h>
#include <gs/media/AudioPlayer.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/media/Thumbnailer.h>
#include <gs/render/FrameSlot.h>

#include <QSize>
#include <QString>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct ID3D11Device;

namespace gs::capture {
class AudioCapture;
class AudioEncoder;
class FrameBuilder;
class VideoEncoder;
struct EncodedSound;
}

namespace gs::record {
class Recorder;
}

namespace gs::app {

enum class CaptureEvent : std::uint8_t { Opened, Cropped, Failed, Thumbnailed };

struct CaptureOptions {
    std::string format;
    bool sound = true;
    std::optional<capture::CropBox> manualCrop;
};

class CaptureFeed final : public capture::SampleSink
{
public:
    using Listener = std::function<void(CaptureEvent event)>;
    using Clock = std::chrono::steady_clock;

    CaptureFeed(ID3D11Device* device, std::unique_ptr<capture::CaptureSource> source);
    ~CaptureFeed() override;
    CaptureFeed(const CaptureFeed&) = delete;
    CaptureFeed& operator=(const CaptureFeed&) = delete;

    bool start(const CaptureOptions& options);
    void stop();
    void setListener(Listener listener);
    void setRecorder(std::shared_ptr<record::Recorder> recorder);
    void setManualCrop(std::optional<capture::CropBox> crop);
    void setMuted(bool muted);
    void setVolume(double volume);
    void setBenchFed(bool fed) noexcept { m_benchFed.store(fed, std::memory_order_relaxed); }

    [[nodiscard]] bool running() const noexcept { return m_running.load(std::memory_order_acquire); }
    [[nodiscard]] QString problem() const;
    [[nodiscard]] std::shared_ptr<render::FrameSlot> frames() const noexcept { return m_slot; }
    [[nodiscard]] const std::shared_ptr<render::FrameSlot>& benchFrames() const noexcept { return m_bench; }
    [[nodiscard]] const std::shared_ptr<media::FrameSnapshots>& snapshots() const noexcept { return m_snapshots; }
    [[nodiscard]] double framesPerSecond() const noexcept;
    [[nodiscard]] capture::CropBox crop() const;
    [[nodiscard]] bool manualCrop() const;
    [[nodiscard]] QSize frameSize() const;
    [[nodiscard]] QSize cropSize() const;
    [[nodiscard]] std::uint16_t touchSession() const;
    [[nodiscard]] QString formatText() const;
    [[nodiscard]] QStringList formats() const;
    [[nodiscard]] bool sound() const;
    [[nodiscard]] media::AudioStats audioStats() const;
    [[nodiscard]] std::uint64_t framesShown() const noexcept { return m_published.load(std::memory_order_relaxed); }
    [[nodiscard]] std::optional<media::Thumbnail> takeThumbnail();

    void offered(const std::vector<capture::CaptureFormat>& formats,
                 const std::optional<capture::CaptureFormat>& chosen) override;
    void sample(const capture::CaptureSample& sample) override;
    void frame(media::VideoFrame frame) override;
    void ended(const std::string& problem) override;
    void heard(std::span<const float> interleaved, int channels, int sampleRate, std::int64_t ptsUs) override;
    void setAudioTap(std::function<void(std::span<const float>, int, int, std::int64_t)> tap);

    void feedSound(std::span<const float> interleaved, int channels, int sampleRate, std::int64_t ptsUs);

private:
    void publish(media::VideoFrame frame);
    void measure(Clock::time_point now);
    void cropFound(const capture::CropBox& box);
    void applyCrop();
    void startSound();
    void notify(CaptureEvent event);
    void recordSound(record::Recorder& recorder, const capture::EncodedSound& sound);

    ID3D11Device* const m_device;
    std::unique_ptr<capture::CaptureSource> m_source;
    std::unique_ptr<capture::FrameBuilder> m_builder;
    const std::shared_ptr<render::FrameSlot> m_slot = std::make_shared<render::FrameSlot>();
    const std::shared_ptr<render::FrameSlot> m_bench = std::make_shared<render::FrameSlot>();
    const std::shared_ptr<media::FrameSnapshots> m_snapshots;
    const std::shared_ptr<media::FrameSnapshots> m_whole;
    std::atomic<bool> m_benchFed{false};
    std::atomic<bool> m_running{false};
    std::atomic<std::uint64_t> m_published{0};
    std::atomic<double> m_framesPerSecond{0.0};
    Clock::time_point m_windowStart = Clock::now();
    std::uint64_t m_windowFrames = 0;
    Clock::time_point m_nextLook;
    std::unique_ptr<media::Thumbnailer> m_thumbnailer;
    Clock::time_point m_nextThumbnail;
    capture::CropDetector m_detector;

    mutable std::mutex m_mutex;
    Listener m_listener;
    CaptureOptions m_options;
    QString m_problem;
    std::vector<capture::CaptureFormat> m_formats;
    std::optional<capture::CaptureFormat> m_chosen;
    std::optional<capture::CropBox> m_autoCrop;
    std::optional<capture::CropBox> m_manualCrop;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    std::uint16_t m_touchSession = 0;
    std::optional<media::Thumbnail> m_thumbnail;

    mutable std::mutex m_soundMutex;
    std::unique_ptr<media::AudioPlayer> m_player;
    std::unique_ptr<capture::AudioCapture> m_capture;
    bool m_muted = false;
    double m_volume = 1.0;
    std::function<void(std::span<const float>, int, int, std::int64_t)> m_tap;

    mutable std::mutex m_recordMutex;
    std::shared_ptr<record::Recorder> m_recorder;
    std::unique_ptr<capture::VideoEncoder> m_videoEncoder;
    std::unique_ptr<capture::AudioEncoder> m_audioEncoder;
    std::uint16_t m_soundSession = 0;
};

}
