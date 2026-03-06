/**
 * @file FluentSlider.h
 * @brief Fluent Focus 滑块 — 带数值标签 + 平滑手柄动画
 */
#ifndef FLUENTSLIDER_H
#define FLUENTSLIDER_H

#include <QWidget>
#include <QPropertyAnimation>

namespace Fluent {

class FluentSlider : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal handleScale READ handleScale WRITE setHandleScale)
public:
    explicit FluentSlider(QWidget* parent = nullptr);

    void setRange(int min, int max);
    void setValue(int value);
    int  value() const { return m_value; }
    void setLabel(const QString& label);
    void setShowValue(bool show);
    void setSuffix(const QString& suffix);

    QSize sizeHint() const override { return {200, 32}; }
    QSize minimumSizeHint() const override { return {100, 28}; }

signals:
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    qreal handleScale() const { return m_handleScale; }
    void setHandleScale(qreal s) { m_handleScale = s; update(); }
    int valueFromX(int x) const;
    QRectF trackRect() const;
    QPointF handleCenter() const;

    int m_min = 0, m_max = 100, m_value = 0;
    QString m_label, m_suffix;
    bool m_showValue = true;
    bool m_dragging = false;
    bool m_hovered = false;
    qreal m_handleScale = 1.0;
    QPropertyAnimation* m_scaleAnim = nullptr;
};

} // namespace Fluent

#endif // FLUENTSLIDER_H
