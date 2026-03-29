#ifndef KEYMAPITEMS_H
#define KEYMAPITEMS_H

#include "KeyMapBase.h"
#include "scripteditordialog.h"
#include "videoform.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include <QPainter>
#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QTimer>
#include <QKeySequence>
#include <QGraphicsSceneMouseEvent>
#include <QtMath>
#include <QDebug>
#include <cmath>
#include <QMetaEnum>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include "FluentComboBox.h"
#include <QTextEdit>
#include <QPushButton>
#include <QInputDialog>
#include <QCheckBox>
#include <QPointer>
#include <QSlider>
#include <QGroupBox>
#include <QGridLayout>
#include <QDoubleSpinBox>

class KeyMapItemCamera; // forward declaration

// ---------------------------------------------------------
// 视角控制区域矩形 (可视化拖拽调整)
// 始终以视角控制组件为中心，仅支持四角对称缩放
// ---------------------------------------------------------
class KeyMapAreaRect : public QGraphicsObject
{
    Q_OBJECT
public:
    KeyMapAreaRect(KeyMapItemCamera* owner, QGraphicsItem* parent = nullptr);

    void setNormalizedSize(double w, double h);
    void syncToScene();   // 根据 owner 位置和场景大小更新像素位置/尺寸

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    enum DragMode { None, TopLeft, TopRight, BottomLeft, BottomRight };
    DragMode hitTest(const QPointF& localPos) const;
    void commitToOwner();

    KeyMapItemCamera* m_owner;
    double m_nw, m_nh;              // 归一化宽高（以 owner 为中心）
    qreal m_pixW, m_pixH;           // 当前场景像素尺寸
    DragMode m_dragMode = None;
    QPointF m_dragStart;
    qreal m_dragStartW, m_dragStartH;
    static constexpr qreal HANDLE = 8.0;
};

// ---------------------------------------------------------
// 辅助工具类：类型与字符串的转换 / Helper: Type-String Conversion
// ---------------------------------------------------------
class KeyMapHelper {
public:
    static KeyMapType getTypeFromString(const QString& typeStr) {
        if (typeStr == "KMT_STEER_WHEEL") return KMT_STEER_WHEEL;
        if (typeStr == "KMT_SCRIPT") return KMT_SCRIPT;
        if (typeStr == "KMT_CAMERA_MOVE") return KMT_CAMERA_MOVE;
        if (typeStr == "KMT_FREE_LOOK") return KMT_FREE_LOOK;
        return KMT_INVALID;
    }

    static QString getStringFromType(KeyMapType type) {
        switch(type) {
        case KMT_STEER_WHEEL: return "KMT_STEER_WHEEL";
        case KMT_SCRIPT: return "KMT_SCRIPT";
        case KMT_CAMERA_MOVE: return "KMT_CAMERA_MOVE";
        case KMT_FREE_LOOK: return "KMT_FREE_LOOK";
        default: return "KMT_INVALID";
        }
    }

    // 将按键转换为存储用的字符串（支持修饰键）
    static QString keyToString(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        // 修饰键单独处理
        switch (key) {
        case Qt::Key_Control: return "Key_Control";
        case Qt::Key_Shift: return "Key_Shift";
        case Qt::Key_Alt: return "Key_Alt";
        case Qt::Key_Meta: return "Key_Meta";
        }
        // 普通按键
        if (modifiers != Qt::NoModifier) {
            return QKeySequence(key | modifiers).toString(QKeySequence::PortableText);
        }
        // 优先使用 QKeySequence 短名格式（"K", "Tab", "Up"），
        // 保持与现有 JSON 配置一致，避免混入 "Key_K" 格式
        QString ks = QKeySequence(key).toString();
        if (!ks.isEmpty()) return ks;
        // Fallback: 使用 QMetaEnum（特殊键如 QuoteLeft 等）
        QMetaEnum m = QMetaEnum::fromType<Qt::Key>();
        const char* s = m.valueToKey(key);
        return s ? QString(s) : QString::number(key);
    }

    // 将按键字符串转换为显示用的字符串（符号化）
    static QString keyToDisplay(const QString& keyStr) {
        QString t = keyStr;
        if (t.startsWith("Key_")) t = t.mid(4);
        // 鼠标按键简化
        if (t == "LeftButton" || t == "Left") return "LMB";
        if (t == "RightButton" || t == "Right") return "RMB";
        if (t == "MiddleButton" || t == "Middle") return "MMB";
        if (t == "SideButton1" || t == "XButton1") return "MB4";
        if (t == "SideButton2" || t == "XButton2") return "MB5";
        // 滚轮
        if (t == "WheelUp") return "滚上";
        if (t == "WheelDown") return "滚下";
        // 符号替换 - 注意区分主键盘和小键盘
        if (t == "Equal") return "=";           // 主键盘 = 键
        if (t == "Plus") return "+";            // 小键盘 + 键
        if (t == "Minus") return "-";
        if (t == "Asterisk") return "*";
        if (t == "Slash") return "/";
        if (t == "QuoteLeft") return "`";       // 反引号
        if (t == "AsciiTilde") return "~";      // 波浪号
        if (t == "Backslash") return "\\";
        if (t == "BracketLeft") return "[";
        if (t == "BracketRight") return "]";
        if (t == "Semicolon") return ";";
        if (t == "Apostrophe") return "'";
        if (t == "Comma") return ",";
        if (t == "Period") return ".";
        if (t == "Space") return "Space";
        if (t == "Tab") return "Tab";
        if (t == "Return" || t == "Enter") return "Enter";
        if (t == "Backspace") return "Backspace";
        if (t == "Escape") return "Esc";
        if (t == "Control") return "Ctrl";
        if (t == "Alt") return "Alt";
        if (t == "Shift") return "Shift";
        if (t == "Meta") return "Win";
        return t;
    }
};

class KeyMapItemSteerWheel;

// ---------------------------------------------------------
// 轮盘子项类 (SteerWheelSubItem)
// 代表WASD方向键中的某一个具体方向按钮
// ---------------------------------------------------------
class SteerWheelSubItem : public QGraphicsObject
{
    Q_OBJECT
public:
    enum Direction { Dir_Up, Dir_Down, Dir_Left, Dir_Right };
    SteerWheelSubItem(Direction dir, KeyMapItemSteerWheel* parentWheel);
    QRectF boundingRect() const override { return QRectF(-15, -15, 30, 30); }

    // 按键绑定与获取
    void setKey(const QString& key) { m_key = key; update(); }
    QString getKey() const { return m_key; }

    // 编辑状态控制
    void setEditing(bool edit);
    bool isEditing() const { return m_isEditing; }

    // 输入事件处理
    void inputKey(QKeyEvent* event);
    void inputMouse(Qt::MouseButton button);
    void inputWheel(int delta);

    void setConflicted(bool conflicted) { if (m_isConflicted != conflicted) { m_isConflicted = conflicted; update(); } }
protected:
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override;
private:
    Direction m_dir; KeyMapItemSteerWheel* m_parentWheel; QString m_key;
    bool m_isEditing = false; bool m_showCursor = false; bool m_isConflicted = false;
    QString m_displayKey; QTimer* m_cursorTimer;
};

// ---------------------------------------------------------
// 轮盘主项类 (KeyMapItemSteerWheel)
// 包含四个方向子项，管理整体轮盘逻辑
// ---------------------------------------------------------
class KeyMapItemSteerWheel : public KeyMapItemBase
{
    Q_OBJECT
public:
    friend class SteerWheelSubItem;
    KeyMapItemSteerWheel(QGraphicsItem *parent = nullptr);
    KeyMapType typeId() const override { return KMT_STEER_WHEEL; }

    // 设置四个方向的按键
    void setKeys(const QString& u, const QString& d, const QString& l, const QString& r);
    // 设置四个方向的偏移量（控制轮盘大小范围）
    void setOffsets(double u, double d, double l, double r);
    // 根据子项位置更新偏移量
    void updateOffsetFromSubItem(SteerWheelSubItem::Direction dir, const QPointF& localPos);
    void updateSubItemsPos();

    // 获取子项信息
    SteerWheelSubItem* getSubItemAt(const QPointF& pos);
    QString getUpKey() const { return m_subUp->getKey(); }
    QString getDownKey() const { return m_subDown->getKey(); }
    QString getLeftKey() const { return m_subLeft->getKey(); }
    QString getRightKey() const { return m_subRight->getKey(); }

    // 设置子项冲突状态
    void setSubItemConflicted(int dir, bool conflicted) {
        if(dir==0) m_subUp->setConflicted(conflicted); if(dir==1) m_subDown->setConflicted(conflicted);
        if(dir==2) m_subLeft->setConflicted(conflicted); if(dir==3) m_subRight->setConflicted(conflicted);
    }

    // 速度倍率
    double speedMultiplier() const { return m_speedMultiplier; }
    void setSpeedMultiplier(double v) { m_speedMultiplier = v; }

    // 序列化支持
    nlohmann::json toJson() const override; void fromJson(const nlohmann::json& json) override;
    void resize(qreal w, qreal h);
    void openSpeedDialog();
protected:
    QRectF boundingRect() const override { return m_rect; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
    QPainterPath shape() const override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            // 关闭按钮
            QRectF closeRect(-4, -18, 12, 12);
            if (closeRect.contains(event->pos()) && scene()) {
                QGraphicsScene* s = scene();
                QTimer::singleShot(0, s, [this, s]() {
                    if (s->items().contains(this)) {
                        s->removeItem(this);
                        deleteLater();
                    }
                });
                event->accept();
                return;
            }
            // 齿轮按钮 (中心上方偏右)
            QRectF gearRect(8, -18, 12, 12);
            if (gearRect.contains(event->pos())) {
                openSpeedDialog();
                event->accept();
                return;
            }
        }
        KeyMapItemBase::mousePressEvent(event);
    }
private:
    QRectF m_rect; SteerWheelSubItem* m_subUp; SteerWheelSubItem* m_subDown;
    SteerWheelSubItem* m_subLeft; SteerWheelSubItem* m_subRight;
    double m_leftOffset = 0.15; double m_rightOffset = 0.15; double m_upOffset = 0.15; double m_downOffset = 0.15;
    double m_speedMultiplier = 1.0;  // 轮盘速度倍率
};

// ---------------------------------------------------------
// 脚本键位类 (KeyMapItemScript)
// 支持一键执行预设的宏脚本
// ---------------------------------------------------------
class KeyMapItemScript : public KeyMapItemBase
{
    Q_OBJECT
public:
    KeyMapItemScript(QGraphicsItem *parent = nullptr) : KeyMapItemBase(parent) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        m_key = "";  // 默认为空，需要用户设置
        m_comment = "Script";

        // 光标闪烁定时器，用于编辑模式
        m_cursorTimer = new QTimer(this);
        m_cursorTimer->setInterval(600);
        connect(m_cursorTimer, &QTimer::timeout, this, [this](){ m_showCursor = !m_showCursor; update(); });
    }

    KeyMapType typeId() const override { return KMT_SCRIPT; }
    QString getScript() const { return m_script; }

    // 进入/退出按键编辑模式
    void setEditing(bool edit) {
        if (m_isEditing == edit) return;
        m_isEditing = edit;
        if (m_isEditing) {
            m_displayKey = "";
            m_showCursor = true;
            m_cursorTimer->start();
            setSelected(true);
        } else {
            m_cursorTimer->stop();
            m_showCursor = false;
        }
        update();
    }
    bool isEditing() const { return m_isEditing; }

    // 处理按键录入（支持修饰键）
    void inputKey(QKeyEvent* event) {
        int key = event->key();
        if (key == Qt::Key_unknown) return;
        m_key = KeyMapHelper::keyToString(key, event->modifiers());
        m_displayKey = m_key;
        update();
    }

    // 处理鼠标按键录入
    void inputMouse(Qt::MouseButton button) {
        QString keyName;
        switch (button) {
        case Qt::LeftButton: keyName = "LeftButton"; break;
        case Qt::RightButton: keyName = "RightButton"; break;
        case Qt::MiddleButton: keyName = "MiddleButton"; break;
        case Qt::XButton1: keyName = "SideButton1"; break;
        case Qt::XButton2: keyName = "SideButton2"; break;
        default: return;
        }
        if (!keyName.isEmpty()) { m_key = keyName; m_displayKey = keyName; update(); }
    }

    // 处理滚轮录入
    void inputWheel(int delta) {
        QString keyName = (delta > 0) ? "WheelUp" : "WheelDown";
        m_key = keyName;
        m_displayKey = keyName;
        update();
    }

    // JSON序列化
    nlohmann::json toJson() const override {
        nlohmann::json json;
        json["type"] = "KMT_SCRIPT";
        QPointF r = getNormalizedPos(scene()?scene()->sceneRect().size():QSizeF(1,1));
        json["pos"] = {{"x", std::round(r.x()*10000.0)/10000.0}, {"y", std::round(r.y()*10000.0)/10000.0}};
        json["key"] = m_key.toStdString();
        json["script"] = m_script.toStdString();
        return json;
    }

    void fromJson(const nlohmann::json& json) override {
        if (json.contains("key")) m_key = QString::fromStdString(json["key"].get<std::string>());
        if (json.contains("script")) m_script = QString::fromStdString(json["script"].get<std::string>());
    }

protected:
    QRectF boundingRect() const override { return QRectF(-25, -25, 50, 50); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override {
        Q_UNUSED(option); Q_UNUSED(widget);
        painter->setRenderHint(QPainter::Antialiasing);

        auto& tm = Fluent::ThemeManager::instance();
        // Fluent Focus 风格配色
        QColor bg = m_isConflicted ? QColor(Fluent::Accent::Error).darker(110) :
                        (m_isEditing ? QColor(tm.card()) :
                             (isSelected() ? QColor(tm.accentPrimary()) : QColor(tm.surface())));
        bg.setAlpha(220);

        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawEllipse(boundingRect());

        // 边框：accent 光晕
        QColor borderColor = m_isConflicted ? QColor(Fluent::Accent::Error) :
                             (m_isEditing ? QColor(tm.accentPrimary()) :
                                  (isSelected() ? QColor(Qt::white) : QColor(tm.border())));
        painter->setPen(QPen(borderColor, m_isEditing ? 2 : 1.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(boundingRect());

        // 文字
        painter->setPen(QColor(tm.textPrimary()));
        QFont font = painter->font();
        font.setBold(true);

        QString t = m_isEditing ? (KeyMapHelper::keyToDisplay(m_displayKey) + (m_showCursor?"|":"")) : KeyMapHelper::keyToDisplay(m_key);

        // 根据文字长度自适应字体大小
        int fontSize = 10;
        if (t.length() > 6) fontSize = 7;
        else if (t.length() > 4) fontSize = 8;
        else if (t.length() > 2) fontSize = 9;
        font.setPointSize(fontSize);
        painter->setFont(font);

        painter->drawText(boundingRect().adjusted(2, 2, -2, -2), Qt::AlignCenter | Qt::TextWordWrap, t.isEmpty()?"?":t);

        if (!m_isEditing) drawGear(painter);
        drawCloseButton(painter, boundingRect());
    }
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        if (m_isEditing) { event->accept(); return; }

        if (event->button() == Qt::LeftButton) {
            if (handleCloseButtonClick(event->pos())) { event->accept(); return; }
            QPointF p = event->pos();
            if (p.x() > 5 && p.y() > 5) {
                openScriptEditor();
                event->accept();
                return;
            }
        }
        KeyMapItemBase::mousePressEvent(event);
    }

private:
    // 绘制设置齿轮图标
    void drawGear(QPainter* painter) {
        auto& tm = Fluent::ThemeManager::instance();
        painter->save();
        painter->translate(14, 14);
        painter->setPen(QPen(QColor(tm.textSecondary()), 1.5));
        painter->setBrush(QColor(tm.surface()));
        painter->drawEllipse(QPoint(0,0), 6, 6);
        painter->setBrush(QColor(tm.textSecondary()));
        painter->drawEllipse(QPoint(0,0), 2, 2);
        for(int i=0; i<8; ++i) { painter->rotate(45); painter->drawLine(0, 6, 0, 8); }
        painter->restore();
    }

    // 打开脚本编辑对话框
    void openScriptEditor() {
        // 保护 this：dlg.exec() 会进入嵌套事件循环，期间用户可能删除本项
        QPointer<KeyMapItemScript> guard(this);
        // 保存位置：exec() 期间视频帧仍在到达，overlay sync 可能改变 sceneRect 和 item 位置
        QPointF savedPos = pos();

        ScriptEditorDialog dlg(m_script);

        // 尝试获取 VideoForm 以设置帧获取回调
        if (scene()) {
            QList<QGraphicsView*> views = scene()->views();
            if (!views.isEmpty()) {
                // 向上遍历父控件链查找 VideoForm
                QWidget* w = views.first()->parentWidget();
                while (w) {
                    VideoForm* videoForm = qobject_cast<VideoForm*>(w);
                    if (videoForm) {
                        dlg.setFrameGrabCallback([videoForm]() -> QImage {
                            return videoForm->grabCurrentFrame();
                        });
                        break;
                    }
                    w = w->parentWidget();
                }
            }
        }

        if (dlg.exec() == QDialog::Accepted) {
            // 对话框关闭后检查 this 是否仍然存在
            if (guard) {
                m_script = dlg.getScript();
            }
        }
        // 恢复位置（防止 exec 期间被 overlay sync 偏移）
        if (guard) {
            setPos(savedPos);
        }
    }

private:
    QString m_script;
    bool m_isEditing = false; bool m_showCursor = false;
    QString m_displayKey; QTimer* m_cursorTimer;
};

// ---------------------------------------------------------
// 视角控制键位类 (KeyMapItemCamera)
// 用于FPS游戏中的鼠标视角移动，支持X/Y轴灵敏度调节
// ---------------------------------------------------------
class KeyMapItemCamera : public KeyMapItemBase
{
    Q_OBJECT
public:
    KeyMapItemCamera(QGraphicsItem *parent = nullptr) : KeyMapItemBase(parent) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        m_key = "";  // 默认为空，需要用户设置
        m_comment = "Camera";

        m_cursorTimer = new QTimer(this);
        m_cursorTimer->setInterval(600);
        connect(m_cursorTimer, &QTimer::timeout, this, [this](){ m_showCursor = !m_showCursor; update(); });
    }

    KeyMapType typeId() const override { return KMT_CAMERA_MOVE; }

    enum EditMode { Edit_None, Edit_Key, Edit_X, Edit_Y };

    // 根据点击位置判断编辑模式：编辑按键、X轴灵敏度或Y轴灵敏度
    void startEditing(const QPointF& pos) {
        bool wasEditing = m_isEditing;
        m_isEditing = true;

        EditMode newMode = Edit_None;
        if (pos.x() < -20) newMode = Edit_X;
        else if (pos.x() > 20) newMode = Edit_Y;
        else newMode = Edit_Key;

        if (!wasEditing || m_editMode != newMode) {
            m_editMode = newMode;
            // 点击 XY 时清空 buffer，实现"全选"效果
            if (m_editMode == Edit_X) m_inputBuffer = "";
            else if (m_editMode == Edit_Y) m_inputBuffer = "";
            else m_displayKey = "";

            m_showCursor = true;
            if (!m_cursorTimer->isActive()) m_cursorTimer->start();
            setSelected(true);
            update();
        }
    }

    void setEditing(bool edit) {
        if (m_isEditing == edit) return;
        m_isEditing = edit;
        if (m_isEditing) {
            m_editMode = Edit_Key;
            m_displayKey = "";
            m_showCursor = true;
            m_cursorTimer->start();
            setSelected(true);
        } else {
            m_editMode = Edit_None;
            m_cursorTimer->stop();
            m_showCursor = false;
        }
        update();
    }
    bool isEditing() const { return m_isEditing; }

    // 处理键盘输入：区别处理绑定按键和输入数字（灵敏度）
    void inputKey(QKeyEvent* event) {
        if (m_editMode == Edit_Key) {
            int key = event->key();
            if (key == Qt::Key_unknown) return;
            m_key = KeyMapHelper::keyToString(key, event->modifiers());
            m_displayKey = m_key;
            update();
        } else if (m_editMode == Edit_X || m_editMode == Edit_Y) {
            if (event->key() == Qt::Key_Backspace) {
                if (!m_inputBuffer.isEmpty()) m_inputBuffer.chop(1);
            } else {
                QString text = event->text();
                if (!text.isEmpty() && (text.at(0).isDigit() || text.at(0) == '.')) {
                    m_inputBuffer.append(text);
                }
            }

            // 如果 buffer 为空，保持原值
            if (!m_inputBuffer.isEmpty()) {
            double val = m_inputBuffer.toDouble();
            if (m_editMode == Edit_X) m_speedX = val;
            else m_speedY = val;
            }
            update();
        }
    }

    void inputMouse(Qt::MouseButton button) {
        if (m_editMode != Edit_Key) return;

        QString keyName;
        switch (button) {
        case Qt::LeftButton: keyName = "LeftButton"; break;
        case Qt::RightButton: keyName = "RightButton"; break;
        case Qt::MiddleButton: keyName = "MiddleButton"; break;
        case Qt::XButton1: keyName = "SideButton1"; break;
        case Qt::XButton2: keyName = "SideButton2"; break;
        default: return;
        }
        if (!keyName.isEmpty()) { m_key = keyName; m_displayKey = keyName; update(); }
    }

    void inputWheel(int delta) {
        if (m_editMode != Edit_Key) return;
        QString keyName = (delta > 0) ? "WheelUp" : "WheelDown";
        m_key = keyName;
        m_displayKey = keyName;
        update();
    }

    nlohmann::json toJson() const override {
        nlohmann::json json;
        json["type"] = "KMT_CAMERA_MOVE";
        QPointF r = getNormalizedPos(scene()?scene()->sceneRect().size():QSizeF(1,1));
        json["pos"] = {{"x", std::round(r.x()*10000.0)/10000.0}, {"y", std::round(r.y()*10000.0)/10000.0}};
        json["key"] = m_key.toStdString();
        json["speedRatioX"] = m_speedX;
        json["speedRatioY"] = m_speedY;
        if (m_areaMode) {
            json["areaMode"] = true;
            // areaX/Y 由 camera 中心位置减去半宽高自动计算
            double ax = std::round((r.x() - m_areaW / 2.0) * 10000.0) / 10000.0;
            double ay = std::round((r.y() - m_areaH / 2.0) * 10000.0) / 10000.0;
            json["areaRect"] = {{"x", ax}, {"y", ay}, {"w", m_areaW}, {"h", m_areaH}};
        }
        return json;
    }

    void fromJson(const nlohmann::json& json) override {
        if (json.contains("key")) m_key = QString::fromStdString(json["key"].get<std::string>());
        if (json.contains("speedRatioX")) m_speedX = json["speedRatioX"].get<double>();
        if (json.contains("speedRatioY")) m_speedY = json["speedRatioY"].get<double>();
        if (json.contains("areaMode")) m_areaMode = json["areaMode"].get<bool>();
        if (json.contains("areaRect") && json["areaRect"].is_object()) {
            auto& ar = json["areaRect"];
            // 只读取宽高，位置由 camera center 自动确定
            if (ar.contains("w")) m_areaW = ar["w"].get<double>();
            if (ar.contains("h")) m_areaH = ar["h"].get<double>();
        }
    }

    QString getKey() const override { return m_key; }

protected:
    QRectF boundingRect() const override { return QRectF(-60, -25, 120, 50); }

    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
        if (change == ItemSceneChange) {
            // 即将被移出场景时隐藏区域矩形
            QGraphicsScene* newScene = value.value<QGraphicsScene*>();
            if (!newScene) hideAreaRect();
        } else if (change == ItemSceneHasChanged) {
            // 加入新场景后恢复区域矩形
            ensureAreaRectIfNeeded();
        } else if (change == ItemPositionHasChanged) {
            // camera 被拖动时，区域矩形跟随
            syncAreaRect();
        }
        return KeyMapItemBase::itemChange(change, value);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override {
        Q_UNUSED(option); Q_UNUSED(widget);
        painter->setRenderHint(QPainter::Antialiasing);

        auto& tm = Fluent::ThemeManager::instance();
        QColor bg = m_isConflicted ? QColor(Fluent::Accent::Error).darker(110) :
                        (m_isEditing ? QColor(tm.card()) :
                             (isSelected() ? QColor(tm.accentPrimary()).darker(130) : QColor(tm.surface())));
        bg.setAlpha(220);

        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawRoundedRect(boundingRect(), 8, 8);

        QColor borderColor = m_isConflicted ? QColor(Fluent::Accent::Error) :
                             (m_isEditing ? QColor(tm.accentPrimary()) :
                                  (isSelected() ? QColor(Qt::white) : QColor(tm.borderSoft())));
        painter->setPen(QPen(borderColor, m_isEditing ? 2 : 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(boundingRect(), 8, 8);

        // 分隔线
        painter->setPen(QPen(QColor(tm.borderSoft()), 1));
        painter->drawLine(-20, -25, -20, 25);
        painter->drawLine(20, -25, 20, 25);

        // 绘制文字信息
        QFont font = painter->font();
        font.setBold(true);
        painter->setPen(QColor(tm.textPrimary()));

        // X轴灵敏度
        font.setPointSize(8);
        painter->setFont(font);
        QString xStr = (m_isEditing && m_editMode == Edit_X) ? (m_inputBuffer + (m_showCursor?"|":"")) : QString::number(m_speedX);
        painter->drawText(QRectF(-60, -25, 40, 50), Qt::AlignCenter, QString("X\n%1").arg(xStr));

        // Y轴灵敏度
        QString yStr = (m_isEditing && m_editMode == Edit_Y) ? (m_inputBuffer + (m_showCursor?"|":"")) : QString::number(m_speedY);
        painter->drawText(QRectF(20, -25, 40, 50), Qt::AlignCenter, QString("Y\n%1").arg(yStr));

        // 激活按键
        font.setPointSize(10);
        painter->setFont(font);
        QString t = (m_isEditing && m_editMode == Edit_Key) ? (KeyMapHelper::keyToDisplay(m_displayKey) + (m_showCursor?"|":"")) : KeyMapHelper::keyToDisplay(m_key);
        painter->drawText(QRectF(-20, -25, 40, 50), Qt::AlignCenter, t.isEmpty()?"?":t);
        drawCloseButton(painter, boundingRect());

        // 齿轮图标 (区域设置按钮)
        {
            QRectF gearRect(8, -18, 12, 12);
            painter->setPen(QPen(QColor(tm.textSecondary()), 1.2));
            painter->setBrush(Qt::NoBrush);
            QPointF gc = gearRect.center();
            painter->drawEllipse(gc, 3.5, 3.5);
            for (int i = 0; i < 6; ++i) {
                double a = i * 3.14159265 / 3.0;
                painter->drawLine(QPointF(gc.x()+3.0*std::cos(a), gc.y()+3.0*std::sin(a)),
                                  QPointF(gc.x()+5.5*std::cos(a), gc.y()+5.5*std::sin(a)));
            }
        }

        // 区域模式标签
        if (m_areaMode) {
            font.setPointSize(7);
            painter->setFont(font);
            painter->setPen(QColor(tm.accentPrimary()));
            painter->drawText(QRectF(-60, 18, 120, 12), Qt::AlignCenter, QObject::tr("区域"));
        }
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        // 关闭按钮优先于编辑状态检查，确保编辑中也能删除
        if (event->button() == Qt::LeftButton && handleCloseButtonClick(event->pos())) {
            event->accept(); return;
        }
        // 齿轮按钮 — 打开区域模式设置
        if (event->button() == Qt::LeftButton) {
            QPointF p = event->pos();
            QRectF gearRect(8, -18, 12, 12);
            if (gearRect.contains(p)) {
                openAreaSettings();
                event->accept(); return;
            }
        }
        if (m_isEditing) {
            event->ignore();
            return;
        }
        KeyMapItemBase::mousePressEvent(event);
    }

private:
    void openAreaSettings() {
        auto* dlg = new QDialog(scene()->views().first());
        dlg->setWindowTitle(QObject::tr("视角控制设置"));
        dlg->setFixedWidth(260);
        auto* layout = new QVBoxLayout(dlg);

        // 模式切换
        auto* modeLayout = new QHBoxLayout;
        auto* modeLabel = new QLabel(QObject::tr("控制模式:"));
        auto* modeCombo = new Fluent::FluentComboBox;
        modeCombo->addItem(QObject::tr("全屏"));
        modeCombo->addItem(QObject::tr("区域"));
        modeCombo->setCurrentIndex(m_areaMode ? 1 : 0);
        modeLayout->addWidget(modeLabel);
        modeLayout->addWidget(modeCombo, 1);
        layout->addLayout(modeLayout);

        auto* okBtn = new QPushButton(QObject::tr("确定"));
        layout->addWidget(okBtn);

        QObject::connect(okBtn, &QPushButton::clicked, dlg, [=]() {
            bool newAreaMode = (modeCombo->currentIndex() == 1);
            if (newAreaMode != m_areaMode) {
                m_areaMode = newAreaMode;
                if (m_areaMode) {
                    showAreaRect();
                } else {
                    hideAreaRect();
                }
                update();
            }
            dlg->accept();
        });

        dlg->exec();
        dlg->deleteLater();
    }

    void showAreaRect() {
        if (m_areaRectItem || !scene()) return;
        m_areaRectItem = new KeyMapAreaRect(this);
        scene()->addItem(m_areaRectItem);
        m_areaRectItem->setNormalizedSize(m_areaW, m_areaH);
        m_areaRectItem->syncToScene();
    }

    void hideAreaRect() {
        if (m_areaRectItem) {
            if (m_areaRectItem->scene()) {
                m_areaRectItem->scene()->removeItem(m_areaRectItem);
            }
            delete m_areaRectItem;
            m_areaRectItem = nullptr;
        }
    }

public:
    // 当场景添加后 / fromJson 后初始化区域矩形
    void ensureAreaRectIfNeeded() {
        if (m_areaMode && !m_areaRectItem && scene()) {
            showAreaRect();
        }
    }

    // 清理（删除 camera item 时同步删除区域矩形）
    ~KeyMapItemCamera() {
        hideAreaRect();
    }

    // 由 KeyMapAreaRect 回调更新归一化宽高
    void setAreaSize(double w, double h) {
        m_areaW = w; m_areaH = h;
    }

    // 场景大小改变时同步区域矩形
    void syncAreaRect() {
        if (m_areaRectItem) m_areaRectItem->syncToScene();
    }

private:
    double m_speedX = 1.0;
    double m_speedY = 1.0;
    bool m_areaMode = false;
    double m_areaW = 0.4, m_areaH = 0.4;
    KeyMapAreaRect* m_areaRectItem = nullptr;
    bool m_isEditing = false; bool m_showCursor = false;
    EditMode m_editMode = Edit_None;
    QString m_displayKey;
    QString m_inputBuffer;
    QTimer* m_cursorTimer;
};

// ---------------------------------------------------------
// 小眼睛自由视角键位类 (KeyMapItemFreeLook)
// 按住热键后启用自由视角，松开后恢复
// 与Camera不同：无边缘修正、无空闲回中
// ---------------------------------------------------------
class KeyMapItemFreeLook : public KeyMapItemBase
{
    Q_OBJECT
public:
    KeyMapItemFreeLook(QGraphicsItem *parent = nullptr) : KeyMapItemBase(parent) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        m_key = "";  // 默认为空，需要用户设置
        m_comment = "FreeLook";

        m_cursorTimer = new QTimer(this);
        m_cursorTimer->setInterval(600);
        connect(m_cursorTimer, &QTimer::timeout, this, [this](){ m_showCursor = !m_showCursor; update(); });
    }

    KeyMapType typeId() const override { return KMT_FREE_LOOK; }

    enum EditMode { Edit_None, Edit_Key, Edit_X, Edit_Y };

    void startEditing(const QPointF& pos) {
        bool wasEditing = m_isEditing;
        m_isEditing = true;

        EditMode newMode = Edit_None;
        if (pos.x() < -15) newMode = Edit_X;
        else if (pos.x() > 15) newMode = Edit_Y;
        else newMode = Edit_Key;

        if (!wasEditing || m_editMode != newMode) {
            m_editMode = newMode;
            // 点击 XY 时清空 buffer，实现"全选"效果
            if (m_editMode == Edit_X) m_inputBuffer = "";
            else if (m_editMode == Edit_Y) m_inputBuffer = "";
            else m_displayKey = "";

            m_showCursor = true;
            if (!m_cursorTimer->isActive()) m_cursorTimer->start();
            setSelected(true);
            update();
        }
    }

    void setEditing(bool edit) {
        if (m_isEditing == edit) return;
        m_isEditing = edit;
        if (m_isEditing) {
            m_editMode = Edit_Key;
            m_displayKey = "";
            m_showCursor = true;
            m_cursorTimer->start();
            setSelected(true);
        } else {
            m_editMode = Edit_None;
            m_cursorTimer->stop();
            m_showCursor = false;
        }
        update();
    }
    bool isEditing() const { return m_isEditing; }

    void inputKey(QKeyEvent* event) {
        if (m_editMode == Edit_Key) {
            int key = event->key();
            if (key == Qt::Key_unknown) return;
            m_key = KeyMapHelper::keyToString(key, event->modifiers());
            m_displayKey = m_key;
            update();
        } else if (m_editMode == Edit_X || m_editMode == Edit_Y) {
            if (event->key() == Qt::Key_Backspace) {
                if (!m_inputBuffer.isEmpty()) m_inputBuffer.chop(1);
            } else {
                QString text = event->text();
                if (!text.isEmpty() && (text.at(0).isDigit() || text.at(0) == '.')) {
                    m_inputBuffer.append(text);
                }
            }

            if (!m_inputBuffer.isEmpty()) {
                double val = m_inputBuffer.toDouble();
                if (m_editMode == Edit_X) m_speedX = val;
                else m_speedY = val;
            }
            update();
        }
    }

    void inputMouse(Qt::MouseButton button) {
        if (m_editMode != Edit_Key) return;

        QString keyName;
        switch (button) {
        case Qt::LeftButton: keyName = "LeftButton"; break;
        case Qt::RightButton: keyName = "RightButton"; break;
        case Qt::MiddleButton: keyName = "MiddleButton"; break;
        case Qt::XButton1: keyName = "SideButton1"; break;
        case Qt::XButton2: keyName = "SideButton2"; break;
        default: return;
        }
        if (!keyName.isEmpty()) { m_key = keyName; m_displayKey = keyName; update(); }
    }

    void inputWheel(int delta) {
        if (m_editMode != Edit_Key) return;
        QString keyName = (delta > 0) ? "WheelUp" : "WheelDown";
        m_key = keyName;
        m_displayKey = keyName;
        update();
    }

    bool resetViewOnRelease() const { return m_resetViewOnRelease; }
    void setResetViewOnRelease(bool reset) { m_resetViewOnRelease = reset; }

    nlohmann::json toJson() const override {
        nlohmann::json json;
        json["type"] = "KMT_FREE_LOOK";
        QPointF r = getNormalizedPos(scene()?scene()->sceneRect().size():QSizeF(1,1));
        nlohmann::json pos = {{"x", std::round(r.x()*10000.0)/10000.0}, {"y", std::round(r.y()*10000.0)/10000.0}};
        json["startPos"] = pos;  // 用于游戏逻辑解析
        json["pos"] = pos;       // 用于 UI 加载位置
        json["key"] = m_key.toStdString();
        json["speedRatioX"] = m_speedX;
        json["speedRatioY"] = m_speedY;
        json["resetViewOnRelease"] = m_resetViewOnRelease;
        return json;
    }

    void fromJson(const nlohmann::json& json) override {
        if (json.contains("key")) m_key = QString::fromStdString(json["key"].get<std::string>());
        if (json.contains("speedRatioX")) m_speedX = json["speedRatioX"].get<double>();
        if (json.contains("speedRatioY")) m_speedY = json["speedRatioY"].get<double>();
        if (json.contains("resetViewOnRelease")) m_resetViewOnRelease = json["resetViewOnRelease"].get<bool>();
    }

    QString getKey() const override { return m_key; }

protected:
    // 椭圆形外观
    QRectF boundingRect() const override { return QRectF(-50, -20, 100, 40); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override {
        Q_UNUSED(option); Q_UNUSED(widget);
        painter->setRenderHint(QPainter::Antialiasing);

        auto& tm = Fluent::ThemeManager::instance();
        // Fluent Focus: accent 色椭圆
        QColor bg = m_isConflicted ? QColor(Fluent::Accent::Error).darker(110) :
                        (m_isEditing ? QColor(tm.card()) :
                             (isSelected() ? QColor(tm.accentHover()) : QColor(tm.surface())));
        bg.setAlpha(220);

        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawEllipse(boundingRect());

        QColor borderColor = m_isConflicted ? QColor(Fluent::Accent::Error) :
                             (m_isEditing ? QColor(tm.accentPrimary()) :
                                  (isSelected() ? QColor(Qt::white) : QColor(tm.borderSoft())));
        painter->setPen(QPen(borderColor, m_isEditing ? 2 : 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(boundingRect());

        QFont font = painter->font();
        font.setBold(true);
        painter->setPen(QColor(tm.textPrimary()));

        // X轴灵敏度（左侧）
        font.setPointSize(7);
        painter->setFont(font);
        QString xStr = (m_isEditing && m_editMode == Edit_X) ? (m_inputBuffer + (m_showCursor?"|":"")) : QString::number(m_speedX);
        painter->drawText(QRectF(-48, -18, 30, 36), Qt::AlignCenter, QString("X\n%1").arg(xStr));

        // Y轴灵敏度（右侧）
        QString yStr = (m_isEditing && m_editMode == Edit_Y) ? (m_inputBuffer + (m_showCursor?"|":"")) : QString::number(m_speedY);
        painter->drawText(QRectF(18, -18, 30, 36), Qt::AlignCenter, QString("Y\n%1").arg(yStr));

        // 激活按键（中间）
        font.setPointSize(9);
        painter->setFont(font);
        QString t = (m_isEditing && m_editMode == Edit_Key) ? (KeyMapHelper::keyToDisplay(m_displayKey) + (m_showCursor?"|":"")) : KeyMapHelper::keyToDisplay(m_key);
        painter->drawText(QRectF(-15, -18, 30, 36), Qt::AlignCenter, t.isEmpty()?"?":t);
        drawGear(painter);
        drawCloseButton(painter, boundingRect());
    }

    // 绘制设置齿轮图标（中间顶部）
    void drawGear(QPainter* painter) {
        auto& tm = Fluent::ThemeManager::instance();
        painter->save();
        painter->translate(0, -14);  // 中间顶部位置
        painter->setPen(QPen(QColor(tm.textSecondary()), 1.2));
        painter->setBrush(QColor(tm.surface()));
        painter->drawEllipse(QPoint(0,0), 5, 5);
        painter->setBrush(QColor(tm.textSecondary()));
        painter->drawEllipse(QPoint(0,0), 2, 2);
        for(int i=0; i<8; ++i) { painter->rotate(45); painter->drawLine(0, 5, 0, 7); }
        painter->restore();
    }

    // 打开设置对话框
    void openSettingsDialog() {
        // 保护 this：dlg.exec() 会进入嵌套事件循环，期间用户可能删除本项
        QPointer<KeyMapItemFreeLook> guard(this);
        QPointF savedPos = pos();
        QDialog dlg;
        dlg.setWindowTitle("小眼睛设置");
        dlg.setFixedSize(280, 120);

        auto* layout = new QVBoxLayout(&dlg);
        layout->setSpacing(12);
        layout->setContentsMargins(16, 16, 16, 16);

        // 复选框：松开时是否重置视角
        auto* checkBox = new QCheckBox("松开热键时重置视角", &dlg);
        checkBox->setChecked(m_resetViewOnRelease);
        checkBox->setToolTip("启用后，松开小眼睛热键时会自动将视角重置到初始位置");
        layout->addWidget(checkBox);

        // 按钮
        auto* btnLayout = new QHBoxLayout();
        auto* okBtn = new QPushButton("确定", &dlg);
        auto* cancelBtn = new QPushButton("取消", &dlg);
        btnLayout->addStretch();
        btnLayout->addWidget(okBtn);
        btnLayout->addWidget(cancelBtn);
        layout->addLayout(btnLayout);

        QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

        if (dlg.exec() == QDialog::Accepted) {
            // 对话框关闭后检查 this 是否仍然存在
            if (guard) {
                m_resetViewOnRelease = checkBox->isChecked();
            }
        }
        // 恢复位置（防止 exec 期间被 overlay sync 偏移）
        if (guard) {
            setPos(savedPos);
        }
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        // 关闭按钮优先于编辑状态检查，确保编辑中也能删除
        if (event->button() == Qt::LeftButton && handleCloseButtonClick(event->pos())) {
            event->accept(); return;
        }
        if (m_isEditing) {
            event->ignore();
            return;
        }
        // 单击齿轮区域打开设置对话框
        if (event->button() == Qt::LeftButton) {
            QPointF p = event->pos();
            if (p.x() > -8 && p.x() < 8 && p.y() < -7) {
                openSettingsDialog();
                event->accept();
                return;
            }
        }
        KeyMapItemBase::mousePressEvent(event);
    }

private:
    double m_speedX = 1.0;
    double m_speedY = 1.0;
    bool m_isEditing = false; bool m_showCursor = false;
    EditMode m_editMode = Edit_None;
    QString m_displayKey;
    QString m_inputBuffer;
    QTimer* m_cursorTimer;
    bool m_resetViewOnRelease = false;  // 松开时是否重置视角
};

// ---------------------------------------------------------
// SteerWheelSubItem 实现部分 (内联函数)
// ---------------------------------------------------------
inline SteerWheelSubItem::SteerWheelSubItem(Direction dir, KeyMapItemSteerWheel* parentWheel) : QGraphicsObject(parentWheel), m_dir(dir), m_parentWheel(parentWheel) {
    setFlags(ItemIsSelectable | ItemIsFocusable); setAcceptHoverEvents(true);
    m_cursorTimer = new QTimer(this); m_cursorTimer->setInterval(600);
    connect(m_cursorTimer, &QTimer::timeout, this, [this](){ m_showCursor = !m_showCursor; update(); });
}
inline void SteerWheelSubItem::setEditing(bool edit) {
    if (m_isEditing == edit) return; m_isEditing = edit;
    if (m_isEditing) { m_displayKey = ""; m_showCursor = true; m_cursorTimer->start(); setSelected(true); }
    else { m_cursorTimer->stop(); m_showCursor = false; } update();
}
inline void SteerWheelSubItem::inputKey(QKeyEvent* event) {
    int key = event->key();
    if (key == Qt::Key_unknown) return;
    m_key = KeyMapHelper::keyToString(key, event->modifiers());
    m_displayKey = m_key; update();
}
inline void SteerWheelSubItem::inputMouse(Qt::MouseButton button) {
    QString keyName;
    switch (button) {
    case Qt::LeftButton: keyName = "LeftButton"; break;
    case Qt::RightButton: keyName = "RightButton"; break;
    case Qt::MiddleButton: keyName = "MiddleButton"; break;
    case Qt::XButton1: keyName = "SideButton1"; break;
    case Qt::XButton2: keyName = "SideButton2"; break;
    default: return;
    }
    if (!keyName.isEmpty()) { m_key = keyName; m_displayKey = keyName; update(); }
}
inline void SteerWheelSubItem::inputWheel(int delta) {
    QString keyName = (delta > 0) ? "WheelUp" : "WheelDown";
    m_key = keyName;
    m_displayKey = keyName;
    update();
}
inline void SteerWheelSubItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing);
    auto& tm = Fluent::ThemeManager::instance();
    QColor bg = m_isConflicted ? QColor(Fluent::Accent::Error) :
                    (m_isEditing ? QColor(tm.card()) : QColor(tm.accentPrimary()));
    bg.setAlpha(m_isEditing ? 230 : 200);
    p->setBrush(bg);
    p->setPen(m_isConflicted ? QPen(QColor(Fluent::Accent::Error), 2) :
              (m_isEditing ? QPen(QColor(tm.accentPrimary()), 2) : QPen(QColor(tm.border()), 1)));
    p->drawEllipse(boundingRect());
    p->setPen(QColor(tm.textPrimary())); QFont f = p->font(); f.setBold(true);
    QString t = m_isEditing ? (KeyMapHelper::keyToDisplay(m_displayKey) + (m_showCursor?"|":"")) : KeyMapHelper::keyToDisplay(m_key);
    // 根据文字长度自适应字体大小
    int fontSize = 9;
    if (t.length() > 6) fontSize = 6;
    else if (t.length() > 3) fontSize = 7;
    else if (t.length() > 1) fontSize = 8;
    f.setPointSize(fontSize);
    p->setFont(f);
    p->drawText(boundingRect(), Qt::AlignCenter, t.isEmpty()?"?":t);
}
inline void SteerWheelSubItem::mousePressEvent(QGraphicsSceneMouseEvent *e) { e->accept(); }
inline void SteerWheelSubItem::mouseMoveEvent(QGraphicsSceneMouseEvent *e) {
    if(m_isEditing) return; QPointF p=mapToParent(e->pos()); m_parentWheel->updateOffsetFromSubItem(m_dir, p);
}
inline void SteerWheelSubItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *e) { QGraphicsObject::mouseReleaseEvent(e); }

// ---------------------------------------------------------
// KeyMapItemSteerWheel 实现部分 (内联函数)
// ---------------------------------------------------------
inline KeyMapItemSteerWheel::KeyMapItemSteerWheel(QGraphicsItem *parent) : KeyMapItemBase(parent) {
    resize(200, 200); m_comment = "方向盘";
    m_subUp = new SteerWheelSubItem(SteerWheelSubItem::Dir_Up, this); m_subDown = new SteerWheelSubItem(SteerWheelSubItem::Dir_Down, this);
    m_subLeft = new SteerWheelSubItem(SteerWheelSubItem::Dir_Left, this); m_subRight = new SteerWheelSubItem(SteerWheelSubItem::Dir_Right, this);
    m_subUp->setKey("Key_W"); m_subDown->setKey("Key_S"); m_subLeft->setKey("Key_A"); m_subRight->setKey("Key_D");
    updateSubItemsPos();
}
inline void KeyMapItemSteerWheel::setKeys(const QString& u, const QString& d, const QString& l, const QString& r) { m_subUp->setKey(u); m_subDown->setKey(d); m_subLeft->setKey(l); m_subRight->setKey(r); }
inline void KeyMapItemSteerWheel::setOffsets(double u, double d, double l, double r) { m_upOffset=u; m_downOffset=d; m_leftOffset=l; m_rightOffset=r; updateSubItemsPos(); }
inline void KeyMapItemSteerWheel::updateOffsetFromSubItem(SteerWheelSubItem::Direction dir, const QPointF& localPos) {
    if (!scene()) return; QSizeF sz = scene()->sceneRect().size(); if (sz.isEmpty()) return;
    double val=0;
    if(dir==SteerWheelSubItem::Dir_Up) val = -localPos.y()/sz.height();
    else if(dir==SteerWheelSubItem::Dir_Down) val = localPos.y()/sz.height();
    else if(dir==SteerWheelSubItem::Dir_Left) val = -localPos.x()/sz.width();
    else if(dir==SteerWheelSubItem::Dir_Right) val = localPos.x()/sz.width();
    if(val<0.02) val=0.02; if(val>0.48) val=0.48;
    if(dir==SteerWheelSubItem::Dir_Up) m_upOffset=val; else if(dir==SteerWheelSubItem::Dir_Down) m_downOffset=val;
    else if(dir==SteerWheelSubItem::Dir_Left) m_leftOffset=val; else m_rightOffset=val;
    updateSubItemsPos(); update();
}
inline void KeyMapItemSteerWheel::updateSubItemsPos() {
    if (!scene()) return; QSizeF sz = scene()->sceneRect().size(); if (sz.isEmpty()) return;
    m_subUp->setPos(0, -m_upOffset*sz.height()); m_subDown->setPos(0, m_downOffset*sz.height());
    m_subLeft->setPos(-m_leftOffset*sz.width(), 0); m_subRight->setPos(m_rightOffset*sz.width(), 0);
}
inline void KeyMapItemSteerWheel::resize(qreal w, qreal h) { prepareGeometryChange(); m_rect = QRectF(-w/2,-h/2,w,h); updateSubItemsPos(); }
inline QPainterPath KeyMapItemSteerWheel::shape() const {
    QPainterPath p; p.addEllipse(QPointF(0,0), 20, 20);
    QPainterPath l; l.moveTo(m_subUp->pos()); l.lineTo(m_subDown->pos()); l.moveTo(m_subLeft->pos()); l.lineTo(m_subRight->pos());
    QPainterPathStroker s; s.setWidth(10); p.addPath(s.createStroke(l)); return p;
}
inline void KeyMapItemSteerWheel::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing);
    auto& tm = Fluent::ThemeManager::instance();
    QColor lineColor = isSelected() ? QColor(tm.accentPrimary()) : QColor(tm.accentPrimary());
    lineColor.setAlpha(isSelected() ? 180 : 100);
    p->setPen(QPen(lineColor, 3));
    p->drawLine(QPointF(0,0), m_subUp->pos()); p->drawLine(QPointF(0,0), m_subDown->pos());
    p->drawLine(QPointF(0,0), m_subLeft->pos()); p->drawLine(QPointF(0,0), m_subRight->pos());
    QColor centerColor(tm.accentPrimary());
    centerColor.setAlpha(isSelected() ? 200 : 120);
    p->setBrush(centerColor); p->setPen(Qt::NoPen); p->drawEllipse(QPointF(0,0), 10, 10);
    // 关闭按钮（中心点左上方）
    QRectF closeRect(-4, -18, 12, 12);
    p->save();
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(Fluent::Accent::Error));
    p->drawRoundedRect(closeRect, 3, 3);
    p->setPen(QPen(Qt::white, 1.5));
    double cx = closeRect.center().x(), cy = closeRect.center().y(), r = 3;
    p->drawLine(QPointF(cx - r, cy - r), QPointF(cx + r, cy + r));
    p->drawLine(QPointF(cx + r, cy - r), QPointF(cx - r, cy + r));
    p->restore();
    // 齿轮按钮（中心点右上方）
    QRectF gearRect(8, -18, 12, 12);
    p->save();
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(tm.textSecondary()));
    p->drawRoundedRect(gearRect, 3, 3);
    // 画简易齿轮图标
    double gcx = gearRect.center().x(), gcy = gearRect.center().y();
    p->setPen(Qt::NoPen);
    p->setBrush(Qt::white);
    p->drawEllipse(QPointF(gcx, gcy), 3.5, 3.5);
    p->setBrush(QColor(tm.textSecondary()));
    p->drawEllipse(QPointF(gcx, gcy), 1.5, 1.5);
    // 齿轮齿（6个小矩形）
    for (int i = 0; i < 6; ++i) {
        double angle = i * 60.0 * M_PI / 180.0;
        double tx = gcx + 3.0 * std::cos(angle);
        double ty = gcy + 3.0 * std::sin(angle);
        p->save();
        p->translate(tx, ty);
        p->rotate(i * 60.0);
        p->setBrush(Qt::white);
        p->drawRect(QRectF(-1, -0.7, 2, 1.4));
        p->restore();
    }
    p->restore();
    // 速度倍率标签（齿轮下方）
    if (std::abs(m_speedMultiplier - 1.0) > 0.001) {
        p->save();
        QFont f = p->font(); f.setPointSizeF(6); f.setBold(true); p->setFont(f);
        p->setPen(QColor(tm.accentPrimary()));
        QString speedText = QString("x%1").arg(m_speedMultiplier, 0, 'f', 1);
        p->drawText(QRectF(4, -6, 20, 10), Qt::AlignCenter, speedText);
        p->restore();
    }
}
inline void KeyMapItemSteerWheel::openSpeedDialog() {
    QDialog dlg;
    dlg.setWindowTitle(QObject::tr("轮盘速度设置"));
    dlg.setFixedSize(280, 120);
    auto* layout = new QVBoxLayout(&dlg);
    auto* label = new QLabel(QObject::tr("速度倍率 (0.1 ~ 5.0):"));
    layout->addWidget(label);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(10, 500); // 0.1x ~ 5.0x
    slider->setValue(static_cast<int>(m_speedMultiplier * 100));
    auto* valueLabel = new QLabel(QString::number(m_speedMultiplier, 'f', 1) + "x");
    valueLabel->setAlignment(Qt::AlignCenter);
    QObject::connect(slider, &QSlider::valueChanged, [valueLabel](int v) {
        valueLabel->setText(QString::number(v / 100.0, 'f', 1) + "x");
    });
    auto* hbox = new QHBoxLayout();
    hbox->addWidget(slider);
    hbox->addWidget(valueLabel);
    layout->addLayout(hbox);
    auto* btnLayout = new QHBoxLayout();
    auto* okBtn = new QPushButton(QObject::tr("确定"));
    auto* cancelBtn = new QPushButton(QObject::tr("取消"));
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() == QDialog::Accepted) {
        m_speedMultiplier = slider->value() / 100.0;
        update();
    }
}
inline nlohmann::json KeyMapItemSteerWheel::toJson() const {
    nlohmann::json json;
    json["type"]="KMT_STEER_WHEEL"; json["comment"]=m_comment.toStdString();
    QPointF r = getNormalizedPos(scene()?scene()->sceneRect().size():QSizeF(1,1));
    json["centerPos"]={{"x", std::round(r.x()*10000.0)/10000.0}, {"y", std::round(r.y()*10000.0)/10000.0}};
    json["leftOffset"]=m_leftOffset; json["rightOffset"]=m_rightOffset; json["upOffset"]=m_upOffset; json["downOffset"]=m_downOffset;
    json["leftKey"]=m_subLeft->getKey().toStdString(); json["rightKey"]=m_subRight->getKey().toStdString();
    json["upKey"]=m_subUp->getKey().toStdString(); json["downKey"]=m_subDown->getKey().toStdString();
    if (std::abs(m_speedMultiplier - 1.0) > 0.001) json["speedMultiplier"]=m_speedMultiplier;
    return json;
}
inline void KeyMapItemSteerWheel::fromJson(const nlohmann::json& json) {
    if(json.contains("leftOffset")) m_leftOffset = json["leftOffset"].get<double>();
    if(json.contains("rightOffset")) m_rightOffset = json["rightOffset"].get<double>();
    if(json.contains("upOffset")) m_upOffset = json["upOffset"].get<double>();
    if(json.contains("downOffset")) m_downOffset = json["downOffset"].get<double>();
    if(json.contains("speedMultiplier")) m_speedMultiplier = json["speedMultiplier"].get<double>();

    if(json.contains("leftKey")) m_subLeft->setKey(QString::fromStdString(json["leftKey"].get<std::string>()));
    if(json.contains("rightKey")) m_subRight->setKey(QString::fromStdString(json["rightKey"].get<std::string>()));
    if(json.contains("upKey")) m_subUp->setKey(QString::fromStdString(json["upKey"].get<std::string>()));
    if(json.contains("downKey")) m_subDown->setKey(QString::fromStdString(json["downKey"].get<std::string>()));
    updateSubItemsPos();
}
inline SteerWheelSubItem* KeyMapItemSteerWheel::getSubItemAt(const QPointF& pos) {
    if (m_subUp->sceneBoundingRect().contains(mapToScene(pos))) return m_subUp;
    return nullptr;
}

// ---------------------------------------------------------
// 工厂实现
// ---------------------------------------------------------
class KeyMapFactoryImpl : public KeyMapFactory {
public:
    KeyMapItemBase* createItem(KeyMapType type) override {
        switch (type) {
        case KMT_STEER_WHEEL: return new KeyMapItemSteerWheel();
        case KMT_SCRIPT: return new KeyMapItemScript();
        case KMT_CAMERA_MOVE: return new KeyMapItemCamera();
        case KMT_FREE_LOOK: return new KeyMapItemFreeLook();
        default: return nullptr;
        }
    }
};

// ---------------------------------------------------------
// KeyMapAreaRect 实现 (必须在 KeyMapItemCamera 定义之后)
// 始终以 owner (KeyMapItemCamera) 为中心，四角对称缩放
// ---------------------------------------------------------
inline KeyMapAreaRect::KeyMapAreaRect(KeyMapItemCamera* owner, QGraphicsItem* parent)
    : QGraphicsObject(parent), m_owner(owner),
      m_nw(0.4), m_nh(0.4), m_pixW(100), m_pixH(100)
{
    setAcceptHoverEvents(true);
    setZValue(-1); // 在键位组件之下
}

inline void KeyMapAreaRect::setNormalizedSize(double w, double h)
{
    m_nw = w; m_nh = h;
}

inline void KeyMapAreaRect::syncToScene()
{
    if (!scene() || !m_owner) return;
    QRectF sr = scene()->sceneRect();
    if (sr.isEmpty()) return;
    prepareGeometryChange();
    m_pixW = m_nw * sr.width();
    m_pixH = m_nh * sr.height();
    // 以 owner 的场景位置为中心
    QPointF ownerCenter = m_owner->pos();
    setPos(ownerCenter.x() - m_pixW / 2.0, ownerCenter.y() - m_pixH / 2.0);
}

inline QRectF KeyMapAreaRect::boundingRect() const
{
    return QRectF(-HANDLE, -HANDLE, m_pixW + HANDLE * 2, m_pixH + HANDLE * 2);
}

inline void KeyMapAreaRect::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);
    QRectF rect(0, 0, m_pixW, m_pixH);

    // 半透明填充
    painter->setBrush(QColor(100, 149, 237, 25));
    painter->setPen(QPen(QColor(100, 149, 237, 180), 1.5, Qt::DashLine));
    painter->drawRect(rect);

    // 四角手柄
    QColor handleColor(100, 149, 237, 220);
    painter->setBrush(handleColor);
    painter->setPen(Qt::NoPen);
    const qreal hs = HANDLE;
    painter->drawEllipse(QPointF(0, 0), hs / 2, hs / 2);
    painter->drawEllipse(QPointF(m_pixW, 0), hs / 2, hs / 2);
    painter->drawEllipse(QPointF(0, m_pixH), hs / 2, hs / 2);
    painter->drawEllipse(QPointF(m_pixW, m_pixH), hs / 2, hs / 2);

    // 中心十字（应与 owner 位置对齐）
    QPointF c(m_pixW / 2, m_pixH / 2);
    painter->setPen(QPen(QColor(100, 149, 237, 120), 1));
    painter->drawLine(QPointF(c.x() - 8, c.y()), QPointF(c.x() + 8, c.y()));
    painter->drawLine(QPointF(c.x(), c.y() - 8), QPointF(c.x(), c.y() + 8));

    // 标签
    painter->setPen(QColor(100, 149, 237, 200));
    QFont f = painter->font();
    f.setPointSize(8);
    painter->setFont(f);
    painter->drawText(rect, Qt::AlignTop | Qt::AlignHCenter, QObject::tr("视角区域"));
}

inline KeyMapAreaRect::DragMode KeyMapAreaRect::hitTest(const QPointF& p) const
{
    const qreal r = HANDLE;
    if (QPointF(p.x(), p.y()).manhattanLength() < r) return TopLeft;
    if (QPointF(p.x() - m_pixW, p.y()).manhattanLength() < r) return TopRight;
    if (QPointF(p.x(), p.y() - m_pixH).manhattanLength() < r) return BottomLeft;
    if (QPointF(p.x() - m_pixW, p.y() - m_pixH).manhattanLength() < r) return BottomRight;
    return None;
}

inline void KeyMapAreaRect::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) { event->ignore(); return; }
    m_dragMode = hitTest(event->pos());
    if (m_dragMode == None) { event->ignore(); return; }
    m_dragStart = event->scenePos();
    m_dragStartW = m_pixW;
    m_dragStartH = m_pixH;
    event->accept();
}

inline void KeyMapAreaRect::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_dragMode == None || !m_owner) return;
    if (!scene()) return;
    QRectF sr = scene()->sceneRect();
    QPointF delta = event->scenePos() - m_dragStart;
    QPointF ownerCenter = m_owner->pos();

    // 四个角都对称缩放：拖角时改变对应方向的宽/高
    // delta 映射到宽/高变化(对称，所以 ×2)
    qreal dw = 0, dh = 0;
    switch (m_dragMode) {
    case TopLeft:     dw = -delta.x() * 2; dh = -delta.y() * 2; break;
    case TopRight:    dw =  delta.x() * 2; dh = -delta.y() * 2; break;
    case BottomLeft:  dw = -delta.x() * 2; dh =  delta.y() * 2; break;
    case BottomRight: dw =  delta.x() * 2; dh =  delta.y() * 2; break;
    default: break;
    }

    qreal newW = m_dragStartW + dw;
    qreal newH = m_dragStartH + dh;

    // 最小尺寸
    if (newW < 30) newW = 30;
    if (newH < 30) newH = 30;

    // 约束：矩形不能超出场景
    qreal maxW = qMin(ownerCenter.x(), sr.width() - ownerCenter.x()) * 2.0;
    qreal maxH = qMin(ownerCenter.y(), sr.height() - ownerCenter.y()) * 2.0;
    if (newW > maxW) newW = maxW;
    if (newH > maxH) newH = maxH;

    prepareGeometryChange();
    m_pixW = newW;
    m_pixH = newH;
    setPos(ownerCenter.x() - m_pixW / 2.0, ownerCenter.y() - m_pixH / 2.0);

    // 更新归一化值
    m_nw = m_pixW / sr.width();
    m_nh = m_pixH / sr.height();

    commitToOwner();
    update();
}

inline void KeyMapAreaRect::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    Q_UNUSED(event);
    m_dragMode = None;
}

inline void KeyMapAreaRect::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    DragMode m = hitTest(event->pos());
    switch (m) {
    case TopLeft:
    case BottomRight:
        setCursor(Qt::SizeFDiagCursor); break;
    case TopRight:
    case BottomLeft:
        setCursor(Qt::SizeBDiagCursor); break;
    default:
        setCursor(Qt::ArrowCursor); break;
    }
}

inline void KeyMapAreaRect::commitToOwner()
{
    if (m_owner) {
        m_owner->setAreaSize(m_nw, m_nh);
    }
}

#endif // KEYMAPITEMS_H
