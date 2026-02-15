#include "fastmsg.h"

std::atomic<quint32> FastTouchSeq::s_counter{0};

// ========== Touch (RESET=1B, 其余=6B) ==========

int FastMsg::serializeTouchInto(char* buf, const FastTouchEvent& e) {
    if (e.action == FTA_RESET) {
        buf[0] = static_cast<char>(FMT_TOUCH_RESET);
        return 1;
    }
    // type = action + 10  (DOWN=0→10, UP=1→11, MOVE=2→12)
    buf[0] = static_cast<char>(e.action + 10);
    buf[1] = static_cast<char>(e.seqId & 0xFF);
    buf[2] = static_cast<char>((e.x >> 8) & 0xFF);
    buf[3] = static_cast<char>(e.x & 0xFF);
    buf[4] = static_cast<char>((e.y >> 8) & 0xFF);
    buf[5] = static_cast<char>(e.y & 0xFF);
    return 6;
}

QByteArray FastMsg::serializeTouch(const FastTouchEvent& e) {
    char buf[6];
    int len = serializeTouchInto(buf, e);
    return QByteArray(buf, len);
}

// ========== Key (3B) ==========

int FastMsg::serializeKeyInto(char* buf, const FastKeyEvent& e) {
    // type = action + 14  (DOWN=0→14, UP=1→15)
    buf[0] = static_cast<char>(e.action + 14);
    buf[1] = static_cast<char>((e.keycode >> 8) & 0xFF);
    buf[2] = static_cast<char>(e.keycode & 0xFF);
    return 3;
}

QByteArray FastMsg::serializeKey(const FastKeyEvent& e) {
    char buf[3];
    serializeKeyInto(buf, e);
    return QByteArray(buf, 3);
}

// ========== Batch (2+6N B) ==========

QByteArray FastMsg::serializeTouchBatch(const QVector<FastTouchEvent>& events) {
    if (events.isEmpty() || events.size() > 255) return QByteArray();

    QByteArray buf;
    buf.reserve(2 + events.size() * 6);
    buf.append(static_cast<char>(FMT_BATCH));
    buf.append(static_cast<char>(events.size()));
    for (const auto& e : events) {
        buf.append(static_cast<char>(e.seqId & 0xFF));
        buf.append(static_cast<char>(e.action));
        buf.append(static_cast<char>((e.x >> 8) & 0xFF));
        buf.append(static_cast<char>(e.x & 0xFF));
        buf.append(static_cast<char>((e.y >> 8) & 0xFF));
        buf.append(static_cast<char>(e.y & 0xFF));
    }
    return buf;
}

// ========== Convenience ==========

QByteArray FastMsg::keyClick(quint16 keycode) {
    char buf[6];
    serializeKeyInto(buf, FastKeyEvent(FKA_DOWN, keycode));
    serializeKeyInto(buf + 3, FastKeyEvent(FKA_UP, keycode));
    return QByteArray(buf, 6);
}

QByteArray FastMsg::disconnect() {
    char buf[1] = { static_cast<char>(FMT_DISCONNECT) };
    return QByteArray(buf, 1);
}
