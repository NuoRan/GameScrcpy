#include <QResizeEvent>
#include <cmath>

#include "keepratiowidget.h"

KeepRatioWidget::KeepRatioWidget(QWidget *parent) : QWidget(parent) {}

KeepRatioWidget::~KeepRatioWidget() {}

void KeepRatioWidget::setWidget(QWidget *w)
{
    if (!w) {
        return;
    }
    w->setParent(this);
    m_subWidget = w;
}

// 设置目标宽高比
void KeepRatioWidget::setWidthHeightRatio(float widthHeightRatio)
{
    if (fabs(m_widthHeightRatio - widthHeightRatio) < 0.000001f) {
        return;
    }
    m_widthHeightRatio = widthHeightRatio;
    adjustSubWidget();
}

// 设置缩放模式
void KeepRatioWidget::setScaleMode(ScaleMode mode)
{
    if (m_scaleMode == mode) {
        return;
    }
    m_scaleMode = mode;
    adjustSubWidget();
}

// 获取适应比例的最佳尺寸
const QSize KeepRatioWidget::goodSize()
{
    if (!m_subWidget || m_widthHeightRatio < 0.0f) {
        return QSize();
    }
    return m_subWidget->size();
}

void KeepRatioWidget::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event)
    adjustSubWidget();
}

// 核心逻辑：计算子控件居中后的位置和大小
void KeepRatioWidget::adjustSubWidget()
{
    if (!m_subWidget) {
        return;
    }

    QSize curSize = size();
    QPoint pos(0, 0);
    int width = 0;
    int height = 0;

    if (m_widthHeightRatio <= 0.0f) {
        // 无比例约束，直接铺满
        width = curSize.width();
        height = curSize.height();
    } else if (m_scaleMode == CoverMode) {
        // Cover模式：填满容器，裁剪超出部分（无黑边）
        float containerRatio = static_cast<float>(curSize.width()) / curSize.height();
        if (containerRatio > m_widthHeightRatio) {
            // 容器更宽，以宽为基准，高会超出
            width = curSize.width();
            height = static_cast<int>(curSize.width() / m_widthHeightRatio);
        } else {
            // 容器更高，以高为基准，宽会超出
            height = curSize.height();
            width = static_cast<int>(curSize.height() * m_widthHeightRatio);
        }
        // 居中（超出部分会被裁剪）
        pos.setX((curSize.width() - width) / 2);
        pos.setY((curSize.height() - height) / 2);
    } else {
        // Fit模式：保持比例，完整显示（可能有黑边）
        if (m_widthHeightRatio > 1.0f) {
            // 以宽为基准
            width = curSize.width();
            height = static_cast<int>(curSize.width() / m_widthHeightRatio);
            pos.setY((curSize.height() - height) / 2);
        } else {
            // 以高为基准
            height = curSize.height();
            width = static_cast<int>(curSize.height() * m_widthHeightRatio);
            pos.setX((curSize.width() - width) / 2);
        }
    }

    m_subWidget->setGeometry(pos.x(), pos.y(), width, height);
}
