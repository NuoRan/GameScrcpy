/**
 * @file FluentToggle.h
 * @brief Fluent Focus 开关组件 — 替代 QCheckBox 的滑块式 Toggle
 */

#ifndef FLUENTTOGGLE_H
#define FLUENTTOGGLE_H

#include <QWidget>

namespace Fluent {

class FluentToggle : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
    Q_PROPERTY(qreal handlePosition READ handlePosition WRITE setHandlePosition)

public:
    explicit FluentToggle(QWidget* parent = nullptr);

    bool isChecked() const { return m_checked; }
    void setChecked(bool checked);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    qreal handlePosition() const { return m_handlePos; }
    void setHandlePosition(qreal pos);
    void animateToggle();

    bool m_checked = false;
    qreal m_handlePos = 0.0;  // 0.0 = off, 1.0 = on
};

} // namespace Fluent

#endif // FLUENTTOGGLE_H
