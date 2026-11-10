#include "AudioOutput.h"

#include "gs/base/Log.h"
#include "gs/platform/Thread.h"

#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

// clang-format off
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
// clang-format on

#include <span>
#include <string_view>

namespace gs::media {

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::string_view kLog = "audio";
constexpr double kHundredNanosecondsPerSecond = 10'000'000.0;

class Event
{
public:
    Event() : m_handle(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
    ~Event() { CloseHandle(m_handle); }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return m_handle; }

private:
    HANDLE m_handle;
};

class ComScope
{
public:
    ComScope() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope()
    {
        if (SUCCEEDED(m_result)) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    [[nodiscard]] HRESULT result() const noexcept { return m_result; }

private:
    HRESULT m_result;
};

class MmcssScope
{
public:
    MmcssScope()
    {
        DWORD task = 0;
        m_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    }
    ~MmcssScope()
    {
        if (m_handle != nullptr) {
            AvRevertMmThreadCharacteristics(m_handle);
        }
    }
    MmcssScope(const MmcssScope&) = delete;
    MmcssScope& operator=(const MmcssScope&) = delete;

private:
    HANDLE m_handle = nullptr;
};

struct FormatDeleter {
    void operator()(WAVEFORMATEX* format) const noexcept { CoTaskMemFree(format); }
};
using MixFormat = std::unique_ptr<WAVEFORMATEX, FormatDeleter>;

bool isFloat32(const WAVEFORMATEX& format)
{
    if (format.wBitsPerSample != 32) {
        return false;
    }
    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }
    return format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
           format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) &&
           reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format).SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}

WAVEFORMATEXTENSIBLE floatFormat(int channels, int sampleRate)
{
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = static_cast<WORD>(channels);
    format.Format.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign = static_cast<WORD>(channels * 4);
    format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER
                           : channels == 2 ? SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT
                                           : 0;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return format;
}

unsigned code(HRESULT result)
{
    return static_cast<unsigned>(result);
}

struct Stream {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    Event ready;
    UINT32 bufferFrames = 0;
    UINT32 periodFrames = 0;
    bool lowLatency = false;
};

}

struct AudioOutput::Wake final : IMMNotificationClient {
    Event event;
    std::atomic<bool> deviceChanged{false};

    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
    {
        if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
    {
        if (flow == eRender && role == eConsole) {
            deviceChanged.store(true, std::memory_order_relaxed);
            SetEvent(event.get());
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override
    {
        SetEvent(event.get());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

AudioOutput::AudioOutput(AudioRegulator& source, int channels, int sampleRate, const std::atomic<float>& gain)
    : m_source(source),
      m_channels(channels),
      m_sampleRate(sampleRate),
      m_gain(gain),
      m_wake(std::make_unique<Wake>()),
      m_thread([this](const std::stop_token& stop) { run(stop); })
{
}

AudioOutput::~AudioOutput() = default;

namespace {

HRESULT open(IMMDeviceEnumerator& devices, int channels, int sampleRate, Stream& stream)
{
    ComPtr<IMMDevice> device;
    HRESULT result = devices.GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result)) {
        return result;
    }
    result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &stream.client);
    if (FAILED(result)) {
        return result;
    }
    WAVEFORMATEX* mixed = nullptr;
    result = stream.client->GetMixFormat(&mixed);
    if (FAILED(result)) {
        return result;
    }
    const MixFormat mix(mixed);
    ComPtr<IAudioClient3> client3;
    if (isFloat32(*mix) && mix->nChannels == channels && mix->nSamplesPerSec == static_cast<DWORD>(sampleRate) &&
        SUCCEEDED(stream.client.As(&client3))) {
        UINT32 defaultPeriod = 0;
        UINT32 fundamental = 0;
        UINT32 minimum = 0;
        UINT32 maximum = 0;
        if (SUCCEEDED(client3->GetSharedModeEnginePeriod(mix.get(), &defaultPeriod, &fundamental, &minimum, &maximum)) &&
            SUCCEEDED(client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minimum, mix.get(),
                                                           nullptr))) {
            stream.lowLatency = true;
            stream.periodFrames = minimum;
        } else {
            stream.client.Reset();
            result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &stream.client);
            if (FAILED(result)) {
                return result;
            }
        }
    }
    if (!stream.lowLatency) {
        const WAVEFORMATEXTENSIBLE format = floatFormat(channels, sampleRate);
        result = stream.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                           AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                               AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                           0, 0, &format.Format, nullptr);
        if (FAILED(result)) {
            return result;
        }
        REFERENCE_TIME period = 0;
        result = stream.client->GetDevicePeriod(&period, nullptr);
        if (FAILED(result)) {
            return result;
        }
        stream.periodFrames = static_cast<UINT32>(static_cast<double>(period) * sampleRate / kHundredNanosecondsPerSecond);
    }
    result = stream.client->GetBufferSize(&stream.bufferFrames);
    if (SUCCEEDED(result)) {
        result = stream.client->GetService(IID_PPV_ARGS(&stream.render));
    }
    if (SUCCEEDED(result)) {
        result = stream.client->SetEventHandle(stream.ready.get());
    }
    return result;
}

void applyGain(std::span<float> samples, int channels, float& current, float target)
{
    if (current == target) {
        if (target != 1.0f) {
            for (float& sample : samples) {
                sample *= target;
            }
        }
        return;
    }
    const std::size_t frames = samples.size() / static_cast<std::size_t>(channels);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float gain =
            current + (target - current) * static_cast<float>(frame + 1) / static_cast<float>(frames);
        for (int channel = 0; channel < channels; ++channel) {
            samples[frame * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel)] *= gain;
        }
    }
    current = target;
}

}

void AudioOutput::run(const std::stop_token& stop)
{
    platform::setCurrentThreadName("gs-audio-out");
    const std::stop_callback wakeOnStop(stop, [this] { SetEvent(m_wake->event.get()); });
    const ComScope com;
    if (FAILED(com.result())) {
        log::error(kLog, "no sound: COM is unavailable ({:#x})", code(com.result()));
        return;
    }
    const MmcssScope priority;
    ComPtr<IMMDeviceEnumerator> devices;
    if (const HRESULT created = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                                 IID_PPV_ARGS(&devices));
        FAILED(created)) {
        log::error(kLog, "no sound: the audio devices cannot be listed ({:#x})", code(created));
        return;
    }
    devices->RegisterEndpointNotificationCallback(m_wake.get());
    float gain = m_gain.load(std::memory_order_relaxed);
    HRESULT lastFailure = S_OK;
    while (!stop.stop_requested()) {
        m_wake->deviceChanged.store(false, std::memory_order_relaxed);
        Stream stream;
        if (const HRESULT opened = open(*devices.Get(), m_channels, m_sampleRate, stream); FAILED(opened)) {
            if (opened != lastFailure) {
                log::warning(kLog, "no sound until an output device is available ({:#x})", code(opened));
                lastFailure = opened;
            }
            m_period.store(0, std::memory_order_relaxed);
            WaitForSingleObject(m_wake->event.get(), INFINITE);
            continue;
        }
        lastFailure = S_OK;
        m_period.store(stream.periodFrames, std::memory_order_relaxed);
        m_lowLatency.store(stream.lowLatency, std::memory_order_relaxed);
        log::info(kLog, "playing on the default device: {} period of {} frames ({:.1f} ms), buffer {} frames",
                  stream.lowLatency ? "low-latency" : "shared", stream.periodFrames,
                  stream.periodFrames * 1000.0 / m_sampleRate, stream.bufferFrames);

        const auto fill = [&]() -> HRESULT {
            UINT32 padding = 0;
            HRESULT result = stream.client->GetCurrentPadding(&padding);
            if (FAILED(result) || padding == stream.bufferFrames) {
                return result;
            }
            const UINT32 room = stream.bufferFrames - padding;
            BYTE* data = nullptr;
            result = stream.render->GetBuffer(room, &data);
            if (FAILED(result)) {
                return result;
            }
            const std::span<float> samples(reinterpret_cast<float*>(data),
                                           static_cast<std::size_t>(room) * static_cast<std::size_t>(m_channels));
            m_source.pull(samples);
            applyGain(samples, m_channels, gain, m_gain.load(std::memory_order_relaxed));
            return stream.render->ReleaseBuffer(room, 0);
        };
        HRESULT result = fill();
        if (SUCCEEDED(result)) {
            result = stream.client->Start();
        }
        const HANDLE events[] = {m_wake->event.get(), stream.ready.get()};
        while (SUCCEEDED(result)) {
            const DWORD signaled = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (signaled == WAIT_OBJECT_0) {
                if (stop.stop_requested() || m_wake->deviceChanged.load(std::memory_order_relaxed)) {
                    break;
                }
                continue;
            }
            result = signaled == WAIT_OBJECT_0 + 1 ? fill() : HRESULT_FROM_WIN32(GetLastError());
        }
        stream.client->Stop();
        if (FAILED(result)) {
            log::warning(kLog, "the output device stopped ({:#x}); reopening", code(result));
        }
    }
    devices->UnregisterEndpointNotificationCallback(m_wake.get());
}

}
