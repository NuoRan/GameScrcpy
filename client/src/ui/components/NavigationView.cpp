/**
 * @file NavigationView.cpp
 * @brief NavigationView 实现 — Fluent 左侧导航栏
 */

#include "NavigationView.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include "MotionTokens.h"

#include <QPainter>
#include <QFrame>
#include <QIcon>
#include <QSvgRenderer>
#include <QPixmap>

namespace Fluent {

// 加载 SVG 并染色为指定颜色
static QIcon tintedSvgIcon(const QString& path, const QColor& color, int size = 20)
{
    QSvgRenderer renderer(path);
    QPixmap px(size, size);
    px.fill(Qt::transparent);
    QPainter p(&px);
    renderer.render(&p);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(px.rect(), color);
    p.end();
    return QIcon(px);
}

NavigationView::NavigationView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("NavigationView"));
    setFixedWidth(Nav::ExpandedWidth);
    setMinimumWidth(Nav::CollapsedWidth);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(4);

    // 汉堡菜单按钮
    m_hamburgerBtn = new QPushButton(this);
    m_hamburgerBtn->setObjectName(QStringLiteral("navHamburger"));
    m_hamburgerBtn->setIcon(tintedSvgIcon(QStringLiteral(":/icons/menu.svg"), QColor(ThemeManager::instance().textSecondary())));
    m_hamburgerBtn->setIconSize(QSize(20, 20));
    m_hamburgerBtn->setFixedHeight(Nav::ItemHeight);
    m_hamburgerBtn->setCursor(Qt::PointingHandCursor);
    mainLayout->addWidget(m_hamburgerBtn);

    connect(m_hamburgerBtn, &QPushButton::clicked, this, &NavigationView::toggleExpanded);

    // 顶部导航区
    m_topLayout = new QVBoxLayout();
    m_topLayout->setContentsMargins(0, 4, 0, 0);
    m_topLayout->setSpacing(2);
    mainLayout->addLayout(m_topLayout);

    // 弹性空间
    mainLayout->addStretch(1);

    // 底部导航区
    m_bottomLayout = new QVBoxLayout();
    m_bottomLayout->setContentsMargins(0, 0, 0, 4);
    m_bottomLayout->setSpacing(2);
    mainLayout->addLayout(m_bottomLayout);

    applyStyle();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyStyle();
        updateButtonStyles();
    });
}

void NavigationView::addItem(const NavItem& item)
{
    m_items.append(item);

    auto* btn = createNavButton(item);
    m_buttons[item.id] = btn;

    if (item.isBottom) {
        m_bottomLayout->addWidget(btn);
    } else {
        m_topLayout->addWidget(btn);
    }

    // 第一个非底部项默认为活跃
    if (m_currentId.isEmpty() && !item.isBottom) {
        setCurrentItem(item.id);
    }
}

void NavigationView::addSeparator()
{
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    auto& tm = ThemeManager::instance();
    sep->setStyleSheet(QStringLiteral("background-color: %1; border: none;").arg(tm.borderSoft()));
    m_topLayout->addWidget(sep);
}

QPushButton* NavigationView::createNavButton(const NavItem& item)
{
    auto* btn = new QPushButton(this);
    btn->setObjectName(QStringLiteral("navItem"));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(Nav::ItemHeight);

    // 使用 SVG 图标
    auto& tm = ThemeManager::instance();
    btn->setIconSize(QSize(20, 20));
    btn->setIcon(tintedSvgIcon(item.icon, QColor(tm.textSecondary())));

    if (m_expanded) {
        btn->setText(QStringLiteral("  ") + item.text);
    } else {
        btn->setText(QString());
    }

    connect(btn, &QPushButton::clicked, this, [this, id = item.id]() {
        setCurrentItem(id);
        emit itemClicked(id);
    });

    return btn;
}

void NavigationView::setCurrentItem(const QString& id)
{
    if (m_currentId == id) return;
    m_currentId = id;
    updateButtonStyles();
}

void NavigationView::updateButtonStyles()
{
    auto& tm = ThemeManager::instance();
    const QString align = m_expanded ? "left" : "center";

    const QString normalStyle = QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 8px;"
        "  padding: 0 12px; text-align: %3; color: %1; font-size: 14px; font-weight: 500; }"
        "QPushButton:hover { background-color: %2; color: %4; }")
        .arg(tm.textSecondary(), tm.navHover(), align, tm.textPrimary());

    const QString activeStyle = QStringLiteral(
        "QPushButton { background-color: %1; border: none; border-radius: 8px;"
        "  padding: 0 12px; text-align: %3; color: %2; font-size: 14px; font-weight: 600; }"
        "QPushButton:hover { background-color: %1; }")
        .arg(tm.navActive(), tm.textPrimary(), align);

    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
        bool isActive = (it.key() == m_currentId);
        it.value()->setStyleSheet(isActive ? activeStyle : normalStyle);

        // 更新图标颜色
        for (const auto& item : m_items) {
            if (item.id == it.key()) {
                QColor iconColor = isActive ? QColor(tm.textPrimary()) : QColor(tm.textSecondary());
                it.value()->setIcon(tintedSvgIcon(item.icon, iconColor));
                break;
            }
        }
    }
}

void NavigationView::setExpanded(bool expanded)
{
    if (m_expanded == expanded) return;
    m_expanded = expanded;

    int fromW = expanded ? Nav::CollapsedWidth : Nav::ExpandedWidth;
    int toW   = expanded ? Nav::ExpandedWidth : Nav::CollapsedWidth;

    if (m_expandAnim) {
        m_expandAnim->stop();
        delete m_expandAnim;
        m_expandAnim = nullptr;
    }

    m_expandAnim = new QPropertyAnimation(this, "navWidth", this);
    m_expandAnim->setDuration(Motion::duration(Motion::Slow));
    m_expandAnim->setStartValue(fromW);
    m_expandAnim->setEndValue(toW);
    m_expandAnim->setEasingCurve(Motion::defaultCurve());

    connect(m_expandAnim, &QPropertyAnimation::finished, this, [this]() {
        m_expandAnim->deleteLater();
        m_expandAnim = nullptr;
    });
    m_expandAnim->start();

    // 更新按钮文字 (图标保持不变，只更改文字)
    for (const auto& item : m_items) {
        auto* btn = m_buttons.value(item.id);
        if (!btn) continue;
        if (expanded) {
            btn->setText(QStringLiteral("  ") + item.text);
        } else {
            btn->setText(QString());
        }
    }

    // 更新样式 (text-align 在展开/折叠时不同)
    updateButtonStyles();

    emit expandedChanged(expanded);
}

void NavigationView::toggleExpanded()
{
    setExpanded(!m_expanded);
}

int NavigationView::navWidth() const { return width(); }

void NavigationView::setNavWidth(int w)
{
    setFixedWidth(w);
}

void NavigationView::applyStyle()
{
    auto& tm = ThemeManager::instance();
    setStyleSheet(QStringLiteral(
        "#NavigationView { background-color: %1; border-right: 1px solid %2; }")
        .arg(tm.navBackground(), tm.borderSoft()));

    m_hamburgerBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 8px;"
        "  font-size: 18px; color: %1; padding: 0; }"
        "QPushButton:hover { background-color: %2; }")
        .arg(tm.textSecondary(), tm.navHover()));
}

void NavigationView::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    // 绘制活跃项指示条
    if (m_currentId.isEmpty()) return;
    auto* btn = m_buttons.value(m_currentId);
    if (!btn) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto& tm = ThemeManager::instance();
    p.setBrush(QColor(tm.accentPrimary()));
    p.setPen(Qt::NoPen);

    // 指示条位置 (按钮左侧)
    QPoint btnPos = btn->mapTo(this, QPoint(0, 0));
    int indicatorY = btnPos.y() + (btn->height() - Nav::IndicatorHeight) / 2;
    p.drawRoundedRect(QRect(4, indicatorY, Nav::IndicatorWidth, Nav::IndicatorHeight), 2, 2);
}

} // namespace Fluent
