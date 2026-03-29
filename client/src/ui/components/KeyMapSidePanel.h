/**
 * @file KeyMapSidePanel.h
 * @brief 侧边键位面板 — 替代 ToolForm 的键位编辑模式
 *
 * 从 VideoForm 右侧滑入，包含:
 * - 配置选择/CRUD
 * - 可拖拽键位组件
 * - 拟人参数滑块
 * - 显示设置
 */
#ifndef KEYMAPSIDEPANEL_H
#define KEYMAPSIDEPANEL_H

#include <QWidget>
#include <QPropertyAnimation>
#include "FluentComboBox.h"
#include <QPushButton>
#include <QLabel>

// 前向声明
namespace Fluent {
class FluentButton;
class FluentSlider;
class FluentToggle;
class FluentCard;
}

// 与 ToolForm 中的 DraggableLabel 兼容
class DraggableLabel;

class KeyMapSidePanel : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int panelWidth READ panelWidth WRITE setPanelWidth)
public:
    explicit KeyMapSidePanel(QWidget* parent = nullptr);

    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }

    int panelWidth() const { return m_currentWidth; }
    void setPanelWidth(int w);

    void refreshConfigList();
    QString currentConfig() const;
    void setCurrentConfig(const QString& filename);
    void setOverlayButtonState(bool checked);

    // 外部同步设置 (从 VideoSettingsPopup)
    void setOverlayChecked(bool checked);
    void setOverlayOpacity(int value);
    void setTipOpacity(int value);

signals:
    void configChanged(const QString& filename);
    void saveRequested();
    void overlayToggled(bool visible);
    void overlayOpacityChanged(int value);
    void scriptTipOpacityChanged(int value);
    void editModeChanged(bool active);
    void closeRequested();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void setupUI();

    bool m_expanded = false;
    int m_currentWidth = 0;
    int m_expandedWidth = 260;

    QPropertyAnimation* m_expandAnim = nullptr;

    // 配置管理
    Fluent::FluentComboBox* m_configCombo = nullptr;
    QPushButton* m_newBtn = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QPushButton* m_folderBtn = nullptr;
    Fluent::FluentButton* m_saveBtn = nullptr;

    // 显示键位
    Fluent::FluentToggle* m_overlayToggle = nullptr;

    // 拟人参数
    Fluent::FluentSlider* m_randomOffsetSlider = nullptr;
    Fluent::FluentSlider* m_steerSmoothSlider = nullptr;
    Fluent::FluentSlider* m_steerCurveSlider = nullptr;
    Fluent::FluentSlider* m_slideCurveSlider = nullptr;

    // 透明度
    Fluent::FluentSlider* m_overlayOpacitySlider = nullptr;
    Fluent::FluentSlider* m_tipOpacitySlider = nullptr;

    // 可拖拽组件
    DraggableLabel* m_clickLabel = nullptr;
    DraggableLabel* m_holdLabel = nullptr;
    DraggableLabel* m_scriptLabel = nullptr;
    DraggableLabel* m_steerLabel = nullptr;
    DraggableLabel* m_cameraLabel = nullptr;
    DraggableLabel* m_freeLookLabel = nullptr;
};

#endif
