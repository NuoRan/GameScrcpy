#ifndef FASTMSG_H
#define FASTMSG_H

#include <QByteArray>
#include <QSize>
#include <QPointF>
#include <QVector>
#include <atomic>

/**
 * @brief 极简控制协议 v2 / Minimal Control Protocol v2
 *
 * 将动作编码进类型字节，seqId 压缩到 1 字节:
 *   Touch DOWN/UP/MOVE = 6 bytes  (was 10)
 *   Touch RESET        = 1 byte   (was 10)
 *   Key DOWN/UP        = 3 bytes  (was 4)
 *   Key Click (D+U)    = 6 bytes  (was 8)
 *   Batch per-event    = 6 bytes  (was 9)
 *   Disconnect         = 1 byte
 */

// ---- 线上消息类型 (action 编码在 type 中) ----
enum FastMsgType : quint8 {
    FMT_TOUCH_DOWN  = 10,   // seqId(1)+x(2)+y(2) = 总 6B
    FMT_TOUCH_UP    = 11,   // seqId(1)+x(2)+y(2) = 总 6B
    FMT_TOUCH_MOVE  = 12,   // seqId(1)+x(2)+y(2) = 总 6B
    FMT_TOUCH_RESET = 13,   // 无载荷 = 总 1B
    FMT_KEY_DOWN    = 14,   // keycode(2) = 总 3B
    FMT_KEY_UP      = 15,   // keycode(2) = 总 3B
    FMT_BATCH       = 16,   // count(1)+[seqId(1)+action(1)+x(2)+y(2)]*N = 2+6N
    FMT_DISCONNECT  = 0xFF, // 无载荷 = 总 1B
};

// ---- 逻辑动作值 (内部使用 & batch 载荷) ----
enum FastTouchAction : quint8 {
    FTA_DOWN  = 0,
    FTA_UP    = 1,
    FTA_MOVE  = 2,
    FTA_RESET = 3,
};

enum FastKeyAction : quint8 {
    FKA_DOWN = 0,
    FKA_UP   = 1,
};

// ---- 事件结构体 (内部表示，不变) ----
struct FastTouchEvent {
    quint32 seqId;
    quint8  action;
    quint16 x, y;   // 归一化 0-65535

    FastTouchEvent() : seqId(0), action(FTA_DOWN), x(0), y(0) {}
    FastTouchEvent(quint32 seq, quint8 act, quint16 px, quint16 py)
        : seqId(seq), action(act), x(px), y(py) {}

    static FastTouchEvent fromNormalized(quint32 seq, quint8 action, double nx, double ny) {
        return FastTouchEvent(seq, action,
            static_cast<quint16>(qBound(0.0, nx, 1.0) * 65535),
            static_cast<quint16>(qBound(0.0, ny, 1.0) * 65535));
    }
};

struct FastKeyEvent {
    quint8  action;
    quint16 keycode;

    FastKeyEvent() : action(FKA_DOWN), keycode(0) {}
    FastKeyEvent(quint8 act, quint16 key) : action(act), keycode(key) {}
};

// ---- 序列化器 ----
class FastMsg {
public:
    /// 触摸: RESET→1B, 其余→6B
    static QByteArray serializeTouch(const FastTouchEvent& e);
    static int serializeTouchInto(char* buf, const FastTouchEvent& e);

    /// 按键: 3B
    static QByteArray serializeKey(const FastKeyEvent& e);
    static int serializeKeyInto(char* buf, const FastKeyEvent& e);

    /// 批量触摸: 2+6N B
    static QByteArray serializeTouchBatch(const QVector<FastTouchEvent>& events);

    /// 按键点击 (DOWN+UP): 6B
    static QByteArray keyClick(quint16 keycode);

    /// 断开连接: 1B
    static QByteArray disconnect();
};

// ---- 全局 seqId 生成器 (线程安全, 0-255 循环) ----
class FastTouchSeq {
public:
    static quint32 next() {
        return (s_counter.fetch_add(1, std::memory_order_relaxed) + 1) & 0xFF;
    }
    static void reset() { s_counter.store(0, std::memory_order_relaxed); }
private:
    static std::atomic<quint32> s_counter;
};

#endif // FASTMSG_H
