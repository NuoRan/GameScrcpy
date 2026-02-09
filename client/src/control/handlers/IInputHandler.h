#ifndef IINPUTHANDLER_H
#define IINPUTHANDLER_H

#include <QObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSize>

class Controller;
class SessionContext;

/**
 * @brief 输入处理器接口 / Input Handler Interface
 *
 * 设计原则 / Design principles：
 * - 单一职责：每个 Handler 只处理一种类型的输入逻辑 / Single responsibility: each handler handles one input type
 * - 责任链模式：事件从上到下传递，Handler 返回是否消费 / Chain of responsibility: events passed down, return consumed flag
 * - 无状态依赖：Handler 通过 SessionContext 访问共享状态 / Stateless dependency: shared state via SessionContext
 */
class IInputHandler : public QObject
{
    Q_OBJECT
public:
    explicit IInputHandler(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IInputHandler() = default;

    /**
     * @brief 初始化 Handler
     * @param controller 控制器引用
     * @param context 会话上下文
     */
    virtual void init(Controller* controller, SessionContext* context) {
        m_controller = controller;
        m_sessionContext = context;
    }

    /**
     * @brief 处理键盘事件
     * @param event 键盘事件
     * @param frameSize 视频帧大小
     * @param showSize 显示窗口大小
     * @return true 表示事件已被消费，不再传递；false 继续传递
     */
    virtual bool handleKeyEvent(const QKeyEvent* event,
                                const QSize& frameSize,
                                const QSize& showSize) {
        Q_UNUSED(event);
        Q_UNUSED(frameSize);
        Q_UNUSED(showSize);
        return false;  // 默认不消费
    }

    /**
     * @brief 处理鼠标事件
     * @param event 鼠标事件
     * @param frameSize 视频帧大小
     * @param showSize 显示窗口大小
     * @return true 表示事件已被消费，不再传递；false 继续传递
     */
    virtual bool handleMouseEvent(const QMouseEvent* event,
                                  const QSize& frameSize,
                                  const QSize& showSize) {
        Q_UNUSED(event);
        Q_UNUSED(frameSize);
        Q_UNUSED(showSize);
        return false;
    }

    /**
     * @brief 处理滚轮事件
     * @param event 滚轮事件
     * @param frameSize 视频帧大小
     * @param showSize 显示窗口大小
     * @return true 表示事件已被消费，不再传递；false 继续传递
     */
    virtual bool handleWheelEvent(const QWheelEvent* event,
                                  const QSize& frameSize,
                                  const QSize& showSize) {
        Q_UNUSED(event);
        Q_UNUSED(frameSize);
        Q_UNUSED(showSize);
        return false;
    }

    /**
     * @brief 窗口失去焦点时重置状态
     */
    virtual void onFocusLost() {}

    /**
     * @brief 重置 Handler 状态
     */
    virtual void reset() {}

    /**
     * @brief 获取 Handler 名称（用于调试）
     */
    virtual QString name() const = 0;

    /**
     * @brief 获取 Handler 优先级（数值越小优先级越高）
     * 默认 100，可重写调整
     */
    virtual int priority() const { return 100; }

protected:
    Controller* m_controller = nullptr;
    SessionContext* m_sessionContext = nullptr;
};

#endif // IINPUTHANDLER_H
