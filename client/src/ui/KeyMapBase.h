#ifndef KEYMAPBASE_H
#define KEYMAPBASE_H

#include <QGraphicsObject>
#include <QJsonObject>
#include <QSharedPointer>
#include <QKeyEvent>
#include <QGraphicsSceneMouseEvent>

// ---------------------------------------------------------
// 键位类型枚举定义
// ---------------------------------------------------------
enum KeyMapType {
    KMT_INVALID = -1,
    KMT_STEER_WHEEL = 2,    // 轮盘（方向控制）
    KMT_SCRIPT = 10,        // 脚本宏
    KMT_CAMERA_MOVE = 20,   // 视角控制（鼠标移动映射）
};

// ---------------------------------------------------------
// 可视化键位基类
// 所有的具体键位（如轮盘、点击）都继承自此类
// ---------------------------------------------------------
class KeyMapItemBase : public QGraphicsObject
{
    Q_OBJECT
public:
    KeyMapItemBase(QGraphicsItem *parent = nullptr) : QGraphicsObject(parent) {
        // 设置图形项属性：可移动、可选中、几何变化发送通知
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    }

    virtual ~KeyMapItemBase() {}

    // 纯虚函数：序列化与反序列化，子类必须实现
    virtual QJsonObject toJson() const = 0;
    virtual void fromJson(const QJsonObject& json) = 0;
    virtual KeyMapType typeId() const = 0;

    // 设置/获取按键冲突状态（冲突时通常显示红色）
    virtual void setConflicted(bool conflicted) {
        if (m_isConflicted != conflicted) {
            m_isConflicted = conflicted;
            update(); // 触发重绘
        }
    }
    bool isConflicted() const { return m_isConflicted; }

    // 获取当前绑定的按键名称
    virtual QString getKey() const { return m_key; }

    // ---------------------------------------------------------
    // 坐标归一化处理
    // 将场景坐标转换为相对比例 (0.0 - 1.0)，适配不同分辨率的设备
    // ---------------------------------------------------------
    QPointF getNormalizedPos(const QSizeF& videoSize) const {
        if (videoSize.isEmpty()) return QPointF(0, 0);
        QPointF center = scenePos();
        return QPointF(center.x() / videoSize.width(), center.y() / videoSize.height());
    }

    // 根据相对比例设置实际坐标
    void setNormalizedPos(const QPointF& ratio, const QSizeF& videoSize) {
        setPos(ratio.x() * videoSize.width(), ratio.y() * videoSize.height());
    }

protected:
    QString m_comment;      // 注释说明
    QString m_key;          // 绑定的按键
    bool m_isConflicted = false; // 是否冲突
};

// ---------------------------------------------------------
// 键位工厂接口
// 用于创建特定类型的键位实例
// ---------------------------------------------------------
class KeyMapFactory
{
public:
    virtual KeyMapItemBase* createItem(KeyMapType type) = 0;
    virtual ~KeyMapFactory() {}
};

#endif // KEYMAPBASE_H
