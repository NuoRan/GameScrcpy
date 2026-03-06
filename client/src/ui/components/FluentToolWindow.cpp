/**
 * @file FluentToolWindow.cpp
 * @brief Fluent Focus 工具窗口基类实现
 */
#include "FluentToolWindow.h"
#include "DesignTokens.h"
#include "MotionTokens.h"

#include <QPainter>
#include <QShowEvent>
#include <QCloseEvent>

#if defined(Q_OS_WIN32)
#include <Windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

using namespace Fluent;

FluentToolWindow::FluentToolWindow(QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    applyDarkStyle();

    m_fadeAnim = new QPropertyAnimation(this, "windowOpacity", this);
    m_fadeAnim->setDuration(Motion::Fast);
    m_fadeAnim->setEasingCurve(Motion::defaultCurve());
}

void FluentToolWindow::setTitle(const QString& title)
{
    m_title = title;
    setWindowTitle(title);
}

void FluentToolWindow::setCustomTitleBarEnabled(bool enabled)
{
    m_customTitleBar = enabled;
    if (enabled) {
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    }
}

void FluentToolWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // Windows 深色标题栏
    setDarkTitleBar();

    // 淡入动画
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(0.0);
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->start();
}

void FluentToolWindow::closeEvent(QCloseEvent* event)
{
    // 淡出可以做但会阻塞，这里直接关闭
    QDialog::closeEvent(event);
}

void FluentToolWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(Dark::Base));

    // 绘制边框
    p.setPen(QPen(QColor(Dark::Border), 1));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);

    // 如果启用自定义标题栏，绘制标题区域
    if (m_customTitleBar && !m_title.isEmpty()) {
        QRect titleRect(16, 8, width() - 32, 28);
        p.setPen(QColor(Dark::TextPrimary));
        QFont f = font();
        f.setPointSize(11);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, m_title);

        // 标题下方分隔线
        p.setPen(QPen(QColor(Dark::Border), 1));
        p.drawLine(0, 40, width(), 40);
    }
}

void FluentToolWindow::applyDarkStyle()
{
    setStyleSheet(QString(
        "QDialog { background-color: %1; color: %2; }"
        "QLabel { color: %2; }"
        "QComboBox { background: %3; color: %2; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; }"
        "QComboBox:hover { border-color: %5; }"
        "QPushButton { background: %3; color: %2; border: 1px solid %4; border-radius: 6px; padding: 6px 16px; }"
        "QPushButton:hover { background: %4; border-color: %5; }"
        "QLineEdit { background: %3; color: %2; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; }"
        "QLineEdit:focus { border-color: %5; }"
        "QScrollBar:vertical { background: transparent; width: 4px; }"
        "QScrollBar::handle:vertical { background: #3f3f46; border-radius: 2px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ).arg(Dark::Base, Dark::TextPrimary, Dark::Surface, Dark::Border, Dark::NavIndicator));
}

void FluentToolWindow::setDarkTitleBar()
{
#if defined(Q_OS_WIN32)
    HWND hwnd = reinterpret_cast<HWND>(winId());
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
#endif
}
