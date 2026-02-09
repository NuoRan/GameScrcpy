#ifndef CORE_IINPUTPROCESSOR_H
#define CORE_IINPUTPROCESSOR_H

#include <functional>
#include <cstdint>
#include <QString>
#include <QSize>

// 前向声明 Qt 事件类型
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

namespace qsc {
namespace core {

/**
 * @brief 输入处理器接口 / Input Processor Interface
 *
 * 定义输入事件处理的通用接口，支持多种实现 / Generic input handling interface, multiple implementations:
 * - GameInputProcessor: 游戏模式（键位映射、视角控制）/ Game mode (key mapping, viewport control)
 * - NormalInputProcessor: 普通模式（直接转发）/ Normal mode (direct forwarding)
 */
class IInputProcessor {
public:
    virtual ~IInputProcessor() = default;

    // === 回调类型定义 ===

    /**
     * @brief 触摸事件回调
     * @param seqId 序列 ID
     * @param action 动作 (0=DOWN, 1=UP, 2=MOVE)
     * @param x 归一化 X (0-65535)
     * @param y 归一化 Y (0-65535)
     */
    using TouchCallback = std::function<void(uint32_t seqId, uint8_t action, uint16_t x, uint16_t y)>;

    /**
     * @brief 按键事件回调
     * @param action 动作 (0=DOWN, 1=UP)
     * @param keycode Android 键码
     */
    using KeyCallback = std::function<void(uint8_t action, int32_t keycode)>;

    /**
     * @brief 光标状态变化回调
     * @param grabbed true=游戏模式(隐藏光标), false=光标模式
     */
    using CursorGrabCallback = std::function<void(bool grabbed)>;

    // === 事件处理 ===

    /**
     * @brief 处理键盘事件
     * @param event 键盘事件
     * @param frameSize 视频帧尺寸
     * @param showSize 显示窗口尺寸
     */
    virtual void processKeyEvent(const QKeyEvent* event,
                                 const QSize& frameSize,
                                 const QSize& showSize) = 0;

    /**
     * @brief 处理鼠标事件
     */
    virtual void processMouseEvent(const QMouseEvent* event,
                                   const QSize& frameSize,
                                   const QSize& showSize) = 0;

    /**
     * @brief 处理滚轮事件
     */
    virtual void processWheelEvent(const QWheelEvent* event,
                                   const QSize& frameSize,
                                   const QSize& showSize) = 0;

    // === 状态控制 ===

    /**
     * @brief 加载键位映射
     * @param json 键位配置 JSON 字符串
     * @param runAutoStart 是否执行自动启动脚本
     */
    virtual void loadKeyMap(const QString& json, bool runAutoStart = true) = 0;

    /**
     * @brief 窗口失去焦点时重置状态
     */
    virtual void onWindowFocusLost() = 0;

    /**
     * @brief 重置所有状态
     */
    virtual void resetState() = 0;

    /**
     * @brief 释放所有触摸点
     */
    virtual void releaseAllTouchPoints() = 0;

    // === 回调设置 ===

    /**
     * @brief 设置触摸回调
     */
    virtual void setTouchCallback(TouchCallback callback) = 0;

    /**
     * @brief 设置按键回调
     */
    virtual void setKeyCallback(KeyCallback callback) = 0;

    /**
     * @brief 设置光标状态回调
     */
    virtual void setCursorGrabCallback(CursorGrabCallback callback) = 0;

    // === 脚本支持 ===

    /**
     * @brief 设置帧获取回调 (用于脚本图像识别)
     */
    using FrameGrabCallback = std::function<void*(void)>; // 返回 QImage*
    virtual void setFrameGrabCallback(FrameGrabCallback callback) = 0;

    /**
     * @brief 脚本提示回调
     * @param msg 消息内容
     * @param durationMs 显示时间(毫秒)
     * @param keyId 按键 ID (同 keyId 的消息会更新)
     */
    using ScriptTipCallback = std::function<void(const QString& msg, int durationMs, int keyId)>;
    virtual void setScriptTipCallback(ScriptTipCallback callback) = 0;

    /**
     * @brief 键位覆盖层更新回调
     */
    using KeyMapOverlayCallback = std::function<void(void)>;
    virtual void setKeyMapOverlayCallback(KeyMapOverlayCallback callback) = 0;

    /**
     * @brief 执行自动启动脚本
     */
    virtual void runAutoStartScripts() = 0;

    /**
     * @brief 重置脚本状态
     */
    virtual void resetScriptState() = 0;

    // === 查询 ===

    /**
     * @brief 是否处于游戏模式
     */
    virtual bool isGameMode() const = 0;

    /**
     * @brief 获取处理器名称 (用于调试)
     */
    virtual const char* name() const = 0;
};

} // namespace core
} // namespace qsc

#endif // CORE_IINPUTPROCESSOR_H
