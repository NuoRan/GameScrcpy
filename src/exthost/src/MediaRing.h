#pragma once

#include "gs/exthost/Media.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace gs::exthost::detail {

inline constexpr std::size_t kSoundRingBytes = std::size_t{1} << 21;

class Section
{
public:
    Section() = default;
    ~Section();
    Section(Section&& other) noexcept;
    Section& operator=(Section&& other) noexcept;
    Section(const Section&) = delete;
    Section& operator=(const Section&) = delete;

    [[nodiscard]] static std::optional<Section> make(std::size_t bytes);
    [[nodiscard]] static std::optional<Section> adopt(HANDLE handle, std::size_t bytes);

    [[nodiscard]] HANDLE handle() const noexcept { return m_handle; }
    [[nodiscard]] std::byte* data() const noexcept { return static_cast<std::byte*>(m_view); }
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }

private:
    void close() noexcept;

    HANDLE m_handle = nullptr;
    void* m_view = nullptr;
    std::size_t m_size = 0;
};

class FrameRing
{
public:
    static constexpr std::uint32_t kSlots = 3;

    [[nodiscard]] static std::shared_ptr<FrameRing> make(std::size_t capacity);
    [[nodiscard]] static std::shared_ptr<FrameRing> adopt(HANDLE section, std::size_t capacity);
    [[nodiscard]] static std::size_t bytesFor(std::size_t capacity) noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
    [[nodiscard]] HANDLE handle() const noexcept { return m_section.handle(); }
    [[nodiscard]] bool write(const FrameView& frame);
    [[nodiscard]] std::optional<FrameView> read();

private:
    FrameRing(Section section, std::size_t capacity) noexcept;

    [[nodiscard]] std::byte* slotAt(std::uint32_t slot) const noexcept;

    Section m_section;
    std::size_t m_capacity = 0;
    std::uint32_t m_back = 0;
    std::uint32_t m_front = 2;
};

class SoundRing
{
public:
    [[nodiscard]] static std::shared_ptr<SoundRing> make(std::size_t capacity = kSoundRingBytes);
    [[nodiscard]] static std::shared_ptr<SoundRing> adopt(HANDLE section, std::size_t capacity);
    [[nodiscard]] static std::size_t bytesFor(std::size_t capacity) noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
    [[nodiscard]] HANDLE handle() const noexcept { return m_section.handle(); }
    [[nodiscard]] bool write(const SoundView& sound);
    [[nodiscard]] std::optional<SoundBlock> read();

private:
    SoundRing(Section section, std::size_t capacity) noexcept;

    void copyIn(std::uint64_t position, const void* source, std::size_t size) const;
    void copyOut(std::uint64_t position, void* target, std::size_t size) const;

    Section m_section;
    std::size_t m_capacity = 0;
};

enum class Written : std::uint8_t { Yes, Refused, Closed };

class StreamBox final : public MediaStream
{
public:
    struct Hooks {
        std::function<void(MediaKind kind, HANDLE section, std::size_t capacity)> made;
        std::function<void(const std::string& why)> ended;
    };

    StreamBox(std::uint64_t id, HANDLE wake, Hooks hooks);
    ~StreamBox() override;
    StreamBox(const StreamBox&) = delete;
    StreamBox& operator=(const StreamBox&) = delete;

    [[nodiscard]] std::uint64_t id() const noexcept override { return m_id; }
    [[nodiscard]] bool wait(const std::stop_token& stop) override;
    [[nodiscard]] std::optional<FrameView> frame() override;
    [[nodiscard]] std::optional<SoundBlock> sound() override;
    [[nodiscard]] std::optional<std::string> ended() const override;

    [[nodiscard]] HANDLE wakeHandle() const noexcept { return m_wake; }
    [[nodiscard]] Written writeFrame(const FrameView& frame);
    [[nodiscard]] Written writeSound(const SoundView& sound);
    void adoptFrames(std::shared_ptr<FrameRing> ring);
    void adoptSounds(std::shared_ptr<SoundRing> ring);
    void end(const std::string& why);
    void close();

private:
    static constexpr std::size_t kMostRetired = 4;

    void finish(const std::string& why, bool tell);

    const std::uint64_t m_id;
    HANDLE m_wake = nullptr;
    Hooks m_hooks;
    mutable std::mutex m_mutex;
    std::shared_ptr<FrameRing> m_frames;
    std::vector<std::shared_ptr<FrameRing>> m_retired;
    std::shared_ptr<SoundRing> m_sounds;
    std::optional<std::string> m_ended;
    std::mutex m_writing;
    std::mutex m_reading;
    std::shared_ptr<FrameRing> m_shown;
};

class SinkBox final : public SinkPort
{
public:
    [[nodiscard]] static std::shared_ptr<SinkBox> make(std::uint64_t id, const SinkStart& start);
    [[nodiscard]] static std::shared_ptr<SinkBox> adopt(std::uint64_t id, const SinkStart& start, HANDLE frames,
                                                        HANDLE sounds, HANDLE wake);
    ~SinkBox() override;
    SinkBox(const SinkBox&) = delete;
    SinkBox& operator=(const SinkBox&) = delete;

    [[nodiscard]] std::uint64_t id() const noexcept override { return m_id; }
    [[nodiscard]] bool frame(const FrameView& frame) override;
    [[nodiscard]] bool sound(const SoundView& sound) override;
    [[nodiscard]] std::optional<std::string> ended() const override;

    [[nodiscard]] const SinkStart& start() const noexcept { return m_start; }
    [[nodiscard]] HANDLE framesHandle() const noexcept { return m_frames != nullptr ? m_frames->handle() : nullptr; }
    [[nodiscard]] HANDLE soundsHandle() const noexcept { return m_sounds != nullptr ? m_sounds->handle() : nullptr; }
    [[nodiscard]] HANDLE wakeHandle() const noexcept { return m_wake; }
    [[nodiscard]] bool wait(const std::stop_token& stop);
    [[nodiscard]] std::optional<FrameView> nextFrame();
    [[nodiscard]] std::optional<SoundBlock> nextSound();
    void setEnded(std::function<void(const std::string& why)> ended);
    void end(const std::string& why);

private:
    SinkBox(std::uint64_t id, SinkStart start, std::shared_ptr<FrameRing> frames, std::shared_ptr<SoundRing> sounds,
            HANDLE wake) noexcept;

    const std::uint64_t m_id;
    const SinkStart m_start;
    std::shared_ptr<FrameRing> m_frames;
    std::shared_ptr<SoundRing> m_sounds;
    HANDLE m_wake = nullptr;
    mutable std::mutex m_mutex;
    std::optional<std::string> m_ended;
    std::function<void(const std::string& why)> m_tell;
    std::mutex m_framesWriting;
    std::mutex m_soundsWriting;
};

class SinkPump
{
public:
    using Frame = std::function<bool(const FrameView& frame)>;
    using Sound = std::function<bool(const SoundView& sound)>;

    SinkPump(std::shared_ptr<SinkBox> box, Frame frame, Sound sound);
    ~SinkPump();
    SinkPump(const SinkPump&) = delete;
    SinkPump& operator=(const SinkPump&) = delete;

private:
    void run(const std::stop_token& stop);

    std::shared_ptr<SinkBox> m_box;
    Frame m_frame;
    Sound m_sound;
    std::jthread m_thread;
};

}
