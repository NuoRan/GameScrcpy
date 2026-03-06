/**
 * @file FluentToolWindow.h
 * @brief Fluent Focus 工具窗口基类 — 用于 ImageCaptureDialog / SelectionEditorDialog
 *
 * 提供:
 * - 深色标题栏 (DWM dark mode on Windows)
 * - 统一边框/背景/字体
 * - 动画入场/出场
 * - 自定义标题栏 (可选)
 */
#ifndef FLUENTTOOLWINDOW_H
#define FLUENTTOOLWINDOW_H

#include <QDialog>
#include <QPropertyAnimation>

namespace Fluent {

class FluentToolWindow : public QDialog
{
    Q_OBJECT
    Q_PROPERTY(qreal windowOpacity READ windowOpacity WRITE setWindowOpacity)
public:
    explicit FluentToolWindow(QWidget* parent = nullptr);

    /// 设置标题
    void setTitle(const QString& title);

    /// 启用自定义标题栏 (可选)
    void setCustomTitleBarEnabled(bool enabled);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void applyDarkStyle();
    void setDarkTitleBar();

    QPropertyAnimation* m_fadeAnim = nullptr;
    bool m_customTitleBar = false;
    QString m_title;
};

} // namespace Fluent

#endif // FLUENTTOOLWINDOW_H
