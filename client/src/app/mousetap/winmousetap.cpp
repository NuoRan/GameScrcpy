// winmousetap.cpp
#include <QDebug>
#include <QWidget>
#include <Windows.h>

#include "winmousetap.h"

WinMouseTap::WinMouseTap() {}

WinMouseTap::~WinMouseTap() {}

void WinMouseTap::initMouseEventTap() {}

void WinMouseTap::quitMouseEventTap() {}

// ---------------------------------------------------------
// Windows 平台鼠标限制实现
// 使用 Windows API ClipCursor 实现
// ---------------------------------------------------------
void WinMouseTap::enableMouseEventTap(QRect rc, bool enabled)
{
    if (enabled && rc.isEmpty()) {
        return;
    }
    if (enabled) {
        RECT mainRect;
        mainRect.left = (LONG)rc.left();
        mainRect.right = (LONG)rc.right();
        mainRect.top = (LONG)rc.top();
        mainRect.bottom = (LONG)rc.bottom();
        ClipCursor(&mainRect); // 限制光标
    } else {
        ClipCursor(Q_NULLPTR); // 释放光标
    }
}
