/**
 * @file UdpVideoClient.cpp
 * @brief 裸 UDP 视频接收器实现
 */

#include "UdpVideoClient.h"
#include <QVariant>
#include <QtDebug>

UdpVideoClient::UdpVideoClient(QObject *parent)
    : QObject(parent)
{
    m_ioThread = new QThread(this);
    m_ioThread->setObjectName("VideoUDP-IO");

    // 不设 parent：允许后续 moveToThread
    m_socket = new QUdpSocket();
    connect(m_socket, &QUdpSocket::readyRead,
            this, &UdpVideoClient::onSocketReadyRead, Qt::DirectConnection);

    // 默认用下限值初始化，configure() 会根据实际参数重新设置
    m_ringBuffer.reserve(m_ringBufferSize);
    m_frameBuffer = new char[m_frameBufferSize];
}

UdpVideoClient::~UdpVideoClient()
{
    close();
    if (m_ioThread) {
        m_ioThread->quit();
        m_ioThread->wait();
    }
    delete m_socket;
    m_socket = nullptr;
    delete[] m_frameBuffer;
    m_frameBuffer = nullptr;
}

void UdpVideoClient::configure(quint32 bitrateBps, quint32 maxFps)
{
    // fps 为 0 表示不限制，默认按 60fps 计算
    quint32 fps = (maxFps > 0) ? maxFps : 60;

    // 环形缓冲区：缓冲约 3 秒数据
    qint64 ringSize = static_cast<qint64>(bitrateBps) / 8 * 3;
    m_ringBufferSize = qMax(static_cast<int>(qMin(ringSize, static_cast<qint64>(64 * 1024 * 1024))),
                            MIN_RING_BUFFER);

    // OS 接收缓冲：约 10 帧数据量
    qint64 recvSize = static_cast<qint64>(bitrateBps) / 8 / fps * 10;
    m_recvBufferSize = qMax(static_cast<int>(qMin(recvSize, static_cast<qint64>(16 * 1024 * 1024))),
                            MIN_RECV_BUFFER);

    // 帧重组缓冲：I 帧可达平均帧的 10-15 倍，不能用 avg*3 估算。
    // 上限取 bitrate/8（=1秒数据=IDR间隔），单帧不可能超此值。最小 1MB，最大 8MB。
    qint64 frameSize = static_cast<qint64>(bitrateBps) / 8;
    m_frameBufferSize = qMax(static_cast<int>(qMin(frameSize, static_cast<qint64>(8 * 1024 * 1024))),
                             MIN_FRAME_BUFFER);

    // 重新分配缓冲区
    m_ringBuffer.reserve(m_ringBufferSize);

    delete[] m_frameBuffer;
    m_frameBuffer = new char[m_frameBufferSize];
    m_frameLen = 0;

    qInfo("[UdpVideoClient] configure: bitrate=%uMbps, fps=%u → ring=%dMB, recv=%dMB, frame=%dKB",
          bitrateBps / 1000000, fps,
          m_ringBufferSize / (1024 * 1024),
          m_recvBufferSize / (1024 * 1024),
          m_frameBufferSize / 1024);
}

bool UdpVideoClient::bind(quint16 port)
{
    ensureIoThread();

    bool result = false;
    QMetaObject::invokeMethod(m_socket, [this, port, &result]() {
        result = m_socket->bind(QHostAddress::Any, port);
        if (result) {
            // 用 configure() 计算的值设置 OS 接收缓冲区
            m_socket->setSocketOption(
                QAbstractSocket::ReceiveBufferSizeSocketOption,
                QVariant(m_recvBufferSize));
            m_active = true;

            qInfo("[UdpVideoClient] bound port %d, ring=%dMB, recv=%dMB, frame=%dKB",
                  port,
                  m_ringBufferSize / (1024 * 1024),
                  m_recvBufferSize / (1024 * 1024),
                  m_frameBufferSize / 1024);
        }
    }, Qt::BlockingQueuedConnection);

    return result;
}

quint16 UdpVideoClient::localPort() const
{
    return m_socket ? m_socket->localPort() : 0;
}

void UdpVideoClient::connectTo(const QHostAddress &host, quint16 port)
{
    Q_UNUSED(host);
    Q_UNUSED(port);
    // UDP 视频是单向接收，不需要 connect
    // 保留此方法使 KcpVideoSocket 可以透明切换
    if (!m_active) {
        ensureIoThread();
        m_active = true;
    }
}

bool UdpVideoClient::isActive() const
{
    return m_active && !m_closed;
}

int UdpVideoClient::recvBlocking(char *buf, int bufSize, int timeoutMs)
{
    if (!buf || bufSize <= 0 || m_closed) return 0;

    QMutexLocker locker(&m_mutex);
    while (m_ringBuffer.available() < bufSize) {
        if (m_closed) return 0;
        bool ok = timeoutMs < 0 ? m_dataAvailable.wait(&m_mutex)
                                : m_dataAvailable.wait(&m_mutex, timeoutMs);
        if (!ok && m_ringBuffer.available() < bufSize) return 0;
    }

    int toRead = qMin(bufSize, m_ringBuffer.available());
    m_ringBuffer.read(buf, toRead);
    return toRead;
}

int UdpVideoClient::available() const
{
    QMutexLocker locker(&m_mutex);
    return m_ringBuffer.available();
}

void UdpVideoClient::close()
{
    m_closed = true;
    m_active = false;
    if (m_socket && m_ioThread && m_ioThread->isRunning()) {
        QMetaObject::invokeMethod(m_socket, [this]() {
            m_socket->close();
        }, Qt::BlockingQueuedConnection);
    } else if (m_socket) {
        m_socket->close();
    }
    m_dataAvailable.wakeAll();
    emit disconnected();
}

QString UdpVideoClient::stats() const
{
    return QString("recv=%1,buf=%2,pkts=%3,gaps=%4,frames=%5,drops=%6")
        .arg(m_totalRecv.load())
        .arg(m_ringBuffer.available())
        .arg(m_totalPackets.load())
        .arg(m_gapCount.load())
        .arg(m_completedFrames.load())
        .arg(m_droppedFrames.load());
}

void UdpVideoClient::ensureIoThread()
{
    if (m_ioThread && !m_ioThread->isRunning()) {
        m_socket->moveToThread(m_ioThread);
        m_ioThread->start();
    }
}

void UdpVideoClient::onSocketReadyRead()
{
    if (m_closed) return;

    char recvBuf[1500];
    bool committed = false;

    while (m_socket->hasPendingDatagrams()) {
        qint64 size = m_socket->readDatagram(recvBuf, sizeof(recvBuf));
        if (size <= SEQ_HEADER_SIZE) continue;

        // 解析头部: seq (4B big-endian) + flags (1B)
        uint32_t seq = (static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[0])) << 24) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[1])) << 16) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[2])) << 8)  |
                        static_cast<uint32_t>(static_cast<uint8_t>(recvBuf[3]));
        uint8_t flags = static_cast<uint8_t>(recvBuf[4]);

        // 首包检测
        if (m_firstPacket) {
            m_expectedSeq = seq;
            m_firstPacket = false;
            emit connected();
        }

        // 丢包统计
        if (seq != m_expectedSeq) {
            uint32_t gap = seq - m_expectedSeq;
            m_gapCount.fetch_add(gap);
        }
        m_expectedSeq = seq + 1;

        int payloadSize = static_cast<int>(size) - SEQ_HEADER_SIZE;
        m_totalPackets++;
        m_totalRecv += payloadSize;

        // ═══ 帧重组状态机 ═══
        //
        // SOF (Start of Frame) → 开始收集新帧
        // EOF (End of Frame)   → 帧接收完成，提交到环形缓冲区
        // seq 不连续           → 有丢包，整帧丢弃（不送解码器）
        //
        // 这保证了解码器只收到完整的帧数据，
        // 即使 WiFi 丢包也不会导致字节流错位→画面持续脏污。

        if (flags & FLAG_SOF) {
            // ── 帧首包 ──
            if (m_frameState == FrameState::COLLECTING) {
                // 上一帧的 EOF 丢失，丢弃不完整帧
                m_droppedFrames++;
            }
            m_frameLen = 0;
            m_lastSeq = seq;

            if (payloadSize <= m_frameBufferSize) {
                memcpy(m_frameBuffer, recvBuf + SEQ_HEADER_SIZE, payloadSize);
                m_frameLen = payloadSize;
            }

            if (flags & FLAG_EOF) {
                // 单包帧（SOF|EOF），直接提交
                commitFrame();
                committed = true;
                m_frameState = FrameState::WAITING_SOF;
            } else {
                m_frameState = FrameState::COLLECTING;
            }
        } else if (m_frameState == FrameState::COLLECTING) {
            // ── 帧中间或尾包 ──
            if (seq != m_lastSeq + 1) {
                // seq 不连续 → 中间有丢包，整帧作废
                m_droppedFrames++;
                m_frameLen = 0;
                m_frameState = FrameState::WAITING_SOF;
            } else {
                m_lastSeq = seq;
                if (m_frameLen + payloadSize > m_frameBufferSize) {
                    // 帧数据超出预期大小，丢弃
                    m_droppedFrames++;
                    m_frameLen = 0;
                    m_frameState = FrameState::WAITING_SOF;
                } else {
                    memcpy(m_frameBuffer + m_frameLen,
                           recvBuf + SEQ_HEADER_SIZE, payloadSize);
                    m_frameLen += payloadSize;

                    if (flags & FLAG_EOF) {
                        // 帧尾包 → 帧接收完成
                        commitFrame();
                        committed = true;
                        m_frameState = FrameState::WAITING_SOF;
                    }
                }
            }
        }
        // else: 非 SOF 且处于 WAITING_SOF → 孤立包（已丢弃帧的残余），忽略
    }

    if (committed) {
        m_dataAvailable.wakeAll();
    }
}

void UdpVideoClient::commitFrame()
{
    if (m_frameLen <= 0) return;

    QMutexLocker locker(&m_mutex);
    if (m_ringBuffer.freeSpace() < m_frameLen) {
        // 关键修复：缓冲区满时丢弃新帧，而非 drop() 旧数据
        //
        // drop() 会移动读指针，导致 demuxer 正在两次 recvBlocking() 之间
        // 被截断帧数据 → 字节流永久错位 → 解码器收到垃圾 → 画面脏污。
        //
        // 丢弃新帧只造成时间跳过（一帧黑/冻结），不破坏已提交的数据流，
        // 下一个 IDR 或后续帧可正常恢复。
        m_droppedFrames++;
        m_frameLen = 0;
        return;
    }
    m_ringBuffer.write(m_frameBuffer, m_frameLen);
    m_completedFrames++;
    m_frameLen = 0;
}
