#include "gs/media/FrameSnapshot.h"

#include "gs/media/VideoDecoder.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>

namespace gs::media {

namespace {

using Microsoft::WRL::ComPtr;

constexpr int kMostSlots = 8;
constexpr unsigned kWantLuma = 1U;
constexpr unsigned kWantColor = 2U;

struct Region {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
};

struct Slot {
    ComPtr<ID3D11Texture2D> staging;
    UINT width = 0;
    UINT height = 0;
    bool pending = false;
    bool color = false;
    std::uint64_t order = 0;
    LumaSnapshot meta;
    int left = 0;
    int top = 0;
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

std::shared_ptr<LumaSnapshot> blankFor(const LumaSnapshot& meta, bool color)
{
    auto snapshot = std::make_shared<LumaSnapshot>(meta);
    snapshot->luma.resize(static_cast<std::size_t>(meta.width) * static_cast<std::size_t>(meta.height));
    if (color) {
        snapshot->chroma.resize(std::size_t{2} * static_cast<std::size_t>(meta.chromaWidth())
                                * static_cast<std::size_t>(meta.chromaHeight()));
    }
    return snapshot;
}

void copyRows(std::uint8_t* to, const std::uint8_t* from, std::ptrdiff_t pitch, int width, int height)
{
    for (int y = 0; y < height; ++y) {
        std::memcpy(to + static_cast<std::ptrdiff_t>(y) * width, from + static_cast<std::ptrdiff_t>(y) * pitch,
                    static_cast<std::size_t>(width));
    }
}

void interleave(std::uint8_t* to, const AVFrame& frame, int width, int height, int left, int top)
{
    for (int y = 0; y < height; ++y) {
        std::uint8_t* row = to + static_cast<std::ptrdiff_t>(y) * 2 * width;
        const std::uint8_t* cb =
            frame.data[1] + static_cast<std::ptrdiff_t>(y + top / 2) * frame.linesize[1] + left / 2;
        const std::uint8_t* cr =
            frame.data[2] + static_cast<std::ptrdiff_t>(y + top / 2) * frame.linesize[2] + left / 2;
        for (int x = 0; x < width; ++x) {
            row[2 * x] = cb[x];
            row[2 * x + 1] = cr[x];
        }
    }
}

bool inMemory(int format) noexcept
{
    return format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P || format == AV_PIX_FMT_NV12;
}

void copyMemory(LumaSnapshot& made, const AVFrame& frame, int left, int top)
{
    copyRows(made.luma.data(), frame.data[0] + static_cast<std::ptrdiff_t>(top) * frame.linesize[0] + left,
             frame.linesize[0], made.width, made.height);
    if (!made.colored()) {
        return;
    }
    if (frame.format == AV_PIX_FMT_NV12) {
        copyRows(made.chroma.data(), frame.data[1] + static_cast<std::ptrdiff_t>(top / 2) * frame.linesize[1] + left,
                 frame.linesize[1], 2 * made.chromaWidth(), made.chromaHeight());
    } else {
        interleave(made.chroma.data(), frame, made.chromaWidth(), made.chromaHeight(), left, top);
    }
}

void copyMapped(LumaSnapshot& made, const D3D11_MAPPED_SUBRESOURCE& mapped, UINT textureHeight, int left, int top)
{
    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
    const auto pitch = static_cast<std::ptrdiff_t>(mapped.RowPitch);
    copyRows(made.luma.data(), pixels + pitch * top + left, pitch, made.width, made.height);
    if (made.colored()) {
        copyRows(made.chroma.data(),
                 pixels + pitch * static_cast<std::ptrdiff_t>(textureHeight) + pitch * (top / 2) + left, pitch,
                 2 * made.chromaWidth(), made.chromaHeight());
    }
}

bool stagingFits(const ComPtr<ID3D11Texture2D>& staging, UINT width, UINT height) noexcept
{
    if (!staging) {
        return false;
    }
    D3D11_TEXTURE2D_DESC description{};
    staging->GetDesc(&description);
    return description.Width == width && description.Height == height;
}

HRESULT makeStaging(ID3D11Device& device, UINT width, UINT height, ComPtr<ID3D11Texture2D>& staging)
{
    D3D11_TEXTURE2D_DESC layout{};
    layout.Width = width;
    layout.Height = height;
    layout.MipLevels = 1;
    layout.ArraySize = 1;
    layout.Format = DXGI_FORMAT_NV12;
    layout.SampleDesc.Count = 1;
    layout.Usage = D3D11_USAGE_STAGING;
    layout.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging.Reset();
    return device.CreateTexture2D(&layout, nullptr, &staging);
}

}

struct FrameSnapshots::State {
    enum class Copy : std::uint8_t {
        Queued,
        RingFull,
        Failed,
    };

    State() : last(av_frame_alloc()) {}

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    std::vector<Slot> ring;
    std::atomic<unsigned> wanted{0};
    std::atomic<std::uint64_t> offered{0};
    std::uint64_t submitted = 0;
    std::size_t pending = 0;
    mutable std::mutex mutex;
    std::uint64_t sequence = 0;
    Listener listener;
    std::shared_ptr<const LumaSnapshot> latest;
    SnapshotCounters counters;
    std::mutex lastMutex;
    FramePtr last;
    LumaSnapshot lastMeta;
    int lastLeft = 0;
    int lastTop = 0;
    std::mutex captureMutex;
    ComPtr<ID3D11Texture2D> captureStaging;
    std::mutex cropMutex;
    std::array<double, 4> crop{0.0, 0.0, 1.0, 1.0};
    bool cropped = false;
    std::mutex watchMutex;
    std::map<std::uint64_t, Watcher> watchers;
    std::uint64_t lastWatcher = 0;

    void tell()
    {
        const std::scoped_lock lock(watchMutex);
        for (const auto& [id, watcher] : watchers) {
            watcher();
        }
    }

    void count(std::uint64_t SnapshotCounters::* field)
    {
        const std::scoped_lock lock(mutex);
        ++(counters.*field);
    }

    std::uint64_t nextSequence()
    {
        const std::scoped_lock lock(mutex);
        return ++sequence;
    }

    Region regionOf(int width, int height)
    {
        std::array<double, 4> box{};
        bool on = false;
        {
            const std::scoped_lock lock(cropMutex);
            box = crop;
            on = cropped;
        }
        if (!on || width < 2 || height < 2) {
            return {0, 0, width, height};
        }
        const auto even = [](int value) { return value & ~1; };
        const int left = std::clamp(even(static_cast<int>(std::floor(box[0] * width + 1e-6))), 0, even(width) - 2);
        const int top = std::clamp(even(static_cast<int>(std::floor(box[1] * height + 1e-6))), 0, even(height) - 2);
        const int right = std::clamp(even(static_cast<int>(std::ceil((box[0] + box[2]) * width - 1e-6)) + 1),
                                     left + 2, even(width));
        const int bottom = std::clamp(even(static_cast<int>(std::ceil((box[1] + box[3]) * height - 1e-6)) + 1),
                                      top + 2, even(height));
        return {left, top, right - left, bottom - top};
    }

    void keep(const AVFrame& frame, const LumaSnapshot& meta, const Region& region)
    {
        const std::scoped_lock lock(lastMutex);
        if (last == nullptr) {
            return;
        }
        av_frame_unref(last.get());
        lastMeta = {};
        lastLeft = 0;
        lastTop = 0;
        if (frame.buf[0] != nullptr && av_frame_ref(last.get(), &frame) >= 0) {
            lastMeta = meta;
            lastLeft = region.left;
            lastTop = region.top;
        }
    }

    void publish(std::shared_ptr<LumaSnapshot> made)
    {
        std::shared_ptr<const LumaSnapshot> shared = std::move(made);
        Listener notify;
        {
            const std::scoped_lock lock(mutex);
            latest = shared;
            ++counters.made;
            notify = listener;
        }
        if (notify) {
            notify(shared);
        }
    }

    Copy copySurface(const AVFrame& frame, const LumaSnapshot& meta, bool color, const Region& region)
    {
        if (!context) {
            return Copy::Failed;
        }
        auto* texture = reinterpret_cast<ID3D11Texture2D*>(frame.data[0]);
        if (texture == nullptr) {
            return Copy::Failed;
        }
        const auto slice = static_cast<UINT>(reinterpret_cast<std::intptr_t>(frame.data[1]));
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        if (description.Format != DXGI_FORMAT_NV12 ||
            static_cast<UINT>(region.left + meta.width) > description.Width ||
            static_cast<UINT>(region.top + meta.height) > description.Height) {
            return Copy::Failed;
        }
        const auto vacant = std::ranges::find_if(ring, [](const Slot& slot) { return !slot.pending; });
        if (vacant == ring.end()) {
            return Copy::RingFull;
        }
        if (!vacant->staging || vacant->width != description.Width || vacant->height != description.Height) {
            vacant->width = 0;
            vacant->height = 0;
            if (FAILED(makeStaging(*device.Get(), description.Width, description.Height, vacant->staging))) {
                return Copy::Failed;
            }
            vacant->width = description.Width;
            vacant->height = description.Height;
        }
        context->CopySubresourceRegion(vacant->staging.Get(), 0, 0, 0, 0, texture,
                                       D3D11CalcSubresource(0, slice, description.MipLevels), nullptr);
        vacant->pending = true;
        vacant->color = color;
        vacant->order = ++submitted;
        vacant->meta = meta;
        vacant->meta.sequence = nextSequence();
        vacant->left = region.left;
        vacant->top = region.top;
        ++pending;
        count(&SnapshotCounters::copied);
        return Copy::Queued;
    }

    std::shared_ptr<LumaSnapshot> captureSurface(const AVFrame& frame, const LumaSnapshot& meta, bool color, int left,
                                                 int top)
    {
        auto* texture = reinterpret_cast<ID3D11Texture2D*>(frame.data[0]);
        if (!context || texture == nullptr) {
            return nullptr;
        }
        const auto slice = static_cast<UINT>(reinterpret_cast<std::intptr_t>(frame.data[1]));
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        if (description.Format != DXGI_FORMAT_NV12 || static_cast<UINT>(left + meta.width) > description.Width ||
            static_cast<UINT>(top + meta.height) > description.Height) {
            return nullptr;
        }
        const std::scoped_lock lock(captureMutex);
        if (!stagingFits(captureStaging, description.Width, description.Height)
            && FAILED(makeStaging(*device.Get(), description.Width, description.Height, captureStaging))) {
            return nullptr;
        }
        context->CopySubresourceRegion(captureStaging.Get(), 0, 0, 0, 0, texture,
                                       D3D11CalcSubresource(0, slice, description.MipLevels), nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(captureStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            return nullptr;
        }
        auto made = blankFor(meta, color);
        copyMapped(*made, mapped, description.Height, left, top);
        context->Unmap(captureStaging.Get(), 0);
        return made;
    }

    Slot* oldestPending()
    {
        Slot* oldest = nullptr;
        for (Slot& slot : ring) {
            if (slot.pending && (oldest == nullptr || slot.order < oldest->order)) {
                oldest = &slot;
            }
        }
        return oldest;
    }

    bool map(Slot& slot, UINT flags)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result = context->Map(slot.staging.Get(), 0, D3D11_MAP_READ, flags, &mapped);
        if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
            count(&SnapshotCounters::stillDrawing);
            return false;
        }
        slot.pending = false;
        --pending;
        if (FAILED(result)) {
            count(&SnapshotCounters::failed);
            return true;
        }
        auto made = blankFor(slot.meta, slot.color);
        copyMapped(*made, mapped, slot.height, slot.left, slot.top);
        context->Unmap(slot.staging.Get(), 0);
        publish(std::move(made));
        return true;
    }
};

FrameSnapshots::FrameSnapshots(ID3D11Device* device, int depth) : m_state(std::make_unique<State>())
{
    if (device != nullptr) {
        m_state->device = device;
        device->GetImmediateContext(&m_state->context);
    }
    m_state->ring.resize(static_cast<std::size_t>(std::clamp(depth, 1, kMostSlots)));
}

FrameSnapshots::~FrameSnapshots() = default;

void FrameSnapshots::setListener(Listener listener)
{
    const std::scoped_lock lock(m_state->mutex);
    m_state->listener = std::move(listener);
}

void FrameSnapshots::setCrop(double x, double y, double width, double height)
{
    const std::scoped_lock lock(m_state->cropMutex);
    m_state->crop = {x, y, width, height};
    m_state->cropped = width > 0.0 && height > 0.0 && (x > 0.0 || y > 0.0 || x + width < 1.0 || y + height < 1.0);
}

void FrameSnapshots::clearCrop()
{
    const std::scoped_lock lock(m_state->cropMutex);
    m_state->crop = {0.0, 0.0, 1.0, 1.0};
    m_state->cropped = false;
}

void FrameSnapshots::request(bool color) noexcept
{
    m_state->wanted.fetch_or(color ? kWantLuma | kWantColor : kWantLuma, std::memory_order_acq_rel);
}

void FrameSnapshots::offer(const VideoFrame& frame)
{
    if (!frame) {
        poll();
        return;
    }
    offer(*frame.avFrame(), frame.ptsUs(), frame.session());
}

void FrameSnapshots::offer(const AVFrame& frame, std::int64_t ptsUs, std::uint16_t session)
{
    State& state = *m_state;
    if (state.pending > 0) {
        poll();
    }
    LumaSnapshot meta;
    meta.frame = state.offered.fetch_add(1, std::memory_order_acq_rel) + 1;
    meta.ptsUs = ptsUs;
    meta.session = session;
    const Region region = state.regionOf(frame.width, frame.height);
    meta.width = region.width;
    meta.height = region.height;
    state.keep(frame, meta, region);
    state.tell();
    if (state.wanted.load(std::memory_order_acquire) == 0) {
        return;
    }
    const unsigned wanted = state.wanted.exchange(0, std::memory_order_acq_rel);
    if (wanted == 0) {
        return;
    }
    const bool color = (wanted & kWantColor) != 0;
    if (frame.width <= 0 || frame.height <= 0) {
        state.count(&SnapshotCounters::failed);
        return;
    }
    if (color) {
        meta.matrix = yuvToRgb(frame);
    }
    if (inMemory(frame.format)) {
        meta.sequence = state.nextSequence();
        auto made = blankFor(meta, color);
        copyMemory(*made, frame, region.left, region.top);
        state.publish(std::move(made));
        return;
    }
    if (frame.format != AV_PIX_FMT_D3D11) {
        state.count(&SnapshotCounters::failed);
        return;
    }
    const State::Copy copied = state.copySurface(frame, meta, color, region);
    if (copied == State::Copy::RingFull) {
        state.count(&SnapshotCounters::ringFull);
        state.wanted.fetch_or(wanted, std::memory_order_acq_rel);
    } else if (copied == State::Copy::Failed) {
        state.count(&SnapshotCounters::failed);
    }
}

void FrameSnapshots::poll()
{
    State& state = *m_state;
    while (state.pending > 0) {
        Slot* oldest = state.oldestPending();
        if (oldest == nullptr || !state.map(*oldest, D3D11_MAP_FLAG_DO_NOT_WAIT)) {
            return;
        }
    }
}

void FrameSnapshots::finish()
{
    State& state = *m_state;
    while (state.pending > 0) {
        Slot* oldest = state.oldestPending();
        if (oldest == nullptr) {
            return;
        }
        static_cast<void>(state.map(*oldest, 0));
    }
}

void FrameSnapshots::forget()
{
    const std::scoped_lock lock(m_state->lastMutex);
    if (m_state->last != nullptr) {
        av_frame_unref(m_state->last.get());
    }
    m_state->lastMeta = {};
}

std::shared_ptr<const LumaSnapshot> FrameSnapshots::capture(bool color)
{
    State& state = *m_state;
    const FramePtr held(av_frame_alloc());
    if (held == nullptr) {
        return nullptr;
    }
    LumaSnapshot meta;
    int left = 0;
    int top = 0;
    {
        const std::scoped_lock lock(state.lastMutex);
        if (state.last == nullptr || state.last->buf[0] == nullptr || av_frame_ref(held.get(), state.last.get()) < 0) {
            return nullptr;
        }
        meta = state.lastMeta;
        left = state.lastLeft;
        top = state.lastTop;
    }
    if (meta.width <= 0 || meta.height <= 0) {
        return nullptr;
    }
    if (color) {
        meta.matrix = yuvToRgb(*held);
    }
    std::shared_ptr<LumaSnapshot> made;
    if (inMemory(held->format)) {
        made = blankFor(meta, color);
        copyMemory(*made, *held, left, top);
    } else if (held->format == AV_PIX_FMT_D3D11) {
        made = state.captureSurface(*held, meta, color, left, top);
    }
    if (made == nullptr) {
        state.count(&SnapshotCounters::failed);
        return nullptr;
    }
    made->sequence = state.nextSequence();
    std::shared_ptr<const LumaSnapshot> shared = std::move(made);
    const std::scoped_lock lock(state.mutex);
    state.latest = shared;
    ++state.counters.made;
    ++state.counters.captured;
    return shared;
}

std::uint64_t FrameSnapshots::offered() const noexcept
{
    return m_state->offered.load(std::memory_order_acquire);
}

std::shared_ptr<const LumaSnapshot> FrameSnapshots::latest() const
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->latest;
}

SnapshotCounters FrameSnapshots::counters() const
{
    const std::scoped_lock lock(m_state->mutex);
    return m_state->counters;
}

std::uint64_t FrameSnapshots::watch(Watcher watcher)
{
    const std::scoped_lock lock(m_state->watchMutex);
    const std::uint64_t id = ++m_state->lastWatcher;
    m_state->watchers.emplace(id, std::move(watcher));
    return id;
}

void FrameSnapshots::unwatch(std::uint64_t watcher)
{
    const std::scoped_lock lock(m_state->watchMutex);
    m_state->watchers.erase(watcher);
}

std::vector<std::uint8_t> rgbOf(const LumaSnapshot& shot, int left, int top, int width, int height)
{
    const std::size_t chromaRow = std::size_t{2} * static_cast<std::size_t>(shot.chromaWidth());
    if (!shot.colored() || width <= 0 || height <= 0 || left < 0 || top < 0 || left > shot.width - width
        || top > shot.height - height
        || shot.luma.size() < static_cast<std::size_t>(shot.width) * static_cast<std::size_t>(shot.height)
        || shot.chroma.size() < chromaRow * static_cast<std::size_t>(shot.chromaHeight())) {
        return {};
    }
    const auto& rows = shot.matrix.rows;
    std::vector<std::uint8_t> rgb(std::size_t{3} * static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* luma = shot.luma.data() + static_cast<std::size_t>(top + y) * static_cast<std::size_t>(shot.width);
        const std::uint8_t* chroma = shot.chroma.data() + static_cast<std::size_t>((top + y) / 2) * chromaRow;
        std::uint8_t* out = rgb.data() + std::size_t{3} * static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
            const int column = left + x;
            const auto light = static_cast<float>(luma[column]);
            const auto blue = static_cast<float>(chroma[2 * (column / 2)]);
            const auto red = static_cast<float>(chroma[2 * (column / 2) + 1]);
            for (std::size_t channel = 0; channel < rows.size(); ++channel) {
                const auto& row = rows[channel];
                const float value = row[0] * light + row[1] * blue + row[2] * red + row[3] * 255.0F;
                out[3 * x + static_cast<int>(channel)] =
                    static_cast<std::uint8_t>(std::clamp(value, 0.0F, 255.0F) + 0.5F);
            }
        }
    }
    return rgb;
}

}
