#ifndef FREELOOKHANDLER_H
#define FREELOOKHANDLER_H

#include "IInputHandler.h"
#include "keymap.h"  // 需要 KeyMap::KeyMapNode 完整定义
#include <QPointF>
#include <QSize>

class Controller;
class SessionContext;

/**
 * @brief 小眼睛（自由视角）处理器 / Free Look (Eye Button) Handler
 *
 * 处理小眼睛按键激活的自由视角功能 / Handles free look activated by eye button hotkey:
 * - 按住热键时，鼠标移动控制独立的触摸点 / While holding hotkey, mouse controls independent touch point
 * - 没有边缘回中和空闲回中 / No edge or idle re-centering
 * - 与主视角控制相互独立 / Independent from main viewport control
 */
class FreeLookHandler : public IInputHandler
{
    Q_OBJECT
public:
    explicit FreeLookHandler(QObject* parent = nullptr);
    ~FreeLookHandler();

    void init(Controller* controller, SessionContext* context) override;

    // IInputHandler interface
    bool handleKeyEvent(const QKeyEvent* event, const QSize& frameSize, const QSize& showSize) override;
    bool handleMouseEvent(const QMouseEvent* event, const QSize& frameSize, const QSize& showSize) override;
    bool handleWheelEvent(const QWheelEvent* event, const QSize& frameSize, const QSize& showSize) override;
    void onFocusLost() override;
    void reset() override;
    int priority() const override { return 70; }  // 比视角控制优先级更高
    QString name() const override { return QStringLiteral("FreeLookHandler"); }

    // 设置 KeyMap 引用
    void setKeyMap(KeyMap* keyMap) { m_keyMap = keyMap; }

    // 小眼睛状态查询（供 ViewportHandler 使用）
    bool isActive() const { return m_state.active; }
    bool hasTouchId() const { return m_state.fastTouchSeqId != 0; }

    // 处理鼠标移动（由 SessionContext::processMouseMove 调用）
    void processMouseDelta(const QPointF& delta, const QSize& frameSize, const QSize& showSize);

    // 处理键盘事件（直接接收 node，由 SessionContext::processFreeLook 调用）
    // 这个方法使用传入的 node，避免重新查找导致的修饰键丢失问题
    void processKeyEvent(const KeyMap::KeyMapNode& node, const QKeyEvent* event,
                         const QSize& frameSize, const QSize& showSize);

    // 设置组合键检测状态（用于修饰键作为热键的场景）
    void setModifierComboDetected(bool detected) { m_modifierComboDetected = detected; }
    bool isModifierComboDetected() const { return m_modifierComboDetected; }

private:
    void sendFastTouch(quint8 action, const QPointF& pos);

    KeyMap* m_keyMap = nullptr;

    QSize m_frameSize;
    QSize m_showSize;

    // 小眼睛状态（与原版 m_ctrlFreeLook 完全一致）
    struct {
        bool active = false;               // 是否正在使用小眼睛
        int triggerKey = Qt::Key_unknown;  // 触发按键
        QPointF startPos;                  // 起始位置（屏幕比例）
        QPointF speedRatio;                // 灵敏度
        QPointF currentPos;                // 当前触摸位置
        quint32 fastTouchSeqId = 0;        // FastMsg 序列 ID
        bool resetViewOnRelease = false;   // 松开时是否重置视角
    } m_state;

    // 组合键检测状态
    bool m_modifierComboDetected = false;
    int m_lastModifierKey = 0;
};

#endif // FREELOOKHANDLER_H
