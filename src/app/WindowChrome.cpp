#include "WindowChrome.h"

#include <QLoggingCategory>
#include <QWKQuick/quickwindowagent.h>

namespace gs::app {

namespace {

Q_LOGGING_CATEGORY(lcChrome, "gs.app.chrome")

}

WindowChrome::WindowChrome(QObject* parent)
    : QObject(parent)
{
}

WindowChrome::~WindowChrome() = default;

bool WindowChrome::attach()
{
    if (m_agent != nullptr) {
        return true;
    }
    if (m_window == nullptr) {
        qCWarning(lcChrome) << "attach() called without a window";
        return false;
    }

    m_agent = new QWK::QuickWindowAgent(this);
    if (!m_agent->setup(m_window)) {
        qCWarning(lcChrome) << "QWindowKit could not take over the window frame";
        delete m_agent;
        m_agent = nullptr;
        return false;
    }

    applyDarkMode();
    applyTitleBar();
    applySystemButtons();
    return true;
}

void WindowChrome::setHitTestVisible(QQuickItem* item, bool visible)
{
    if (m_agent != nullptr && item != nullptr) {
        m_agent->setHitTestVisible(item, visible);
    }
}

void WindowChrome::setWindow(QQuickWindow* window)
{
    if (m_window == window) {
        return;
    }
    if (m_agent != nullptr) {
        qCWarning(lcChrome) << "The window cannot change after attach()";
        return;
    }
    m_window = window;
    emit windowChanged();
}

void WindowChrome::setTitleBar(QQuickItem* item)
{
    if (m_titleBar == item) {
        return;
    }
    m_titleBar = item;
    applyTitleBar();
    emit titleBarChanged();
}

void WindowChrome::setMinimizeButton(QQuickItem* item)
{
    if (m_minimizeButton == item) {
        return;
    }
    m_minimizeButton = item;
    applySystemButtons();
    emit minimizeButtonChanged();
}

void WindowChrome::setMaximizeButton(QQuickItem* item)
{
    if (m_maximizeButton == item) {
        return;
    }
    m_maximizeButton = item;
    applySystemButtons();
    emit maximizeButtonChanged();
}

void WindowChrome::setCloseButton(QQuickItem* item)
{
    if (m_closeButton == item) {
        return;
    }
    m_closeButton = item;
    applySystemButtons();
    emit closeButtonChanged();
}

void WindowChrome::setDarkMode(bool dark)
{
    if (m_darkMode == dark) {
        return;
    }
    m_darkMode = dark;
    applyDarkMode();
    emit darkModeChanged();
}

void WindowChrome::applyTitleBar()
{
    if (m_agent != nullptr && m_titleBar != nullptr) {
        m_agent->setTitleBar(m_titleBar);
    }
}

void WindowChrome::applySystemButtons()
{
    if (m_agent == nullptr) {
        return;
    }
    if (m_minimizeButton != nullptr) {
        m_agent->setSystemButton(QWK::WindowAgentBase::Minimize, m_minimizeButton);
    }
    if (m_maximizeButton != nullptr) {
        m_agent->setSystemButton(QWK::WindowAgentBase::Maximize, m_maximizeButton);
    }
    if (m_closeButton != nullptr) {
        m_agent->setSystemButton(QWK::WindowAgentBase::Close, m_closeButton);
    }
}

void WindowChrome::applyDarkMode()
{
    if (m_agent != nullptr) {
        m_agent->setWindowAttribute(QStringLiteral("dark-mode"), m_darkMode);
    }
}

}
