/**
 * @file SettingRow.h
 * @brief 设置项统一组件 — 左侧标题/描述 + 右侧控件
 */

#ifndef SETTINGROW_H
#define SETTINGROW_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>

namespace Fluent {

class SettingRow : public QWidget
{
    Q_OBJECT

public:
    explicit SettingRow(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setDescription(const QString& desc);
    void setIcon(const QString& iconText);  // emoji or fontawesome

    /// 设置右侧控件 (Toggle, ComboBox, Slider, etc.)
    void setWidget(QWidget* control);

    /// 获取右侧控件
    QWidget* widget() const { return m_control; }

private:
    void applyStyle();

    QHBoxLayout* m_layout = nullptr;
    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_descLabel = nullptr;
    QWidget* m_control = nullptr;
};

} // namespace Fluent

#endif // SETTINGROW_H
