/**
 * @file OnboardingOverlay.cpp
 * @brief 引导覆盖层实现 — 聚光灯 + 提示卡片 + 动画
 */

#include "OnboardingOverlay.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include "MotionTokens.h"

#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QApplication>

namespace Fluent {

OnboardingOverlay::OnboardingOverlay(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);

    // ── 卡片容器 ──
    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("onboardingCard"));
    m_card->setFixedWidth(340);

    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(24, 24, 24, 20);
    cardLayout->setSpacing(12);

    // 图标
    m_iconLabel = new QLabel(m_card);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_iconLabel);

    // 标题
    m_titleLabel = new QLabel(m_card);
    m_titleLabel->setObjectName(QStringLiteral("onboardingTitle"));
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_titleLabel);

    // 描述
    m_descLabel = new QLabel(m_card);
    m_descLabel->setObjectName(QStringLiteral("onboardingDesc"));
    m_descLabel->setWordWrap(true);
    m_descLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_descLabel);

    // 步骤指示器
    m_stepLabel = new QLabel(m_card);
    m_stepLabel->setObjectName(QStringLiteral("onboardingStep"));
    m_stepLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_stepLabel);

    cardLayout->addSpacing(4);

    // 按钮行
    auto* btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(10);

    m_skipBtn = new QPushButton(tr("跳过"), m_card);
    m_skipBtn->setObjectName(QStringLiteral("onboardingSkip"));
    m_skipBtn->setCursor(Qt::PointingHandCursor);
    btnLayout->addWidget(m_skipBtn);

    btnLayout->addStretch();

    m_prevBtn = new QPushButton(tr("上一步"), m_card);
    m_prevBtn->setObjectName(QStringLiteral("onboardingPrev"));
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    btnLayout->addWidget(m_prevBtn);

    m_nextBtn = new QPushButton(tr("下一步"), m_card);
    m_nextBtn->setObjectName(QStringLiteral("onboardingNext"));
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    btnLayout->addWidget(m_nextBtn);

    cardLayout->addLayout(btnLayout);

    // ── 信号 ──
    connect(m_skipBtn, &QPushButton::clicked, this, [this]() {
        hide();
        emit finished();
    });

    connect(m_prevBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentStep > 0) {
            showStep(m_currentStep - 1);
        }
    });

    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentStep < m_steps.size() - 1) {
            showStep(m_currentStep + 1);
        } else {
            hide();
            emit finished();
        }
    });

    applyStyle();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyStyle();
    });
}

void OnboardingOverlay::setSteps(const QVector<OnboardingStep>& steps)
{
    m_steps = steps;
    m_currentStep = 0;
}

void OnboardingOverlay::start()
{
    if (m_steps.isEmpty()) return;

    QWidget* p = parentWidget();
    if (p) {
        setGeometry(p->rect());
        raise();
    }
    show();
    showStep(0);

    // 淡入动画
    auto* effect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(effect);
    auto* fade = new QPropertyAnimation(effect, "opacity", this);
    fade->setDuration(Motion::duration(Motion::Enter));
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(Motion::enterCurve());
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void OnboardingOverlay::showStep(int index)
{
    if (index < 0 || index >= m_steps.size()) return;
    m_currentStep = index;

    const auto& step = m_steps[index];

    // 在显示步骤前执行回调 (如切换页面，让 target 变为 visible)
    if (step.beforeShow) {
        step.beforeShow();
        // 让事件循环处理完布局，确保 target 已更新 geometry
        QApplication::processEvents();
    }

    // 图标
    if (step.icon.isEmpty()) {
        m_iconLabel->hide();
    } else {
        m_iconLabel->setText(step.icon);
        m_iconLabel->show();
    }

    m_titleLabel->setText(step.title);
    m_descLabel->setText(step.description);
    m_stepLabel->setText(QStringLiteral("%1 / %2").arg(index + 1).arg(m_steps.size()));

    // 按钮状态
    m_prevBtn->setVisible(index > 0);
    m_nextBtn->setText(index < m_steps.size() - 1 ? tr("下一步") : tr("开始使用"));
    m_skipBtn->setVisible(index < m_steps.size() - 1);

    m_card->adjustSize();
    layoutCard();
    update();
}

void OnboardingOverlay::layoutCard()
{
    if (m_steps.isEmpty()) return;

    const auto& step = m_steps[m_currentStep];
    const QRect overlayRect = rect();

    if (!step.target || !step.target->isVisible()) {
        // 无目标 → 居中
        int x = (overlayRect.width() - m_card->width()) / 2;
        int y = (overlayRect.height() - m_card->height()) / 2;
        m_card->move(x, y);
        return;
    }

    // 计算目标控件在覆盖层坐标系中的位置
    QRect targetRect = spotlightRect();
    const int margin = 16;
    const int cardW = m_card->width();
    const int cardH = m_card->height();

    // 优先：下方 → 上方 → 右侧 → 左侧
    int x, y;

    // 下方
    if (targetRect.bottom() + margin + cardH <= overlayRect.height()) {
        x = targetRect.center().x() - cardW / 2;
        y = targetRect.bottom() + margin;
    }
    // 上方
    else if (targetRect.top() - margin - cardH >= 0) {
        x = targetRect.center().x() - cardW / 2;
        y = targetRect.top() - margin - cardH;
    }
    // 右侧
    else if (targetRect.right() + margin + cardW <= overlayRect.width()) {
        x = targetRect.right() + margin;
        y = targetRect.center().y() - cardH / 2;
    }
    // 左侧
    else {
        x = targetRect.left() - margin - cardW;
        y = targetRect.center().y() - cardH / 2;
    }

    // 边界约束
    x = qBound(margin, x, overlayRect.width() - cardW - margin);
    y = qBound(margin, y, overlayRect.height() - cardH - margin);

    m_card->move(x, y);
}

QRect OnboardingOverlay::spotlightRect() const
{
    if (m_currentStep < 0 || m_currentStep >= m_steps.size()) return {};
    const auto& step = m_steps[m_currentStep];
    if (!step.target || !step.target->isVisible()) return {};

    // 将目标控件的全局坐标映射到覆盖层坐标系
    QPoint topLeft = step.target->mapToGlobal(QPoint(0, 0));
    topLeft = mapFromGlobal(topLeft);
    QRect r(topLeft, step.target->size());

    // 外扩 padding
    const int pad = 8;
    return r.adjusted(-pad, -pad, pad, pad);
}

void OnboardingOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 1) 半透明遮罩
    QColor overlayColor(0, 0, 0, 140);

    QRect spot = spotlightRect();
    if (spot.isValid()) {
        // 使用 QPainterPath 裁剪出聚光灯区域
        QPainterPath fullPath;
        fullPath.addRect(rect());

        QPainterPath spotPath;
        spotPath.addRoundedRect(spot, Radius::Medium, Radius::Medium);

        QPainterPath dimPath = fullPath.subtracted(spotPath);
        p.fillPath(dimPath, overlayColor);

        // 聚光灯边框
        p.setPen(QPen(QColor(Accent::Primary), 2));
        p.drawRoundedRect(spot, Radius::Medium, Radius::Medium);
    } else {
        // 无聚光灯 → 全遮罩
        p.fillRect(rect(), overlayColor);
    }
}

void OnboardingOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutCard();
}

void OnboardingOverlay::mousePressEvent(QMouseEvent* event)
{
    // 点击聚光灯区域外 = 不做任何操作 (防止误触底层控件)
    event->accept();
}

void OnboardingOverlay::applyStyle()
{
    auto& tm = ThemeManager::instance();

    m_card->setStyleSheet(QStringLiteral(
        "#onboardingCard {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 16px;"
        "}")
        .arg(tm.card(), tm.border()));

    m_iconLabel->setStyleSheet(QStringLiteral(
        "font-size: 36px; background: transparent; padding: 4px 0;"));

    m_titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 17px; font-weight: 700; color: %1; background: transparent;")
        .arg(tm.textPrimary()));

    m_descLabel->setStyleSheet(QStringLiteral(
        "font-size: 13px; color: %1; background: transparent; line-height: 1.6;")
        .arg(tm.textSecondary()));

    m_stepLabel->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: %1; background: transparent;")
        .arg(tm.textTertiary()));

    const int r = Radius::Medium;

    // 跳过按钮 (文字按钮)
    m_skipBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: none;"
        "  font-size: 12px; padding: 6px 12px; }"
        "QPushButton:hover { color: %2; }")
        .arg(tm.textTertiary(), tm.textSecondary()));

    // 上一步按钮
    m_prevBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3;"
        "  border-radius: %4px; font-size: 12px; padding: 6px 16px; }"
        "QPushButton:hover { background: %3; }")
        .arg(tm.surface(), tm.textPrimary(), tm.border()).arg(r));

    // 下一步按钮 (主色)
    m_nextBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: #fff; border: none;"
        "  border-radius: %2px; font-size: 12px; font-weight: 600; padding: 6px 20px; }"
        "QPushButton:hover { background: %3; }")
        .arg(tm.accentPrimary()).arg(r).arg(tm.accentHover()));
}

} // namespace Fluent
