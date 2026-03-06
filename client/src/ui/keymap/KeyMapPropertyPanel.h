/**
 * @file KeyMapPropertyPanel.h
 * @brief 键位属性编辑面板 — 选中某键位后显示其属性，支持编辑
 *
 * 在 KeyMapSidePanel 底部区域弹出，显示:
 * - 类型标签 (点击/长按/轮盘/脚本/视角/小眼睛)
 * - 热键编辑
 * - 位置 X/Y
 * - 脚本内容预览 (仅脚本类型)
 * - 自动启动切换
 * - 删除按钮
 */
#ifndef KEYMAPPROPERTYPANEL_H
#define KEYMAPPROPERTYPANEL_H

#include <QWidget>
#include <QPropertyAnimation>

class QLabel;
class QLineEdit;
class QTextEdit;

namespace Fluent {
class FluentButton;
class FluentToggle;
class FluentSlider;
}

class KeyMapPropertyPanel : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal panelOpacity READ panelOpacity WRITE setPanelOpacity)
public:
    explicit KeyMapPropertyPanel(QWidget* parent = nullptr);

    // 键位类型定义 (与 KeyMapType 对应)
    enum ItemType {
        Click = 0,
        Hold  = 1,
        SteerWheel = 2,
        Script = 10,
        CameraMove = 20,
        FreeLook = 21
    };

    struct KeyMapItemInfo {
        ItemType type = Click;
        QString  hotkey;
        double   posX = 0.0;
        double   posY = 0.0;
        QString  scriptContent;
        bool     autoStart = false;
        QString  displayName;
    };

    void showForItem(const KeyMapItemInfo& info);
    void hidePanel();
    bool isShowing() const { return m_showing; }

    KeyMapItemInfo currentInfo() const { return m_info; }

signals:
    void hotkeyChanged(const QString& oldKey, const QString& newKey);
    void positionChanged(double x, double y);
    void scriptEditRequested();
    void autoStartToggled(bool enabled);
    void deleteRequested();
    void panelClosed();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void setupUI();
    void updateFromInfo();
    QString typeDisplayName(ItemType type) const;

    qreal panelOpacity() const { return m_opacity; }
    void  setPanelOpacity(qreal v) { m_opacity = v; update(); }

    bool  m_showing = false;
    qreal m_opacity = 0.0;
    KeyMapItemInfo m_info;

    QPropertyAnimation* m_fadeAnim = nullptr;

    // UI 元素
    QLabel*  m_titleLabel = nullptr;
    QLabel*  m_typeLabel = nullptr;
    QLabel*  m_typeBadge = nullptr;

    QLabel*    m_hotkeyLabel = nullptr;
    QLineEdit* m_hotkeyEdit = nullptr;
    Fluent::FluentButton* m_hotkeyChangeBtn = nullptr;

    QLabel*    m_posLabel = nullptr;
    QLineEdit* m_posXEdit = nullptr;
    QLineEdit* m_posYEdit = nullptr;

    QLabel*    m_scriptLabel = nullptr;
    QTextEdit* m_scriptPreview = nullptr;
    Fluent::FluentButton* m_editScriptBtn = nullptr;

    QLabel*  m_autoStartLabel = nullptr;
    Fluent::FluentToggle* m_autoStartToggle = nullptr;

    Fluent::FluentButton* m_deleteBtn = nullptr;
    Fluent::FluentButton* m_closeBtn = nullptr;
};

#endif // KEYMAPPROPERTYPANEL_H
