#include <QDebug>
#include <QTime>
#include <QDateTime>
#include <QApplication>

#include "compat.h"
#include "demuxer.h"
#include "kcpvideosocket.h"
#include "videosocket.h"
#include "interfaces/IVideoChannel.h"

// 解码线程优先级提升所需的平台头文件
#ifdef Q_OS_WIN
#include <windows.h>
#include <avrt.h>   // MMCSS 实时调度
#else
#include <pthread.h>
#include <sched.h>
#endif

#define HEADER_SIZE 12

// ---------------------------------------------------------
// 数据包标志位与掩码
// ---------------------------------------------------------
#define SC_PACKET_FLAG_CONFIG    (UINT64_C(1) << 63) // 配置包标志
#define SC_PACKET_FLAG_KEY_FRAME (UINT64_C(1) << 62) // 关键帧标志
#define SC_PACKET_PTS_MASK (SC_PACKET_FLAG_KEY_FRAME - 1) // PTS 掩码

typedef qint32 (*ReadPacketFunc)(void *, quint8 *, qint32);

Demuxer::Demuxer(QObject *parent)
    : QThread(parent)
{}

Demuxer::~Demuxer() {}

// FFmpeg 日志重定向
// 仅输出 WARNING 及以上级别；INFO/DEBUG/VERBOSE 静默丢弃
// 避免每帧 nal_unit_type 等调试日志刷屏
static void avLogCallback(void *avcl, int level, const char *fmt, va_list vl)
{
    Q_UNUSED(avcl)

    // 过滤低优先级日志
    if (level > AV_LOG_WARNING) return;

    // 格式化 FFmpeg 日志消息
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, vl);

    // 去掉尾部换行
    int len = static_cast<int>(strlen(buf));
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    if (len == 0) return;

    switch (level) {
    case AV_LOG_PANIC:
    case AV_LOG_FATAL:
        qFatal("[FFmpeg] %s", buf);
        break;
    case AV_LOG_ERROR:
        qCritical("[FFmpeg] %s", buf);
        break;
    case AV_LOG_WARNING:
        qWarning("[FFmpeg] %s", buf);
        break;
    }
    return;
}

// ---------------------------------------------------------
// 全局初始化/反初始化
// ---------------------------------------------------------
bool Demuxer::init()
{
#ifdef KZSCRCPY_LAVF_REQUIRES_REGISTER_ALL
    av_register_all();
#endif
    if (avformat_network_init()) {
        return false;
    }
    av_log_set_callback(avLogCallback);
    return true;
}

void Demuxer::deInit()
{
    avformat_network_deinit();
}

void Demuxer::installKcpVideoSocket(KcpVideoSocket *kcpVideoSocket)
{
    m_kcpVideoSocket = kcpVideoSocket;
    m_videoSocket = nullptr;  // 互斥
    m_videoChannel = nullptr;
}

void Demuxer::installVideoSocket(VideoSocket *videoSocket)
{
    // 关键：在主线程中把 socket 移动到 Demuxer 线程
    videoSocket->moveToThread(this);
    m_videoSocket = videoSocket;
    m_kcpVideoSocket = nullptr;  // 互斥
    m_videoChannel = nullptr;
}

void Demuxer::installVideoChannel(qsc::core::IVideoChannel* channel)
{
    m_videoChannel = channel;
    m_kcpVideoSocket = nullptr;
    m_videoSocket = nullptr;
}

void Demuxer::setFrameSize(const QSize &frameSize)
{
    m_frameSize = frameSize;
}

void Demuxer::setVideoCodec(const QString &codec)
{
    m_videoCodec = codec;
}

// ---------------------------------------------------------
// 网络数据接收封装
// 支持三种模式：
// 1. IVideoChannel 接口（新架构推荐）
// 2. KCP (KcpVideoSocket) - WiFi 模式
// 3. TCP (VideoSocket) - USB 模式
// ---------------------------------------------------------
qint32 Demuxer::recvData(quint8 *buf, qint32 bufSize)
{
    if (!buf || m_stopRequested.load()) {
        return 0;
    }

    // 优先使用 IVideoChannel 接口
    if (m_videoChannel) {
        return m_videoChannel->recv(buf, bufSize);
    }

    // KCP 模式 (WiFi)
    if (m_kcpVideoSocket) {
        return m_kcpVideoSocket->subThreadRecvData(buf, bufSize);
    }

    // TCP 模式 (USB)
    if (m_videoSocket) {
        return m_videoSocket->subThreadRecvData(buf, bufSize);
    }

    return 0;
}

// ---------------------------------------------------------
// 线程控制
// ---------------------------------------------------------
bool Demuxer::startDecode()
{
    if (!m_kcpVideoSocket && !m_videoSocket) {
        return false;
    }
    // 重置停止标志
    m_stopRequested.store(false);
    // 使用 NormalPriority 避免在 MMCSS 环境下 InheritPriority 导致 "参数错误"
    // 实际优先级由 run() 内部通过 SetThreadPriority + MMCSS 设置
    start(QThread::NormalPriority);
    return true;
}

void Demuxer::stopDecode()
{
    // 设置停止标志，让 run() 循环退出
    m_stopRequested.store(true);

    // KCP 模式: KcpVideoSocket 是线程安全的，可以从主线程关闭
    if (m_kcpVideoSocket) {
        m_kcpVideoSocket->close();
    }

    // TCP 模式: 通知 VideoSocket 停止等待
    // requestStop() 只设置原子标志，是线程安全的
    // QTcpSocket 本身不能跨线程操作，但原子标志可以
    if (m_videoSocket) {
        m_videoSocket->requestStop();
    }

    // 等待线程结束 (最多等待 500ms，因为现在超时只有 100ms)
    if (!wait(500)) {
        qWarning() << "Demuxer thread did not exit in time, forcing termination";
        terminate();
        wait(100);
    }
}

// ---------------------------------------------------------
// 线程主循环
// 负责初始化解码上下文，循环接收并分发数据包
// ---------------------------------------------------------
void Demuxer::run()
{
    // 提升解码线程优先级
#ifdef Q_OS_WIN
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    // MMCSS: 注册为 "Pro Audio" 获得内核级优先级提升
    void* mmcssHandle = nullptr;
    {
        // 动态加载 avrt.dll 以避免编译依赖
        typedef HANDLE (WINAPI *PAvSetMmThreadCharacteristicsA)(LPCSTR, LPDWORD);
        typedef BOOL (WINAPI *PAvSetMmThreadPriority)(HANDLE, AVRT_PRIORITY);
        HMODULE hAvrt = LoadLibraryA("avrt.dll");
        if (hAvrt) {
            auto pfnSet = (PAvSetMmThreadCharacteristicsA)GetProcAddress(hAvrt, "AvSetMmThreadCharacteristicsA");
            auto pfnPri = (PAvSetMmThreadPriority)GetProcAddress(hAvrt, "AvSetMmThreadPriority");
            if (pfnSet) {
                DWORD taskIndex = 0;
                mmcssHandle = pfnSet("Pro Audio", &taskIndex);
                if (mmcssHandle && pfnPri) {
                    pfnPri(mmcssHandle, AVRT_PRIORITY_CRITICAL);
                    qInfo("[Demuxer] MMCSS registered: Pro Audio, index=%u", taskIndex);
                }
            }
        }
    }
#else
    // Linux/macOS: 尝试设置实时调度，失败则 nice(-10)
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        // 回退到降低 nice 值
        nice(-10);
    }
#endif

    m_codecCtx = Q_NULLPTR;
    m_parser = Q_NULLPTR;
    AVPacket *packet = Q_NULLPTR;
    const AVCodec* codec = Q_NULLPTR;

    // KCP 模式：首先接收 Video Header (12 字节: codec_id + width + height)
    // TCP 模式：设备信息已在 TcpServerHandler::readInfo() 中读取，跳过此步骤
    if (m_kcpVideoSocket) {
    quint8 videoHeader[12];
    if (recvData(videoHeader, 12) != 12) {
            qCritical("Failed to receive video header (KCP mode)!");
        goto runQuit;
    }

        quint32 codecId = (videoHeader[0] << 24) | (videoHeader[1] << 16) | (videoHeader[2] << 8) | videoHeader[3];
        quint32 width = (videoHeader[4] << 24) | (videoHeader[5] << 16) | (videoHeader[6] << 8) | videoHeader[7];
        quint32 height = (videoHeader[8] << 24) | (videoHeader[9] << 16) | (videoHeader[10] << 8) | videoHeader[11];

        Q_UNUSED(codecId);
        // 更新帧大小（如果服务器发送的和预设的不同）
        if (width > 0 && height > 0) {
            m_frameSize = QSize(width, height);
        }
        qInfo() << "KCP mode: received video header, size:" << m_frameSize;
    } else {
        qInfo() << "TCP mode: using pre-set frame size:" << m_frameSize;
    }

    // 查找解码器
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        qCritical("H.264 decoder not found");
        goto runQuit;
    }

    // 分配上下文
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qCritical("Could not allocate codec context");
        goto runQuit;
    }
    m_codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;  // 允许不规范的加速技巧
    m_codecCtx->thread_count = 1;                // 单线程避免帧重排序延迟
    m_codecCtx->thread_type = 0;                 // 禁用多线程缓冲
    m_codecCtx->width = m_frameSize.width();
    m_codecCtx->height = m_frameSize.height();
    m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

    // 初始化解析器
    m_parser = av_parser_init(AV_CODEC_ID_H264);
    if (!m_parser) {
        qCritical("Could not initialize parser");
        goto runQuit;
    }

    // 关键标志位：允许解析不完整帧，降低延迟
    m_parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;

    packet = av_packet_alloc();
    if (!packet) {
        qCritical("OOM");
        goto runQuit;
    }

    // 接收循环
    for (;;) {
        // 检查停止请求
        if (m_stopRequested.load()) {
            break;
        }

        bool ok = recvPacket(packet);
        if (!ok) {
            break;
        }

        ok = pushPacket(packet);
        av_packet_unref(packet);
        if (!ok) {
            break;
        }
    }

    if (m_pending) {
        av_packet_free(&m_pending);
    }

    av_packet_free(&packet);
    av_parser_close(m_parser);

runQuit:
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
    }

    if (m_kcpVideoSocket) {
        // KcpVideoSocket 在主线程创建（含 QTimer），不能在 Demuxer 线程操作
        // 将对象移回主线程，由 deleteLater() 在主线程事件循环中安全 close+销毁
        KcpVideoSocket *socket = m_kcpVideoSocket.data();
        m_kcpVideoSocket = Q_NULLPTR;
        socket->moveToThread(QApplication::instance()->thread());
        QMetaObject::invokeMethod(socket, "deleteLater", Qt::QueuedConnection);
    }

    if (m_videoSocket) {
        m_videoSocket->close();
        delete m_videoSocket;
        m_videoSocket = Q_NULLPTR;
    }

    emit onStreamStop();
}

// ---------------------------------------------------------
// 接收一个完整的数据包
// 包括 Header (12字节) 和 Payload (H.264 NALU)
// ---------------------------------------------------------
bool Demuxer::recvPacket(AVPacket *packet)
{
    if (!packet) {
        return false;
    }

    // 读取头部
    quint8 header[12];
    qint32 ret = recvData(header, 12);
    if (ret != 12) {
        return false;
    }

    // 解析包大小
    uint32_t len = (header[8] << 24) | (header[9] << 16) | (header[10] << 8) | header[11];

    if (av_new_packet(packet, len)) {
        return false;
    }

    // 读取数据体
    if (recvData(packet->data, len) != (int)len) {
        av_packet_unref(packet);
        return false;
    }

    // 解析 PTS
    uint64_t pts = ((uint64_t)header[0] << 56) | ((uint64_t)header[1] << 48) |
                   ((uint64_t)header[2] << 40) | ((uint64_t)header[3] << 32) |
                   ((uint64_t)header[4] << 24) | ((uint64_t)header[5] << 16) |
                   ((uint64_t)header[6] << 8) | header[7];

    packet->pts = pts;
    packet->dts = pts;

    return true;
}

// ---------------------------------------------------------
// 处理并分发数据包
// 区分 Config 包和数据包，处理包拼接逻辑
// ---------------------------------------------------------
bool Demuxer::pushPacket(AVPacket *packet)
{
    bool isConfig = packet->pts == AV_NOPTS_VALUE;

    // Config 包需要和后续的数据包拼接后才能解码
    if (m_pending || isConfig) {
        qint32 offset;
        if (m_pending) {
            offset = m_pending->size;
            if (av_grow_packet(m_pending, packet->size)) {
                qCritical("Could not grow packet");
                return false;
            }
        } else {
            offset = 0;
            m_pending = av_packet_alloc();
            if (av_new_packet(m_pending, packet->size)) {
                av_packet_free(&m_pending);
                qCritical("Could not create packet");
                return false;
            }
        }

        memcpy(m_pending->data + offset, packet->data, static_cast<unsigned int>(packet->size));

        if (!isConfig) {
            // 拼接完成，准备发送
            m_pending->pts = packet->pts;
            m_pending->dts = packet->dts;
            m_pending->flags = packet->flags;
            packet = m_pending;
        }
    }

    if (isConfig) {
        bool ok = processConfigPacket(packet);
        if (!ok) {
            return false;
        }
    } else {
        // 解析并分发
        bool ok = parse(packet);

        if (m_pending) {
            av_packet_free(&m_pending);
        }

        if (!ok) {
            return false;
        }
    }
    return true;
}

bool Demuxer::processConfigPacket(AVPacket *packet)
{
    emit getConfigFrame(packet);
    return true;
}

// ---------------------------------------------------------
// H.264 解析逻辑
// ---------------------------------------------------------
bool Demuxer::parse(AVPacket *packet)
{
    quint8 *inData = packet->data;
    int inLen = packet->size;
    quint8 *outData = Q_NULLPTR;
    int outLen = 0;
    // 调用 FFmpeg 解析器，标记关键帧
    int r = av_parser_parse2(m_parser, m_codecCtx, &outData, &outLen, inData, inLen, AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);

    Q_ASSERT(r == inLen);
    (void)r;
    Q_ASSERT(outLen == inLen);

    if (m_parser->key_frame == 1) {
        packet->flags |= AV_PKT_FLAG_KEY;
    }

    bool ok = processFrame(packet);
    if (!ok) {
        qCritical("Could not process frame");
        return false;
    }

    return true;
}

bool Demuxer::processFrame(AVPacket *packet)
{
    packet->dts = packet->pts;
    emit getFrame(packet);
    return true;
}
