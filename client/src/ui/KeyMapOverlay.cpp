#include "KeyMapOverlay.h"
#include "KeyMapItems.h"
#include <QPainterPath>

// 静态成员变量定义
QHash<QString, QPointF> KeyMapOverlay::s_posOverrides;
QSet<QString> KeyMapOverlay::s_hiddenKeys;  // 被隐藏的按键

void KeyMapOverlay::setKeyPosOverride(const QString& keyName, double x, double y)
{
    if (x == -1) {
        // x=-1 表示隐藏该按键 UI
        s_hiddenKeys.insert(keyName);
        s_posOverrides.remove(keyName);
    } else if (x == 0.0 && y == 0.0) {
        // 清除覆盖，恢复原位置，同时取消隐藏
        s_posOverrides.remove(keyName);
        s_hiddenKeys.remove(keyName);
    } else {
        // 设置新位置，同时取消隐藏
        s_posOverrides[keyName] = QPointF(x, y);
        s_hiddenKeys.remove(keyName);
    }
}

bool KeyMapOverlay::isKeyHidden(const QString& keyName)
{
    return s_hiddenKeys.contains(keyName);
}

QPointF KeyMapOverlay::getKeyPosOverride(const QString& keyName)
{
    return s_posOverrides.value(keyName, QPointF(-1, -1));
}

void KeyMapOverlay::clearAllOverrides()
{
    s_posOverrides.clear();
    s_hiddenKeys.clear();
}

KeyMapOverlay::KeyMapOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);  // 鼠标事件穿透
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet("background: transparent;");
}

void KeyMapOverlay::setKeyInfos(const QList<KeyInfo>& infos)
{
    m_keyInfos = infos;
    update();
}

void KeyMapOverlay::clear()
{
    m_keyInfos.clear();
    update();
}

void KeyMapOverlay::setOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
    update();
}

void KeyMapOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    if (m_keyInfos.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(m_opacity);

    QSize sz = size();
    for (const auto& info : m_keyInfos) {
        drawKeyItem(painter, info, sz);
    }
}

void KeyMapOverlay::drawKeyItem(QPainter& painter, const KeyInfo& info, const QSize& widgetSize)
{
    // 用显示名称来匹配（与 getkeypos/setKeyUIPos 使用相同格式）
    QString displayName = KeyMapHelper::keyToDisplay(info.label);

    // 检查按键是否被隐藏
    if (s_hiddenKeys.contains(displayName)) {
        return;  // 不绘制隐藏的按键
    }

    // 检查是否有位置覆盖
    QPointF pos = info.pos;
    QPointF override = s_posOverrides.value(displayName, QPointF(-1, -1));
    if (override.x() >= 0 && override.y() >= 0) {
        pos = override;
    }

    QPoint center(
        static_cast<int>(pos.x() * widgetSize.width()),
        static_cast<int>(pos.y() * widgetSize.height())
    );

    if (info.type == "steerWheel") {
        int radius = static_cast<int>(info.size.width() * widgetSize.width() * 0.5);
        if (radius < 30) radius = 50;
        drawSteerWheel(painter, info, center, radius);
    } else if (info.type == "mouseMove" || info.type == "camera") {
        drawCameraKey(painter, info, center);
    } else if (info.type == "freeLook") {
        drawFreeLookKey(painter, info, center);
    } else {
        // click, longPress, script 等普通按键
        drawClickKey(painter, info, center);
    }
}

void KeyMapOverlay::drawClickKey(QPainter& painter, const KeyInfo& info, const QPoint& center)
{
    int radius = 22;

    // 背景圆
    painter.setBrush(QColor(30, 30, 30, 180));
    painter.setPen(QPen(QColor(100, 100, 100), 2));
    painter.drawEllipse(center, radius, radius);

    // 热键标签（使用 keyToDisplay 转换显示）
    QString displayText = KeyMapHelper::keyToDisplay(info.label);

    painter.setPen(QColor(255, 255, 255));
    QFont font = painter.font();
    font.setBold(true);

    // 根据文字长度自适应字体大小
    int fontSize = 12;
    if (displayText.length() > 6) fontSize = 8;
    else if (displayText.length() > 3) fontSize = 10;
    font.setPixelSize(fontSize);
    painter.setFont(font);

    QRect textRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    painter.drawText(textRect, Qt::AlignCenter, displayText);
}

void KeyMapOverlay::drawSteerWheel(QPainter& painter, const KeyInfo& info, const QPoint& center, int radius)
{
    // 外圈
    painter.setBrush(QColor(30, 30, 30, 120));
    painter.setPen(QPen(QColor(80, 80, 80), 2));
    painter.drawEllipse(center, radius, radius);

    // 内圈指示器
    int innerRadius = radius / 3;
    painter.setBrush(QColor(60, 60, 60, 150));
    painter.setPen(QPen(QColor(120, 120, 120), 2));
    painter.drawEllipse(center, innerRadius, innerRadius);

    // 绘制 WASD 子按键
    QFont font = painter.font();
    font.setPixelSize(14);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255));

    int offset = radius - 20;

    // 默认按键（使用 keyToDisplay 转换显示）
    QString upKey = "W";
    QString downKey = "S";
    QString leftKey = "A";
    QString rightKey = "D";

    // 从 subKeys 获取实际热键并转换为显示格式
    for (const auto& sub : info.subKeys) {
        if (sub.type == "up") upKey = KeyMapHelper::keyToDisplay(sub.label);
        else if (sub.type == "down") downKey = KeyMapHelper::keyToDisplay(sub.label);
        else if (sub.type == "left") leftKey = KeyMapHelper::keyToDisplay(sub.label);
        else if (sub.type == "right") rightKey = KeyMapHelper::keyToDisplay(sub.label);
    }

    QRect upRect(center.x() - 15, center.y() - offset - 10, 30, 20);
    QRect downRect(center.x() - 15, center.y() + offset - 10, 30, 20);
    QRect leftRect(center.x() - offset - 15, center.y() - 10, 30, 20);
    QRect rightRect(center.x() + offset - 15, center.y() - 10, 30, 20);

    painter.drawText(upRect, Qt::AlignCenter, upKey);
    painter.drawText(downRect, Qt::AlignCenter, downKey);
    painter.drawText(leftRect, Qt::AlignCenter, leftKey);
    painter.drawText(rightRect, Qt::AlignCenter, rightKey);
}

void KeyMapOverlay::drawCameraKey(QPainter& painter, const KeyInfo& info, const QPoint& center)
{
    int radius = 35;

    // 椭圆背景
    painter.setBrush(QColor(30, 30, 30, 150));
    painter.setPen(QPen(QColor(100, 149, 237), 2));  // 蓝色边框
    painter.drawEllipse(center, radius + 10, radius);

    // 标签
    painter.setPen(QColor(255, 255, 255));
    QFont font = painter.font();
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);

    QString label = info.label.isEmpty() ? "视角" : info.label;
    QRect textRect(center.x() - radius - 10, center.y() - radius, (radius + 10) * 2, radius * 2);
    painter.drawText(textRect, Qt::AlignCenter, label);
}

void KeyMapOverlay::drawFreeLookKey(QPainter& painter, const KeyInfo& info, const QPoint& center)
{
    int radiusX = 40;
    int radiusY = 30;

    // 椭圆背景
    painter.setBrush(QColor(30, 30, 30, 150));
    painter.setPen(QPen(QColor(255, 165, 0), 2));  // 橙色边框
    painter.drawEllipse(center, radiusX, radiusY);

    // 热键标签（使用 keyToDisplay 转换显示）
    QString displayText = KeyMapHelper::keyToDisplay(info.label);
    if (displayText.isEmpty()) displayText = "Alt";

    painter.setPen(QColor(255, 255, 255));
    QFont font = painter.font();
    font.setPixelSize(12);
    font.setBold(true);
    painter.setFont(font);

    QRect textRect(center.x() - radiusX, center.y() - radiusY, radiusX * 2, radiusY * 2);
    painter.drawText(textRect, Qt::AlignCenter, displayText);
}
