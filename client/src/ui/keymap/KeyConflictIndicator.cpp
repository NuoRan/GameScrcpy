/**
 * @file KeyConflictIndicator.cpp
 * @brief 键位冲突可视化指示器实现
 */
#include "KeyConflictIndicator.h"
#include "../theme/DesignTokens.h"
#include "../theme/MotionTokens.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

using namespace Fluent;

KeyConflictIndicator::KeyConflictIndicator(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_TranslucentBackground, true);

    // ─── 脉冲动画 (用于冲突态) ───────────────────
    m_pulseAnim = new QPropertyAnimation(this, "pulsePhase", this);
    m_pulseAnim->setDuration(1200);
    m_pulseAnim->setStartValue(0.0);
    m_pulseAnim->setEndValue(1.0);
    m_pulseAnim->setLoopCount(-1); // 无限循环
    m_pulseAnim->setEasingCurve(QEasingCurve::SineCurve);

    // ─── 选中缩放动画 ──────────────────────────────
    m_scaleAnim = new QPropertyAnimation(this, "selectScale", this);
    m_scaleAnim->setDuration(Motion::Normal);
    m_scaleAnim->setEasingCurve(Motion::springCurve());
}

void KeyConflictIndicator::setState(State state)
{
    if (m_state == state) return;

    State old = m_state;
    m_state = state;

    // 停止旧状态的动画
    if (old == Conflicted) stopPulse();
    if (old == Selected) animateSelect(false);

    // 启动新状态的动画
    switch (state) {
        case Normal:
            // Normal: no special overlay
            break;
        case Selected:
            animateSelect(true);
            break;
        case Conflicted:
            startPulse();
            if (!m_conflictMsg.isEmpty()) {
                emit conflictDetected(m_conflictMsg);
            }
            break;
        case Dragging:
            // 拖拽态由外部设置 opacity=0.7
            break;
    }

    update();
}

void KeyConflictIndicator::setConflictKeys(const QSet<QString>& keys)
{
    m_conflictKeys = keys;
    if (keys.isEmpty() && m_state == Conflicted) {
        setState(Normal);
        emit conflictResolved();
    }
}

void KeyConflictIndicator::setConflictMessage(const QString& msg)
{
    m_conflictMsg = msg;
}

QRect KeyConflictIndicator::indicatorRect() const
{
    int margin = 4;
    return rect().adjusted(margin, margin, -margin, -margin);
}

void KeyConflictIndicator::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRectF r = indicatorRect();

    switch (m_state) {
        case Normal:
            // 无特殊绘制 — 正常状态无边框覆盖
            break;

        case Selected: {
            // Accent 蓝色边框 + 微放大 + 阴影
            p.save();
            QPointF center = r.center();
            p.translate(center);
            p.scale(m_selectScale, m_selectScale);
            p.translate(-center);

            // 阴影 (简易)
            QColor shadowColor(0, 0, 0, 40);
            p.setPen(Qt::NoPen);
            p.setBrush(shadowColor);
            p.drawRoundedRect(r.adjusted(-2, -1, 2, 3), Radius::Medium, Radius::Medium);

            // 边框
            QPen accentPen(QColor(Accent::Primary), 2.0);
            p.setPen(accentPen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, Radius::Medium, Radius::Medium);

            p.restore();
            break;
        }

        case Conflicted: {
            // 红色脉冲边框
            qreal alpha = 0.4 + 0.6 * qAbs(qSin(m_pulsePhase * M_PI));
            QColor conflictColor(Accent::Error);
            conflictColor.setAlphaF(alpha);

            QPen pen(conflictColor, 2.5);
            pen.setStyle(Qt::SolidLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, Radius::Medium, Radius::Medium);

            // 外发光
            QColor glow(Accent::Error);
            glow.setAlphaF(static_cast<float>(alpha * 0.3));
            QPen glowPen(glow, 5.0);
            p.setPen(glowPen);
            p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), Radius::Medium + 1, Radius::Medium + 1);
            break;
        }

        case Dragging: {
            // 虚线辅助框
            QPen dashPen(QColor(Accent::Primary), 1.5, Qt::DashLine);
            p.setPen(dashPen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, Radius::Medium, Radius::Medium);

            // 半透明覆盖
            QColor overlay(Dark::Base);
            overlay.setAlphaF(0.3f);
            p.setPen(Qt::NoPen);
            p.setBrush(overlay);
            p.drawRoundedRect(r, Radius::Medium, Radius::Medium);
            break;
        }
    }
}

void KeyConflictIndicator::startPulse()
{
    m_pulseAnim->start();
}

void KeyConflictIndicator::stopPulse()
{
    m_pulseAnim->stop();
    m_pulsePhase = 0.0;
    update();
}

void KeyConflictIndicator::animateSelect(bool selected)
{
    m_scaleAnim->stop();
    m_scaleAnim->setStartValue(m_selectScale);
    m_scaleAnim->setEndValue(selected ? 1.05 : 1.0);
    m_scaleAnim->start();
}

// Note: For Dragging state opacity, use QGraphicsOpacityEffect on the target widget externally.
