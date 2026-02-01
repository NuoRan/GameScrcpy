#include "scriptapi.h"
#include "controller.h"
#include "inputconvertgame.h"
#include "fastmsg.h"
#include "keycodes.h"

#ifdef ENABLE_IMAGE_MATCHING
#include "imagematcher.h"
#endif

#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QKeySequence>

// ---------------------------------------------------------
// 构造与初始化
// ---------------------------------------------------------
ScriptApi::ScriptApi(Controller* controller, InputConvertGame* game, QObject *parent)
    : QObject(parent), m_controller(controller), m_gameConvert(game)
{
}

void ScriptApi::setVideoSize(const QSize& size)
{
    if (size.isValid()) {
        m_videoSize = size;
    }
}

void ScriptApi::setAnchorPosition(const QPointF& pos)
{
    m_anchorPos = pos;
}

// ---------------------------------------------------------
// 坐标归一化：将 0.0-1.0 转换为 0-65535
// ---------------------------------------------------------
void ScriptApi::normalizePos(double x, double y, quint16& outX, quint16& outY)
{
    // 如果坐标 < 0，则使用预设的锚点位置
    double tx = (x < 0) ? m_anchorPos.x() : x;
    double ty = (y < 0) ? m_anchorPos.y() : y;

    // 限制范围 0.0 - 1.0
    tx = qBound(0.0, tx, 1.0);
    ty = qBound(0.0, ty, 1.0);

    // 转换为 0-65535
    outX = static_cast<quint16>(tx * 65535.0);
    outY = static_cast<quint16>(ty * 65535.0);
}

// ---------------------------------------------------------
// 发送 FastMsg 触摸事件
// ---------------------------------------------------------
void ScriptApi::sendFastTouch(quint32 seqId, quint8 action, quint16 x, quint16 y)
{
    if (!m_controller) {
        qWarning() << "[FAST_TOUCH] sendFastTouch FAILED: no controller!";
        return;
    }

    QByteArray data;
    // 使用 Raw 版本，因为坐标已经是 0-65535 范围
    if (action == FTA_DOWN) {
        data = FastMsg::touchDownRaw(seqId, x, y);
    } else if (action == FTA_UP) {
        data = FastMsg::touchUpRaw(seqId, x, y);
    } else if (action == FTA_MOVE) {
        data = FastMsg::touchMoveRaw(seqId, x, y);
    } else {
        qWarning() << "[FAST_TOUCH] Unknown action:" << action;
        return;
    }

    m_controller->postFastMsg(data);
}

// ---------------------------------------------------------
// 发送 FastMsg 按键事件
// ---------------------------------------------------------
void ScriptApi::sendFastKey(quint8 action, quint16 keycode)
{
    if (!m_controller) {
        qWarning() << "[FAST_KEY] sendFastKey FAILED: no controller!";
        return;
    }

    QByteArray data;
    if (action == FKA_DOWN) {
        data = FastMsg::keyDown(keycode);
    } else {
        data = FastMsg::keyUp(keycode);
    }

    m_controller->postFastMsg(data);
}

// ---------------------------------------------------------
// JS API 实现：点击、长按、滑动、按键等
// ---------------------------------------------------------

void ScriptApi::click(double x, double y)
{
    if (!m_isPress) return;

    quint32 seqId = FastTouchSeq::next();
    quint16 nx, ny;
    normalizePos(x, y, nx, ny);

    sendFastTouch(seqId, FTA_DOWN, nx, ny);
    sendFastTouch(seqId, FTA_UP, nx, ny);
}

void ScriptApi::holdpress(double x, double y)
{
    int keyId = m_keyId;
    quint16 nx, ny;
    normalizePos(x, y, nx, ny);

    if (m_isPress) {
        // 按下：如果之前的触摸还没释放，先释放它（防止快速连击时触摸点泄漏）
        if (m_touchSeqIds.contains(keyId)) {
            quint32 oldSeqId = m_touchSeqIds[keyId];
            sendFastTouch(oldSeqId, FTA_UP, nx, ny);
            m_touchSeqIds.remove(keyId);
        }
        // 生成全新的序列 ID
        quint32 seqId = FastTouchSeq::next();
        m_touchSeqIds[keyId] = seqId;
        sendFastTouch(seqId, FTA_DOWN, nx, ny);
    } else {
        // 抬起：使用之前按下时的序列 ID
        if (m_touchSeqIds.contains(keyId)) {
            quint32 seqId = m_touchSeqIds[keyId];
            sendFastTouch(seqId, FTA_UP, nx, ny);
            m_touchSeqIds.remove(keyId);
        }
        // 如果没有对应的按下记录，直接忽略（避免发送无意义的 UP 事件）
    }
}

void ScriptApi::release()
{
    quint32 seqId = FastTouchSeq::next();
    quint16 nx, ny;
    normalizePos(m_anchorPos.x(), m_anchorPos.y(), nx, ny);
    sendFastTouch(seqId, FTA_UP, nx, ny);
}

void ScriptApi::slide(double sx, double sy, double ex, double ey, int delayMs, int num)
{
    if (!m_isPress) return;
    if (num <= 0) num = 1;

    quint32 seqId = FastTouchSeq::next();
    quint16 nx, ny;

    // 1. 按下起点
    normalizePos(sx, sy, nx, ny);
    sendFastTouch(seqId, FTA_DOWN, nx, ny);

    // 2. 插值计算移动路径
    double stepX = (ex - sx) / num;
    double stepY = (ey - sy) / num;
    int stepTime = delayMs / num;

    for (int i = 1; i <= num; ++i) {
        if (stepTime > 0) delay(stepTime);
        normalizePos(sx + stepX * i, sy + stepY * i, nx, ny);
        sendFastTouch(seqId, FTA_MOVE, nx, ny);
    }

    // 3. 抬起终点
    normalizePos(ex, ey, nx, ny);
    sendFastTouch(seqId, FTA_UP, nx, ny);
}

void ScriptApi::key(const QString& keyName)
{
    if (!m_controller) return;
    int code = getAndroidKeyCode(keyName);
    if (code != AKEYCODE_UNKNOWN) {
        if (m_isPress) {
            sendFastKey(FKA_DOWN, static_cast<quint16>(code));
        } else {
            sendFastKey(FKA_UP, static_cast<quint16>(code));
        }
    }
}

// 阻塞式延时（使用 EventLoop 防止界面卡死）
void ScriptApi::delay(int ms)
{
    if (ms <= 0) return;
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void ScriptApi::resetview()
{
    if (!m_isPress) return;
    if (m_gameConvert) {
        m_gameConvert->script_resetView();
    }
}

void ScriptApi::directionreset()
{
    if (!m_isPress) return;
    if (!m_controller) return;

    // 发送 WASD 的抬起事件，防止卡键
    sendFastKey(FKA_UP, AKEYCODE_W);
    sendFastKey(FKA_UP, AKEYCODE_A);
    sendFastKey(FKA_UP, AKEYCODE_S);
    sendFastKey(FKA_UP, AKEYCODE_D);
}

void ScriptApi::setRadialParam(double up, double down, double left, double right)
{
    if (!m_isPress) return;
    if (m_gameConvert) {
        m_gameConvert->script_setSteerWheelOffset(up, down, left, right);
    }
}

void ScriptApi::shotmode(bool enter)
{
    if (!m_isPress) return;
    if (m_gameConvert) {
        m_gameConvert->script_setGameMapMode(enter);
    }
}

QVariantMap ScriptApi::getmousepos()
{
    QVariantMap map;
    double x = 0.5, y = 0.5;

    if (m_gameConvert) {
        QPointF p = m_gameConvert->script_getMousePos();
        x = p.x();
        y = p.y();
    }
    map.insert("x", x);
    map.insert("y", y);
    return map;
}

QVariantMap ScriptApi::getkeypos(const QString& keyName)
{
    if (!m_gameConvert) return QVariantMap();
    int qtKey = getQtKey(keyName);
    return m_gameConvert->script_getKeyPos(qtKey);
}

int ScriptApi::getKeyState(const QString& keyName)
{
    if (!m_gameConvert) return 0;
    int qtKey = getQtKey(keyName);
    return m_gameConvert->script_getKeyState(qtKey);
}

void ScriptApi::tip(const QString& msg)
{
    Q_UNUSED(msg)
    // 调试用 tip 功能，生产环境不输出
}

// ---------------------------------------------------------
// 图像识别 API 实现
// ---------------------------------------------------------
QVariantMap ScriptApi::findImage(const QString& imageName,
                                  double x1, double y1,
                                  double x2, double y2,
                                  double threshold)
{
    QVariantMap result;
    result.insert("found", false);
    result.insert("x", 0.0);
    result.insert("y", 0.0);
    result.insert("confidence", 0.0);

#ifdef ENABLE_IMAGE_MATCHING
    // 1. 获取当前视频帧
    if (!m_frameGrabCallback) {
        qWarning() << "[findImage] No frame grab callback set";
        return result;
    }

    QImage currentFrame = m_frameGrabCallback();
    if (currentFrame.isNull()) {
        qWarning() << "[findImage] Failed to grab current frame";
        return result;
    }

    // 2. 加载模板图片
    QImage templateImage = ImageMatcher::loadTemplateImage(imageName);
    if (templateImage.isNull()) {
        qWarning() << "[findImage] Failed to load template:" << imageName;
        return result;
    }

    // 3. 构造搜索区域
    QRectF searchRegion(x1, y1, x2 - x1, y2 - y1);

    // 4. 执行模板匹配
    ImageMatcher matcher;
    ImageMatchResult matchResult = matcher.findTemplate(currentFrame, templateImage, threshold, searchRegion);

    // 5. 返回结果
    result.insert("found", matchResult.found);
    result.insert("x", matchResult.x);
    result.insert("y", matchResult.y);
    result.insert("confidence", matchResult.confidence);
#else
    Q_UNUSED(imageName);
    Q_UNUSED(x1); Q_UNUSED(y1);
    Q_UNUSED(x2); Q_UNUSED(y2);
    Q_UNUSED(threshold);
    qWarning() << "[findImage] Image matching is disabled (OpenCV not available)";
#endif

    return result;
}

// ---------------------------------------------------------
// 键值转换辅助函数
// ---------------------------------------------------------
int ScriptApi::getAndroidKeyCode(const QString& keyName)
{
    QString k = keyName.toUpper();
    if (k == "W") return AKEYCODE_W;
    if (k == "A") return AKEYCODE_A;
    if (k == "S") return AKEYCODE_S;
    if (k == "D") return AKEYCODE_D;
    if (k == "SPACE") return AKEYCODE_SPACE;
    if (k == "ENTER") return AKEYCODE_ENTER;
    if (k == "ESC") return AKEYCODE_ESCAPE;
    if (k == "BACK") return AKEYCODE_BACK;
    if (k == "HOME") return AKEYCODE_HOME;
    if (k == "MENU") return AKEYCODE_MENU;
    if (k == "UP") return AKEYCODE_DPAD_UP;
    if (k == "DOWN") return AKEYCODE_DPAD_DOWN;
    if (k == "LEFT") return AKEYCODE_DPAD_LEFT;
    if (k == "RIGHT") return AKEYCODE_DPAD_RIGHT;

    if (k.startsWith("F")) {
        bool ok;
        int num = k.mid(1).toInt(&ok);
        if (ok && num >= 1 && num <= 12) {
            return AKEYCODE_F1 + (num - 1);
        }
    }
    if (k.length() == 1) {
        char c = k.at(0).toLatin1();
        if (c >= '0' && c <= '9') return AKEYCODE_0 + (c - '0');
        if (c >= 'A' && c <= 'Z') return AKEYCODE_A + (c - 'A');
    }
    return AKEYCODE_UNKNOWN;
}

int ScriptApi::getQtKey(const QString& keyName)
{
    QKeySequence seq(keyName);
    if (seq.count() > 0) {
        return seq[0].key();
    }
    return Qt::Key_unknown;
}
