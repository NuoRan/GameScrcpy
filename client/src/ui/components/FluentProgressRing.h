/**
 * @file FluentProgressRing.h
 * @brief Fluent Focus 环形进度指示器
 */
#ifndef FLUENTPROGRESSRING_H
#define FLUENTPROGRESSRING_H

#include <QWidget>
#include <QPropertyAnimation>

namespace Fluent {

class FluentProgressRing : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal rotation READ rotation WRITE setRotation)
    Q_PROPERTY(qreal arcLength READ arcLength WRITE setArcLength)
public:
    explicit FluentProgressRing(QWidget* parent = nullptr);

    void setIndeterminate(bool ind);
    void setValue(int v); // 0~100
    void setStrokeWidth(int w) { m_strokeWidth = w; update(); }

    QSize sizeHint() const override { return {48, 48}; }
    QSize minimumSizeHint() const override { return {24, 24}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    qreal rotation() const { return m_rotation; }
    void setRotation(qreal r) { m_rotation = r; update(); }
    qreal arcLength() const { return m_arcLength; }
    void setArcLength(qreal l) { m_arcLength = l; update(); }

    bool m_indeterminate = true;
    int m_value = 0;
    int m_strokeWidth = 4;
    qreal m_rotation = 0;
    qreal m_arcLength = 80;
    QPropertyAnimation* m_rotAnim = nullptr;
    QPropertyAnimation* m_arcAnim = nullptr;
};

} // namespace Fluent
#endif
