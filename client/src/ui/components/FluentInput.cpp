/**
 * @file FluentInput.cpp
 * @brief Fluent Focus 输入框实现
 */
#include "FluentInput.h"
#include "../theme/DesignTokens.h"
#include "../theme/MotionTokens.h"

#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QEvent>
#include <QFocusEvent>

namespace Fluent {

FluentInput::FluentInput(QWidget* parent)
    : QWidget(parent)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(2);

    // ─── 输入行 ─────────────────────────────────
    auto* row = new QWidget(this);
    row->setFixedHeight(36);
    auto* hbox = new QHBoxLayout(row);
    hbox->setContentsMargins(8, 0, 4, 0);
    hbox->setSpacing(4);

    m_prefixLabel = new QLabel(this);
    m_prefixLabel->setFixedSize(20, 20);
    m_prefixLabel->setAlignment(Qt::AlignCenter);
    m_prefixLabel->setStyleSheet(QString("color: %1; font-size: 14px;").arg(Dark::TextSecondary));
    m_prefixLabel->hide();
    hbox->addWidget(m_prefixLabel);

    m_edit = new QLineEdit(this);
    m_edit->setFrame(false);
    m_edit->setStyleSheet(QString(
        "QLineEdit { background: transparent; color: %1; font-size: %2px; "
        "padding: 4px 0; border: none; }"
    ).arg(Dark::TextPrimary).arg(Font::Body));
    m_edit->installEventFilter(this);
    hbox->addWidget(m_edit, 1);

    m_suffixLabel = new QLabel(this);
    m_suffixLabel->setFixedSize(20, 20);
    m_suffixLabel->setAlignment(Qt::AlignCenter);
    m_suffixLabel->setStyleSheet(QString("color: %1; font-size: 14px;").arg(Dark::TextSecondary));
    m_suffixLabel->hide();
    hbox->addWidget(m_suffixLabel);

    m_clearBtn = new QToolButton(this);
    m_clearBtn->setText(QStringLiteral("x")); // close
    m_clearBtn->setFixedSize(20, 20);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setStyleSheet(QString(
        "QToolButton { background: transparent; color: %1; border: none; font-size: 12px; border-radius: 10px; }"
        "QToolButton:hover { background: %2; }"
    ).arg(Dark::TextSecondary, Dark::Surface));
    m_clearBtn->hide();
    hbox->addWidget(m_clearBtn);

    vbox->addWidget(row);

    // ─── 错误文字 ───────────────────────────────
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QString("color: %1; font-size: %2px; padding-left: 8px;")
                                    .arg(Accent::Error).arg(Font::Caption));
    m_errorLabel->hide();
    vbox->addWidget(m_errorLabel);

    // ─── 动画 ────────────────────────────────────
    m_focusAnim = new QPropertyAnimation(this, "focusProgress", this);
    m_focusAnim->setDuration(Motion::Fast);
    m_focusAnim->setEasingCurve(Motion::defaultCurve());

    // ─── 信号 ────────────────────────────────────
    connect(m_edit, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (m_clearEnabled && m_clearBtn) {
            m_clearBtn->setVisible(!t.isEmpty() && m_edit->hasFocus());
        }
        emit textChanged(t);
    });
    connect(m_edit, &QLineEdit::editingFinished, this, &FluentInput::editingFinished);
    connect(m_edit, &QLineEdit::returnPressed, this, &FluentInput::returnPressed);
    connect(m_clearBtn, &QToolButton::clicked, m_edit, &QLineEdit::clear);

    setMinimumHeight(38);
}

// ─── 公共接口 ─────────────────────────────────────────────
void FluentInput::setText(const QString& text)          { m_edit->setText(text); }
QString FluentInput::text() const                        { return m_edit->text(); }
void FluentInput::setPlaceholderText(const QString& t)  { m_edit->setPlaceholderText(t); }

void FluentInput::setPrefixIcon(const QString& icon)
{
    m_prefixLabel->setText(icon);
    m_prefixLabel->setVisible(!icon.isEmpty());
}

void FluentInput::setSuffixIcon(const QString& icon)
{
    m_suffixLabel->setText(icon);
    m_suffixLabel->setVisible(!icon.isEmpty());
}

void FluentInput::setClearButtonEnabled(bool enabled)
{
    m_clearEnabled = enabled;
    m_clearBtn->setVisible(enabled && !m_edit->text().isEmpty() && m_edit->hasFocus());
}

void FluentInput::setError(const QString& message)
{
    m_hasError = true;
    m_errorLabel->setText(message);
    m_errorLabel->show();
    update();
}

void FluentInput::clearError()
{
    m_hasError = false;
    m_errorLabel->hide();
    update();
}

void FluentInput::setReadOnly(bool ro) { m_edit->setReadOnly(ro); }

// ─── 绘制 ─────────────────────────────────────────────────
void FluentInput::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 输入框区域 (第一个子 widget 的几何)
    QRect inputRect(0, 0, width(), 36);

    // 背景
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(Dark::InputBg));
    p.drawRoundedRect(inputRect, Radius::Small, Radius::Small);

    // 边框
    QColor borderColor = m_hasError ? QColor(Accent::Error) :
                         (m_focusProgress > 0.01 ? QColor(Accent::Primary) : QColor(Dark::InputBorder));
    p.setPen(QPen(borderColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(inputRect.adjusted(0, 0, -1, -1), Radius::Small, Radius::Small);

    // 底部焦点指示线 — 从中心向两侧展开
    if (m_focusProgress > 0.01) {
        QColor lineColor = m_hasError ? QColor(Accent::Error) : QColor(Accent::Primary);
        p.setPen(QPen(lineColor, 2));
        int y = inputRect.bottom();
        int cx = inputRect.center().x();
        int halfW = static_cast<int>(cx * m_focusProgress);
        p.drawLine(cx - halfW, y, cx + halfW, y);
    }
}

bool FluentInput::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_edit) {
        if (event->type() == QEvent::FocusIn) {
            m_focusAnim->stop();
            m_focusAnim->setStartValue(m_focusProgress);
            m_focusAnim->setEndValue(1.0);
            m_focusAnim->start();
            if (m_clearEnabled && !m_edit->text().isEmpty())
                m_clearBtn->show();
        } else if (event->type() == QEvent::FocusOut) {
            m_focusAnim->stop();
            m_focusAnim->setStartValue(m_focusProgress);
            m_focusAnim->setEndValue(0.0);
            m_focusAnim->start();
            m_clearBtn->hide();
        }
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace Fluent
