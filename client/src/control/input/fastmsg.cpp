#include "fastmsg.h"

// C-I09: 全局计数器 (线程安全)
std::atomic<quint32> FastTouchSeq::s_counter{0};

// ========== 工具方法 ==========

void FastMsg::writeU8(QByteArray& buf, quint8 v) {
    buf.append(static_cast<char>(v));
}

void FastMsg::writeU16(QByteArray& buf, quint16 v) {
    buf.append(static_cast<char>((v >> 8) & 0xFF));
    buf.append(static_cast<char>(v & 0xFF));
}

void FastMsg::writeU32(QByteArray& buf, quint32 v) {
    buf.append(static_cast<char>((v >> 24) & 0xFF));
    buf.append(static_cast<char>((v >> 16) & 0xFF));
    buf.append(static_cast<char>((v >> 8) & 0xFF));
    buf.append(static_cast<char>(v & 0xFF));
}

// ========== 单事件序列化 ==========

QByteArray FastMsg::serializeTouch(const FastTouchEvent& event) {
    QByteArray buf;
    buf.reserve(10);

    writeU8(buf, FMT_FAST_TOUCH);
    writeU32(buf, event.seqId);
    writeU8(buf, event.action);
    writeU16(buf, event.x);
    writeU16(buf, event.y);

    return buf;
}

QByteArray FastMsg::serializeKey(const FastKeyEvent& event) {
    QByteArray buf;
    buf.reserve(4);

    writeU8(buf, FMT_FAST_KEY);
    writeU8(buf, event.action);
    writeU16(buf, event.keycode);

    return buf;
}

// ========== 批量序列化 ==========

QByteArray FastMsg::serializeTouchBatch(const QVector<FastTouchEvent>& events) {
    if (events.isEmpty() || events.size() > 255) {
        return QByteArray();
    }

    QByteArray buf;
    buf.reserve(2 + events.size() * 9);

    writeU8(buf, FMT_FAST_BATCH);
    writeU8(buf, static_cast<quint8>(events.size()));

    for (const auto& event : events) {
        writeU32(buf, event.seqId);
        writeU8(buf, event.action);
        writeU16(buf, event.x);
        writeU16(buf, event.y);
    }

    return buf;
}

// ========== 便捷方法（归一化浮点坐标）==========

QByteArray FastMsg::touchDown(quint32 seqId, double x, double y) {
    return serializeTouch(FastTouchEvent::fromNormalized(seqId, FTA_DOWN, x, y));
}

QByteArray FastMsg::touchUp(quint32 seqId, double x, double y) {
    return serializeTouch(FastTouchEvent::fromNormalized(seqId, FTA_UP, x, y));
}

QByteArray FastMsg::touchMove(quint32 seqId, double x, double y) {
    return serializeTouch(FastTouchEvent::fromNormalized(seqId, FTA_MOVE, x, y));
}

// ========== 便捷方法（原始 quint16 坐标）==========

QByteArray FastMsg::touchDownRaw(quint32 seqId, quint16 x, quint16 y) {
    return serializeTouch(FastTouchEvent(seqId, FTA_DOWN, x, y));
}

QByteArray FastMsg::touchUpRaw(quint32 seqId, quint16 x, quint16 y) {
    return serializeTouch(FastTouchEvent(seqId, FTA_UP, x, y));
}

QByteArray FastMsg::touchMoveRaw(quint32 seqId, quint16 x, quint16 y) {
    return serializeTouch(FastTouchEvent(seqId, FTA_MOVE, x, y));
}

QByteArray FastMsg::keyDown(quint16 keycode) {
    return serializeKey(FastKeyEvent(FKA_DOWN, keycode));
}

QByteArray FastMsg::keyUp(quint16 keycode) {
    return serializeKey(FastKeyEvent(FKA_UP, keycode));
}

QByteArray FastMsg::keyClick(quint16 keycode) {
    QByteArray buf;
    buf.append(keyDown(keycode));
    buf.append(keyUp(keycode));
    return buf;
}
