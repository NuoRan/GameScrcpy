/**
 * @file MotionTokens.h
 * @brief Fluent Focus 动效令牌 — 统一的动画时长、缓动曲线、便捷动画工厂
 *
 * 所有动画必须引用此处常量，确保全局一致。
 * 当"减少动效"开关打开时，Motion::duration() 返回 0。
 */

#ifndef MOTIONTOKENS_H
#define MOTIONTOKENS_H

#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <QWidget>

namespace Fluent {
namespace Motion {

// ─── 原始时长 (ms) ──────────────────────────────────────
inline constexpr int Fast   = 100;   // 微交互 (hover)
inline constexpr int Normal = 200;   // 状态切换 (toggle/tab)
inline constexpr int Slow   = 300;   // 面板展开/收起
inline constexpr int Enter  = 250;   // 元素进场
inline constexpr int Exit   = 150;   // 元素退场
inline constexpr int Spring = 400;   // 弹性效果

// ─── 全局减少动效开关 ──────────────────────────────────
inline bool& reduceMotion() {
    static bool s_reduce = false;
    return s_reduce;
}

// 获取实际时长 (若减少动效则返回0)
inline int duration(int raw) {
    return reduceMotion() ? 0 : raw;
}

// ─── 缓动曲线 ──────────────────────────────────────────
inline QEasingCurve defaultCurve() { return QEasingCurve(QEasingCurve::OutCubic); }
inline QEasingCurve enterCurve()   { return QEasingCurve(QEasingCurve::OutQuart); }
inline QEasingCurve exitCurve()    { return QEasingCurve(QEasingCurve::InCubic); }
inline QEasingCurve springCurve()  { return QEasingCurve(QEasingCurve::OutBack); }

// ─── 便捷动画工厂 ──────────────────────────────────────

/**
 * 淡入动画
 */
inline QPropertyAnimation* fadeIn(QWidget* w, int dur = Enter) {
    auto* effect = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(effect);
    auto* anim = new QPropertyAnimation(effect, "opacity", w);
    anim->setDuration(duration(dur));
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(enterCurve());
    return anim;
}

/**
 * 淡出动画
 */
inline QPropertyAnimation* fadeOut(QWidget* w, int dur = Exit) {
    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(effect);
    }
    auto* anim = new QPropertyAnimation(effect, "opacity", w);
    anim->setDuration(duration(dur));
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(exitCurve());
    return anim;
}

/**
 * 高度展开/收起动画
 */
inline QPropertyAnimation* expandHeight(QWidget* w, int from, int to, int dur = Slow) {
    auto* anim = new QPropertyAnimation(w, "maximumHeight", w);
    anim->setDuration(duration(dur));
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->setEasingCurve(defaultCurve());
    return anim;
}

/**
 * 宽度展开/收起动画 (用于导航栏)
 */
inline QPropertyAnimation* expandWidth(QWidget* w, int from, int to, int dur = Slow) {
    auto* anim = new QPropertyAnimation(w, "maximumWidth", w);
    anim->setDuration(duration(dur));
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->setEasingCurve(defaultCurve());
    return anim;
}

} // namespace Motion
} // namespace Fluent

#endif // MOTIONTOKENS_H
