/**
 * @file NavigationView.h
 * @brief Fluent Focus 左侧导航栏 — 可折叠，带活动指示器
 *
 * Windows 11 Settings 风格的 NavigationView：
 * - 顶部汉堡菜单按钮控制展开/折叠
 * - 导航项: 图标 + 文字 (折叠时仅显示图标)
 * - 活跃项左侧 3px 圆角指示条
 * - 底部固定导航项 (设置)
 */

#ifndef NAVIGATIONVIEW_H
#define NAVIGATIONVIEW_H

#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QList>
#include <QIcon>

namespace Fluent {

struct NavItem {
    QString id;
    QString icon;     // resource path e.g. ":/icons/home.svg"
    QString text;
    bool isBottom = false;  // 底部固定项
};

class NavigationView : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int navWidth READ navWidth WRITE setNavWidth)

public:
    explicit NavigationView(QWidget* parent = nullptr);

    /// 添加导航项
    void addItem(const NavItem& item);

    /// 添加分隔线
    void addSeparator();

    /// 设置当前活跃项
    void setCurrentItem(const QString& id);
    QString currentItem() const { return m_currentId; }

    /// 展开/折叠
    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }
    void toggleExpanded();

    int navWidth() const;
    void setNavWidth(int w);

signals:
    void itemClicked(const QString& id);
    void expandedChanged(bool expanded);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void rebuildItems();
    void applyStyle();
    QPushButton* createNavButton(const NavItem& item);
    void updateButtonStyles();

    QVBoxLayout* m_topLayout = nullptr;
    QVBoxLayout* m_bottomLayout = nullptr;
    QPushButton* m_hamburgerBtn = nullptr;

    QList<NavItem> m_items;
    QMap<QString, QPushButton*> m_buttons;
    QString m_currentId;
    bool m_expanded = true;

    QPropertyAnimation* m_expandAnim = nullptr;
};

} // namespace Fluent

#endif // NAVIGATIONVIEW_H
