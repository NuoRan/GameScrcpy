#include "MediaRing.h"

#include "gs/exthost/Module.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include <utility>

namespace gs::exthost {

std::size_t packedBytes(PixelFormat format, std::uint32_t width, std::uint32_t height) noexcept
{
    if (width > kMostFrameSide || height > kMostFrameSide) {
        return 0;
    }
    const std::size_t across = width;
    const std::size_t down = height;
    switch (format) {
    case PixelFormat::Bgra:
        return across * down * 4;
    case PixelFormat::Nv12:
        return ((across + 1) / 2 * 2) * (down + (down + 1) / 2);
    case PixelFormat::None:
        break;
    }
    return 0;
}

std::string checkFrame(const FrameView& frame)
{
    if (frame.format != PixelFormat::Bgra && frame.format != PixelFormat::Nv12) {
        return "the pixel format is neither BGRA nor NV12";
    }
    if (frame.width < 2 || frame.height < 2 || frame.width > kMostFrameSide || frame.height > kMostFrameSide ||
        std::uint64_t{frame.width} * frame.height > kMostFramePixels) {
        return "the picture is smaller than 2 × 2, wider or higher than 4096 or larger than 10,000,000 pixels";
    }
    if (frame.data == nullptr) {
        return "the picture has no pixels";
    }
    if (frame.format == PixelFormat::Bgra) {
        return frame.stride >= frame.width * 4 ? std::string() : std::string("a row of pixels is longer than the stride");
    }
    if (frame.width % 2 != 0 || frame.height % 2 != 0) {
        return "an NV12 picture has an even width and height";
    }
    if (frame.chroma == nullptr || frame.stride < frame.width || frame.chromaStride < frame.width) {
        return "an NV12 plane is missing or its stride is shorter than a row";
    }
    return {};
}

std::string checkSound(const SoundView& sound)
{
    if (sound.format != SampleFormat::S16 && sound.format != SampleFormat::F32) {
        return "the samples are neither 16-bit nor float";
    }
    if (sound.channels < 1 || sound.channels > kMostChannels) {
        return "the sound has fewer than 1 or more than 8 channels";
    }
    if (sound.rate != kSoundRate) {
        return "the sound is not at 48000 frames a second";
    }
    if (sound.frames < 1 || sound.frames > kMostSoundFrames) {
        return "the sound has fewer than 1 or more than 48000 frames";
    }
    if (sound.data == nullptr) {
        return "the sound has no samples";
    }
    return {};
}

bool validSourceId(std::string_view id) noexcept
{
    return !id.empty() && id.size() <= kMostSourceIdBytes && validUtf8(id) &&
           std::ranges::none_of(id, [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

}

namespace gs::exthost::detail {

namespace {

constexpr std::uint32_t kFrameMagic = 0x46525347;
constexpr std::uint32_t kSoundMagic = 0x53525347;
constexpr std::uint32_t kLayoutVersion = 1;
constexpr std::uint32_t kFresh = 4;
constexpr std::uint32_t kIndex = 3;
constexpr std::size_t kLine = 64;
constexpr std::size_t kMostFrameCapacity = static_cast<std::size_t>(kMostFramePixels) * 4;
constexpr std::size_t kLeastSoundCapacity = std::size_t{1} << 16;

constexpr std::size_t lineUp(std::size_t bytes) noexcept
{
    return (bytes + kLine - 1) / kLine * kLine;
}

constexpr std::uint64_t padded(std::uint64_t bytes) noexcept
{
    return (bytes + 7U) & ~std::uint64_t{7};
}

struct FrameHead {
    std::uint32_t magic = kFrameMagic;
    std::uint32_t version = kLayoutVersion;
    std::uint64_t capacity = 0;
    std::atomic<std::uint32_t> state{1};
    std::uint32_t reserved = 0;
};

struct SlotHead {
    std::uint32_t format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::uint32_t chromaStride = 0;
    std::uint32_t reserved = 0;
    std::uint64_t chromaOffset = 0;
    std::uint64_t timeUs = 0;
};

struct SoundHead {
    std::uint32_t magic = kSoundMagic;
    std::uint32_t version = kLayoutVersion;
    std::uint64_t capacity = 0;
    std::byte firstGap[kLine - 16]{};
    std::atomic<std::uint64_t> head{0};
    std::byte headGap[kLine - sizeof(std::atomic<std::uint64_t>)]{};
    std::atomic<std::uint64_t> tail{0};
    std::byte tailGap[kLine - sizeof(std::atomic<std::uint64_t>)]{};
};

struct SoundRecord {
    std::uint32_t bytes = 0;
    std::uint32_t format = 0;
    std::uint32_t channels = 0;
    std::uint32_t rate = 0;
    std::uint32_t frames = 0;
    std::uint32_t reserved = 0;
    std::uint64_t timeUs = 0;
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(sizeof(SoundRecord) == 32);
static_assert(sizeof(SoundHead) == 3 * kLine);

constexpr std::size_t kFrameHeadBytes = lineUp(sizeof(FrameHead));
constexpr std::size_t kSlotHeadBytes = lineUp(sizeof(SlotHead));
constexpr std::size_t kSoundHeadBytes = sizeof(SoundHead);

std::size_t slotBytes(std::size_t capacity) noexcept
{
    return kSlotHeadBytes + lineUp(capacity);
}

FrameHead& frameHead(const Section& section) noexcept
{
    return *std::launder(reinterpret_cast<FrameHead*>(section.data()));
}

SoundHead& soundHead(const Section& section) noexcept
{
    return *std::launder(reinterpret_cast<SoundHead*>(section.data()));
}

void closeHandle(HANDLE handle) noexcept
{
    if (handle != nullptr) {
        CloseHandle(handle);
    }
}

}

Section::~Section()
{
    close();
}

Section::Section(Section&& other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr))
    , m_view(std::exchange(other.m_view, nullptr))
    , m_size(std::exchange(other.m_size, 0))
{
}

Section& Section::operator=(Section&& other) noexcept
{
    if (this != &other) {
        close();
        m_handle = std::exchange(other.m_handle, nullptr);
        m_view = std::exchange(other.m_view, nullptr);
        m_size = std::exchange(other.m_size, 0);
    }
    return *this;
}

void Section::close() noexcept
{
    if (m_view != nullptr) {
        UnmapViewOfFile(m_view);
        m_view = nullptr;
    }
    closeHandle(std::exchange(m_handle, nullptr));
    m_size = 0;
}

std::optional<Section> Section::make(std::size_t bytes)
{
    Section made;
    const auto wide = static_cast<std::uint64_t>(bytes);
    made.m_handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(wide >> 32U),
                                       static_cast<DWORD>(wide & 0xFFFFFFFFU), nullptr);
    if (made.m_handle == nullptr) {
        return std::nullopt;
    }
    made.m_view = MapViewOfFile(made.m_handle, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
    if (made.m_view == nullptr) {
        return std::nullopt;
    }
    made.m_size = bytes;
    return made;
}

std::optional<Section> Section::adopt(HANDLE handle, std::size_t bytes)
{
    if (handle == nullptr) {
        return std::nullopt;
    }
    Section made;
    made.m_handle = handle;
    made.m_view = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
    if (made.m_view == nullptr) {
        return std::nullopt;
    }
    made.m_size = bytes;
    return made;
}

FrameRing::FrameRing(Section section, std::size_t capacity) noexcept
    : m_section(std::move(section))
    , m_capacity(capacity)
{
}

std::size_t FrameRing::bytesFor(std::size_t capacity) noexcept
{
    return kFrameHeadBytes + kSlots * slotBytes(capacity);
}

std::shared_ptr<FrameRing> FrameRing::make(std::size_t capacity)
{
    if (capacity == 0 || capacity > kMostFrameCapacity) {
        return nullptr;
    }
    std::optional<Section> section = Section::make(bytesFor(capacity));
    if (!section) {
        return nullptr;
    }
    FrameHead* head = new (section->data()) FrameHead{};
    head->capacity = capacity;
    return std::shared_ptr<FrameRing>(new FrameRing(std::move(*section), capacity));
}

std::shared_ptr<FrameRing> FrameRing::adopt(HANDLE handle, std::size_t capacity)
{
    if (capacity == 0 || capacity > kMostFrameCapacity) {
        closeHandle(handle);
        return nullptr;
    }
    std::optional<Section> section = Section::adopt(handle, bytesFor(capacity));
    if (!section) {
        return nullptr;
    }
    const FrameHead& head = frameHead(*section);
    if (head.magic != kFrameMagic || head.version != kLayoutVersion || head.capacity != capacity) {
        return nullptr;
    }
    return std::shared_ptr<FrameRing>(new FrameRing(std::move(*section), capacity));
}

std::byte* FrameRing::slotAt(std::uint32_t slot) const noexcept
{
    return m_section.data() + kFrameHeadBytes + std::size_t{slot} * slotBytes(m_capacity);
}

bool FrameRing::write(const FrameView& frame)
{
    if (!checkFrame(frame).empty() || packedBytes(frame.format, frame.width, frame.height) > m_capacity) {
        return false;
    }
    if (m_back >= kSlots) {
        m_back = 0;
    }
    std::byte* slot = slotAt(m_back);
    std::byte* pixels = slot + kSlotHeadBytes;
    const bool bgra = frame.format == PixelFormat::Bgra;
    const std::uint32_t stride = bgra ? frame.width * 4 : frame.width;
    for (std::uint32_t row = 0; row < frame.height; ++row) {
        std::memcpy(pixels + std::size_t{row} * stride, frame.data + std::size_t{row} * frame.stride, stride);
    }
    std::uint64_t chromaOffset = 0;
    if (!bgra) {
        chromaOffset = std::uint64_t{stride} * frame.height;
        for (std::uint32_t row = 0; row < frame.height / 2; ++row) {
            std::memcpy(pixels + chromaOffset + std::size_t{row} * stride,
                        frame.chroma + std::size_t{row} * frame.chromaStride, stride);
        }
    }
    const SlotHead head{.format = static_cast<std::uint32_t>(frame.format),
                        .width = frame.width,
                        .height = frame.height,
                        .stride = stride,
                        .chromaStride = bgra ? 0U : stride,
                        .reserved = 0,
                        .chromaOffset = chromaOffset,
                        .timeUs = frame.timeUs};
    std::memcpy(slot, &head, sizeof(head));
    const std::uint32_t previous = frameHead(m_section).state.exchange(m_back | kFresh, std::memory_order_acq_rel);
    m_back = previous & kIndex;
    return true;
}

std::optional<FrameView> FrameRing::read()
{
    FrameHead& shared = frameHead(m_section);
    if ((shared.state.load(std::memory_order_acquire) & kFresh) == 0) {
        return std::nullopt;
    }
    const std::uint32_t previous = shared.state.exchange(m_front, std::memory_order_acq_rel);
    const std::uint32_t slot = previous & kIndex;
    if (slot >= kSlots) {
        return std::nullopt;
    }
    m_front = slot;
    SlotHead head;
    std::memcpy(&head, slotAt(slot), sizeof(head));
    const auto format = static_cast<PixelFormat>(head.format);
    if ((format != PixelFormat::Bgra && format != PixelFormat::Nv12) || head.width < 2 || head.height < 2 ||
        head.width > kMostFrameSide || head.height > kMostFrameSide) {
        return std::nullopt;
    }
    const std::size_t needed = packedBytes(format, head.width, head.height);
    const std::uint32_t stride = format == PixelFormat::Bgra ? head.width * 4 : head.width;
    const bool planar = format == PixelFormat::Nv12;
    if (needed == 0 || needed > m_capacity || head.stride != stride ||
        (planar && (head.chromaStride != stride || head.chromaOffset != std::uint64_t{stride} * head.height))) {
        return std::nullopt;
    }
    const auto* pixels = reinterpret_cast<const std::uint8_t*>(slotAt(slot) + kSlotHeadBytes);
    return FrameView{.format = format,
                     .width = head.width,
                     .height = head.height,
                     .stride = stride,
                     .chromaStride = planar ? stride : 0U,
                     .data = pixels,
                     .chroma = planar ? pixels + head.chromaOffset : nullptr,
                     .timeUs = head.timeUs};
}

SoundRing::SoundRing(Section section, std::size_t capacity) noexcept
    : m_section(std::move(section))
    , m_capacity(capacity)
{
}

std::size_t SoundRing::bytesFor(std::size_t capacity) noexcept
{
    return kSoundHeadBytes + capacity;
}

std::shared_ptr<SoundRing> SoundRing::make(std::size_t capacity)
{
    if (capacity < kLeastSoundCapacity || capacity > kSoundRingBytes || (capacity & (capacity - 1)) != 0) {
        return nullptr;
    }
    std::optional<Section> section = Section::make(bytesFor(capacity));
    if (!section) {
        return nullptr;
    }
    SoundHead* head = new (section->data()) SoundHead{};
    head->capacity = capacity;
    return std::shared_ptr<SoundRing>(new SoundRing(std::move(*section), capacity));
}

std::shared_ptr<SoundRing> SoundRing::adopt(HANDLE handle, std::size_t capacity)
{
    if (capacity < kLeastSoundCapacity || capacity > kSoundRingBytes || (capacity & (capacity - 1)) != 0) {
        closeHandle(handle);
        return nullptr;
    }
    std::optional<Section> section = Section::adopt(handle, bytesFor(capacity));
    if (!section) {
        return nullptr;
    }
    const SoundHead& head = soundHead(*section);
    if (head.magic != kSoundMagic || head.version != kLayoutVersion || head.capacity != capacity) {
        return nullptr;
    }
    return std::shared_ptr<SoundRing>(new SoundRing(std::move(*section), capacity));
}

void SoundRing::copyIn(std::uint64_t position, const void* source, std::size_t size) const
{
    std::byte* data = m_section.data() + kSoundHeadBytes;
    const auto offset = static_cast<std::size_t>(position & (m_capacity - 1));
    const std::size_t first = std::min(size, m_capacity - offset);
    std::memcpy(data + offset, source, first);
    if (size > first) {
        std::memcpy(data, static_cast<const std::byte*>(source) + first, size - first);
    }
}

void SoundRing::copyOut(std::uint64_t position, void* target, std::size_t size) const
{
    const std::byte* data = m_section.data() + kSoundHeadBytes;
    const auto offset = static_cast<std::size_t>(position & (m_capacity - 1));
    const std::size_t first = std::min(size, m_capacity - offset);
    std::memcpy(target, data + offset, first);
    if (size > first) {
        std::memcpy(static_cast<std::byte*>(target) + first, data, size - first);
    }
}

bool SoundRing::write(const SoundView& sound)
{
    if (!checkSound(sound).empty()) {
        return false;
    }
    const std::size_t sampleBytes = sound.format == SampleFormat::S16 ? 2 : 4;
    const std::size_t bytes = std::size_t{sound.frames} * sound.channels * sampleBytes;
    const std::uint64_t need = sizeof(SoundRecord) + padded(bytes);
    SoundHead& shared = soundHead(m_section);
    const std::uint64_t head = shared.head.load(std::memory_order_relaxed);
    const std::uint64_t tail = shared.tail.load(std::memory_order_acquire);
    const std::uint64_t used = head - tail;
    if (used > m_capacity || need > m_capacity - used) {
        return false;
    }
    const SoundRecord record{.bytes = static_cast<std::uint32_t>(bytes),
                             .format = static_cast<std::uint32_t>(sound.format),
                             .channels = sound.channels,
                             .rate = sound.rate,
                             .frames = sound.frames,
                             .reserved = 0,
                             .timeUs = sound.timeUs};
    copyIn(head, &record, sizeof(record));
    copyIn(head + sizeof(record), sound.data, bytes);
    shared.head.store(head + need, std::memory_order_release);
    return true;
}

std::optional<SoundBlock> SoundRing::read()
{
    SoundHead& shared = soundHead(m_section);
    const std::uint64_t tail = shared.tail.load(std::memory_order_relaxed);
    const std::uint64_t head = shared.head.load(std::memory_order_acquire);
    if (head == tail) {
        return std::nullopt;
    }
    const std::uint64_t used = head - tail;
    SoundRecord record;
    if (used > m_capacity || used < sizeof(record)) {
        shared.tail.store(head, std::memory_order_release);
        return std::nullopt;
    }
    copyOut(tail, &record, sizeof(record));
    const auto format = static_cast<SampleFormat>(record.format);
    const std::uint64_t sampleBytes = format == SampleFormat::S16 ? 2 : format == SampleFormat::F32 ? 4 : 0;
    const std::uint64_t bytes = std::uint64_t{record.frames} * record.channels * sampleBytes;
    const std::uint64_t need = sizeof(record) + padded(bytes);
    if (sampleBytes == 0 || record.channels < 1 || record.channels > kMostChannels || record.frames < 1 ||
        record.frames > kMostSoundFrames || record.rate < 8'000 || record.rate > 192'000 || record.bytes != bytes ||
        need > used) {
        shared.tail.store(head, std::memory_order_release);
        return std::nullopt;
    }
    SoundBlock block;
    block.channels = record.channels;
    block.rate = record.rate;
    block.timeUs = record.timeUs;
    const std::size_t count = std::size_t{record.frames} * record.channels;
    block.samples.resize(count);
    if (format == SampleFormat::F32) {
        copyOut(tail + sizeof(record), block.samples.data(), static_cast<std::size_t>(bytes));
    } else {
        std::vector<std::int16_t> raw(count);
        copyOut(tail + sizeof(record), raw.data(), static_cast<std::size_t>(bytes));
        std::ranges::transform(raw, block.samples.begin(),
                               [](std::int16_t sample) { return static_cast<float>(sample) / 32768.0F; });
    }
    shared.tail.store(tail + need, std::memory_order_release);
    return block;
}

StreamBox::StreamBox(std::uint64_t id, HANDLE wake, Hooks hooks)
    : m_id(id)
    , m_wake(wake != nullptr ? wake : CreateEventW(nullptr, FALSE, FALSE, nullptr))
    , m_hooks(std::move(hooks))
{
}

StreamBox::~StreamBox()
{
    closeHandle(m_wake);
}

bool StreamBox::wait(const std::stop_token& stop)
{
    if (m_wake == nullptr) {
        return false;
    }
    const HANDLE wake = m_wake;
    const std::stop_callback wakeUp(stop, [wake] { SetEvent(wake); });
    {
        const std::scoped_lock lock(m_mutex);
        if (m_ended) {
            return !stop.stop_requested();
        }
    }
    WaitForSingleObject(wake, INFINITE);
    return !stop.stop_requested();
}

std::optional<FrameView> StreamBox::frame()
{
    const std::scoped_lock reading(m_reading);
    std::shared_ptr<FrameRing> ring;
    {
        const std::scoped_lock lock(m_mutex);
        ring = m_frames;
    }
    if (ring == nullptr) {
        return std::nullopt;
    }
    std::optional<FrameView> view = ring->read();
    if (view) {
        m_shown = std::move(ring);
    }
    return view;
}

std::optional<SoundBlock> StreamBox::sound()
{
    const std::scoped_lock reading(m_reading);
    std::shared_ptr<SoundRing> ring;
    {
        const std::scoped_lock lock(m_mutex);
        ring = m_sounds;
    }
    return ring != nullptr ? ring->read() : std::nullopt;
}

std::optional<std::string> StreamBox::ended() const
{
    const std::scoped_lock lock(m_mutex);
    return m_ended;
}

Written StreamBox::writeFrame(const FrameView& frame)
{
    if (!checkFrame(frame).empty()) {
        return Written::Refused;
    }
    const std::size_t needed = packedBytes(frame.format, frame.width, frame.height);
    const std::scoped_lock writing(m_writing);
    std::shared_ptr<FrameRing> ring;
    {
        const std::scoped_lock lock(m_mutex);
        if (m_ended) {
            return Written::Closed;
        }
        ring = m_frames;
    }
    if (ring == nullptr || ring->capacity() < needed) {
        ring = FrameRing::make(needed);
        if (ring == nullptr) {
            return Written::Refused;
        }
        if (m_hooks.made) {
            m_hooks.made(MediaKind::Video, ring->handle(), ring->capacity());
        }
        const std::scoped_lock lock(m_mutex);
        if (m_frames != nullptr) {
            m_retired.push_back(std::move(m_frames));
            if (m_retired.size() > kMostRetired) {
                m_retired.erase(m_retired.begin());
            }
        }
        m_frames = ring;
    }
    if (!ring->write(frame)) {
        return Written::Refused;
    }
    SetEvent(m_wake);
    return Written::Yes;
}

Written StreamBox::writeSound(const SoundView& sound)
{
    if (!checkSound(sound).empty()) {
        return Written::Refused;
    }
    const std::scoped_lock writing(m_writing);
    std::shared_ptr<SoundRing> ring;
    {
        const std::scoped_lock lock(m_mutex);
        if (m_ended) {
            return Written::Closed;
        }
        ring = m_sounds;
    }
    if (ring == nullptr) {
        ring = SoundRing::make();
        if (ring == nullptr) {
            return Written::Refused;
        }
        if (m_hooks.made) {
            m_hooks.made(MediaKind::Audio, ring->handle(), ring->capacity());
        }
        const std::scoped_lock lock(m_mutex);
        m_sounds = ring;
    }
    static_cast<void>(ring->write(sound));
    SetEvent(m_wake);
    return Written::Yes;
}

void StreamBox::adoptFrames(std::shared_ptr<FrameRing> ring)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (m_frames != nullptr) {
            m_retired.push_back(std::move(m_frames));
            if (m_retired.size() > kMostRetired) {
                m_retired.erase(m_retired.begin());
            }
        }
        m_frames = std::move(ring);
    }
    SetEvent(m_wake);
}

void StreamBox::adoptSounds(std::shared_ptr<SoundRing> ring)
{
    {
        const std::scoped_lock lock(m_mutex);
        m_sounds = std::move(ring);
    }
    SetEvent(m_wake);
}

void StreamBox::end(const std::string& why)
{
    finish(why, true);
}

void StreamBox::close()
{
    finish("the source was closed", false);
}

void StreamBox::finish(const std::string& why, bool tell)
{
    {
        const std::scoped_lock lock(m_mutex);
        if (m_ended) {
            return;
        }
        m_ended = why.empty() ? std::string("the source stopped") : why;
    }
    if (m_wake != nullptr) {
        SetEvent(m_wake);
    }
    if (tell && m_hooks.ended) {
        m_hooks.ended(why);
    }
}

SinkBox::SinkBox(std::uint64_t id, SinkStart start, std::shared_ptr<FrameRing> frames, std::shared_ptr<SoundRing> sounds,
                 HANDLE wake) noexcept
    : m_id(id)
    , m_start(std::move(start))
    , m_frames(std::move(frames))
    , m_sounds(std::move(sounds))
    , m_wake(wake)
{
}

SinkBox::~SinkBox()
{
    closeHandle(m_wake);
}

std::shared_ptr<SinkBox> SinkBox::make(std::uint64_t id, const SinkStart& start)
{
    std::shared_ptr<FrameRing> frames;
    if (start.width > 0 && start.height > 0) {
        frames = FrameRing::make(packedBytes(PixelFormat::Bgra, start.width, start.height));
        if (frames == nullptr) {
            return nullptr;
        }
    }
    std::shared_ptr<SoundRing> sounds;
    if (start.audio) {
        sounds = SoundRing::make();
        if (sounds == nullptr) {
            return nullptr;
        }
    }
    const HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (wake == nullptr) {
        return nullptr;
    }
    return std::shared_ptr<SinkBox>(new SinkBox(id, start, std::move(frames), std::move(sounds), wake));
}

std::shared_ptr<SinkBox> SinkBox::adopt(std::uint64_t id, const SinkStart& start, HANDLE frames, HANDLE sounds,
                                        HANDLE wake)
{
    std::shared_ptr<FrameRing> pictures;
    bool good = wake != nullptr;
    if (start.width > 0 && start.height > 0) {
        pictures = FrameRing::adopt(frames, packedBytes(PixelFormat::Bgra, start.width, start.height));
        good = good && pictures != nullptr;
    } else {
        closeHandle(frames);
    }
    std::shared_ptr<SoundRing> sound;
    if (start.audio) {
        sound = SoundRing::adopt(sounds, kSoundRingBytes);
        good = good && sound != nullptr;
    } else {
        closeHandle(sounds);
    }
    if (!good) {
        closeHandle(wake);
        return nullptr;
    }
    return std::shared_ptr<SinkBox>(new SinkBox(id, start, std::move(pictures), std::move(sound), wake));
}

bool SinkBox::frame(const FrameView& frame)
{
    if (m_frames == nullptr || frame.format != PixelFormat::Bgra || frame.width != m_start.width ||
        frame.height != m_start.height || ended()) {
        return false;
    }
    {
        const std::scoped_lock writing(m_framesWriting);
        if (!m_frames->write(frame)) {
            return false;
        }
    }
    SetEvent(m_wake);
    return true;
}

bool SinkBox::sound(const SoundView& sound)
{
    if (m_sounds == nullptr || ended()) {
        return false;
    }
    {
        const std::scoped_lock writing(m_soundsWriting);
        static_cast<void>(m_sounds->write(sound));
    }
    SetEvent(m_wake);
    return true;
}

std::optional<std::string> SinkBox::ended() const
{
    const std::scoped_lock lock(m_mutex);
    return m_ended;
}

bool SinkBox::wait(const std::stop_token& stop)
{
    if (m_wake == nullptr) {
        return false;
    }
    const HANDLE wake = m_wake;
    const std::stop_callback wakeUp(stop, [wake] { SetEvent(wake); });
    {
        const std::scoped_lock lock(m_mutex);
        if (m_ended) {
            return !stop.stop_requested();
        }
    }
    WaitForSingleObject(wake, INFINITE);
    return !stop.stop_requested();
}

std::optional<FrameView> SinkBox::nextFrame()
{
    return m_frames != nullptr ? m_frames->read() : std::nullopt;
}

std::optional<SoundBlock> SinkBox::nextSound()
{
    return m_sounds != nullptr ? m_sounds->read() : std::nullopt;
}

void SinkBox::setEnded(std::function<void(const std::string& why)> ended)
{
    const std::scoped_lock lock(m_mutex);
    m_tell = std::move(ended);
}

void SinkBox::end(const std::string& why)
{
    std::function<void(const std::string& why)> tell;
    {
        const std::scoped_lock lock(m_mutex);
        if (m_ended) {
            return;
        }
        m_ended = why.empty() ? std::string("the sink stopped") : why;
        tell = m_tell;
    }
    if (m_wake != nullptr) {
        SetEvent(m_wake);
    }
    if (tell) {
        tell(why);
    }
}

SinkPump::SinkPump(std::shared_ptr<SinkBox> box, Frame frame, Sound sound)
    : m_box(std::move(box))
    , m_frame(std::move(frame))
    , m_sound(std::move(sound))
    , m_thread([this](const std::stop_token& stop) { run(stop); })
{
}

SinkPump::~SinkPump() = default;

void SinkPump::run(const std::stop_token& stop)
{
    while (m_box->wait(stop)) {
        while (std::optional<SoundBlock> block = m_box->nextSound()) {
            if (!m_sound || block->channels == 0) {
                continue;
            }
            const SoundView view{.format = SampleFormat::F32,
                                 .channels = block->channels,
                                 .rate = block->rate,
                                 .frames = static_cast<std::uint32_t>(block->samples.size() / block->channels),
                                 .data = block->samples.data(),
                                 .timeUs = block->timeUs};
            if (!m_sound(view)) {
                m_box->end("the extension did not take the sound");
                return;
            }
        }
        if (const std::optional<FrameView> frame = m_box->nextFrame(); frame && m_frame && !m_frame(*frame)) {
            m_box->end("the extension did not take the picture");
            return;
        }
        if (m_box->ended()) {
            return;
        }
    }
}

}
