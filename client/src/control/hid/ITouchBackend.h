/**
 * @file ITouchBackend.h
 * @brief 触控后端接口 / Touch Backend Interface
 *
 * 定义硬件级触控后端的通用接口，支持多种实现:
 * - AoaHidBackend: AOA HID 协议 (USB 直连)
 * - Esp32HidBackend: ESP32 串口 HID (预留)
 * - ScrcpyBackend: 现有 scrcpy 注入 (包装)
 */
#ifndef ITOUCHBACKEND_H
#define ITOUCHBACKEND_H

#include <cstdint>
#include <string>
#include <functional>

/// 触控动作，与 FastMsg FTA_xxx 对齐
enum class TouchAction : uint8_t {
    Down = 0,  // FTA_DOWN
    Up   = 1,  // FTA_UP
    Move = 2,  // FTA_MOVE
};

/// 按键动作
enum class HidKeyAction : uint8_t {
    Down = 0,
    Up   = 1,
};

/**
 * @brief 触控后端抽象接口
 *
 * 所有坐标使用 0-65535 范围 (与 FastMsg 一致)，
 * touchId 使用 1-255 (与 FastMsg seqId 一致)。
 * 后端内部负责到各自协议坐标系的转换。
 */
class ITouchBackend {
public:
    virtual ~ITouchBackend() = default;

    // 生命周期
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isConnected() const = 0;
    virtual std::string statusText() const = 0;

    // 触摸: x,y 范围 0-65535, touchId 范围 1-255
    virtual bool sendTouch(TouchAction action, uint8_t touchId,
                           uint16_t x, uint16_t y) = 0;

    // 释放所有触摸点
    virtual void resetAllTouch() = 0;

    /// 设置伴侣回调: 每次触摸时通知归一化坐标 (显示方向, 0.0-1.0)
    using TouchPosCb = std::function<void(float nx, float ny)>;
    void setTouchPositionCallback(TouchPosCb cb) { m_touchPosCb = std::move(cb); }

    // 按键支持 (部分后端支持)
    virtual bool supportsKeys() const { return false; }
    virtual bool sendKey(HidKeyAction action, int32_t androidKeycode) {
        (void)action; (void)androidKeycode; return false;
    }

protected:
    TouchPosCb m_touchPosCb;
};

#endif // ITOUCHBACKEND_H
