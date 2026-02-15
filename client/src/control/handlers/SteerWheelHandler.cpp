#define _USE_MATH_DEFINES
#include <cmath>
#include "SteerWheelHandler.h"
#include "controller.h"
#include "SessionContext.h"
#include "fastmsg.h"
#include "ConfigCenter.h"
#include <QDebug>
#include <QRandomGenerator>

// 静态辅助函数：应用随机偏移
static QPointF applyRandomOffset(const QPointF& pos, const QSize& targetSize) {
    int offsetLevel = qsc::ConfigCenter::instance().randomOffset();
    if (offsetLevel <= 0 || targetSize.isEmpty()) {
        return pos;
    }

    // offsetLevel 0~100 映射到 0~50 像素
    double maxPixelOffset = offsetLevel * 0.5;

    // 生成随机偏移（像素）
    double offsetX = (QRandomGenerator::global()->generateDouble() - 0.5) * 2.0 * maxPixelOffset;
    double offsetY = (QRandomGenerator::global()->generateDouble() - 0.5) * 2.0 * maxPixelOffset;

    // 转换为归一化偏移量
    double normalizedOffsetX = offsetX / targetSize.width();
    double normalizedOffsetY = offsetY / targetSize.height();

    QPointF result = pos;
    result.rx() += normalizedOffsetX;
    result.ry() += normalizedOffsetY;

    // 确保结果在 0~1 范围内
    result.setX(qBound(0.001, result.x(), 0.999));
    result.setY(qBound(0.001, result.y(), 0.999));

    return result;
}

// 静态辅助函数：获取目标尺寸
static QSize getTargetSize(const QSize& frameSize, const QSize& showSize) {
    if (frameSize.isValid() && !frameSize.isEmpty()) {
        return frameSize;
    }
    return showSize;
}

SteerWheelHandler::SteerWheelHandler(QObject* parent)
    : IInputHandler(parent)
{
    // 初始化定时器
    m_state.delayData.timer = new QTimer(this);
    m_state.delayData.timer->setSingleShot(true);
    connect(m_state.delayData.timer, &QTimer::timeout, this, &SteerWheelHandler::onSteerWheelTimer);

    m_state.firstPressTimer = new QTimer(this);
    m_state.firstPressTimer->setSingleShot(true);
    m_state.firstPressTimer->setInterval(5);
    connect(m_state.firstPressTimer, &QTimer::timeout, this, &SteerWheelHandler::onFirstPressTimer);

    m_state.humanizeTimer = new QTimer(this);
    m_state.humanizeTimer->setSingleShot(true);
    connect(m_state.humanizeTimer, &QTimer::timeout, this, &SteerWheelHandler::onHumanizeTimer);
}

SteerWheelHandler::~SteerWheelHandler()
{
    reset();
}

void SteerWheelHandler::init(Controller* controller, SessionContext* context)
{
    IInputHandler::init(controller, context);
}

bool SteerWheelHandler::handleKeyEvent(const QKeyEvent* event,
                                       const QSize& frameSize,
                                       const QSize& showSize)
{
    if (!m_keyMap) return false;

    m_frameSize = frameSize;
    m_showSize = showSize;

    int key = event->key();
    const KeyMap::KeyMapNode& node = m_keyMap->getKeyMapNodeKey(key);

    // 只处理轮盘类型
    if (node.type != KeyMap::KMT_STEER_WHEEL) {
        return false;
    }

    // 检查是否是轮盘的按键
    bool isSteerKey = (key == node.data.steerWheel.up.key ||
                       key == node.data.steerWheel.down.key ||
                       key == node.data.steerWheel.left.key ||
                       key == node.data.steerWheel.right.key);

    if (!isSteerKey) {
        return false;
    }

    processSteerWheel(node, event);
    return true;  // 消费事件
}

void SteerWheelHandler::onFocusLost()
{
    reset();
}

void SteerWheelHandler::reset()
{
    m_state.firstPressTimer->stop();
    m_state.humanizeTimer->stop();
    m_state.delayData.timer->stop();

    if (m_state.fastTouchSeqId != 0) {
        sendFastTouch(FTA_UP, m_state.delayData.currentPos);
        m_state.fastTouchSeqId = 0;
    }

    m_state.pressedUp = false;
    m_state.pressedDown = false;
    m_state.pressedLeft = false;
    m_state.pressedRight = false;
    m_state.isFirstPress = true;
    m_state.delayData.pressedNum = 0;
    m_state.delayData.queuePos.clear();
    m_state.delayData.queueTimer.clear();
}

void SteerWheelHandler::setCoefficient(double up, double down, double left, double right)
{
    if (m_keyMap) {
        m_keyMap->setSteerWheelCoefficient(up, down, left, right);

        // 如果轮盘正在活动，立即触发重新计算
        if (m_state.fastTouchSeqId != 0 && m_state.delayData.pressedNum > 0) {
            const KeyMap::KeyMapNode* node = m_keyMap->getSteerWheelNode();
            if (node) {
                m_state.delayData.timer->stop();
                m_state.delayData.queueTimer.clear();
                m_state.delayData.queuePos.clear();
                executeMove(*node);
            }
        }
    }
}

void SteerWheelHandler::resetCoefficient()
{
    if (m_keyMap) {
        m_keyMap->setSteerWheelCoefficient(1.0, 1.0, 1.0, 1.0);
    }
}

void SteerWheelHandler::resetWheel()
{
    // 用于场景切换后重置轮盘状态（如跑步时按F进车）
    // 游戏内的轮盘已经被重置，但用户仍然按着方向键

    // 1. 停止所有定时器和清空队列
    if (m_state.firstPressTimer) {
        m_state.firstPressTimer->stop();
    }
    if (m_state.delayData.timer) {
        m_state.delayData.timer->stop();
    }
    m_state.delayData.queueTimer.clear();
    m_state.delayData.queuePos.clear();

    // 2. 释放当前触摸（如果有）
    if (m_state.fastTouchSeqId != 0) {
        sendFastTouch(FTA_UP, m_state.delayData.currentPos);
        m_state.fastTouchSeqId = 0;
    }

    // 3. 重置首次按键状态
    m_state.isFirstPress = true;
    m_state.pendingNode = nullptr;

    // 4. 如果仍有方向键按住，重新触发轮盘
    int pressedNum = 0;
    if (m_state.pressedUp) ++pressedNum;
    if (m_state.pressedRight) ++pressedNum;
    if (m_state.pressedDown) ++pressedNum;
    if (m_state.pressedLeft) ++pressedNum;

    if (pressedNum > 0 && m_keyMap) {
        m_state.delayData.pressedNum = pressedNum;
        const KeyMap::KeyMapNode* node = m_keyMap->getSteerWheelNode();
        if (node) {
            // 直接执行轮盘移动，不走首次按键延迟
            m_state.isFirstPress = false;
            executeMove(*node);
        }
    }
}

void SteerWheelHandler::onSteerWheelTimer()
{
    if (m_state.delayData.queuePos.empty()) {
        return;
    }

    m_state.delayData.currentPos = m_state.delayData.queuePos.dequeue();
    sendFastTouch(FTA_MOVE, m_state.delayData.currentPos);

    if (m_state.delayData.queuePos.empty() && m_state.delayData.pressedNum == 0) {
        sendFastTouch(FTA_UP, m_state.delayData.currentPos);
        m_state.fastTouchSeqId = 0;
        return;
    }

    if (!m_state.delayData.queuePos.empty()) {
        m_state.delayData.timer->start(m_state.delayData.queueTimer.dequeue());
    }
}

void SteerWheelHandler::onFirstPressTimer()
{
    if (!m_state.pendingNode) return;

    if (m_state.delayData.pressedNum > 0) {
        executeMove(*m_state.pendingNode);
    }
}

void SteerWheelHandler::onHumanizeTimer()
{
    // 只有轮盘活动时才触发波动
    if (m_state.fastTouchSeqId == 0 || m_state.delayData.pressedNum <= 0) {
        return;
    }

    // 生成新的轻微波动目标（比状态变化时更小的波动）
    // 角度：±10%的轻微波动（相比状态变化时的±30%）
    double angleVariation = 0.10;
    m_state.targetAngleOffset = (QRandomGenerator::global()->generateDouble() * 2.0 - 1.0) * angleVariation * M_PI / 4.0;

    // 长度：±5%的轻微波动（相比状态变化时的±10%）
    double lengthVariation = 0.05;
    m_state.targetLengthFactor = 1.0 + (QRandomGenerator::global()->generateDouble() * 2.0 - 1.0) * lengthVariation;

    // 触发轮盘重新计算（会平滑过渡到新目标）
    if (m_keyMap) {
        const KeyMap::KeyMapNode* node = m_keyMap->getSteerWheelNode();
        if (node) {
            executeMove(*node);
        }
    }

    // 设置下次波动时间（2-8秒随机）
    int nextInterval = 2000 + QRandomGenerator::global()->bounded(6000);
    m_state.humanizeTimer->start(nextInterval);
}

void SteerWheelHandler::processSteerWheel(const KeyMap::KeyMapNode& node, const QKeyEvent* event)
{
    int key = event->key();
    bool flag = event->type() == QEvent::KeyPress;

    // 更新按键状态
    if (key == node.data.steerWheel.up.key) {
        m_state.pressedUp = flag;
    } else if (key == node.data.steerWheel.right.key) {
        m_state.pressedRight = flag;
    } else if (key == node.data.steerWheel.down.key) {
        m_state.pressedDown = flag;
    } else {
        m_state.pressedLeft = flag;
    }

    // 计算当前按下的键数
    int pressedNum = 0;
    if (m_state.pressedUp) ++pressedNum;
    if (m_state.pressedRight) ++pressedNum;
    if (m_state.pressedDown) ++pressedNum;
    if (m_state.pressedLeft) ++pressedNum;
    m_state.delayData.pressedNum = pressedNum;

    // 所有键都松开了
    if (pressedNum == 0) {
        m_state.firstPressTimer->stop();
        m_state.isFirstPress = true;

        if (m_state.delayData.timer->isActive()) {
            m_state.delayData.timer->stop();
            m_state.delayData.queueTimer.clear();
            m_state.delayData.queuePos.clear();
        }

        if (m_state.fastTouchSeqId != 0) {
            sendFastTouch(FTA_UP, m_state.delayData.currentPos);
            m_state.fastTouchSeqId = 0;
        }
        return;
    }

    // 首次按下：等待检测组合键
    if (m_state.isFirstPress && flag) {
        m_state.pendingNode = &node;
        m_state.isFirstPress = false;
        m_state.firstPressTimer->start();
        return;
    }

    // 首次延迟定时器还在运行
    if (m_state.firstPressTimer->isActive()) {
        return;
    }

    executeMove(node);
}

void SteerWheelHandler::executeMove(const KeyMap::KeyMapNode& node)
{
    // 计算当前按键状态哈希
    int currentState = (m_state.pressedUp ? 1 : 0) |
                       (m_state.pressedDown ? 2 : 0) |
                       (m_state.pressedLeft ? 4 : 0) |
                       (m_state.pressedRight ? 8 : 0);

    // 检测按键状态变化
    if (currentState != m_state.lastPressedState) {
        m_state.lastPressedState = currentState;

        double angleVariation = 0.30;
        m_state.targetAngleOffset = (QRandomGenerator::global()->generateDouble() * 2.0 - 1.0) * angleVariation * M_PI / 4.0;

        double lengthVariation = 0.10;
        m_state.targetLengthFactor = 1.0 + (QRandomGenerator::global()->generateDouble() * 2.0 - 1.0) * lengthVariation;

        if (!m_state.humanizeTimer->isActive() && currentState != 0) {
            int nextInterval = 2000 + QRandomGenerator::global()->bounded(6000);
            m_state.humanizeTimer->start(nextInterval);
        }
    }

    // 平滑过渡
    double smoothFactor = 0.2;
    m_state.currentAngleOffset += (m_state.targetAngleOffset - m_state.currentAngleOffset) * smoothFactor;
    m_state.currentLengthFactor += (m_state.targetLengthFactor - m_state.currentLengthFactor) * smoothFactor;

    // 计算偏移
    QPointF offset(0.0, 0.0);
    int pressedNum = 0;

    if (m_state.pressedUp) {
        ++pressedNum;
        offset.ry() -= node.data.steerWheel.up.extendOffset * m_keyMap->getSteerWheelCoefficient(0);
    }
    if (m_state.pressedDown) {
        ++pressedNum;
        offset.ry() += node.data.steerWheel.down.extendOffset * m_keyMap->getSteerWheelCoefficient(1);
    }
    if (m_state.pressedLeft) {
        ++pressedNum;
        offset.rx() -= node.data.steerWheel.left.extendOffset * m_keyMap->getSteerWheelCoefficient(2);
    }
    if (m_state.pressedRight) {
        ++pressedNum;
        offset.rx() += node.data.steerWheel.right.extendOffset * m_keyMap->getSteerWheelCoefficient(3);
    }

    // 对角方向时应用角度偏移
    if (pressedNum > 1 && (offset.x() != 0 || offset.y() != 0)) {
        double cosA = std::cos(m_state.currentAngleOffset);
        double sinA = std::sin(m_state.currentAngleOffset);
        QPointF rotatedOffset(
            offset.x() * cosA - offset.y() * sinA,
            offset.x() * sinA + offset.y() * cosA
        );
        offset = rotatedOffset;
    }

    // 应用长度系数
    offset *= m_state.currentLengthFactor;

    m_state.delayData.timer->stop();
    m_state.delayData.queueTimer.clear();
    m_state.delayData.queuePos.clear();

    // 如果还没开始触摸，先按下（应用随机偏移）
    if (m_state.fastTouchSeqId == 0) {
        QSize targetSize = getTargetSize(m_frameSize, m_showSize);
        QPointF randomCenterPos = applyRandomOffset(node.data.steerWheel.centerPos, targetSize);
        m_state.fastTouchSeqId = FastTouchSeq::next();
        m_state.delayData.currentPos = randomCenterPos;
        sendFastTouch(FTA_DOWN, randomCenterPos);

        getDelayQueue(randomCenterPos, randomCenterPos + offset,
                      0.01, 0.002, 2, 8,
                      m_state.delayData.queuePos,
                      m_state.delayData.queueTimer);
    } else {
        getDelayQueue(m_state.delayData.currentPos, node.data.steerWheel.centerPos + offset,
                      0.01, 0.002, 2, 8,
                      m_state.delayData.queuePos,
                      m_state.delayData.queueTimer);
    }

    if (!m_state.delayData.queuePos.empty()) {
        m_state.delayData.timer->start();
    }

    // 所有按键都松开，停止间隙波动定时器
    if (currentState == 0) {
        m_state.humanizeTimer->stop();
    }
}

void SteerWheelHandler::sendFastTouch(quint8 action, const QPointF& pos)
{
    if (!m_controller) return;

    quint16 nx = static_cast<quint16>(qBound(0.0, pos.x(), 1.0) * 65535);
    quint16 ny = static_cast<quint16>(qBound(0.0, pos.y(), 1.0) * 65535);

    // 栈缓冲区序列化，避免堆分配
    char buf[10];
    FastTouchEvent evt(m_state.fastTouchSeqId, action, nx, ny);
    int len = FastMsg::serializeTouchInto(buf, evt);
    m_controller->postFastMsg(buf, len);
}

void SteerWheelHandler::getDelayQueue(const QPointF& start, const QPointF& end,
                                      double distanceStep, double posStep,
                                      quint32 lowestTimer, quint32 highestTimer,
                                      QQueue<QPointF>& queuePos, QQueue<quint32>& queueTimer)
{
    Q_UNUSED(posStep)

    // 获取平滑度和曲线设置
    int smoothLevel = qsc::ConfigCenter::instance().steerWheelSmooth();
    int curveLevel = qsc::ConfigCenter::instance().steerWheelCurve();

    double x1 = start.x();
    double y1 = start.y();
    double x2 = end.x();
    double y2 = end.y();

    double dx = x2 - x1;
    double dy = y2 - y1;
    double distance = std::sqrt(dx * dx + dy * dy);

    if (distance < 0.0001) {
        queuePos.enqueue(end);
        queueTimer.enqueue(lowestTimer);
        return;
    }

    // 根据平滑度计算步数：平滑度越高，步数越多
    double smoothMultiplier = 1.0 + (smoothLevel / 100.0) * 4.0;
    double adjustedDistanceStep = distanceStep / smoothMultiplier;

    int steps = static_cast<int>(distance / adjustedDistanceStep);
    if (steps < 1) steps = 1;

    // 垂直方向向量（用于曲线偏移）
    double perpX = -dy / distance;
    double perpY = dx / distance;

    // ===== 多频率曲线叠加，模拟真实人手的多曲度轨迹 =====

    // 主曲线（1个周期）：整体弧度方向，幅度最大
    double mainDirection = (QRandomGenerator::global()->bounded(2) == 0) ? 1.0 : -1.0;
    double mainAmplitude = (curveLevel / 100.0) * 0.2 * distance;

    // 次级波动（1.5-2.5个周期）：中间的起伏变化
    double secondFreq = 1.5 + QRandomGenerator::global()->generateDouble();  // 1.5 ~ 2.5
    double secondDirection = (QRandomGenerator::global()->bounded(2) == 0) ? 1.0 : -1.0;
    double secondAmplitude = (curveLevel / 100.0) * 0.08 * distance;

    // 微小波动（3-5个周期）：细微的不规则抖动
    double microFreq = 3.0 + QRandomGenerator::global()->generateDouble() * 2.0;  // 3 ~ 5
    double microDirection = (QRandomGenerator::global()->bounded(2) == 0) ? 1.0 : -1.0;
    double microAmplitude = (curveLevel / 100.0) * 0.03 * distance;

    // 随机相位偏移，让每次轨迹不同
    double mainPhase = QRandomGenerator::global()->generateDouble() * 0.2;      // 0 ~ 0.2
    double secondPhase = QRandomGenerator::global()->generateDouble() * M_PI;   // 0 ~ π
    double microPhase = QRandomGenerator::global()->generateDouble() * M_PI * 2; // 0 ~ 2π

    // 计算延迟时间（基于平滑度）
    quint32 baseDelay = (lowestTimer + highestTimer) / 2;
    quint32 stepDelay = static_cast<quint32>(baseDelay * (1.0 + smoothLevel / 50.0));

    for (int i = 1; i <= steps; i++) {
        double t = static_cast<double>(i) / steps;

        // 线性插值基础位置
        double baseX = x1 + dx * t;
        double baseY = y1 + dy * t;

        // 多频率曲线叠加
        // 主曲线：sin(π*(t+phase))，在中间达到最大，首尾归零
        double mainOffset = std::sin(M_PI * (t + mainPhase)) * mainAmplitude * mainDirection;
        // 首尾衰减，确保起点终点在直线上
        mainOffset *= std::sin(M_PI * t);

        // 次级波动：添加中间的起伏变化
        double secondOffset = std::sin(secondFreq * M_PI * t + secondPhase) * secondAmplitude * secondDirection;
        // 首尾衰减
        secondOffset *= std::sin(M_PI * t);

        // 微小波动：细微的不规则抖动
        double microOffset = std::sin(microFreq * M_PI * t + microPhase) * microAmplitude * microDirection;
        // 首尾衰减
        microOffset *= std::sin(M_PI * t);

        // 合并所有曲线偏移
        double totalOffset = mainOffset + secondOffset + microOffset;

        // 应用曲线偏移
        double finalX = baseX + perpX * totalOffset;
        double finalY = baseY + perpY * totalOffset;

        queuePos.enqueue(QPointF(finalX, finalY));
        queueTimer.enqueue(stepDelay);
    }
}
