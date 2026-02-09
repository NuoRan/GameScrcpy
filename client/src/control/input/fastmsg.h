#ifndef FASTMSG_H
#define FASTMSG_H

#include <QByteArray>
#include <QSize>
#include <QPointF>
#include <QVector>
#include <atomic>  // C-I09: 线程安全

/**
 * @brief 快速消息协议 - 零延迟控制 / Fast Message Protocol - Zero-Latency Control
 *
 * 设计原则 / Design principles:
 * 1. 紧凑：最小化消息大小 / Compact: minimize message size
 * 2. 无状态：每个消息完全自包含 / Stateless: each message is self-contained
 * 3. 批量友好：支持多个事件合并发送 / Batch-friendly: supports merging events
 */

// 消息类型 / Message types
enum FastMsgType : quint8 {
    FMT_FAST_TOUCH = 100,   // 快速触摸 / Fast touch: 10 bytes
    FMT_FAST_KEY = 101,     // 快速按键 / Fast key: 4 bytes
    FMT_FAST_BATCH = 102,   // 批量事件 / Batch events: 2+N*9 bytes
    FMT_DISCONNECT = 200,   // 断开连接 / Disconnect: 1 byte
};

// 触摸动作 / Touch actions
enum FastTouchAction : quint8 {
    FTA_DOWN = 0,
    FTA_UP = 1,
    FTA_MOVE = 2,
    FTA_RESET = 3,  // 释放全部触摸点 / Release all touch points
};

// 按键动作
enum FastKeyAction : quint8 {
    FKA_DOWN = 0,
    FKA_UP = 1,
};

/**
 * @brief 快速触摸事件 / Fast Touch Event
 */
struct FastTouchEvent {
    quint32 seqId;      // 触摸序列 ID / Touch sequence ID
    quint8 action;      // 触摸动作 / Touch action (FTA_*)
    quint16 x;          // 归一化 X (0-65535) / Normalized X
    quint16 y;          // 归一化 Y (0-65535) / Normalized Y

    FastTouchEvent() : seqId(0), action(FTA_DOWN), x(0), y(0) {}
    FastTouchEvent(quint32 seq, quint8 act, quint16 px, quint16 py)
        : seqId(seq), action(act), x(px), y(py) {}

    // 从归一化浮点坐标创建
    static FastTouchEvent fromNormalized(quint32 seq, quint8 action, double nx, double ny) {
        quint16 px = static_cast<quint16>(qBound(0.0, nx, 1.0) * 65535);
        quint16 py = static_cast<quint16>(qBound(0.0, ny, 1.0) * 65535);
        return FastTouchEvent(seq, action, px, py);
    }
};

/**
 * 快速按键事件
 */
struct FastKeyEvent {
    quint8 action;      // FKA_DOWN / FKA_UP
    quint16 keycode;    // Android keycode

    FastKeyEvent() : action(FKA_DOWN), keycode(0) {}
    FastKeyEvent(quint8 act, quint16 key) : action(act), keycode(key) {}
};

/**
 * 快速消息序列化器
 */
class FastMsg {
public:
    // ========== 单事件序列化 ==========

    /**
     * 序列化快速触摸事件
     * 格式：type(1) + seqId(4) + action(1) + x(2) + y(2) = 10 bytes
     */
    static QByteArray serializeTouch(const FastTouchEvent& event);

    /**
     * P-KCP: 零堆分配版本 — 直接写入调用方提供的栈缓冲区
     * @param buf 至少 10 字节的缓冲区
     * @return 写入字节数 (固定 10)
     */
    static int serializeTouchInto(char* buf, const FastTouchEvent& event);

    /**
     * 序列化快速按键事件
     * 格式：type(1) + action(1) + keycode(2) = 4 bytes
     */
    static QByteArray serializeKey(const FastKeyEvent& event);

    // ========== 批量序列化 ==========

    /**
     * 序列化批量触摸事件
     * 格式：type(1) + count(1) + events[count * 9]
     * 每个事件：seqId(4) + action(1) + x(2) + y(2) = 9 bytes
     */
    static QByteArray serializeTouchBatch(const QVector<FastTouchEvent>& events);

    // ========== 便捷方法（归一化浮点坐标 0.0-1.0）==========

    /**
     * 创建触摸 DOWN 事件（归一化浮点坐标）
     */
    static QByteArray touchDown(quint32 seqId, double x, double y);

    /**
     * 创建触摸 UP 事件（归一化浮点坐标）
     */
    static QByteArray touchUp(quint32 seqId, double x, double y);

    /**
     * 创建触摸 MOVE 事件（归一化浮点坐标）
     */
    static QByteArray touchMove(quint32 seqId, double x, double y);

    // ========== 便捷方法（原始 quint16 坐标 0-65535）==========

    /**
     * 创建触摸 DOWN 事件（原始坐标）
     */
    static QByteArray touchDownRaw(quint32 seqId, quint16 x, quint16 y);

    /**
     * 创建触摸 UP 事件（原始坐标）
     */
    static QByteArray touchUpRaw(quint32 seqId, quint16 x, quint16 y);

    /**
     * 创建触摸 MOVE 事件（原始坐标）
     */
    static QByteArray touchMoveRaw(quint32 seqId, quint16 x, quint16 y);

    /**
     * 创建按键 DOWN 事件
     */
    static QByteArray keyDown(quint16 keycode);

    /**
     * 创建按键 UP 事件
     */
    static QByteArray keyUp(quint16 keycode);

    /**
     * 创建完整点击（DOWN + UP）
     * 返回两个消息的合并
     */
    static QByteArray keyClick(quint16 keycode);

    /**
     * 创建断开连接消息
     * 格式：type(1) = 1 byte
     */
    static QByteArray disconnect();

private:
    // 工具方法
    static void writeU8(QByteArray& buf, quint8 v);
    static void writeU16(QByteArray& buf, quint16 v);
    static void writeU32(QByteArray& buf, quint32 v);
};

/**
 * 全局触摸序列 ID 生成器
 * C-I09: 使用 atomic 保证线程安全
 */
class FastTouchSeq {
public:
    static quint32 next() {
        return s_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    static void reset() {
        s_counter.store(0, std::memory_order_relaxed);
    }

private:
    static std::atomic<quint32> s_counter;
};

#endif // FASTMSG_H
