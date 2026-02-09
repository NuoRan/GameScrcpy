#ifndef MAGNETICWIDGET_H
#define MAGNETICWIDGET_H

#include <QPointer>
#include <QWidget>

// ---------------------------------------------------------
// 磁性吸附窗口基类 / Magnetic Snap Widget Base Class
// 实现窗口吸附到另一个窗口边缘的功能
// Implements window snapping to another window's edges
// ---------------------------------------------------------
class MagneticWidget : public QWidget
{
    Q_OBJECT

public:
    enum AdsorbPosition
    {
        AP_OUTSIDE_LEFT = 0x01,   // 吸附外部左边框 / Snap to outer left edge
        AP_OUTSIDE_TOP = 0x02,    // 吸附外部上边框 / Snap to outer top edge
        AP_OUTSIDE_RIGHT = 0x04,  // 吸附外部右边框 / Snap to outer right edge
        AP_OUTSIDE_BOTTOM = 0x08, // 吸附外部下边框 / Snap to outer bottom edge
        AP_INSIDE_LEFT = 0x10,    // 吸附内部左边框 / Snap to inner left edge
        AP_INSIDE_TOP = 0x20,     // 吸附内部上边框 / Snap to inner top edge
        AP_INSIDE_RIGHT = 0x40,   // 吸附内部右边框 / Snap to inner right edge
        AP_INSIDE_BOTTOM = 0x80,  // 吸附内部下边框 / Snap to inner bottom edge
        AP_ALL = 0xFF,            // 全吸附 / All edges
    };
    Q_DECLARE_FLAGS(AdsorbPositions, AdsorbPosition)

public:
    explicit MagneticWidget(QWidget *adsorbWidget, AdsorbPositions adsorbPos = AP_ALL);
    ~MagneticWidget();

    bool isAdsorbed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    void getGeometry(QRect &relativeWidgetRect, QRect &targetWidgetRect);

private:
    AdsorbPositions m_adsorbPos = AP_ALL;
    QPoint m_relativePos;
    bool m_adsorbed = false;
    QPointer<QWidget> m_adsorbWidget;
    QSize m_adsorbWidgetSize;
    AdsorbPosition m_curAdsorbPosition;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(MagneticWidget::AdsorbPositions)
#endif // MAGNETICWIDGET_H
