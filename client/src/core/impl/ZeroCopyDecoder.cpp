#include "ZeroCopyDecoder.h"
#include "PerformanceMonitor.h"
#include <QDebug>
#include <QDateTime>
#include <QMutex>
#include <QElapsedTimer>
#include <cstring>

// SIMD 加速头文件
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#ifdef __SSE2__
#include <emmintrin.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
}

// D3D11VA GPU 直通所需头文件
// d3d11.h 必须先以 C++ 链接包含（含 operator== 重载），
// 之后 hwcontext_d3d11va.h 在 extern "C" 中再 #include <d3d11.h> 时
// 会被 include guard 跳过，避免 C2733 错误。
#ifdef _WIN32
#include <d3d11.h>
extern "C" {
#include "libavutil/hwcontext_d3d11va.h"
}
#endif

#define LOG_TAG "ZeroCopyDecoder"

// SIMD 加速的内存拷贝
// 对于 32 字节对齐的大块数据（如 YUV 帧），使用 SIMD 流式存储避免缓存污染
static void simdMemcpy(void* dst, const void* src, size_t size)
{
#ifdef __AVX2__
    // AVX2: 256-bit (32字节) 流式拷贝
    // 源和目标都是 32 字节对齐的（FramePool 保证）
    const size_t avxBlocks = size / 32;
    const size_t remainder = size % 32;
    const __m256i* srcPtr = reinterpret_cast<const __m256i*>(src);
    __m256i* dstPtr = reinterpret_cast<__m256i*>(dst);

    for (size_t i = 0; i < avxBlocks; ++i) {
        __m256i data = _mm256_load_si256(srcPtr + i);
        _mm256_stream_si256(dstPtr + i, data);
    }
    _mm_sfence();  // 确保流式存储完成

    if (remainder > 0) {
        memcpy(reinterpret_cast<uint8_t*>(dst) + avxBlocks * 32,
               reinterpret_cast<const uint8_t*>(src) + avxBlocks * 32, remainder);
    }
#elif defined(__SSE2__)
    // SSE2: 128-bit (16字节) 流式拷贝
    const size_t sseBlocks = size / 16;
    const size_t remainder = size % 16;
    const __m128i* srcPtr = reinterpret_cast<const __m128i*>(src);
    __m128i* dstPtr = reinterpret_cast<__m128i*>(dst);

    for (size_t i = 0; i < sseBlocks; ++i) {
        __m128i data = _mm_load_si128(srcPtr + i);
        _mm_stream_si128(dstPtr + i, data);
    }
    _mm_sfence();

    if (remainder > 0) {
        memcpy(reinterpret_cast<uint8_t*>(dst) + sseBlocks * 16,
               reinterpret_cast<const uint8_t*>(src) + sseBlocks * 16, remainder);
    }
#else
    memcpy(dst, src, size);
#endif
}

// NV12 UV 去交织的 SIMD 加速
// 将 UVUVUV... 交织数据分离为独立的 UUU... 和 VVV... 平面
// SSE2: 使用 _mm_shuffle_epi8 / maskmove 实现 ~4x 加速
// AVX2: 使用 _mm256_shuffle_epi8 实现 ~8x 加速
static void simdDeinterleaveUV(const uint8_t* src, uint8_t* dstU, uint8_t* dstV,
                                int width, int height, int srcStride, int dstUStride, int dstVStride)
{
#ifdef __AVX2__
    // AVX2: 每次处理 32 个 UV 对 (64 字节输入 → 32U + 32V)
    const __m256i shuffleU = _mm256_setr_epi8(
        0, 2, 4, 6, 8, 10, 12, 14,  // 低 128 位偶数位置 (U)
        -1, -1, -1, -1, -1, -1, -1, -1,
        0, 2, 4, 6, 8, 10, 12, 14,
        -1, -1, -1, -1, -1, -1, -1, -1
    );
    const __m256i shuffleV = _mm256_setr_epi8(
        1, 3, 5, 7, 9, 11, 13, 15,  // 低 128 位奇数位置 (V)
        -1, -1, -1, -1, -1, -1, -1, -1,
        1, 3, 5, 7, 9, 11, 13, 15,
        -1, -1, -1, -1, -1, -1, -1, -1
    );

    for (int y = 0; y < height; ++y) {
        const uint8_t* row = src + y * srcStride;
        uint8_t* uRow = dstU + y * dstUStride;
        uint8_t* vRow = dstV + y * dstVStride;
        int x = 0;

        // AVX2 主循环: 每次处理 16 个 UV 对 (32 字节)
        for (; x + 16 <= width; x += 16) {
            __m256i uv = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(row + x * 2));
            __m256i u = _mm256_shuffle_epi8(uv, shuffleU);
            __m256i v = _mm256_shuffle_epi8(uv, shuffleV);

            // 结果在每个 128 位 lane 的低 8 字节中
            // 提取并合并: lane0[0:7] + lane1[0:7] → 16 字节连续 U
            __m128i u_lo = _mm256_castsi256_si128(u);
            __m128i u_hi = _mm256_extracti128_si256(u, 1);
            __m128i u_packed = _mm_unpacklo_epi64(u_lo, u_hi);

            __m128i v_lo = _mm256_castsi256_si128(v);
            __m128i v_hi = _mm256_extracti128_si256(v, 1);
            __m128i v_packed = _mm_unpacklo_epi64(v_lo, v_hi);

            _mm_storeu_si128(reinterpret_cast<__m128i*>(uRow + x), u_packed);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(vRow + x), v_packed);
        }

        // 标量尾部
        for (; x < width; ++x) {
            uRow[x] = row[x * 2];
            vRow[x] = row[x * 2 + 1];
        }
    }
#elif defined(__SSE2__)
    // SSE2: 使用 _mm_and / _mm_srli 偶奇分离
    const __m128i maskLow = _mm_set1_epi16(0x00FF);

    for (int y = 0; y < height; ++y) {
        const uint8_t* row = src + y * srcStride;
        uint8_t* uRow = dstU + y * dstUStride;
        uint8_t* vRow = dstV + y * dstVStride;
        int x = 0;

        // SSE2 主循环: 每次处理 8 个 UV 对 (16 字节)
        for (; x + 8 <= width; x += 8) {
            __m128i uv = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + x * 2));

            // U = 偶数字节 (位置 0,2,4,6,8,10,12,14)
            __m128i u16 = _mm_and_si128(uv, maskLow);
            // V = 奇数字节 (位置 1,3,5,7,9,11,13,15)
            __m128i v16 = _mm_srli_epi16(uv, 8);

            // 16位 → 8位 pack: u16 和 v16 各有 8 个 16 位值
            __m128i u8 = _mm_packus_epi16(u16, u16);  // 低 8 字节有效
            __m128i v8 = _mm_packus_epi16(v16, v16);  // 低 8 字节有效

            // 存储 8 个 U 和 V
            _mm_storel_epi64(reinterpret_cast<__m128i*>(uRow + x), u8);
            _mm_storel_epi64(reinterpret_cast<__m128i*>(vRow + x), v8);
        }

        // 标量尾部
        for (; x < width; ++x) {
            uRow[x] = row[x * 2];
            vRow[x] = row[x * 2 + 1];
        }
    }
#else
    // 标量回退
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = src + y * srcStride;
        uint8_t* uRow = dstU + y * dstUStride;
        uint8_t* vRow = dstV + y * dstVStride;
        for (int x = 0; x < width; ++x) {
            uRow[x] = row[x * 2];
            vRow[x] = row[x * 2 + 1];
        }
    }
#endif
}

// 静态硬件像素格式（用于回调）
static int s_hwPixFmtGlobal = AV_PIX_FMT_NONE;
// 标记硬件格式协商是否在运行时失败（getHwFormat 回调中设置）
static bool s_hwFormatFailed = false;

namespace qsc {
namespace core {

// 硬件加速类型优先级
static const AVHWDeviceType hwDeviceTypes[] = {
#ifdef _WIN32
    AV_HWDEVICE_TYPE_D3D11VA,
    AV_HWDEVICE_TYPE_DXVA2,
    AV_HWDEVICE_TYPE_CUDA,
#elif defined(__APPLE__)
    AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#elif defined(__linux__)
    AV_HWDEVICE_TYPE_VAAPI,
    AV_HWDEVICE_TYPE_VDPAU,
    AV_HWDEVICE_TYPE_CUDA,
#endif
    AV_HWDEVICE_TYPE_NONE
};

// 硬件解码器缓存
struct HwDecoderCache {
    QMutex mutex;
    bool initialized = false;
    AVHWDeviceType cachedType = AV_HWDEVICE_TYPE_NONE;
    AVPixelFormat cachedPixFmt = AV_PIX_FMT_NONE;
    QString cachedName;

    void detectOnce(AVCodecID codecId) {
        QMutexLocker locker(&mutex);
        if (initialized) return;
        initialized = true;

        const ::AVCodec* codec = avcodec_find_decoder(codecId);
        if (!codec) return;

        for (int i = 0; hwDeviceTypes[i] != AV_HWDEVICE_TYPE_NONE; i++) {
            AVHWDeviceType type = hwDeviceTypes[i];
            const char* typeName = av_hwdevice_get_type_name(type);

            for (int j = 0;; j++) {
                const AVCodecHWConfig* config = avcodec_get_hw_config(codec, j);
                if (!config) break;

                if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                    config->device_type == type) {

                    AVBufferRef* testCtx = nullptr;
                    int ret = av_hwdevice_ctx_create(&testCtx, type, nullptr, nullptr, 0);
                    if (ret >= 0) {
                        cachedType = type;
                        cachedPixFmt = config->pix_fmt;
                        cachedName = QString::fromUtf8(typeName);
                        av_buffer_unref(&testCtx);
                        qInfo("[ZeroCopyDecoder] Cached HW decoder: %s for %s",
                              qPrintable(cachedName), avcodec_get_name(codecId));
                        return;
                    } else {
                        char errBuf[256];
                        av_strerror(ret, errBuf, sizeof(errBuf));
                        qWarning("[ZeroCopyDecoder] Failed to create %s context: %s",
                                 typeName, errBuf);
                    }
                }
            }
        }
        qInfo("[ZeroCopyDecoder] No HW decoder for %s", avcodec_get_name(codecId));
    }
};

static HwDecoderCache s_h264Cache;

// ---------------------------------------------------------
// 构造与析构
// ---------------------------------------------------------
ZeroCopyDecoder::ZeroCopyDecoder(QObject* parent)
    : QObject(parent)
    , m_hwPixFmt(-1)  // AV_PIX_FMT_NONE
{
    m_fpsTimer.start();
}

ZeroCopyDecoder::~ZeroCopyDecoder()
{
    close();
}

// ---------------------------------------------------------
// 硬件格式回调（全局静态函数）
// ---------------------------------------------------------
static AVPixelFormat getHwFormat(AVCodecContext* ctx, const AVPixelFormat* pix_fmts)
{
    Q_UNUSED(ctx);
    // 优先返回硬件格式
    for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == s_hwPixFmtGlobal) {
            return *p;
        }
    }
    // 硬件格式不在候选列表中，标记失败并回退到软件格式
    s_hwFormatFailed = true;
    qWarning("[ZeroCopyDecoder] HW format %d not in offered list, falling back to software", s_hwPixFmtGlobal);

    // 从候选列表中选择第一个软件像素格式
    for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == AV_PIX_FMT_YUV420P || *p == AV_PIX_FMT_NV12 ||
            *p == AV_PIX_FMT_YUV420P10LE || *p == AV_PIX_FMT_YUV444P) {
            qInfo("[ZeroCopyDecoder] Selected software format: %d", *p);
            return *p;
        }
    }
    // 最后手段：返回列表中第一个格式
    if (pix_fmts[0] != AV_PIX_FMT_NONE) {
        qInfo("[ZeroCopyDecoder] Using first available format: %d", pix_fmts[0]);
        return pix_fmts[0];
    }
    return AV_PIX_FMT_NONE;
}

// ---------------------------------------------------------
// 初始化硬件解码器
// ---------------------------------------------------------
bool ZeroCopyDecoder::initHardwareDecoder(const AVCodec* codec)
{
    HwDecoderCache* cache = nullptr;
    if (codec->id == AV_CODEC_ID_H264) {
        cache = &s_h264Cache;
    }

    if (cache && cache->initialized && cache->cachedType != AV_HWDEVICE_TYPE_NONE) {
        int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, cache->cachedType, nullptr, nullptr, 0);
        if (ret >= 0) {
            m_hwPixFmt = cache->cachedPixFmt;
            s_hwPixFmtGlobal = m_hwPixFmt;
            m_hwDecoderName = cache->cachedName;
            return true;
        }
    }

    // 回退到完整检测
    for (int i = 0; hwDeviceTypes[i] != AV_HWDEVICE_TYPE_NONE; i++) {
        AVHWDeviceType type = hwDeviceTypes[i];

        for (int j = 0;; j++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(codec, j);
            if (!config) break;

            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == type) {

                int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, type, nullptr, nullptr, 0);
                if (ret >= 0) {
                    m_hwPixFmt = config->pix_fmt;
                    s_hwPixFmtGlobal = m_hwPixFmt;
                    m_hwDecoderName = QString::fromUtf8(av_hwdevice_get_type_name(type));
                    return true;
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------
// 硬件帧转软件帧
// ---------------------------------------------------------
bool ZeroCopyDecoder::transferHwFrame(AVFrame* hwFrame, AVFrame* swFrame)
{
    if (!hwFrame || !swFrame) return false;

    // 先释放旧的软件帧数据
    av_frame_unref(swFrame);

    int ret = av_hwframe_transfer_data(swFrame, hwFrame, 0);
    if (ret < 0) {
        char errorbuf[256];
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        qWarning("[ZeroCopyDecoder] HW frame transfer error: %s", errorbuf);
        return false;
    }

    // 复制必要的元数据
    swFrame->pts = hwFrame->pts;
    swFrame->width = hwFrame->width;
    swFrame->height = hwFrame->height;

    // 调试日志
    static int lastHwW = 0, lastHwH = 0;
    if (hwFrame->width != lastHwW || hwFrame->height != lastHwH) {
        qInfo("[ZeroCopyDecoder] HW transfer: hwFrame=%dx%d swFrame=%dx%d format=%d linesize=[%d,%d,%d]",
              hwFrame->width, hwFrame->height, swFrame->width, swFrame->height,
              swFrame->format, swFrame->linesize[0], swFrame->linesize[1], swFrame->linesize[2]);
        lastHwW = hwFrame->width;
        lastHwH = hwFrame->height;
    }

    return true;
}

// ---------------------------------------------------------
// 打开解码器
// ---------------------------------------------------------
bool ZeroCopyDecoder::open(int codecId)
{
    if (m_isOpen) {
        close();
    }

    AVCodecID avCodecId = static_cast<AVCodecID>(codecId);
    const char* codecName;
    switch (avCodecId) {
        case AV_CODEC_ID_H264: codecName = "H.264"; break;
        default: codecName = "Unknown"; break;
    }

    // 预检测硬件解码器
    if (avCodecId == AV_CODEC_ID_H264) {
        s_h264Cache.detectOnce(avCodecId);
    }

    // 查找解码器
    const AVCodec* codec = avcodec_find_decoder(avCodecId);
    if (!codec) {
        qCritical("[ZeroCopyDecoder] %s decoder not found!", codecName);
        return false;
    }

    // 分配上下文
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qCritical("[ZeroCopyDecoder] Could not allocate decoder context");
        return false;
    }

    // 尝试硬件解码（除非强制软解）
    bool hwEnabled = false;
    if (!m_forceSwDecode) {
        hwEnabled = initHardwareDecoder(codec);
    } else {
        qInfo("[ZeroCopyDecoder] Hardware decode disabled (forced software mode)");
    }
    s_hwFormatFailed = false;  // 重置格式协商标记
    if (hwEnabled) {
        m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
        m_codecCtx->get_format = getHwFormat;

        m_hwFrame = av_frame_alloc();
        m_swFrame = av_frame_alloc();
        if (!m_hwFrame || !m_swFrame) {
            qCritical("[ZeroCopyDecoder] Could not allocate HW/SW frames");
            close();
            return false;
        }
    }

    // 低延迟设置
    m_codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;

    // [超低延迟优化] 禁用帧级多线程以消除解码缓冲
    // 帧级多线程 (FF_THREAD_FRAME) 需要缓冲多帧才能并行，增加 1-2 帧延迟
    // 硬件解码时线程数设为 1，软解时保留少量线程
    if (hwEnabled) {
        m_codecCtx->thread_count = 1;        // HW 解码只需 1 个解码线程
        m_codecCtx->thread_type = 0;          // 禁用所有多线程模式
    } else {
        // 软解：使用 slice 级多线程（不增加延迟）而非帧级多线程
        m_codecCtx->thread_type = FF_THREAD_SLICE;
    }

    // 打开解码器
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        qCritical("[ZeroCopyDecoder] Could not open %s codec", codecName);
        close();
        return false;
    }

    // 分配解码帧和复用 packet
    m_decodeFrame = av_frame_alloc();
    m_packet = av_packet_alloc();
    if (!m_decodeFrame || !m_packet) {
        qCritical("[ZeroCopyDecoder] Could not allocate frame/packet");
        close();
        return false;
    }

    m_isOpen = true;
    m_codecId = codecId;

    qInfo("[ZeroCopyDecoder] Opened with %s (%s)",
          hwEnabled ? qPrintable(m_hwDecoderName) : "software", codecName);

    return true;
}

// ---------------------------------------------------------
// 关闭解码器
// ---------------------------------------------------------
void ZeroCopyDecoder::close()
{
    // 标记为关闭，防止新的解码请求
    m_isOpen = false;

    // 1. 先释放数据包
    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }

    // 2. 释放所有帧（在 codec context 之前）
    if (m_decodeFrame) {
        av_frame_free(&m_decodeFrame);
        m_decodeFrame = nullptr;
    }
    if (m_hwFrame) {
        av_frame_free(&m_hwFrame);
        m_hwFrame = nullptr;
    }
    if (m_swFrame) {
        av_frame_free(&m_swFrame);
        m_swFrame = nullptr;
    }

    // 3. 释放 codec context（这会释放其内部的 hw_device_ctx 引用）
    //    必须在 m_hwDeviceCtx 之前释放！
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }

    // 4. 最后释放硬件设备上下文
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }

    // 5. 释放截图缓存的 AVFrame 引用
    if (m_lastAVFrame) {
        av_frame_free(&m_lastAVFrame);
        m_lastAVFrame = nullptr;
    }

    m_hwPixFmt = AV_PIX_FMT_NONE;
    s_hwPixFmtGlobal = AV_PIX_FMT_NONE;
    s_hwFormatFailed = false;
    m_consecutiveErrors = 0;

    // 重置分辨率检测
    m_decodedWidth = 0;
    m_decodedHeight = 0;
}

// ---------------------------------------------------------
// 解码
// ---------------------------------------------------------
bool ZeroCopyDecoder::decode(const uint8_t* data, int size, int64_t pts, int flags)
{
    if (!m_isOpen || !m_codecCtx || !data || size <= 0) {
        return false;
    }

    // 开始计时（真正的解码延迟）
    QElapsedTimer decodeTimer;
    decodeTimer.start();

    // 设置 packet 数据（不复制）
    m_packet->data = const_cast<uint8_t*>(data);
    m_packet->size = size;
    m_packet->pts = pts;

    // 发送到解码器
    int ret = avcodec_send_packet(m_codecCtx, m_packet);
    if (ret < 0) {
        char errorbuf[256];
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        qWarning("[ZeroCopyDecoder] Send packet error: %s", errorbuf);
        m_packet->data = nullptr;
        m_packet->size = 0;

        // 连续失败计数，超过阈值则关闭硬件解码重新用软解打开
        m_consecutiveErrors++;
        if (m_consecutiveErrors >= 3 && m_hwDeviceCtx) {
            qWarning("[ZeroCopyDecoder] %d consecutive errors with HW decoder, reopening in software mode",
                     m_consecutiveErrors);
            int savedCodecId = m_codecId;
            close();
            // 重新打开时禁止硬件加速
            m_forceSwDecode = true;
            open(savedCodecId);
        }
        return false;
    }
    m_consecutiveErrors = 0;  // 发送成功，重置计数

    // 接收解码帧
    AVFrame* receiveFrame = (m_hwDeviceCtx && !s_hwFormatFailed) ? m_hwFrame : m_decodeFrame;

    ret = avcodec_receive_frame(m_codecCtx, receiveFrame);
    if (ret == 0) {
        // 成功解码 — 根据实际帧格式决定路径
        bool isHwFrame = (receiveFrame->format == m_hwPixFmt) && m_hwDeviceCtx && !s_hwFormatFailed;

        if (isHwFrame) {
            // GPU 直通路径: 跳过 av_hwframe_transfer_data
            if (m_gpuDirectEnabled && m_hwPixFmt == AV_PIX_FMT_D3D11) {
                double decodeLatencyMs = decodeTimer.nsecsElapsed() / 1000000.0;
                qsc::PerformanceMonitor::instance().reportDecodeLatency(decodeLatencyMs);
                processGPUDirectFrame(m_hwFrame);
                // 注意: GPU 直通路径中 m_hwFrame 的 unref 延迟到渲染完成后
            } else {
                // 旧路径: 硬解 GPU → CPU
                if (transferHwFrame(m_hwFrame, m_swFrame)) {
                    double decodeLatencyMs = decodeTimer.nsecsElapsed() / 1000000.0;
                    qsc::PerformanceMonitor::instance().reportDecodeLatency(decodeLatencyMs);
                    processDecodedFrame(m_swFrame);
                }
                av_frame_unref(m_hwFrame);
            }
        } else {
            // 软解（含 HW 回退到 SW 的情况）：报告解码延迟
            double decodeLatencyMs = decodeTimer.nsecsElapsed() / 1000000.0;
            qsc::PerformanceMonitor::instance().reportDecodeLatency(decodeLatencyMs);

            processDecodedFrame(receiveFrame);
        }
        av_frame_unref(receiveFrame);
    } else if (ret != AVERROR(EAGAIN)) {
        char errorbuf[256];
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        qWarning("[ZeroCopyDecoder] Receive frame error: %s", errorbuf);
    }

    m_packet->data = nullptr;
    m_packet->size = 0;
    return true;
}

// ---------------------------------------------------------
// 处理解码后的帧
// ---------------------------------------------------------
void ZeroCopyDecoder::processDecodedFrame(AVFrame* frame)
{
    if (!frame || frame->width <= 0 || frame->height <= 0) {
        return;
    }

    // 检测分辨率变化（仅记录，不干预）
    if (m_decodedWidth != 0 && m_decodedHeight != 0) {
        if (frame->width != m_decodedWidth || frame->height != m_decodedHeight) {
            qInfo("[ZeroCopyDecoder] Resolution changed: %dx%d -> %dx%d",
                  m_decodedWidth, m_decodedHeight, frame->width, frame->height);
        }
    }
    m_decodedWidth = frame->width;
    m_decodedHeight = frame->height;

    // 确保是 YUV420P 或 NV12 格式
    const bool isYUV420P = (frame->format == AV_PIX_FMT_YUV420P);
    const bool isNV12 = (frame->format == AV_PIX_FMT_NV12);

    if (!isYUV420P && !isNV12) {
        qWarning("[ZeroCopyDecoder] Unsupported pixel format: %d", frame->format);
        return;
    }

    // 报告帧已解码
    qsc::PerformanceMonitor::instance().reportFrameDecoded();

    // 更新 FPS
    updateFps();

    int w = frame->width;
    int h = frame->height;
    int uvW = w / 2;
    int uvH = h / 2;

    // 调试：打印 AVFrame 的实际尺寸和 linesize
    static int lastW = 0, lastH = 0;
    if (w != lastW || h != lastH) {
        qInfo("[ZeroCopyDecoder] AVFrame size: %dx%d, linesize[0]=%d linesize[1]=%d linesize[2]=%d",
              w, h, frame->linesize[0], frame->linesize[1], frame->linesize[2]);
        lastW = w;
        lastH = h;
    }

    // 优先使用 FrameQueue
    if (m_frameQueue) {
        // 报告帧队列深度和帧池使用情况
        qsc::PerformanceMonitor::instance().reportFrameQueueDepth(static_cast<int>(m_frameQueue->queueSize()));
        qsc::PerformanceMonitor::instance().reportFramePoolUsage(
            m_frameQueue->usedFrames(), m_frameQueue->poolSize());

        FrameData* poolFrame = m_frameQueue->acquireFrame();
        if (poolFrame) {
            // 检查帧池尺寸是否匹配
            if (poolFrame->width != w || poolFrame->height != h) {
                qInfo("[ZeroCopyDecoder] Frame size changed: %dx%d -> %dx%d",
                      poolFrame->width, poolFrame->height, w, h);
                // 1. 先释放当前帧
                m_frameQueue->releaseFrame(poolFrame);
                // 2. 清空队列中所有旧尺寸的帧（否则消费者会收到旧尺寸帧）
                m_frameQueue->clear();
                // 3. 调整帧池尺寸
                m_frameQueue->resize(w, h);
                // 4. 重新获取帧
                poolFrame = m_frameQueue->acquireFrame();

                // 5. 如果获取的帧仍然是旧尺寸（被消费者持有后释放的），跳过这一帧
                if (poolFrame && (poolFrame->width != w || poolFrame->height != h)) {
                    qWarning("[ZeroCopyDecoder] Got stale frame after resize, skipping");
                    m_frameQueue->releaseFrame(poolFrame);
                    poolFrame = nullptr;
                }
            }

            if (poolFrame && poolFrame->dataY) {
                if (isNV12) {
                    // NV12 转 YUV420P: 在解码端做 UV 去交织
                    // 直接 NV12 传 GPU 存在 GL_LUMINANCE_ALPHA 兑容性问题（ANGLE/不同驱动）
                    // SIMD 加速去交织

                    // 复制 Y 平面
                    if (frame->linesize[0] == poolFrame->linesizeY) {
                        simdMemcpy(poolFrame->dataY, frame->data[0], poolFrame->linesizeY * h);
                    } else {
                        for (int y = 0; y < h; ++y) {
                            memcpy(poolFrame->dataY + y * poolFrame->linesizeY,
                                   frame->data[0] + y * frame->linesize[0], w);
                        }
                    }

                    // NV12 UV 去交织 SIMD 加速
                    // 替代逐像素循环，使用 SSE2/AVX2 批量分离 UV 交织数据
                    simdDeinterleaveUV(frame->data[1],
                                       poolFrame->dataU, poolFrame->dataV,
                                       uvW, uvH,
                                       frame->linesize[1],
                                       poolFrame->linesizeU, poolFrame->linesizeV);

                    poolFrame->isNV12 = false;  // 已转为 YUV420P
                } else {
                    // YUV420P 格式：3 个独立平面
                    // 检查 linesize 是否完全匹配
                    const bool yLineSizeMatch = (frame->linesize[0] == poolFrame->linesizeY);
                    const bool uLineSizeMatch = (frame->linesize[1] == poolFrame->linesizeU);
                    const bool vLineSizeMatch = (frame->linesize[2] == poolFrame->linesizeV);
                    const bool allMatch = yLineSizeMatch && uLineSizeMatch && vLineSizeMatch;

                    if (allMatch) {
                        // SIMD 加速整块拷贝
                        simdMemcpy(poolFrame->dataY, frame->data[0], poolFrame->linesizeY * h);
                        simdMemcpy(poolFrame->dataU, frame->data[1], poolFrame->linesizeU * uvH);
                        simdMemcpy(poolFrame->dataV, frame->data[2], poolFrame->linesizeV * uvH);
                    } else {
                        // 次优路径：linesize 不匹配，逐行拷贝
                        if (yLineSizeMatch) {
                            simdMemcpy(poolFrame->dataY, frame->data[0], poolFrame->linesizeY * h);
                        } else {
                            for (int y = 0; y < h; ++y) {
                                memcpy(poolFrame->dataY + y * poolFrame->linesizeY,
                                       frame->data[0] + y * frame->linesize[0], w);
                            }
                        }

                        if (uLineSizeMatch) {
                            simdMemcpy(poolFrame->dataU, frame->data[1], poolFrame->linesizeU * uvH);
                        } else {
                            for (int y = 0; y < uvH; ++y) {
                                memcpy(poolFrame->dataU + y * poolFrame->linesizeU,
                                       frame->data[1] + y * frame->linesize[1], uvW);
                            }
                        }

                        if (vLineSizeMatch) {
                            simdMemcpy(poolFrame->dataV, frame->data[2], poolFrame->linesizeV * uvH);
                        } else {
                            for (int y = 0; y < uvH; ++y) {
                                memcpy(poolFrame->dataV + y * poolFrame->linesizeV,
                                       frame->data[2] + y * frame->linesize[2], uvW);
                            }
                        }
                    }
                }

                poolFrame->width = w;
                poolFrame->height = h;
                poolFrame->pts = frame->pts;

                // 入队
                if (!m_frameQueue->pushFrame(poolFrame)) {
                    qWarning("[ZeroCopyDecoder] Frame queue full, dropping frame");
                } else {
                    emit frameReady();
                }
            }
        }

        // 保存 AVFrame 引用用于截图（仅增加引用计数，无数据拷贝）
        {
            QMutexLocker locker(&m_screenshotMutex);
            if (m_lastAVFrame) {
                av_frame_unref(m_lastAVFrame);
            } else {
                m_lastAVFrame = av_frame_alloc();
            }
            if (m_lastAVFrame) {
                av_frame_ref(m_lastAVFrame, frame);
            }
            m_lastWidth = w;
            m_lastHeight = h;
            m_screenshotCacheStale = true;
        }
        return;  // 零拷贝路径完成，不再使用回调
    }

    // 回调路径：兼容旧接口（无 FrameQueue 时使用）
    // 注意：回调路径不支持 NV12 格式，需要硬件解码时使用 FrameQueue 路径
    if (m_frameCallback && isYUV420P) {
        // 保存 AVFrame 引用用于截图（与零拷贝路径相同的懒拷贝策略）
        {
            QMutexLocker locker(&m_screenshotMutex);
            if (m_lastAVFrame) {
                av_frame_unref(m_lastAVFrame);
            } else {
                m_lastAVFrame = av_frame_alloc();
            }
            if (m_lastAVFrame) {
                av_frame_ref(m_lastAVFrame, frame);
            }
            m_lastWidth = w;
            m_lastHeight = h;
            m_screenshotCacheStale = true;
        }

        // 发送回调
        FrameData callbackFrame;
        callbackFrame.width = w;
        callbackFrame.height = h;
        callbackFrame.dataY = frame->data[0];
        callbackFrame.dataU = frame->data[1];
        callbackFrame.dataV = frame->data[2];
        callbackFrame.linesizeY = frame->linesize[0];
        callbackFrame.linesizeU = frame->linesize[1];
        callbackFrame.linesizeV = frame->linesize[2];
        callbackFrame.pts = frame->pts;
        m_frameCallback(&callbackFrame);
    }
}

// ---------------------------------------------------------
// FPS 统计
// ---------------------------------------------------------
void ZeroCopyDecoder::updateFps()
{
    m_frameCount++;

    // 使用 QElapsedTimer 替代 QDateTime 系统调用 (monotonic clock, 无系统调用)
    if (!m_fpsTimer.isValid()) {
        m_fpsTimer.start();
        return;
    }

    qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed >= 1000) {
        m_currentFps = static_cast<quint32>(m_frameCount * 1000 / elapsed);

        // 报告 FPS 到性能监控器
        qsc::PerformanceMonitor::instance().reportFps(m_currentFps);

        emit fpsUpdated(m_currentFps);
        m_frameCount = 0;
        m_fpsTimer.restart();
    }
}

// ---------------------------------------------------------
// 设置帧队列
// ---------------------------------------------------------
void ZeroCopyDecoder::setFrameQueue(FrameQueue* queue)
{
    m_frameQueue = queue;
}

// ---------------------------------------------------------
// 设置帧回调
// ---------------------------------------------------------
void ZeroCopyDecoder::setFrameCallback(FrameCallback callback)
{
    m_frameCallback = std::move(callback);
}

// ---------------------------------------------------------
// 硬件加速状态
// ---------------------------------------------------------
bool ZeroCopyDecoder::isHardwareAccelerated() const
{
    return m_hwDeviceCtx != nullptr;
}

// ---------------------------------------------------------
// 截图
// ---------------------------------------------------------
void ZeroCopyDecoder::peekFrame(std::function<void(int, int, uint8_t*)> callback)
{
    if (!callback) return;

    QMutexLocker locker(&m_screenshotMutex);

    if (!m_lastAVFrame || m_lastWidth <= 0 || m_lastHeight <= 0) {
        // 回退到缓存数据
        if (m_lastFrameY.empty()) return;
    }

    int w = m_lastWidth;
    int h = m_lastHeight;
    int uvW = w / 2;
    int uvH = h / 2;

    // 懒转换：仅在截图缓存过期时才从 AVFrame 拷贝
    if (m_screenshotCacheStale && m_lastAVFrame && m_lastAVFrame->data[0]) {
        m_lastFrameY.resize(w * h);
        m_lastFrameU.resize(uvW * uvH);
        m_lastFrameV.resize(uvW * uvH);

        // 复制 Y 平面
        if (m_lastAVFrame->linesize[0] == w) {
            memcpy(m_lastFrameY.data(), m_lastAVFrame->data[0], w * h);
        } else {
            for (int y = 0; y < h; ++y)
                memcpy(m_lastFrameY.data() + y * w, m_lastAVFrame->data[0] + y * m_lastAVFrame->linesize[0], w);
        }

        // 检查是否 NV12
        bool isNV12 = (m_lastAVFrame->format == AV_PIX_FMT_NV12);
        if (isNV12) {
            const uint8_t* uvSrc = m_lastAVFrame->data[1];
            int uvLinesize = m_lastAVFrame->linesize[1];
            for (int y = 0; y < uvH; ++y) {
                const uint8_t* uvRow = uvSrc + y * uvLinesize;
                uint8_t* uRow = m_lastFrameU.data() + y * uvW;
                uint8_t* vRow = m_lastFrameV.data() + y * uvW;
                for (int x = 0; x < uvW; ++x) {
                    uRow[x] = uvRow[x * 2];
                    vRow[x] = uvRow[x * 2 + 1];
                }
            }
        } else {
            if (m_lastAVFrame->linesize[1] == uvW) {
                memcpy(m_lastFrameU.data(), m_lastAVFrame->data[1], uvW * uvH);
            } else {
                for (int y = 0; y < uvH; ++y)
                    memcpy(m_lastFrameU.data() + y * uvW, m_lastAVFrame->data[1] + y * m_lastAVFrame->linesize[1], uvW);
            }
            if (m_lastAVFrame->linesize[2] == uvW) {
                memcpy(m_lastFrameV.data(), m_lastAVFrame->data[2], uvW * uvH);
            } else {
                for (int y = 0; y < uvH; ++y)
                    memcpy(m_lastFrameV.data() + y * uvW, m_lastAVFrame->data[2] + y * m_lastAVFrame->linesize[2], uvW);
            }
        }
        m_screenshotCacheStale = false;
    }

    if (m_lastFrameY.empty()) return;

    // YUV420P → RGB32
    std::vector<uint8_t> rgb32(w * h * 4);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int yIdx = y * w + x;
            int uvIdx = (y / 2) * uvW + (x / 2);

            int Y = m_lastFrameY[yIdx];
            int U = m_lastFrameU[uvIdx] - 128;
            int V = m_lastFrameV[uvIdx] - 128;

            // BT.709
            int R = qBound(0, static_cast<int>(Y + 1.5748 * V), 255);
            int G = qBound(0, static_cast<int>(Y - 0.1873 * U - 0.4681 * V), 255);
            int B = qBound(0, static_cast<int>(Y + 1.8556 * U), 255);

            int pixelIdx = (y * w + x) * 4;
            rgb32[pixelIdx + 0] = B;
            rgb32[pixelIdx + 1] = G;
            rgb32[pixelIdx + 2] = R;
            rgb32[pixelIdx + 3] = 255;
        }
    }

    callback(w, h, rgb32.data());
}

// ---------------------------------------------------------
// GPU 直通帧处理
// 将 D3D11VA 解码的 GPU 帧直接通过 FrameQueue 传递给渲染器
// 不调用 av_hwframe_transfer_data，消除 GPU→CPU 回读
// ---------------------------------------------------------
void ZeroCopyDecoder::processGPUDirectFrame(AVFrame* hwFrame)
{
#ifdef _WIN32
    if (!hwFrame || hwFrame->width <= 0 || hwFrame->height <= 0) {
        av_frame_unref(hwFrame);
        return;
    }

    // 从 AVFrame 提取 D3D11 纹理信息
    // D3D11VA 解码输出: hwFrame->data[0] = ID3D11Texture2D*
    //                   hwFrame->data[1] = (intptr_t) subresource index
    auto* d3d11Texture = reinterpret_cast<void*>(hwFrame->data[0]);
    int textureIndex = static_cast<int>(reinterpret_cast<intptr_t>(hwFrame->data[1]));

    if (!d3d11Texture) {
        qWarning("[ZeroCopyDecoder] GPU direct: null D3D11 texture");
        av_frame_unref(hwFrame);
        return;
    }

    int w = hwFrame->width;
    int h = hwFrame->height;

    // 检测分辨率变化
    if (m_decodedWidth != 0 && m_decodedHeight != 0) {
        if (w != m_decodedWidth || h != m_decodedHeight) {
            qInfo("[ZeroCopyDecoder] GPU direct: resolution changed %dx%d -> %dx%d",
                  m_decodedWidth, m_decodedHeight, w, h);
        }
    }
    m_decodedWidth = w;
    m_decodedHeight = h;

    qsc::PerformanceMonitor::instance().reportFrameDecoded();
    updateFps();

    if (m_frameQueue) {
        FrameData* poolFrame = m_frameQueue->acquireFrame();
        if (poolFrame) {
            // 设置 GPU 直通标记
            poolFrame->isGPUDirect = true;
            poolFrame->d3d11Texture = d3d11Texture;
            poolFrame->d3d11TextureIndex = textureIndex;
            poolFrame->width = w;
            poolFrame->height = h;
            poolFrame->isNV12 = true;  // D3D11VA 输出始终是 NV12
            poolFrame->pts = hwFrame->pts;

            // 克隆 AVFrame 引用，延长 GPU 纹理生命周期到渲染完成
            AVFrame* clonedFrame = av_frame_clone(hwFrame);
            poolFrame->hwAVFrame = clonedFrame;

            m_frameQueue->pushFrame(poolFrame);
            emit frameReady();
        } else {
            qsc::PerformanceMonitor::instance().reportFrameDropped();
        }
    }

    // unref 原始 hwFrame (clonedFrame 已持有独立的纹理引用)
    av_frame_unref(hwFrame);
#else
    // 非 Windows 平台不支持 D3D11VA，回退到旧路径
    if (transferHwFrame(hwFrame, m_swFrame)) {
        processDecodedFrame(m_swFrame);
    }
    av_frame_unref(hwFrame);
#endif
}

// ---------------------------------------------------------
// 获取 D3D11 设备指针
// 用于渲染器初始化 WGL_NV_DX_interop
// ---------------------------------------------------------
void* ZeroCopyDecoder::getD3D11Device() const
{
#ifdef _WIN32
    if (!m_hwDeviceCtx) return nullptr;

    AVHWDeviceContext* deviceCtx = reinterpret_cast<AVHWDeviceContext*>(
        reinterpret_cast<AVBufferRef*>(m_hwDeviceCtx)->data);
    if (!deviceCtx) return nullptr;

    auto* d3d11DeviceCtx = static_cast<AVD3D11VADeviceContext*>(deviceCtx->hwctx);
    return d3d11DeviceCtx ? d3d11DeviceCtx->device : nullptr;
#else
    return nullptr;
#endif
}

} // namespace core
} // namespace qsc
