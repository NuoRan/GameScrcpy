#ifndef BUFFERUTIL_H
#define BUFFERUTIL_H
#include <QBuffer>

/**
 * @brief 字节序列化工具 / Byte Serialization Utility
 *
 * 提供大端序 (Big-Endian) 的整数读写方法，用于 scrcpy 协议消息编解码。
 * Provides big-endian integer read/write methods for scrcpy protocol encoding/decoding.
 */
class BufferUtil
{
public:
    static void write16(QBuffer &buffer, quint16 value);
    static void write32(QBuffer &buffer, quint32 value);
    static void write64(QBuffer &buffer, quint64 value);
    static quint16 read16(QBuffer &buffer);
    static quint32 read32(QBuffer &buffer);
    static quint64 read64(QBuffer &buffer);
};

#endif // BUFFERUTIL_H
