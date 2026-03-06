/**
 * @file FluentInfoBar.cpp
 * @brief FluentInfoBar 实现 — 右上角自动堆叠通知条
 */

#include "FluentInfoBar.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include "MotionTokens.h"

#include <QHBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPainter>
#include <QPaintEvent>

namespace Fluent {

static constexpr int BarHeight = 44;
static constexpr int BarMarginRight = 16;
static constexpr int BarMarginTop = 12;
static constexpr int BarSpacing = 8;
static constexpr int BarMaxWidth = 360;

FluentInfoBar::FluentInfoBar(Level level, const QString& message, int durationMs, QWidget* parent)
    : QFrame(parent), m_level(level)
{
    setObjectName(QStringLiteral("FluentInfoBar"));
    setFixedHeight(BarHeight);
    setMaximumWidth(BarMaxWidth);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(Spacing::S);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(20, 20);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_iconLabel);

    m_messageLabel = new QLabel(message, this);
    m_messageLabel->setObjectName(QStringLiteral("infoBarMessage"));
    layout->addWidget(m_messageLabel, 1);

    // 图标 & 条纹颜色
    // 注意：使用 ASCII/Latin-1 字符代替 Unicode dingbats (ℹ✓⚠✕)
    // 以避免 Qt 6.10.1 DirectWrite 字体回退时 CreateFontFaceFromHDC 失败
    // 导致 font engine 为 NULL 进而崩溃的问题
    QString iconText;
    QString accentColor;
    switch (level) {
    case InfoLevel:
        iconText = QStringLiteral("i");
        accentColor = Accent::Info;
        break;
    case SuccessLevel:
        iconText = QStringLiteral("OK");
        accentColor = Accent::Success;
        break;
    case WarningLevel:
        iconText = QStringLiteral("!");
        accentColor = Accent::Warning;
        break;
    case ErrorLevel:
        iconText = QStringLiteral("X");
        accentColor = Accent::Error;
        break;
    }
    m_iconLabel->setText(iconText);

    // 样式
    auto& tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "#FluentInfoBar { background-color: %1; border: 1px solid %2;"
        "  border-left: 3px solid %3; border-radius: 8px; }"
        "#infoBarMessage { color: %4; font-size: 13px; background: transparent; }")
        .arg(tm.card(), tm.border(), accentColor, tm.textPrimary()));
    m_iconLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 14px; font-weight: 700; background: transparent;").arg(accentColor));

    // 自动消失
    m_dismissTimer.setSingleShot(true);
    m_dismissTimer.setInterval(durationMs);
    connect(&m_dismissTimer, &QTimer::timeout, this, &FluentInfoBar::animateOut);

    // 注册到列表
    activeBarList().append(this);
}

FluentInfoBar::~FluentInfoBar()
{
    activeBarList().removeOne(this);
    if (parentWidget()) {
        repositionAll(parentWidget());
    }
}

QList<FluentInfoBar*>& FluentInfoBar::activeBarList()
{
    static QList<FluentInfoBar*> s_list;
    return s_list;
}

void FluentInfoBar::positionInParent()
{
    if (!parentWidget()) return;
    repositionAll(parentWidget());
}

void FluentInfoBar::repositionAll(QWidget* parent)
{
    int y = BarMarginTop;
    for (auto* bar : activeBarList()) {
        if (bar->parentWidget() != parent) continue;
        int x = parent->width() - bar->width() - BarMarginRight;
        bar->move(x, y);
        y += bar->height() + BarSpacing;
    }
}

void FluentInfoBar::showEvent(QShowEvent* event)
{
    QFrame::showEvent(event);
    adjustSize();
    setFixedWidth(qMin(sizeHint().width() + 28, BarMaxWidth));
    positionInParent();
    animateIn();
    m_dismissTimer.start();
}

void FluentInfoBar::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);
}

void FluentInfoBar::animateIn()
{
    auto* effect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(effect);
    auto* anim = new QPropertyAnimation(effect, "opacity", this);
    anim->setDuration(Motion::duration(Motion::Enter));
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(Motion::enterCurve());
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void FluentInfoBar::animateOut()
{
    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(effect);
    }
    auto* anim = new QPropertyAnimation(effect, "opacity", this);
    anim->setDuration(Motion::duration(Motion::Exit));
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(Motion::exitCurve());
    connect(anim, &QAbstractAnimation::finished, this, &QObject::deleteLater);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ─── 快捷方法 ──────────────────────────────────────────────

FluentInfoBar* FluentInfoBar::info(QWidget* parent, const QString& message, int durationMs)
{
    auto* bar = new FluentInfoBar(InfoLevel, message, durationMs, parent);
    bar->show();
    return bar;
}

FluentInfoBar* FluentInfoBar::success(QWidget* parent, const QString& message, int durationMs)
{
    auto* bar = new FluentInfoBar(SuccessLevel, message, durationMs, parent);
    bar->show();
    return bar;
}

FluentInfoBar* FluentInfoBar::warning(QWidget* parent, const QString& message, int durationMs)
{
    auto* bar = new FluentInfoBar(WarningLevel, message, durationMs, parent);
    bar->show();
    return bar;
}

FluentInfoBar* FluentInfoBar::error(QWidget* parent, const QString& message, int durationMs)
{
    auto* bar = new FluentInfoBar(ErrorLevel, message, durationMs, parent);
    bar->show();
    return bar;
}

} // namespace Fluent
