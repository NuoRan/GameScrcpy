#include "fastmsg.h"

std::atomic<uint32_t> FastTouchSeq::s_counter{0};

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

std::vector<uint8_t> FastMsg::serializeTouch(const FastTouchEvent& e) {
    char buf[6];
    int len = serializeTouchInto(buf, e);
    return std::vector<uint8_t>(reinterpret_cast<uint8_t*>(buf), reinterpret_cast<uint8_t*>(buf) + len);
}

// ========== Key (3B) ==========

int FastMsg::serializeKeyInto(char* buf, const FastKeyEvent& e) {
    // type = action + 14  (DOWN=0→14, UP=1→15)
    buf[0] = static_cast<char>(e.action + 14);
    buf[1] = static_cast<char>((e.keycode >> 8) & 0xFF);
    buf[2] = static_cast<char>(e.keycode & 0xFF);
    return 3;
}

std::vector<uint8_t> FastMsg::serializeKey(const FastKeyEvent& e) {
    char buf[3];
    serializeKeyInto(buf, e);
    return std::vector<uint8_t>(reinterpret_cast<uint8_t*>(buf), reinterpret_cast<uint8_t*>(buf) + 3);
}

// ========== Batch (2+6N B) ==========

std::vector<uint8_t> FastMsg::serializeTouchBatch(const std::vector<FastTouchEvent>& events) {
    if (events.empty() || events.size() > 255) return std::vector<uint8_t>();

    std::vector<uint8_t> buf;
    buf.reserve(2 + events.size() * 6);
    buf.push_back(static_cast<uint8_t>(FMT_BATCH));
    buf.push_back(static_cast<uint8_t>(events.size()));
    for (const auto& e : events) {
        buf.push_back(static_cast<uint8_t>(e.seqId & 0xFF));
        buf.push_back(static_cast<uint8_t>(e.action));
        buf.push_back(static_cast<uint8_t>((e.x >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(e.x & 0xFF));
        buf.push_back(static_cast<uint8_t>((e.y >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(e.y & 0xFF));
    }
    return buf;
}

// ========== Convenience ==========

std::vector<uint8_t> FastMsg::keyClick(uint16_t keycode) {
    char buf[6];
    serializeKeyInto(buf, FastKeyEvent(FKA_DOWN, keycode));
    serializeKeyInto(buf + 3, FastKeyEvent(FKA_UP, keycode));
    return std::vector<uint8_t>(reinterpret_cast<uint8_t*>(buf), reinterpret_cast<uint8_t*>(buf) + 6);
}

std::vector<uint8_t> FastMsg::disconnect() {
    uint8_t buf[1] = { static_cast<uint8_t>(FMT_DISCONNECT) };
    return std::vector<uint8_t>(buf, buf + 1);
}

std::vector<uint8_t> FastMsg::setVideoBitRate(uint32_t bitrate) {
    uint8_t buf[5];
    buf[0] = static_cast<uint8_t>(FMT_SET_VIDEO_BITRATE);
    buf[1] = static_cast<uint8_t>((bitrate >> 24) & 0xFF);
    buf[2] = static_cast<uint8_t>((bitrate >> 16) & 0xFF);
    buf[3] = static_cast<uint8_t>((bitrate >>  8) & 0xFF);
    buf[4] = static_cast<uint8_t>((bitrate      ) & 0xFF);
    return std::vector<uint8_t>(buf, buf + 5);
}

std::vector<uint8_t> FastMsg::setDisplayPower(bool on) {
    uint8_t buf[2];
    buf[0] = static_cast<uint8_t>(FMT_SET_DISPLAY_POWER);
    buf[1] = on ? 1 : 0;
    return std::vector<uint8_t>(buf, buf + 2);
}


