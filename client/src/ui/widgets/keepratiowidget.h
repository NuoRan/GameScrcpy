#ifndef KEEPRATIOWIDGET_H
#define KEEPRATIOWIDGET_H

#include <QPointer>
#include <QWidget>

// ---------------------------------------------------------
// 保持比例容器控件
// 无论窗口如何缩放，始终保持子控件的宽高比
// ---------------------------------------------------------
class KeepRatioWidget : public QWidget
{
    Q_OBJECT
public:
    // 缩放模式
    enum ScaleMode {
        FitMode,   // 保持比例，完整显示，可能有黑边
        CoverMode  // 保持比例，填满容器，裁剪超出部分（无黑边）
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
