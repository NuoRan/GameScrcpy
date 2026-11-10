#include "gs/capture/AudioCapture.h"

#include "MediaFoundation.h"

#include <gs/platform/Thread.h>

#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace gs::capture {

namespace {

using Microsoft::WRL::ComPtr;

constexpr REFERENCE_TIME kBufferDuration = 200'000;

enum class SampleType : std::uint8_t { Float32, Int16, Int24, Int32 };

struct Shape {
    int channels = 0;
    int sampleRate = 0;
    SampleType type = SampleType::Float32;
    std::size_t bytesPerFrame = 0;
};

struct FormatDeleter {
    void operator()(WAVEFORMATEX* format) const noexcept { CoTaskMemFree(format); }
};

class Priority
{
public:
    Priority()
    {
        DWORD task = 0;
        m_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    }
    ~Priority()
    {
        if (m_handle != nullptr) {
            AvRevertMmThreadCharacteristics(m_handle);
        }
    }
    Priority(const Priority&) = delete;
    Priority& operator=(const Priority&) = delete;

private:
    HANDLE m_handle = nullptr;
};

std::optional<Shape> shapeOf(const WAVEFORMATEX& format)
{
    const bool extensible = format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                            format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    const GUID subFormat = extensible ? reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format).SubFormat : GUID{};
    const bool floating = format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                          (extensible && subFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    const bool integer = format.wFormatTag == WAVE_FORMAT_PCM || (extensible && subFormat == KSDATAFORMAT_SUBTYPE_PCM);
    Shape shape{.channels = format.nChannels,
                .sampleRate = static_cast<int>(format.nSamplesPerSec),
                .bytesPerFrame = format.nBlockAlign};
    if (shape.channels <= 0 || shape.sampleRate <= 0 || shape.bytesPerFrame == 0) {
        return std::nullopt;
    }
    if (floating && format.wBitsPerSample == 32) {
        shape.type = SampleType::Float32;
    } else if (integer && format.wBitsPerSample == 16) {
        shape.type = SampleType::Int16;
    } else if (integer && format.wBitsPerSample == 24) {
        shape.type = SampleType::Int24;
    } else if (integer && format.wBitsPerSample == 32) {
        shape.type = SampleType::Int32;
    } else {
        return std::nullopt;
    }
    return shape;
}

void convert(const BYTE* data, UINT32 frames, const Shape& shape, std::vector<float>& out)
{
    const std::size_t count = static_cast<std::size_t>(frames) * static_cast<std::size_t>(shape.channels);
    out.resize(count);
    const std::size_t width = shape.bytesPerFrame / static_cast<std::size_t>(shape.channels);
    for (std::size_t index = 0; index < count; ++index) {
        const BYTE* at = data + index * width;
        switch (shape.type) {
        case SampleType::Float32:
            std::memcpy(&out[index], at, sizeof(float));
            break;
        case SampleType::Int16: {
            std::int16_t value = 0;
            std::memcpy(&value, at, sizeof(value));
            out[index] = static_cast<float>(value) / 32768.0F;
            break;
        }
        case SampleType::Int24: {
            const std::int32_t value =
                static_cast<std::int32_t>((static_cast<std::uint32_t>(at[0]) << 8U) |
                                          (static_cast<std::uint32_t>(at[1]) << 16U) |
                                          (static_cast<std::uint32_t>(at[2]) << 24U)) >>
                8;
            out[index] = static_cast<float>(value) / 8388608.0F;
            break;
        }
        case SampleType::Int32: {
            std::int32_t value = 0;
            std::memcpy(&value, at, sizeof(value));
            out[index] = static_cast<float>(static_cast<double>(value) / 2147483648.0);
            break;
        }
        }
    }
}

}

struct AudioCapture::Wake {
    Event stop;
};

AudioCapture::AudioCapture(std::wstring endpoint, Samples samples)
    : m_endpoint(std::move(endpoint)), m_samples(std::move(samples)), m_wake(std::make_unique<Wake>()),
      m_thread([this](const std::stop_token& stop) { run(stop); })
{
}

AudioCapture::~AudioCapture()
{
    m_thread.request_stop();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::string AudioCapture::problem() const
{
    const std::scoped_lock lock(m_mutex);
    return m_problem;
}

void AudioCapture::fail(std::string reason)
{
    const std::scoped_lock lock(m_mutex);
    m_problem = std::move(reason);
}

void AudioCapture::run(const std::stop_token& stop)
{
    platform::setCurrentThreadName("gs-capture-sound");
    const std::stop_callback wakeOnStop(stop, [this] { SetEvent(m_wake->stop.get()); });
    const ComScope com;
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) ||
        FAILED(devices->GetDevice(m_endpoint.c_str(), &device)) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client))) {
        fail("the sound of the capture device cannot be opened");
        return;
    }
    WAVEFORMATEX* mixed = nullptr;
    if (FAILED(client->GetMixFormat(&mixed)) || mixed == nullptr) {
        fail("the sound of the capture device has no format");
        return;
    }
    const std::unique_ptr<WAVEFORMATEX, FormatDeleter> mix(mixed);
    const std::optional<Shape> shape = shapeOf(*mix);
    if (!shape) {
        fail("the sound of the capture device has a format GameScrcpy does not read");
        return;
    }
    const Event ready;
    ComPtr<IAudioCaptureClient> capture;
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, kBufferDuration, 0,
                                  mix.get(), nullptr)) ||
        FAILED(client->SetEventHandle(ready.get())) || FAILED(client->GetService(IID_PPV_ARGS(&capture))) ||
        FAILED(client->Start())) {
        fail("the sound of the capture device cannot start");
        return;
    }
    const Priority priority;
    std::vector<float> converted;
    const HANDLE events[] = {m_wake->stop.get(), ready.get()};
    bool going = true;
    while (going && !stop.stop_requested()) {
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
            break;
        }
        for (;;) {
            UINT32 next = 0;
            if (FAILED(capture->GetNextPacketSize(&next)) || next == 0) {
                break;
            }
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 position = 0;
            UINT64 counter = 0;
            if (const HRESULT got = capture->GetBuffer(&data, &frames, &flags, &position, &counter); FAILED(got)) {
                fail(failureText("the sound of the capture device stopped", got));
                going = false;
                break;
            }
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr) {
                converted.assign(static_cast<std::size_t>(frames) * static_cast<std::size_t>(shape->channels), 0.0F);
            } else {
                convert(data, frames, *shape, converted);
            }
            capture->ReleaseBuffer(frames);
            if (m_samples && frames > 0) {
                m_samples(converted, shape->channels, shape->sampleRate, static_cast<std::int64_t>(counter / 10));
            }
        }
    }
    client->Stop();
}

}
