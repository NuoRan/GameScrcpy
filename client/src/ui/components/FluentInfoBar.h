/**
 * @file FluentInfoBar.h
 * @brief Fluent Focus 通知条 — 右上角自动堆叠、自动消失的轻提示
 *
 * 用法:
 *   FluentInfoBar::success(parentWidget, "连接成功");
 *   FluentInfoBar::error(parentWidget, "连接失败: 超时");
 */

#ifndef FLUENTINFOBAR_H
#define FLUENTINFOBAR_H

#include <QFrame>
#include <QLabel>
#include <QTimer>
#include <QList>

namespace Fluent {

class FluentInfoBar : public QFrame
{
    Q_OBJECT

public:
    enum Level { InfoLevel, SuccessLevel, WarningLevel, ErrorLevel };

    /// 快捷方法 (自动管理生命周期)
    static FluentInfoBar* info(QWidget* parent, const QString& message, int durationMs = 3000);
    static FluentInfoBar* success(QWidget* parent, const QString& message, int durationMs = 3000);
    static FluentInfoBar* warning(QWidget* parent, const QString& message, int durationMs = 4000);
    static FluentInfoBar* error(QWidget* parent, const QString& message, int durationMs = 5000);

    ~FluentInfoBar() override;

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    explicit FluentInfoBar(Level level, const QString& message, int durationMs, QWidget* parent);
    void positionInParent();
    void animateIn();
    void animateOut();

    static QList<FluentInfoBar*>& activeBarList();
    static void repositionAll(QWidget* parent);

    Level m_level;
    QLabel* m_iconLabel;
    QLabel* m_messageLabel;
    QTimer m_dismissTimer;
};

} // namespace Fluent

#endif // FLUENTINFOBAR_H
