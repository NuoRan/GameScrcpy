/**
 * @file KeyMapPropertyPanel.cpp
 * @brief 键位属性编辑面板实现
 */
#include "KeyMapPropertyPanel.h"
#include "../theme/DesignTokens.h"
#include "../theme/MotionTokens.h"
#include "../components/FluentButton.h"
#include "../components/FluentToggle.h"

#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QDoubleValidator>

using namespace Fluent;

KeyMapPropertyPanel::KeyMapPropertyPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();

    m_fadeAnim = new QPropertyAnimation(this, "panelOpacity", this);
    m_fadeAnim->setDuration(Motion::Fast);
    m_fadeAnim->setEasingCurve(Motion::defaultCurve());

    hide();
}

void KeyMapPropertyPanel::setupUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 12);
    root->setSpacing(8);

    // ─── 标题栏 ──────────────────────────────────
    auto* titleRow = new QHBoxLayout();
    m_titleLabel = new QLabel(tr("属性编辑"), this);
    m_titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: 600;")
                                    .arg(Dark::TextPrimary).arg(Font::Subtitle));
    titleRow->addWidget(m_titleLabel);
    titleRow->addStretch();
    m_closeBtn = new FluentButton(QStringLiteral("x"), this);
    m_closeBtn->setFixedSize(24, 24);
    connect(m_closeBtn, &FluentButton::clicked, this, &KeyMapPropertyPanel::hidePanel);
    titleRow->addWidget(m_closeBtn);
    root->addLayout(titleRow);

    // ─── 分隔线 ──────────────────────────────────
    auto* sep1 = new QWidget(this);
    sep1->setFixedHeight(1);
    sep1->setStyleSheet(QString("background: %1;").arg(Dark::Border));
    root->addWidget(sep1);

    // ─── 类型 ────────────────────────────────────
    auto* typeRow = new QHBoxLayout();
    m_typeLabel = new QLabel(tr("类型:"), this);
    m_typeLabel->setStyleSheet(QString("color: %1; font-size: %2px;").arg(Dark::TextSecondary).arg(Font::Body));
    typeRow->addWidget(m_typeLabel);

    m_typeBadge = new QLabel(this);
    m_typeBadge->setStyleSheet(QString(
        "background: %1; color: %2; font-size: %3px; padding: 2px 8px; border-radius: %4px;"
    ).arg(Accent::Subtle, Accent::Primary).arg(Font::Caption).arg(Radius::Small));
    typeRow->addWidget(m_typeBadge);
    typeRow->addStretch();
    root->addLayout(typeRow);

    // ─── 热键 ────────────────────────────────────
    auto* hotkeyRow = new QHBoxLayout();
    m_hotkeyLabel = new QLabel(tr("热键:"), this);
    m_hotkeyLabel->setStyleSheet(QString("color: %1; font-size: %2px;").arg(Dark::TextSecondary).arg(Font::Body));
    hotkeyRow->addWidget(m_hotkeyLabel);

    m_hotkeyEdit = new QLineEdit(this);
    m_hotkeyEdit->setFixedWidth(60);
    m_hotkeyEdit->setMaxLength(8);
    m_hotkeyEdit->setAlignment(Qt::AlignCenter);
    m_hotkeyEdit->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; "
        "padding: 2px 4px; font-size: %5px; font-weight: bold; }"
        "QLineEdit:focus { border-color: %6; }"
    ).arg(Dark::InputBg, Dark::TextPrimary, Dark::InputBorder)
     .arg(Radius::Small).arg(Font::Body).arg(Accent::Primary));
    hotkeyRow->addWidget(m_hotkeyEdit);

    m_hotkeyChangeBtn = new FluentButton(tr("修改"), this);
    m_hotkeyChangeBtn->setFixedHeight(24);
    hotkeyRow->addWidget(m_hotkeyChangeBtn);
    hotkeyRow->addStretch();

    connect(m_hotkeyChangeBtn, &FluentButton::clicked, this, [this]() {
        QString oldKey = m_info.hotkey;
        QString newKey = m_hotkeyEdit->text().trimmed();
        if (!newKey.isEmpty() && newKey != oldKey) {
            m_info.hotkey = newKey;
            emit hotkeyChanged(oldKey, newKey);
        }
    });
    root->addLayout(hotkeyRow);

    // ─── 位置 ────────────────────────────────────
    auto* posRow = new QHBoxLayout();
    m_posLabel = new QLabel(tr("位置:"), this);
    m_posLabel->setStyleSheet(QString("color: %1; font-size: %2px;").arg(Dark::TextSecondary).arg(Font::Body));
    posRow->addWidget(m_posLabel);

    auto* posXLabel = new QLabel("X", this);
    posXLabel->setStyleSheet(QString("color: %1; font-size: %2px;").arg(Dark::TextTertiary).arg(Font::Caption));
    posRow->addWidget(posXLabel);

    m_posXEdit = new QLineEdit(this);
    m_posXEdit->setFixedWidth(52);
    m_posXEdit->setValidator(new QDoubleValidator(0.0, 1.0, 4, m_posXEdit));
    m_posXEdit->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; "
        "padding: 2px; font-size: %5px; }"
    ).arg(Dark::InputBg, Dark::TextPrimary, Dark::InputBorder).arg(Radius::Small).arg(Font::Caption));
    posRow->addWidget(m_posXEdit);

    auto* posYLabel = new QLabel("Y", this);
    posYLabel->setStyleSheet(QString("color: %1; font-size: %2px;").arg(Dark::TextTertiary).arg(Font::Caption));
    posRow->addWidget(posYLabel);

    m_posYEdit = new QLineEdit(this);
    m_posYEdit->setFixedWidth(52);
    m_posYEdit->setValidator(new QDoubleValidator(0.0, 1.0, 4, m_posYEdit));
    m_posYEdit->setStyleSheet(m_posXEdit->styleSheet());
    posRow->addWidget(m_posYEdit);
    posRow->addStretch();

    connect(m_posXEdit, &QLineEdit::editingFinished, this, [this]() {
        double x = m_posXEdit->text().toDouble();
        double y = m_posYEdit->text().toDouble();
        m_info.posX = x;
        m_info.posY = y;
        emit positionChanged(x, y);
    });
    connect(m_posYEdit, &QLineEdit::editingFinished, this, [this]() {
        double x = m_posXEdit->text().toDouble();
        double y = m_posYEdit->text().toDouble();
        m_info.posX = x;
        m_info.posY = y;
        emit positionChanged(x, y);
    });
    root->addLayout(posRow);

    // ─── 分隔线 ──────────────────────────────────
    auto* sep2 = new QWidget(this);
    sep2->setFixedHeight(1);
    sep2->setStyleSheet(QString("background: %1;").arg(Dark::Border));
    root->addWidget(sep2);

    // ─── 脚本预览 (仅 Script 类型可见) ──────────
    m_scriptLabel = new QLabel(tr("脚本:"), this);
    m_scriptLabel->setStyleSheet(QString("color: %1; font-size: %2px;").arg(Dark::TextSecondary).arg(Font::Body));
    root->addWidget(m_scriptLabel);

    m_scriptPreview = new QTextEdit(this);
    m_scriptPreview->setReadOnly(true);
    m_scriptPreview->setFixedHeight(60);
    m_scriptPreview->setStyleSheet(QString(
        "QTextEdit { background: %1; color: %2; border: 1px solid %3; border-radius: %4px; "
        "font-family: 'Consolas', 'Courier New', monospace; font-size: %5px; padding: 4px; }"
    ).arg(Dark::InputBg, Dark::TextPrimary, Dark::InputBorder).arg(Radius::Small).arg(Font::Caption));
    root->addWidget(m_scriptPreview);

    auto* scriptBtnRow = new QHBoxLayout();
    m_editScriptBtn = new FluentButton(tr("编辑脚本"), this);
    m_editScriptBtn->setFixedHeight(28);
    connect(m_editScriptBtn, &FluentButton::clicked, this, &KeyMapPropertyPanel::scriptEditRequested);
    scriptBtnRow->addWidget(m_editScriptBtn);
    scriptBtnRow->addStretch();
    root->addLayout(scriptBtnRow);

    // ─── 自动启动 ────────────────────────────────
    auto* autoRow = new QHBoxLayout();
    m_autoStartLabel = new QLabel(tr("自动启动"), this);
    m_autoStartLabel->setStyleSheet(QString("color: %1; font-size: %2px;").arg(Dark::TextPrimary).arg(Font::Body));
    autoRow->addWidget(m_autoStartLabel);
    autoRow->addStretch();
    m_autoStartToggle = new FluentToggle(this);
    connect(m_autoStartToggle, &FluentToggle::toggled, this, [this](bool checked) {
        m_info.autoStart = checked;
        emit autoStartToggled(checked);
    });
    autoRow->addWidget(m_autoStartToggle);
    root->addLayout(autoRow);

    // ─── 分隔线 ──────────────────────────────────
    auto* sep3 = new QWidget(this);
    sep3->setFixedHeight(1);
    sep3->setStyleSheet(QString("background: %1;").arg(Dark::Border));
    root->addWidget(sep3);

    // ─── 删除按钮 ────────────────────────────────
    m_deleteBtn = new FluentButton(tr("删除键位"), FluentButton::Danger, this);
    m_deleteBtn->setFixedHeight(32);
    connect(m_deleteBtn, &FluentButton::clicked, this, &KeyMapPropertyPanel::deleteRequested);
    root->addWidget(m_deleteBtn);

    root->addStretch();

    setMinimumWidth(240);
}

void KeyMapPropertyPanel::showForItem(const KeyMapItemInfo& info)
{
    m_info = info;
    updateFromInfo();

    show();
    m_showing = true;
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(m_opacity);
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->start();
}

void KeyMapPropertyPanel::hidePanel()
{
    m_showing = false;
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(m_opacity);
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacity < 0.01) {
            hide();
            emit panelClosed();
        }
    }, Qt::UniqueConnection);
}

void KeyMapPropertyPanel::updateFromInfo()
{
    m_typeBadge->setText(typeDisplayName(m_info.type));
    m_hotkeyEdit->setText(m_info.hotkey);
    m_posXEdit->setText(QString::number(m_info.posX, 'f', 4));
    m_posYEdit->setText(QString::number(m_info.posY, 'f', 4));

    // 脚本区域仅在 Script 类型可见
    bool isScript = (m_info.type == Script);
    m_scriptLabel->setVisible(isScript);
    m_scriptPreview->setVisible(isScript);
    m_editScriptBtn->setVisible(isScript);
    if (isScript) {
        m_scriptPreview->setPlainText(m_info.scriptContent);
    }

    m_autoStartToggle->setChecked(m_info.autoStart);

    // 标题更新
    if (!m_info.displayName.isEmpty()) {
        m_titleLabel->setText(tr("属性 — %1").arg(m_info.displayName));
    } else {
        m_titleLabel->setText(tr("属性编辑"));
    }
}

QString KeyMapPropertyPanel::typeDisplayName(ItemType type) const
{
    switch (type) {
        case Click:      return tr("点击");
        case Hold:       return tr("长按");
        case SteerWheel: return tr("轮盘");
        case Script:     return tr("脚本");
        case CameraMove: return tr("视角");
        case FreeLook:   return tr("小眼睛");
        default:         return tr("未知");
    }
}

void KeyMapPropertyPanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(m_opacity);

    // 背景
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(Dark::Card));
    p.drawRoundedRect(rect(), Radius::Medium, Radius::Medium);

    // 左侧 accent 指示线
    p.setBrush(QColor(Accent::Primary));
    p.drawRoundedRect(QRect(0, 8, 3, height() - 16), 1, 1);
}
