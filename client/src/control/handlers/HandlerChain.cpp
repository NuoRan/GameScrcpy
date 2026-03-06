#include "HandlerChain.h"
#include <algorithm>
#define LOG_TAG "HandlerChain"
#include "Logger.h"

HandlerChain::HandlerChain()
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

    m_handlers.push_back(handler);
    m_sorted = false;

    // 如果已初始化，立即初始化新 Handler
    if (m_controller && m_sessionContext) {
        handler->init(m_controller, m_sessionContext);
    }

    LOGD() << "[HandlerChain] Added handler:" << handler->name().c_str()
             << "priority:" << handler->priority();
}

void HandlerChain::removeHandler(IInputHandler* handler)
{
    if (!handler) return;

    auto it = std::find(m_handlers.begin(), m_handlers.end(), handler);
    if (it != m_handlers.end()) {
        m_handlers.erase(it);
    }

    LOGD() << "[HandlerChain] Removed handler:" << handler->name().c_str();
}

void HandlerChain::clear()
{
    // HandlerChain 不拥有 handler 的所有权（由 SessionContext 管理生命周期）
    // 仅清空列表，不 delete handler，避免双重释放
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

bool HandlerChain::dispatchKeyEvent(const InputEvent& event,
                                    const Size& frameSize,
                                    const Size& showSize)
{
    sortHandlers();

    for (auto handler : m_handlers) {
        if (handler->handleKeyEvent(event, frameSize, showSize)) {
            return true;  // 事件被消费
        }
    }
    return false;
}

bool HandlerChain::dispatchMouseEvent(const InputEvent& event,
                                      const Size& frameSize,
                                      const Size& showSize)
{
    sortHandlers();

    for (auto handler : m_handlers) {
        if (handler->handleMouseEvent(event, frameSize, showSize)) {
            return true;
        }
    }
    return false;
}

bool HandlerChain::dispatchWheelEvent(const InputEvent& event,
                                      const Size& frameSize,
                                      const Size& showSize)
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
