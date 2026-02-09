#ifndef HANDLERCHAIN_H
#define HANDLERCHAIN_H

#include <QObject>
#include <QList>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSize>
#include <algorithm>

#include "IInputHandler.h"

class Controller;
class SessionContext;

/**
 * @brief 输入处理器责任链 / Input Handler Chain of Responsibility
 *
 * 管理多个 IInputHandler，按优先级顺序分发事件。
 * Manages multiple IInputHandlers, dispatching events by priority order.
 * 事件会依次传递给所有 Handler，直到被消费。
 * Events are passed to each handler in turn until consumed.
 */
class HandlerChain : public QObject
{
    Q_OBJECT
public:
    explicit HandlerChain(QObject* parent = nullptr);
    ~HandlerChain();

    /**
     * @brief 初始化所有 Handler
     * @param controller 控制器引用
     * @param context 会话上下文
     */
    void init(Controller* controller, SessionContext* context);

    /**
     * @brief 添加处理器（自动按优先级排序）
     * @param handler 处理器实例（所有权转移给 HandlerChain）
     */
    void addHandler(IInputHandler* handler);

    /**
     * @brief 移除处理器
     * @param handler 处理器实例
     */
    void removeHandler(IInputHandler* handler);

    /**
     * @brief 清空所有处理器
     */
    void clear();

    /**
     * @brief 分发键盘事件
     * @return true 表示事件被某个 Handler 消费
     */
    bool dispatchKeyEvent(const QKeyEvent* event,
                          const QSize& frameSize,
                          const QSize& showSize);

    /**
     * @brief 分发鼠标事件
     * @return true 表示事件被某个 Handler 消费
     */
    bool dispatchMouseEvent(const QMouseEvent* event,
                            const QSize& frameSize,
                            const QSize& showSize);

    /**
     * @brief 分发滚轮事件
     * @return true 表示事件被某个 Handler 消费
     */
    bool dispatchWheelEvent(const QWheelEvent* event,
                            const QSize& frameSize,
                            const QSize& showSize);

    /**
     * @brief 通知所有 Handler 窗口失去焦点
     */
    void onFocusLost();

    /**
     * @brief 重置所有 Handler 状态
     */
    void reset();

    /**
     * @brief 获取 Handler 数量
     */
    int count() const { return m_handlers.size(); }

private:
    void sortHandlers();

    QList<IInputHandler*> m_handlers;
    Controller* m_controller = nullptr;
    SessionContext* m_sessionContext = nullptr;
    bool m_sorted = true;
};

#endif // HANDLERCHAIN_H
