#ifndef SCRIPTAPI_H
#define SCRIPTAPI_H

#include <QObject>
#include <QPointF>
#include <QSize>
#include <QPointer>
#include <QVariantMap>
#include <QMap>
#include <QImage>
#include <functional>

class Controller;
class InputConvertGame;

// 帧获取回调类型
using FrameGrabCallback = std::function<QImage()>;

// ---------------------------------------------------------
// 脚本 API 接口类 (FastMsg 协议版本)
// 此类的方法通过 QJSEngine 暴露给 JavaScript，供按键映射脚本调用
// 使用全新的 FastMsg 协议实现毫秒级响应
// ---------------------------------------------------------
class ScriptApi : public QObject
{
    Q_OBJECT
public:
    explicit ScriptApi(Controller* controller, InputConvertGame* game, QObject *parent = nullptr);

    // 设置基础参数
    void setVideoSize(const QSize& size);
    void setAnchorPosition(const QPointF& pos);
    void setPress(bool press) { m_isPress = press; }
    void setKeyId(int id) { m_keyId = id; }  // 用于标识触发该操作的按键

    // 设置帧获取回调 (由 VideoForm 设置)
    void setFrameGrabCallback(FrameGrabCallback callback) { m_frameGrabCallback = callback; }

    // ---------------------------------------------------------
    // 暴露给 JS 的方法 (Q_INVOKABLE)
    // ---------------------------------------------------------

    // 模拟点击操作
    Q_INVOKABLE void click(double x = -1, double y = -1);

    // 模拟长按（按下/抬起状态分离）
    Q_INVOKABLE void holdpress(double x = -1, double y = -1);

    // 释放当前按键绑定的触摸点
    Q_INVOKABLE void release();

    // 模拟滑动操作
    Q_INVOKABLE void slide(double sx, double sy, double ex, double ey, int delayMs, int num);

    // 模拟物理按键（如 Home, Back, W, A, S, D）
    Q_INVOKABLE void key(const QString& keyName);

    // 脚本延时
    Q_INVOKABLE void delay(int ms);

    // 重置视角（用于FPS游戏鼠标重置）
    Q_INVOKABLE void resetview();

    // 重置方向键（发送 WASD 抬起事件）
    Q_INVOKABLE void directionreset();

    // 动态调整轮盘灵敏度参数
    Q_INVOKABLE void setRadialParam(double up, double down, double left, double right);

    // 切换游戏/普通模式
    Q_INVOKABLE void shotmode(bool enter);

    // 获取当前状态
    Q_INVOKABLE QVariantMap getmousepos();
    Q_INVOKABLE QVariantMap getkeypos(const QString& keyName);
    Q_INVOKABLE int getKeyState(const QString& keyName);

    // 调试输出
    Q_INVOKABLE void tip(const QString& msg);

    // ---------------------------------------------------------
    // 图像识别 API
    // ---------------------------------------------------------

    /**
     * @brief 区域找图
     * @param imageName 模板图片名称 (相对于 keymap/images/)
     * @param x1, y1 搜索区域左上角 (0.0~1.0)，可选，默认全图
     * @param x2, y2 搜索区域右下角 (0.0~1.0)，可选
     * @param threshold 相似度阈值 (0.0~1.0)，默认 0.8
     * @return {found, x, y, confidence}
     */
    Q_INVOKABLE QVariantMap findImage(const QString& imageName,
                                       double x1 = 0, double y1 = 0,
                                       double x2 = 1, double y2 = 1,
                                       double threshold = 0.8);

private:
    // 坐标归一化：将 0.0-1.0 转换为 0-65535
    void normalizePos(double x, double y, quint16& outX, quint16& outY);

    // 发送 FastMsg 触摸事件
    void sendFastTouch(quint32 seqId, quint8 action, quint16 x, quint16 y);

    // 发送 FastMsg 按键事件
    void sendFastKey(quint8 action, quint16 keycode);

    // 获取 Android 键码
    int getAndroidKeyCode(const QString& keyName);
    int getQtKey(const QString& keyName);

private:
    QPointer<Controller> m_controller;
    QPointer<InputConvertGame> m_gameConvert;
    QSize m_videoSize = QSize(1920, 1080);
    QPointF m_anchorPos;
    bool m_isPress = true;
    int m_keyId = -1;  // 当前触发的按键 ID

    // 帧获取回调
    FrameGrabCallback m_frameGrabCallback;

    // holdpress 专用：每个按键 ID 对应的触摸序列 ID
    // key: 按键ID, value: 当前活跃的触摸序列ID
    QMap<int, quint32> m_touchSeqIds;
};

#endif // SCRIPTAPI_H
