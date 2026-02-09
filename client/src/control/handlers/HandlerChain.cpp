#include "HandlerChain.h"
#include <QDebug>

HandlerChain::HandlerChain(QObject* parent)
    : QObject(parent)
{
}

HandlerChain::~HandlerChain()
{
    clear();
}

void HandlerChain::init(Controller* controller, SessionContext* context)
{
    m_controller = controller;
    m_sessionContext = context;

    // 初始化所有已添加的 Handler
    for (auto handler : m_handlers) {
        handler->init(controller, context);
    }
}

void HandlerChain::addHandler(IInputHandler* handler)
{
    if (!handler) return;

    handler->setParent(this);  // 转移所有权
    m_handlers.append(handler);
    m_sorted = false;

    // 如果已初始化，立即初始化新 Handler
    if (m_controller && m_sessionContext) {
        handler->init(m_controller, m_sessionContext);
    }

    qDebug() << "[HandlerChain] Added handler:" << handler->name()
             << "priority:" << handler->priority();
}

void HandlerChain::removeHandler(IInputHandler* handler)
{
    if (!handler) return;

    m_handlers.removeOne(handler);
    handler->setParent(nullptr);

    qDebug() << "[HandlerChain] Removed handler:" << handler->name();
}

void HandlerChain::clear()
{
    for (auto handler : m_handlers) {
        handler->setParent(nullptr);
        delete handler;
    }
    m_handlers.clear();
}

void HandlerChain::sortHandlers()
{
    if (m_sorted) return;

    std::sort(m_handlers.begin(), m_handlers.end(),
              [](IInputHandler* a, IInputHandler* b) {
                  return a->priority() < b->priority();
              });
    m_sorted = true;
}

bool HandlerChain::dispatchKeyEvent(const QKeyEvent* event,
                                    const QSize& frameSize,
                                    const QSize& showSize)
{
    sortHandlers();

    for (auto handler : m_handlers) {
        if (handler->handleKeyEvent(event, frameSize, showSize)) {
            return true;  // 事件被消费
        }
    }
    return false;
}

bool HandlerChain::dispatchMouseEvent(const QMouseEvent* event,
                                      const QSize& frameSize,
                                      const QSize& showSize)
{
    sortHandlers();

    for (auto handler : m_handlers) {
        if (handler->handleMouseEvent(event, frameSize, showSize)) {
            return true;
        }
    }
    return false;
}

bool HandlerChain::dispatchWheelEvent(const QWheelEvent* event,
                                      const QSize& frameSize,
                                      const QSize& showSize)
{
    sortHandlers();

    for (auto handler : m_handlers) {
        if (handler->handleWheelEvent(event, frameSize, showSize)) {
            return true;
        }
    }
    return false;
}

void HandlerChain::onFocusLost()
{
    for (auto handler : m_handlers) {
        handler->onFocusLost();
    }
}

void HandlerChain::reset()
{
    for (auto handler : m_handlers) {
        handler->reset();
    }
}
