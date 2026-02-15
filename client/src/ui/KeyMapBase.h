#ifndef KEYMAPBASE_H
#define KEYMAPBASE_H

#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QJsonObject>
#include <QSharedPointer>
#include <QKeyEvent>
#include <QGraphicsSceneMouseEvent>

// ---------------------------------------------------------
// 键位类型枚举 / Key Map Type Enumeration
// ---------------------------------------------------------
enum KeyMapType {
    KMT_INVALID = -1,
    KMT_STEER_WHEEL = 2,    // 轮盘（方向控制）/ Steer wheel (direction control)
    KMT_SCRIPT = 10,        // 脚本宏 / Script macro
    KMT_CAMERA_MOVE = 20,   // 视角控制（鼠标移动映射）/ Camera control (mouse move mapping)
    KMT_FREE_LOOK = 21,     // 小眼睛自由视角 / Free-look (eye icon)
};

// ---------------------------------------------------------
// 可视化键位基类 / Visual Key Map Item Base Class
// 所有具体键位（如轮盘、点击）都继承自此类。
// All specific key map items (steer wheel, click, etc.) inherit from this.
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

    // 关闭按钮：在编辑模式下右上角绘制 X 按钮
    static void drawCloseButton(QPainter* painter, const QRectF& itemRect) {
        QRectF closeRect = closeButtonRect(itemRect);
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(220, 38, 38, 200));
        painter->drawEllipse(closeRect);
        painter->setPen(QPen(Qt::white, 1.5));
        double cx = closeRect.center().x(), cy = closeRect.center().y(), r = 3;
        painter->drawLine(QPointF(cx - r, cy - r), QPointF(cx + r, cy + r));
        painter->drawLine(QPointF(cx + r, cy - r), QPointF(cx - r, cy + r));
        painter->restore();
    }

    static QRectF closeButtonRect(const QRectF& itemRect) {
        return QRectF(itemRect.right() - 12, itemRect.top(), 12, 12);
    }

    // 检测点击是否命中关闭按钮，命中则从场景中移除自身
    bool handleCloseButtonClick(const QPointF& localPos) {
        QRectF cr = closeButtonRect(boundingRect());
        if (cr.contains(localPos) && scene()) {
            scene()->removeItem(this);
            deleteLater();
            return true;
        }
        return false;
    }

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
    // 拖拽边界约束：防止键位被拖到视频窗口外消失
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
        if (change == ItemPositionChange && scene()) {
            QRectF sr = scene()->sceneRect();
            if (!sr.isEmpty()) {
                QPointF newPos = value.toPointF();
                QRectF br = boundingRect();
                // 确保键位中心 ± 半径不超出场景边界
                double margin = qMin(br.width(), br.height()) / 2;
                newPos.setX(qBound(sr.left() - margin, newPos.x(), sr.right() + margin));
                newPos.setY(qBound(sr.top() - margin, newPos.y(), sr.bottom() + margin));
                return newPos;
            }
        }
        return QGraphicsObject::itemChange(change, value);
    }

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
