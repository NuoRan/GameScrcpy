/**
 * @file FluentDialog.cpp
 * @brief FluentDialog 实现 — 遮罩 + 卡片式弹窗 + 动画
 */

#include "FluentDialog.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include "MotionTokens.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>

namespace Fluent {

FluentDialog::FluentDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setupUI();
    applyStyle();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyStyle();
    });
}

void FluentDialog::setupUI()
{
    auto* overlay = new QVBoxLayout(this);
    overlay->setAlignment(Qt::AlignCenter);
    overlay->setContentsMargins(40, 40, 40, 40);

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("dialogCard"));
    m_card->setMinimumWidth(360);
    m_card->setMaximumWidth(480);

    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(28, 28, 28, 24);
    cardLayout->setSpacing(Spacing::L);

    // 标题
    m_titleLabel = new QLabel(m_card);
    m_titleLabel->setObjectName(QStringLiteral("dialogTitle"));
    m_titleLabel->setWordWrap(true);
    cardLayout->addWidget(m_titleLabel);

    // 消息
    m_messageLabel = new QLabel(m_card);
    m_messageLabel->setObjectName(QStringLiteral("dialogMessage"));
    m_messageLabel->setWordWrap(true);
    cardLayout->addWidget(m_messageLabel);

    // 输入框 (默认隐藏)
    m_inputEdit = new QLineEdit(m_card);
    m_inputEdit->setVisible(false);
    cardLayout->addWidget(m_inputEdit);

    // 弹性间距
    cardLayout->addSpacing(Spacing::S);

    // 按钮栏
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(Spacing::M);
    btnLayout->addStretch();

    m_secondaryBtn = new QPushButton(tr("取消"), m_card);
    m_secondaryBtn->setObjectName(QStringLiteral("dialogSecondary"));
    m_secondaryBtn->setMinimumWidth(80);
    m_secondaryBtn->setVisible(false);
    btnLayout->addWidget(m_secondaryBtn);

    m_primaryBtn = new QPushButton(tr("确定"), m_card);
    m_primaryBtn->setObjectName(QStringLiteral("dialogPrimary"));
    m_primaryBtn->setMinimumWidth(80);
    btnLayout->addWidget(m_primaryBtn);

    cardLayout->addLayout(btnLayout);

    overlay->addWidget(m_card);

    // 信号
    connect(m_primaryBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_secondaryBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void FluentDialog::applyStyle()
{
    auto& tm = ThemeManager::instance();

    m_card->setStyleSheet(QStringLiteral(
        "#dialogCard {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 16px;"
        "}")
        .arg(tm.card(), tm.border()));

    m_titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 18px; font-weight: 700; color: %1; background: transparent;")
        .arg(tm.textPrimary()));

    m_messageLabel->setStyleSheet(QStringLiteral(
        "font-size: 13px; color: %1; background: transparent; line-height: 1.5;")
        .arg(tm.textSecondary()));

    const int r = Radius::Medium;
    const QString btnBase = QStringLiteral(
        "QPushButton { border-radius: %1px; padding: 8px 20px; font-size: 13px; font-weight: 500; }").arg(r);

    if (m_dangerMode) {
        m_primaryBtn->setStyleSheet(btnBase + QStringLiteral(
            "QPushButton { background: %1; color: #fff; border: none; }"
            "QPushButton:hover { background: #dc2626; }").arg(Accent::Error));
    } else {
        m_primaryBtn->setStyleSheet(btnBase + QStringLiteral(
            "QPushButton { background: %1; color: #fff; border: none; }"
            "QPushButton:hover { background: %2; }").arg(tm.accentPrimary(), tm.accentHover()));
    }

    m_secondaryBtn->setStyleSheet(btnBase + QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; }"
        "QPushButton:hover { background: %3; }").arg(tm.surface(), tm.textPrimary(), tm.border()));

    m_inputEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: %1; border: 1px solid %2; border-radius: 8px;"
        "  padding: 8px 12px; color: %3; font-size: 13px; }"
        "QLineEdit:focus { border-color: %4; }")
        .arg(tm.inputBg(), tm.inputBorder(), tm.textPrimary(), tm.inputFocusBorder()));
}

void FluentDialog::setTitle(const QString& title) { m_titleLabel->setText(title); }
void FluentDialog::setMessage(const QString& message) { m_messageLabel->setText(message); }
void FluentDialog::setDialogType(DialogType type)
{
    m_type = type;
    m_inputEdit->setVisible(type == Input);
    m_secondaryBtn->setVisible(type == Confirm || type == Input);
}
void FluentDialog::setContentWidget(QWidget* w)
{
    if (m_contentWidget) {
        m_card->layout()->removeWidget(m_contentWidget);
        m_contentWidget->deleteLater();
    }
    m_contentWidget = w;
    if (w) {
        auto* layout = qobject_cast<QVBoxLayout*>(m_card->layout());
        // 在 inputEdit 之后插入
        layout->insertWidget(3, w);
    }
}
void FluentDialog::setPrimaryButtonText(const QString& text) { m_primaryBtn->setText(text); }
void FluentDialog::setSecondaryButtonText(const QString& text)
{
    m_secondaryBtn->setText(text);
    m_secondaryBtn->setVisible(!text.isEmpty());
}
void FluentDialog::setDangerMode(bool danger)
{
    m_dangerMode = danger;
    applyStyle();
}
QString FluentDialog::inputText() const { return m_inputEdit->text(); }
void FluentDialog::setInputPlaceholder(const QString& ph) { m_inputEdit->setPlaceholderText(ph); }
void FluentDialog::setInputText(const QString& text) { m_inputEdit->setText(text); }

// ─── 动画 ──────────────────────────────────────────────────

void FluentDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // 卡片进场动画 (淡入 + 微缩放)
    auto* effect = new QGraphicsOpacityEffect(m_card);
    m_card->setGraphicsEffect(effect);

    auto* fade = new QPropertyAnimation(effect, "opacity", this);
    fade->setDuration(Motion::duration(Motion::Enter));
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(Motion::enterCurve());
    fade->start(QAbstractAnimation::DeleteWhenStopped);

    // Focus to input if Input mode
    if (m_type == Input && m_inputEdit->isVisible()) {
        m_inputEdit->setFocus();
        m_inputEdit->selectAll();
    }
}

void FluentDialog::paintEvent(QPaintEvent*)
{
    // 不绘制半透明遮罩，保持完全透明背景
}

void FluentDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

// ─── 快捷静态方法 ──────────────────────────────────────────

void FluentDialog::info(QWidget* parent, const QString& title, const QString& message)
{
    FluentDialog dlg(parent);
    dlg.setTitle(title);
    dlg.setMessage(message);
    dlg.setDialogType(Info);
    dlg.exec();
}

void FluentDialog::error(QWidget* parent, const QString& title, const QString& message)
{
    FluentDialog dlg(parent);
    dlg.setTitle(title);
    dlg.setMessage(message);
    dlg.setDialogType(Error);
    dlg.exec();
}

void FluentDialog::warning(QWidget* parent, const QString& title, const QString& message)
{
    FluentDialog dlg(parent);
    dlg.setTitle(title);
    dlg.setMessage(message);
    dlg.setDialogType(Warning);
    dlg.exec();
}

bool FluentDialog::confirm(QWidget* parent, const QString& title, const QString& message)
{
    FluentDialog dlg(parent);
    dlg.setTitle(title);
    dlg.setMessage(message);
    dlg.setDialogType(Confirm);
    dlg.setPrimaryButtonText(tr("确认"));
    dlg.setSecondaryButtonText(tr("取消"));
    return dlg.exec() == QDialog::Accepted;
}

QString FluentDialog::input(QWidget* parent, const QString& title, const QString& placeholder, const QString& defaultText)
{
    FluentDialog dlg(parent);
    dlg.setTitle(title);
    dlg.setMessage({});
    dlg.setDialogType(Input);
    dlg.setInputPlaceholder(placeholder);
    dlg.setInputText(defaultText);
    dlg.setPrimaryButtonText(tr("确认"));
    dlg.setSecondaryButtonText(tr("取消"));
    if (dlg.exec() == QDialog::Accepted)
        return dlg.inputText();
    return {};
}

} // namespace Fluent
