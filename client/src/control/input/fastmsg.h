#ifndef FASTMSG_H
#define FASTMSG_H

#include <vector>
#include <cstdint>
#include <algorithm>
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
enum FastMsgType : uint8_t {
    FMT_TOUCH_DOWN  = 10,   // seqId(1)+x(2)+y(2) = 总 6B
    FMT_TOUCH_UP    = 11,   // seqId(1)+x(2)+y(2) = 总 6B
    FMT_TOUCH_MOVE  = 12,   // seqId(1)+x(2)+y(2) = 总 6B
    FMT_TOUCH_RESET = 13,   // 无载荷 = 总 1B
    FMT_KEY_DOWN    = 14,   // keycode(2) = 总 3B
    FMT_KEY_UP      = 15,   // keycode(2) = 总 3B
    FMT_BATCH       = 16,   // count(1)+[seqId(1)+action(1)+x(2)+y(2)]*N = 2+6N
    FMT_SET_VIDEO_BITRATE = 20, // bitrate(4) = 总 5B
    FMT_SET_DISPLAY_POWER = 21, // mode(1) = 总 2B
    FMT_DISCONNECT  = 0xFF, // 无载荷 = 总 1B
};

// ---- 逻辑动作值 (内部使用 & batch 载荷) ----
enum FastTouchAction : uint8_t {
    FTA_DOWN  = 0,
    FTA_UP    = 1,
    FTA_MOVE  = 2,
    FTA_RESET = 3,
};

enum FastKeyAction : uint8_t {
    FKA_DOWN = 0,
    FKA_UP   = 1,
};

// ---- 事件结构体 (内部表示，不变) ----
struct FastTouchEvent {
    uint32_t seqId;
    uint8_t  action;
    uint16_t x, y;   // 归一化 0-65535

    FastTouchEvent() : seqId(0), action(FTA_DOWN), x(0), y(0) {}
    FastTouchEvent(uint32_t seq, uint8_t act, uint16_t px, uint16_t py)
        : seqId(seq), action(act), x(px), y(py) {}

    static FastTouchEvent fromNormalized(uint32_t seq, uint8_t action, double nx, double ny) {
        return FastTouchEvent(seq, action,
            static_cast<uint16_t>(std::clamp(nx, 0.0, 1.0) * 65535),
            static_cast<uint16_t>(std::clamp(ny, 0.0, 1.0) * 65535));
    }
};

struct FastKeyEvent {
    uint8_t  action;
    uint16_t keycode;

    FastKeyEvent() : action(FKA_DOWN), keycode(0) {}
    FastKeyEvent(uint8_t act, uint16_t key) : action(act), keycode(key) {}
};

// ---- 序列化器 ----
class FastMsg {
public:
    /// 触摸: RESET→1B, 其余→6B
    static std::vector<uint8_t> serializeTouch(const FastTouchEvent& e);
    static int serializeTouchInto(char* buf, const FastTouchEvent& e);

    /// 按键: 3B
    static std::vector<uint8_t> serializeKey(const FastKeyEvent& e);
    static int serializeKeyInto(char* buf, const FastKeyEvent& e);

    /// 批量触摸: 2+6N B
    static std::vector<uint8_t> serializeTouchBatch(const std::vector<FastTouchEvent>& events);

    /// 按键点击 (DOWN+UP): 6B
    static std::vector<uint8_t> keyClick(uint16_t keycode);

    /// 断开连接: 1B
    static std::vector<uint8_t> disconnect();

    /// 设置视频码率: 5B (type + uint32 bitrate)
    static std::vector<uint8_t> setVideoBitRate(uint32_t bitrate);

    /// 设置显示电源: 2B (type + uint8 mode)
    static std::vector<uint8_t> setDisplayPower(bool on);
};

// ---- 全局 seqId 生成器 (线程安全, 1-255 循环, 跳过 0) ----
// 注意: seqId=0 被多处 Handler 用作"无活跃触摸"哨兵值，必须跳过
class FastTouchSeq {
public:
    static uint32_t next() {
        uint32_t v = (s_counter.fetch_add(1, std::memory_order_relaxed) + 1) & 0xFF;
        if (v == 0) {
            v = (s_counter.fetch_add(1, std::memory_order_relaxed) + 1) & 0xFF;
        }
        return v;
    }
    static void reset() { s_counter.store(0, std::memory_order_relaxed); }
private:
    static std::atomic<uint32_t> s_counter;
};

#endif // FASTMSG_H
