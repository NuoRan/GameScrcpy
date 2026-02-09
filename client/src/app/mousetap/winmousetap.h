// winmousetap.h
#ifndef WINMOUSETAP_H
#define WINMOUSETAP_H

#include "mousetap.h"

// ---------------------------------------------------------
// Windows 鼠标限制具体实现类 / Windows Mouse Clip Implementation
// ---------------------------------------------------------
class WinMouseTap : public MouseTap
{
public:
    WinMouseTap();
    virtual ~WinMouseTap();

    void initMouseEventTap() override;
    void quitMouseEventTap() override;
    void enableMouseEventTap(QRect rc, bool enabled) override;
};

#endif // WINMOUSETAP_H
