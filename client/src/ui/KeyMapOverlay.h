#ifndef KEYMAPOVERLAY_H
#define KEYMAPOVERLAY_H

#include <QWidget>
#include <QList>
#include <QHash>
#include <QSet>
#include <QPainter>

// ---------------------------------------------------------
// KeyMapOverlay - 键位提示覆盖层 / Key Map Hint Overlay
// 在视频流上半透明显示已配置的键位位置和热键
// Semi-transparent overlay on video stream showing configured key positions and hotkeys
// ---------------------------------------------------------
class KeyMapOverlay : public QWidget
{
    Q_OBJECT
public:
    // 键位信息结构
    struct KeyInfo {
        QString label;      // 热键标签 (如 "W", "Space", "LMB")
        QString type;       // 类型 (click, steerWheel, camera 等)
        QPointF pos;        // 归一化位置 (0-1)
        QSizeF size;        // 归一化大小 (可选，用于轮盘等)
        QList<KeyInfo> subKeys;  // 子按键 (用于轮盘的 WASD)
    };

    explicit KeyMapOverlay(QWidget *parent = nullptr);
    ~KeyMapOverlay() override = default;

    void setKeyInfos(const QList<KeyInfo>& infos);
    void clear();

    void setOpacity(qreal opacity);  // 0.0 - 1.0
    qreal opacity() const { return m_opacity; }

    // 设置指定热键的 UI 位置覆盖 (用于脚本动态更新)
    // 如果 x=0, y=0 则清除覆盖，恢复原位置
    // 如果 x=-1 则隐藏该按键 UI
    static void setKeyPosOverride(const QString& keyName, double x, double y);
    static QPointF getKeyPosOverride(const QString& keyName);
    static bool isKeyHidden(const QString& keyName);  // 检查按键是否被隐藏
    static void clearAllOverrides();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void drawKeyItem(QPainter& painter, const KeyInfo& info, const QSize& widgetSize);
    void drawClickKey(QPainter& painter, const KeyInfo& info, const QPoint& center);
    void drawSteerWheel(QPainter& painter, const KeyInfo& info, const QPoint& center, int radius);
    void drawCameraKey(QPainter& painter, const KeyInfo& info, const QPoint& center);
    void drawFreeLookKey(QPainter& painter, const KeyInfo& info, const QPoint& center);

private:
    QList<KeyInfo> m_keyInfos;
    qreal m_opacity = 0.6;

    // 静态位置覆盖映射 (key: 按键名称, value: 覆盖位置)
    static QHash<QString, QPointF> s_posOverrides;
    // 被隐藏的按键列表
    static QSet<QString> s_hiddenKeys;
};

#endif // KEYMAPOVERLAY_H#endif // KEYMAPOVERLAY_H
