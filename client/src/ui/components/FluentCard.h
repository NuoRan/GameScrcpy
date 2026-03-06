/**
 * @file FluentCard.h
 * @brief Fluent Focus 卡片容器 — 统一的圆角、边框、阴影外观
 */

#ifndef FLUENTCARD_H
#define FLUENTCARD_H

#include <QFrame>

namespace Fluent {

class FluentCard : public QFrame
{
    Q_OBJECT
    Q_PROPERTY(bool hoverable READ isHoverable WRITE setHoverable)
    Q_PROPERTY(bool clickable READ isClickable WRITE setClickable)

public:
    explicit FluentCard(QWidget* parent = nullptr);

    void setHoverable(bool v) { m_hoverable = v; }
    bool isHoverable() const { return m_hoverable; }

    void setClickable(bool v);
    bool isClickable() const { return m_clickable; }

    /// 设置内边距 (统一 padding)
    void setPadding(int padding);
    void setPadding(int h, int v);

signals:
    void clicked();

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void applyStyle();

    bool m_hoverable = false;
    bool m_clickable = false;
    bool m_pressed = false;
};

} // namespace Fluent

#endif // FLUENTCARD_H
