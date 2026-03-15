/**
 * @file controller.h
 * @brief 设备控制器 / Device Controller
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 *
 * 控制器负责 / Controller responsibilities:
 * - 发送控制消息到 Android 设备 / Send control messages to Android device
 * - 键鼠事件转发 / Keyboard and mouse event forwarding
 * - 脚本引擎管理 / Script engine management
 * - Android 功能快捷操作 (返回、主页、菜单等) / Android shortcuts (back, home, menu, etc.)
 */

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "GameTypes.h"
#include <opencv2/core.hpp>
#include <string>
#include <functional>
#include <vector>
#include <cstdint>

#include "GameSignal.h"
#include "keycodes.h"
#include "GrayFrame.h"

class NativeTcpSocket;
class KcpControlSocket;
class Receiver;
class DeviceMsg;
class ControlSender;
class SessionContext;
class TouchRouter;
struct InputEvent;

// 前向声明
namespace qsc { namespace core { class IControlChannel; } }

/// KCP 发送回调函数类型 / KCP send callback function type
using KcpSendCallback = std::function<int64_t(const char*, int)>;

/**
 * @brief 设备控制器类 / Device Controller Class
 *
 * 精简版控制器，主要负责 / Streamlined controller, mainly responsible for:
 * - 消息发送 (触摸、按键、快速消息) / Message sending (touch, key, fast messages)
 * - 输入处理委托给 SessionContext / Input processing delegated to SessionContext
 * - 脚本管理 / Script management
 */
class Controller
{
public:
    Controller(KcpSendCallback sendCallback, std::string gameScript = "");
    virtual ~Controller();

    void startSender();
    void stopSender();

    void postFastMsg(const std::vector<uint8_t> &data);  // FastMsg 协议快速发送
    void postFastMsg(const char *data, int len);  // P-KCP: 零分配版本
    void recvDeviceMsg(DeviceMsg *deviceMsg);

    // 脚本管理
    void updateScript(std::string gameScript = "", bool runAutoStartScripts = true);
    bool isCurrentCustomKeymap();

    // Android 常用功能快捷接口
    void postGoBack();
    void postGoHome();
    void postGoMenu();
    void postAppSwitch();
    void postPower();
    void postVolumeUp();
    void postVolumeDown();

    // 输入转换接口（委托给 SessionContext）
    void mouseEvent(const InputEvent& from, const Size &frameSize, const Size &showSize);
    void wheelEvent(const InputEvent& from, const Size &frameSize, const Size &showSize);
    void keyEvent(const InputEvent& from, const Size &frameSize, const Size &showSize);

    // 返回键/亮屏
    void postBackOrScreenOn(bool down);

    // 发送按键点击事件 (down + up)
    void postKeyCodeClick(AndroidKeycode keycode);

    // 设置移动设备分辨率
    void setMobileSize(const Size &size);

    // 设置控制 socket
    void setControlSocket(KcpControlSocket *socket);  // WiFi 模式 (KCP)
    void setTcpControlSocket(NativeTcpSocket *socket);     // USB 模式 (TCP)

    // 设置控制通道接口
    void setControlChannel(qsc::core::IControlChannel* channel);

    // 设置帧获取回调 (用于脚本图像识别)
    void setFrameGrabCallback(std::function<cv::Mat()> callback);
    void setGrayFrameGrabCallback(GrayFrameGrabCallback callback);

    // 连接脚本 tip 信号
    void connectScriptTipSignal(std::function<void(const std::string&, int, int)> callback);

    // 连接键位覆盖层更新信号
    void connectKeyMapOverlayUpdateSignal(std::function<void()> callback);

    // 发送断开消息给服务端
    void postDisconnect();

    // 运行时参数调整
    void postSetVideoBitRate(uint32_t bitrate);
    void postSetDisplayPower(bool on);

    // 窗口失去焦点时调用，重置输入状态
    void onWindowFocusLost();

    // 重置脚本状态（进入编辑模式时调用）
    void resetScriptState();

    // 执行自动启动脚本（视频流准备好后调用）
    void runAutoStartScripts();

    // 释放全部触摸点（窗口关闭/切换键位时调用）
    void resetAllTouchPoints();

    // 获取 SessionContext（供其他模块访问）
    SessionContext* sessionContext() const { return m_sessionContext; }

    // 触控路由器
    void setTouchRouter(TouchRouter* router);
    TouchRouter* touchRouter() const { return m_touchRouter; }

    Signal<bool> grabCursor;

private:
    KcpSendCallback m_sendCallback;
    ControlSender* m_controlSender = nullptr;
    TouchRouter* m_touchRouter = nullptr;  // 不拥有, 外部管理
    Receiver* m_receiver = nullptr;

    SessionContext* m_sessionContext = nullptr;

    Size m_mobileSize;
    std::function<cv::Mat()> m_frameGrabCallback;
    GrayFrameGrabCallback m_grayFrameGrabCallback;
    std::function<void(const std::string&, int, int)> m_scriptTipCallback;
    std::function<void()> m_overlayUpdateCallback;
    bool m_senderStarted = false;
    bool m_disconnectSent = false;
};

#endif // CONTROLLER_H
