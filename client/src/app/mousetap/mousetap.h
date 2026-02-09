// mousetap.h
#ifndef MOUSETAP_H
#define MOUSETAP_H
#include <QRect>

class QWidget;

// ---------------------------------------------------------
// 鼠标限制工具基类 (抽象类) / Mouse Clip Cursor Base Class (Abstract)
// 用于限制鼠标光标在特定区域内移动
// Restricts mouse cursor movement within a specified region
// ---------------------------------------------------------
class MouseTap
{
public:
    static MouseTap *getInstance();
    virtual void initMouseEventTap() = 0;
    virtual void quitMouseEventTap() = 0;

    // 启用或禁用鼠标限制 / Enable or disable mouse clipping
    // rc: 基于全局屏幕坐标系的矩形区域 / Rectangle in global screen coordinates
    virtual void enableMouseEventTap(QRect rc, bool enabled) = 0;

private:
    static MouseTap *s_instance;
};
#endif // MOUSETAP_H
