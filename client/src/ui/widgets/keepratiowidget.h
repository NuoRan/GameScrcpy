#ifndef KEEPRATIOWIDGET_H
#define KEEPRATIOWIDGET_H

#include <QPointer>
#include <QWidget>

// ---------------------------------------------------------
// 保持比例容器控件 / Keep-Aspect-Ratio Container Widget
// 无论窗口如何缩放，始终保持子控件的宽高比
// Always maintains child widget's aspect ratio regardless of window resize
// ---------------------------------------------------------
class KeepRatioWidget : public QWidget
{
    Q_OBJECT
public:
    // 缩放模式 / Scale mode
    enum ScaleMode {
        FitMode,   // 保持比例，完整显示，可能有黑边 / Fit: full display, may have letterbox
        CoverMode  // 保持比例，填满容器，裁剪超出部分 / Cover: fill container, crop overflow
    };

    explicit KeepRatioWidget(QWidget *parent = nullptr);
    ~KeepRatioWidget();

    void setWidget(QWidget *w);
    void setWidthHeightRatio(float widthHeightRatio);
    void setScaleMode(ScaleMode mode);
    ScaleMode scaleMode() const { return m_scaleMode; }
    const QSize goodSize();

protected:
    void resizeEvent(QResizeEvent *event);
    void adjustSubWidget();

private:
    float m_widthHeightRatio = -1.0f;
    ScaleMode m_scaleMode = FitMode;
    QPointer<QWidget> m_subWidget;
    QSize m_goodSize;
};

#endif // KEEPRATIOWIDGET_H
