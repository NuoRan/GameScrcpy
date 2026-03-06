#include "WasapiPlayer.h"
#define LOG_TAG "WasapiPlayer"
#include "Logger.h"

#pragma comment(lib, "Ole32.lib")

WasapiPlayer::WasapiPlayer() = default;

WasapiPlayer::~WasapiPlayer()
{
    shutdown();
}

bool WasapiPlayer::initialize(int sampleRate, int channels, int bitsPerSample, PullCallback pullCb)
{
    if (m_running.load()) return false;

    m_sampleRate    = sampleRate;
    m_channels      = channels;
    m_bitsPerSample = bitsPerSample;
    m_frameSize     = static_cast<UINT32>(channels * (bitsPerSample / 8));
    m_callback      = std::move(pullCb);
    m_initOk.store(false);

    m_readyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_stopEvent  = CreateEventW(nullptr, TRUE,  FALSE, nullptr);

    m_feedThread = std::thread(&WasapiPlayer::feedThreadFunc, this);

    // 阻塞等待 feed 线程完成 WASAPI 初始化
    WaitForSingleObject(m_readyEvent, 5000);
    CloseHandle(m_readyEvent);
    m_readyEvent = nullptr;

    return m_initOk.load();
}

void WasapiPlayer::shutdown()
{
    if (!m_running.load() && !m_feedThread.joinable()) return;

    m_running.store(false);
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_feedThread.joinable()) m_feedThread.join();

    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

void WasapiPlayer::feedThreadFunc()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInit = SUCCEEDED(hr) || hr == S_FALSE; // S_FALSE = already initialized

    do {
        // 1. 创建设备枚举器
        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&m_enumerator));
        if (FAILED(hr)) {
            LOGW() << "WasapiPlayer: CoCreateInstance(MMDeviceEnumerator) failed";
            break;
        }

        // 2. 获取默认音频输出设备
        hr = m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device);
        if (FAILED(hr)) {
            LOGW() << "WasapiPlayer: GetDefaultAudioEndpoint failed";
            break;
        }

        // 3. 激活 IAudioClient
        hr = m_device->Activate(
            __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&m_audioClient));
        if (FAILED(hr)) {
            LOGW() << "WasapiPlayer: Activate(IAudioClient) failed";
            break;
        }

        // 4. 设置音频格式 (PCM)
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = static_cast<WORD>(m_channels);
        wfx.nSamplesPerSec  = static_cast<DWORD>(m_sampleRate);
        wfx.wBitsPerSample  = static_cast<WORD>(m_bitsPerSample);
        wfx.nBlockAlign     = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize          = 0;

        // 5. 初始化 (共享模式, 自动格式转换)
        //    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM: Windows 自动转换到系统混音格式
        //    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY: 使用高质量重采样
        REFERENCE_TIME bufferDuration = 100000; // 10ms (100ns 单位)
        hr = m_audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            bufferDuration,
            0,       // 共享模式 periodicity 必须为 0
            &wfx,
            nullptr  // AudioSessionGUID
        );
        if (FAILED(hr)) {
            LOGW() << "WasapiPlayer: IAudioClient::Initialize failed";
            break;
        }

        // 6. 获取缓冲区大小
        hr = m_audioClient->GetBufferSize(&m_bufferFrames);
        if (FAILED(hr)) {
            LOGW() << "WasapiPlayer: GetBufferSize failed";
            break;
        }

        // 7. 获取渲染客户端
        hr = m_audioClient->GetService(
            __uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(&m_renderClient));
        if (FAILED(hr)) {
            LOGW() << "WasapiPlayer: GetService(IAudioRenderClient) failed";
            break;
        }

        // 8. 预填充静音
        {
            BYTE* pData = nullptr;
            hr = m_renderClient->GetBuffer(m_bufferFrames, &pData);
            if (SUCCEEDED(hr)) {
                memset(pData, 0, m_bufferFrames * m_frameSize);
                m_renderClient->ReleaseBuffer(m_bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
            }
        }

        // 9. 开始播放
        hr = m_audioClient->Start();
        if (FAILED(hr)) {
            LOGW() << "WasapiPlayer: Start failed";
            break;
        }

        m_initOk.store(true);
        m_running.store(true);
        LOGI() << "WasapiPlayer: Started (shared mode, auto-convert), "
               << m_sampleRate << "Hz, " << m_channels << "ch, "
               << m_bitsPerSample << "bit, buffer: " << m_bufferFrames << " frames";

    } while (false);

    // 通知初始化完成
    SetEvent(m_readyEvent);

    if (!m_initOk.load()) {
        releaseResources();
        if (comInit) CoUninitialize();
        return;
    }

    // ─── Feed 循环: 轮询 WASAPI 缓冲, 填充 PCM 数据 ───
    while (m_running.load()) {
        // 检查停止事件 (5ms 超时 = 轮询间隔)
        DWORD result = WaitForSingleObject(m_stopEvent, 5);
        if (result == WAIT_OBJECT_0) break;

        UINT32 padding = 0;
        hr = m_audioClient->GetCurrentPadding(&padding);
        if (FAILED(hr)) break;

        UINT32 available = m_bufferFrames - padding;
        if (available == 0) continue;

        BYTE* pData = nullptr;
        hr = m_renderClient->GetBuffer(available, &pData);
        if (FAILED(hr)) continue;

        if (m_callback) {
            m_callback(reinterpret_cast<char*>(pData),
                       static_cast<int64_t>(available) * m_frameSize);
        } else {
            memset(pData, 0, available * m_frameSize);
        }

        m_renderClient->ReleaseBuffer(available, 0);
    }

    // 停止并清理
    if (m_audioClient) m_audioClient->Stop();
    releaseResources();
    if (comInit) CoUninitialize();

    LOGI() << "WasapiPlayer: Feed thread exited";
}

void WasapiPlayer::releaseResources()
{
    if (m_renderClient) { m_renderClient->Release(); m_renderClient = nullptr; }
    if (m_audioClient)  { m_audioClient->Release();  m_audioClient = nullptr; }
    if (m_device)       { m_device->Release();        m_device = nullptr; }
    if (m_enumerator)   { m_enumerator->Release();    m_enumerator = nullptr; }
}
